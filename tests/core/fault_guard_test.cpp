#include "shmscope/core/fault_guard.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

    using shmscope::copyDiffGuarded;
    using shmscope::copyGuarded;

    class TruncatableMapping {
    public:
        explicit TruncatableMapping(std::size_t size) : size_(size) {
            std::string path = (std::filesystem::temp_directory_path() /
                                "shmscope-guard-XXXXXX")
                                   .string();
            fd_ = ::mkstemp(path.data());
            if (fd_ < 0) return;
            ::unlink(path.c_str());
            if (::ftruncate(fd_, static_cast<off_t>(size_)) != 0) return;

            void* base = ::mmap(nullptr, size_, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd_, 0);
            if (base != MAP_FAILED) base_ = static_cast<std::byte*>(base);
        }

        ~TruncatableMapping() {
            if (base_ != nullptr) ::munmap(base_, size_);
            if (fd_ >= 0) ::close(fd_);
        }

        TruncatableMapping(const TruncatableMapping&) = delete;
        TruncatableMapping& operator=(const TruncatableMapping&) = delete;
        TruncatableMapping(TruncatableMapping&&) = delete;
        TruncatableMapping& operator=(TruncatableMapping&&) = delete;

        [[nodiscard]] bool ready() const { return base_ != nullptr; }
        [[nodiscard]] std::byte* data() const { return base_; }
        [[nodiscard]] std::size_t size() const { return size_; }

        bool truncate(std::size_t size) {
            return ::ftruncate(fd_, static_cast<off_t>(size)) == 0;
        }

    private:
        std::size_t size_;
        int fd_ = -1;
        std::byte* base_ = nullptr;
    };

    constexpr std::size_t SIZE = 4 * 16384;

    TEST(FaultGuardTest, CopiesReadableMemory) {
        TruncatableMapping mapping(SIZE);
        ASSERT_TRUE(mapping.ready());
        mapping.data()[5] = std::byte{42};
        std::vector<std::byte> out(SIZE);

        EXPECT_TRUE(copyGuarded(out.data(), mapping.data(), SIZE));
        EXPECT_EQ(out[5], std::byte{42});
    }

    TEST(FaultGuardTest, DiffsReadableMemory) {
        TruncatableMapping mapping(SIZE);
        ASSERT_TRUE(mapping.ready());
        mapping.data()[9] = std::byte{7};
        std::vector<std::byte> current(SIZE);
        std::vector<std::byte> previous(SIZE);
        std::vector<std::uint8_t> heat(SIZE);

        EXPECT_TRUE(copyDiffGuarded(mapping.data(), current.data(),
                                    previous.data(), heat.data(), SIZE));
        EXPECT_EQ(current[9], std::byte{7});
        EXPECT_GT(heat[9], 0);
    }

    TEST(FaultGuardTest, ATruncatedMappingReportsFailure) {
        TruncatableMapping mapping(SIZE);
        ASSERT_TRUE(mapping.ready());
        std::vector<std::byte> out(SIZE);
        ASSERT_TRUE(mapping.truncate(0));

        EXPECT_FALSE(copyGuarded(out.data(), mapping.data(), SIZE));
    }

    TEST(FaultGuardTest, ATruncatedDiffReportsFailure) {
        TruncatableMapping mapping(SIZE);
        ASSERT_TRUE(mapping.ready());
        std::vector<std::byte> current(SIZE);
        std::vector<std::byte> previous(SIZE);
        std::vector<std::uint8_t> heat(SIZE);
        ASSERT_TRUE(mapping.truncate(0));

        EXPECT_FALSE(copyDiffGuarded(mapping.data(), current.data(),
                                     previous.data(), heat.data(), SIZE));
    }

    TEST(FaultGuardTest, CopiesWorkAgainAfterAFault) {
        TruncatableMapping mapping(SIZE);
        ASSERT_TRUE(mapping.ready());
        std::vector<std::byte> out(SIZE);
        ASSERT_TRUE(mapping.truncate(0));
        ASSERT_FALSE(copyGuarded(out.data(), mapping.data(), SIZE));

        ASSERT_TRUE(mapping.truncate(SIZE));

        EXPECT_TRUE(copyGuarded(out.data(), mapping.data(), SIZE));
    }

    TEST(FaultGuardTest, RepeatedFaultsAreAllCaught) {
        TruncatableMapping mapping(SIZE);
        ASSERT_TRUE(mapping.ready());
        std::vector<std::byte> out(SIZE);
        ASSERT_TRUE(mapping.truncate(0));

        for (int i = 0; i < 100; ++i) {
            EXPECT_FALSE(copyGuarded(out.data(), mapping.data(), SIZE));
        }
    }

    TEST(FaultGuardTest, ZeroBytesNeverFault) {
        std::vector<std::byte> out(1);

        EXPECT_TRUE(copyGuarded(out.data(), out.data(), 0));
    }

    TEST(FaultGuardDeathTest, FaultsOutsideTheGuardStillCrash) {
        GTEST_FLAG_SET(death_test_style, "threadsafe");
        EXPECT_DEATH(
            {
                TruncatableMapping mapping(SIZE);
                std::vector<std::byte> out(SIZE);
                if (!mapping.ready() || !mapping.truncate(0)) std::exit(0);
                static_cast<void>(
                    copyGuarded(out.data(), mapping.data(), SIZE));
                volatile std::byte value = mapping.data()[100];
                static_cast<void>(value);
                std::exit(0);
            },
            "");
    }

}  // namespace
