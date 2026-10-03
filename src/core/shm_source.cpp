#include "shmscope/core/shm_source.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "shmscope/core/fault_guard.hpp"
#include "shmscope/core/source.hpp"

namespace shmscope {

    namespace {
#ifdef __APPLE__
        constexpr std::size_t MAX_NAME = 31;
#else
        constexpr std::size_t MAX_NAME = 255;
#endif

        std::string errnoMessage(int error) {
            return std::generic_category().message(error);
        }

        std::optional<SegmentIdentity> identify(const int fd) noexcept {
            struct stat info{};
            if (::fstat(fd, &info) != 0) return std::nullopt;

#ifdef __APPLE__
            if (info.st_size <= 0) return std::nullopt;

            const auto length =
                std::min(static_cast<std::size_t>(info.st_size),
                         static_cast<std::size_t>(::getpagesize()));
            void* base = ::mmap(nullptr, length, PROT_READ, MAP_SHARED, fd, 0);
            if (base == MAP_FAILED) return std::nullopt;

            auto address = reinterpret_cast<mach_vm_address_t>(base);
            mach_vm_size_t size = 0;
            vm_region_top_info_data_t top{};
            mach_msg_type_number_t count = VM_REGION_TOP_INFO_COUNT;
            mach_port_t object = MACH_PORT_NULL;
            const auto result = ::mach_vm_region(
                ::mach_task_self(), &address, &size, VM_REGION_TOP_INFO,
                reinterpret_cast<vm_region_info_t>(&top), &count, &object);
            ::munmap(base, length);

            if (result != KERN_SUCCESS || top.obj_id == 0) return std::nullopt;

            return SegmentIdentity{.first = top.obj_id};
#else
            return SegmentIdentity{
                .first = static_cast<std::uint64_t>(info.st_dev),
                .second = static_cast<std::uint64_t>(info.st_ino)};
#endif
        }

    }  // namespace

    OpenResult ShmSource::open(std::string_view name,
                               const ShmOptions options) {
        std::string path(name);
        if (!path.starts_with('/')) {
            path.insert(path.begin(), '/');
        }

        if (path.size() > MAX_NAME) {
            return std::unexpected(
                std::format("{}: name is {} characters, the limit is {}", path,
                            path.size(), MAX_NAME));
        }

        const int fd = ::shm_open(path.c_str(), O_RDONLY, 0);
        if (fd < 0) {
            const int code = errno;
            return std::unexpected(
                std::format("{}: {}", path, errnoMessage(code)));
        }

        struct stat info{};
        if (::fstat(fd, &info) != 0) {
            const int code = errno;
            auto error = std::format("{}: fstat: {}", path, errnoMessage(code));
            ::close(fd);
            return std::unexpected(std::move(error));
        }

        if (info.st_size <= 0) {
            ::close(fd);
            return std::unexpected(std::format(
                "{}: object is empty, writer has not sized it", path));
        }

        const auto size = static_cast<std::size_t>(info.st_size);
        void* base = ::mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
        if (base == MAP_FAILED) {
            const int code = errno;
            auto error = std::format("{}: mmap: {}", path, errnoMessage(code));
            ::close(fd);
            return std::unexpected(std::move(error));
        }

        return std::make_unique<ShmSource>(Token{}, std::move(path), fd,
                                           static_cast<const std::byte*>(base),
                                           size, options);
    }

    ShmSource::ShmSource(Token, std::string name, int fd, const std::byte* base,
                         std::size_t size, const ShmOptions options)
        : name_(std::move(name)),
          fd_(fd),
          base_(base),
          size_(size),
          options_(options),
          identity_(identify(fd)),
          checkedAt_(std::chrono::steady_clock::now()),
          current_(size),
          previous_(size),
          heat_(size) {
        if (!copyGuarded(current_.data(), base_, size_)) {
            std::ranges::fill(current_, std::byte{0});
        }
    }

    ShmSource::~ShmSource() {
        if (base_ != nullptr) ::munmap(const_cast<std::byte*>(base_), size_);
        ::close(fd_);
    }

    std::string_view ShmSource::name() const noexcept { return name_; }

    Frame ShmSource::poll() noexcept {
        followSize();
        checkName();

        if (size_ > 0) {
            std::ranges::swap(current_, previous_);
            if (!copyDiffGuarded(base_, current_.data(), previous_.data(),
                                 heat_.data(), size_)) {
                std::ranges::swap(current_, previous_);
                followSize();
            }
        }

        ++sequence_;
        return Frame{.bytes = current_, .heat = heat_, .sequence = sequence_};
    }

    void ShmSource::followSize() noexcept {
        struct stat info{};
        if (::fstat(fd_, &info) != 0 || info.st_size < 0) return;

        const auto size = static_cast<std::size_t>(info.st_size);
        if (size != size_) remap(size);
    }

    void ShmSource::remap(const std::size_t size) noexcept {
        if (base_ != nullptr) ::munmap(const_cast<std::byte*>(base_), size_);
        base_ = nullptr;
        size_ = 0;

        if (size > 0) {
            void* base = ::mmap(nullptr, size, PROT_READ, MAP_SHARED, fd_, 0);
            if (base != MAP_FAILED) {
                base_ = static_cast<const std::byte*>(base);
                size_ = size;
            }
        }

        current_.assign(size_, std::byte{0});
        if (size_ > 0 && !copyGuarded(current_.data(), base_, size_)) {
            std::ranges::fill(current_, std::byte{0});
        }
        previous_ = current_;
        heat_.assign(size_, 0);
    }

    void ShmSource::checkName() noexcept {
        const auto now = std::chrono::steady_clock::now();
        if (now - checkedAt_ < options_.checkEvery) return;
        checkedAt_ = now;

        const int fd = ::shm_open(name_.c_str(), O_RDONLY, 0);
        if (fd < 0) {
            if (errno == ENOENT) state_ = SourceState::REMOVED;
            return;
        }

        const auto other = identify(fd);
        ::close(fd);
        if (!identity_ || !other) return;

        state_ =
            *other == *identity_ ? SourceState::LIVE : SourceState::REPLACED;
    }

}  // namespace shmscope
