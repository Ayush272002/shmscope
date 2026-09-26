#include "shmscope/shm_source.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <expected>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "shmscope/diff.hpp"
#include "shmscope/source.hpp"

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

    }  // namespace

    OpenResult ShmSource::open(std::string_view name) {
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
                                           size);
    }

    ShmSource::ShmSource(Token, std::string name, int fd, const std::byte* base,
                         std::size_t size)
        : name_(std::move(name)),
          fd_(fd),
          base_(base),
          size_(size),
          current_(size),
          previous_(size),
          heat_(size) {
        std::memcpy(current_.data(), base_, size_);
    }

    ShmSource::~ShmSource() {
        ::munmap(const_cast<std::byte*>(base_), size_);
        ::close(fd_);
    }

    std::string_view ShmSource::name() const noexcept { return name_; }

    Frame ShmSource::poll() noexcept {
        std::ranges::swap(current_, previous_);
        copyDiff(base_, current_.data(), previous_.data(), heat_.data(), size_);
        ++sequence_;
        return Frame{.bytes = current_, .heat = heat_, .sequence = sequence_};
    }

}  // namespace shmscope
