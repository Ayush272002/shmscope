#include "shmscope/shm_source.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "shmscope/source.hpp"

namespace {

    using shmscope::HEAT_MAX;
    using shmscope::ShmSource;
    using shmscope::Source;

    class TestSegment {
    public:
        explicit TestSegment(std::size_t size) : size_(size) {
            static std::atomic<int> counter{0};
            name_ = std::format("/shmscope.t.{}.{}", ::getpid(), counter++);
            fd_ = ::shm_open(name_.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
            if (fd_ < 0 || size_ == 0) {
                return;
            }
            if (::ftruncate(fd_, static_cast<off_t>(size_)) != 0) {
                return;
            }
            void* base = ::mmap(nullptr, size_, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd_, 0);
            if (base != MAP_FAILED) {
                base_ = static_cast<std::byte*>(base);
            }
        }

        ~TestSegment() {
            if (base_ != nullptr) {
                ::munmap(base_, size_);
            }
            if (fd_ >= 0) {
                ::close(fd_);
                ::shm_unlink(name_.c_str());
            }
        }

        TestSegment(const TestSegment&) = delete;
        TestSegment& operator=(const TestSegment&) = delete;
        TestSegment(TestSegment&&) = delete;
        TestSegment& operator=(TestSegment&&) = delete;

        [[nodiscard]] const std::string& name() const { return name_; }
        [[nodiscard]] bool ready() const { return fd_ >= 0; }

        void write(std::size_t at, std::byte value) { base_[at] = value; }

    private:
        std::string name_;
        std::size_t size_;
        int fd_ = -1;
        std::byte* base_ = nullptr;
    };

    std::unique_ptr<Source> openOrFail(std::string_view name) {
        auto source = ShmSource::open(name);
        if (!source) {
            ADD_FAILURE() << "open failed: " << source.error();
            return nullptr;
        }
        return std::move(*source);
    }

    TEST(ShmSource, OpensAnExistingObject) {
        TestSegment segment(4096);
        ASSERT_TRUE(segment.ready());

        auto source = openOrFail(segment.name());

        ASSERT_NE(source, nullptr);
        EXPECT_EQ(source->name(), segment.name());
    }

    TEST(ShmSource, AddsTheLeadingSlash) {
        TestSegment segment(4096);
        ASSERT_TRUE(segment.ready());

        auto source = openOrFail(std::string_view(segment.name()).substr(1));

        ASSERT_NE(source, nullptr);
        EXPECT_EQ(source->name(), segment.name());
    }

    TEST(ShmSource, MissingObjectIsAnError) {
        auto source = ShmSource::open("/shmscope.no.such.object");

        ASSERT_FALSE(source.has_value());
        EXPECT_NE(source.error().find("/shmscope.no.such.object"),
                  std::string::npos);
    }

    TEST(ShmSource, OverlongNameIsRejectedBeforeOpening) {
        auto source = ShmSource::open(std::string(300, 'x'));

        ASSERT_FALSE(source.has_value());
        EXPECT_NE(source.error().find("limit"), std::string::npos);
    }

    TEST(ShmSource, EmptyObjectIsAnError) {
        TestSegment segment(0);  // created but never sized
        ASSERT_TRUE(segment.ready());

        auto source = ShmSource::open(segment.name());

        ASSERT_FALSE(source.has_value());
        EXPECT_NE(source.error().find("empty"), std::string::npos);
    }

    TEST(ShmSource, FrameCoversTheWholeObject) {
        TestSegment segment(1000);
        ASSERT_TRUE(segment.ready());
        auto source = openOrFail(segment.name());
        ASSERT_NE(source, nullptr);

        // macOS rounds the object up to a whole page, so at least 1000.
        EXPECT_GE(source->poll().bytes.size(), 1000U);
    }

    TEST(ShmSource, FirstFrameShowsContentsAndIsCold) {
        TestSegment segment(4096);
        ASSERT_TRUE(segment.ready());
        segment.write(0, std::byte{0xab});
        auto source = openOrFail(segment.name());
        ASSERT_NE(source, nullptr);

        const auto frame = source->poll();

        EXPECT_EQ(frame.bytes[0], std::byte{0xab});
        for (const auto h : frame.heat) {
            ASSERT_EQ(h, 0);
        }
    }

    TEST(ShmSource, WritesAppearWithFullHeat) {
        TestSegment segment(4096);
        ASSERT_TRUE(segment.ready());
        auto source = openOrFail(segment.name());
        ASSERT_NE(source, nullptr);

        segment.write(100, std::byte{1});
        segment.write(4095, std::byte{2});
        const auto frame = source->poll();

        EXPECT_EQ(frame.bytes[100], std::byte{1});
        EXPECT_EQ(frame.bytes[4095], std::byte{2});
        EXPECT_EQ(frame.heat[100], HEAT_MAX);
        EXPECT_EQ(frame.heat[4095], HEAT_MAX);
        EXPECT_EQ(frame.heat[99], 0);
        EXPECT_EQ(frame.heat[101], 0);
    }

    TEST(ShmSource, HeatFadesOneStepPerPoll) {
        TestSegment segment(4096);
        ASSERT_TRUE(segment.ready());
        auto source = openOrFail(segment.name());
        ASSERT_NE(source, nullptr);

        segment.write(7, std::byte{1});
        ASSERT_EQ(source->poll().heat[7], HEAT_MAX);

        for (int expected = HEAT_MAX - 1; expected >= 0; --expected) {
            EXPECT_EQ(source->poll().heat[7], expected);
        }
        EXPECT_EQ(source->poll().heat[7], 0);  // stays cold
    }

    TEST(ShmSource, SequenceCountsPolls) {
        TestSegment segment(4096);
        ASSERT_TRUE(segment.ready());
        auto source = openOrFail(segment.name());
        ASSERT_NE(source, nullptr);

        EXPECT_EQ(source->poll().sequence, 1U);
        EXPECT_EQ(source->poll().sequence, 2U);
        EXPECT_EQ(source->poll().sequence, 3U);
    }

    TEST(ShmSource, RewritingTheSameValueStaysCold) {
        TestSegment segment(4096);
        ASSERT_TRUE(segment.ready());
        segment.write(9, std::byte{3});
        auto source = openOrFail(segment.name());
        ASSERT_NE(source, nullptr);
        ASSERT_EQ(source->poll().heat[9], 0);

        segment.write(9, std::byte{3});

        EXPECT_EQ(source->poll().heat[9], 0);
    }

    TEST(ShmSource, ContinuousWritesStayHot) {
        TestSegment segment(4096);
        ASSERT_TRUE(segment.ready());
        auto source = openOrFail(segment.name());
        ASSERT_NE(source, nullptr);

        for (int value = 1; value <= 5; ++value) {
            segment.write(64, static_cast<std::byte>(value));
            EXPECT_EQ(source->poll().heat[64], HEAT_MAX);
        }
    }

    TEST(ShmSource, TwoReadersOfOneObjectAreIndependent) {
        TestSegment segment(4096);
        ASSERT_TRUE(segment.ready());
        auto first = openOrFail(segment.name());
        auto second = openOrFail(segment.name());
        ASSERT_NE(first, nullptr);
        ASSERT_NE(second, nullptr);

        segment.write(0, std::byte{1});
        EXPECT_EQ(first->poll().heat[0], HEAT_MAX);
        EXPECT_EQ(first->poll().heat[0], HEAT_MAX - 1);

        const auto frame = second->poll();
        EXPECT_EQ(frame.sequence, 1U);
        EXPECT_EQ(frame.bytes[0], std::byte{1});
        EXPECT_EQ(frame.heat[0], HEAT_MAX);
    }

    TEST(ShmSource, NameAtTheLimitIsNotRejectedForLength) {
        // Long enough to hit macOS's 31, short enough for Linux's 255: the
        // open fails because nothing exists, not because of the length.
        const std::string name = "/" + std::string(30, 'n');

        auto source = ShmSource::open(name);

        ASSERT_FALSE(source.has_value());
        EXPECT_EQ(source.error().find("limit"), std::string::npos);
    }

    TEST(ShmSource, KeepsReadingAfterWriterUnlinks) {
        auto segment = std::make_unique<TestSegment>(4096);
        ASSERT_TRUE(segment->ready());
        segment->write(0, std::byte{5});
        auto source = openOrFail(segment->name());
        ASSERT_NE(source, nullptr);

        segment.reset();  // unmaps and unlinks the name

        EXPECT_EQ(source->poll().bytes[0], std::byte{5});
    }

}  // namespace
