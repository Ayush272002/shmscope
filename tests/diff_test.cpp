#include "shmscope/diff.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include <gtest/gtest.h>
#include <hwy/targets.h>

#include "shmscope/source.hpp"

namespace {

    using shmscope::copyDiff;
    using shmscope::HEAT_MAX;

    void referenceCopyDiff(const std::vector<std::byte>& mapping,
                           std::vector<std::byte>& current,
                           const std::vector<std::byte>& previous,
                           std::vector<std::uint8_t>& heat) {
        for (std::size_t i = 0; i < mapping.size(); ++i) {
            current[i] = mapping[i];
            if (current[i] != previous[i]) {
                heat[i] = HEAT_MAX;
            } else if (heat[i] > 0) {
                --heat[i];
            }
        }
    }

    struct Buffers {
        explicit Buffers(std::size_t size)
            : mapping(size), current(size), previous(size), heat(size) {}

        void run() {
            copyDiff(mapping.data(), current.data(), previous.data(),
                     heat.data(), mapping.size());
        }

        std::vector<std::byte> mapping;
        std::vector<std::byte> current;
        std::vector<std::byte> previous;
        std::vector<std::uint8_t> heat;
    };

    class CopyDiffSizes : public ::testing::TestWithParam<std::size_t> {};

    INSTANTIATE_TEST_SUITE_P(Boundaries, CopyDiffSizes,
                             ::testing::Values(0, 1, 7, 15, 16, 17, 31, 32, 33,
                                               63, 64, 65, 127, 128, 129, 255,
                                               256, 257, 1000, 4099, 65536));

    void expectMatchesReference(std::size_t size) {
        std::mt19937 rng(static_cast<unsigned>(size));
        Buffers simd(size);
        for (std::size_t i = 0; i < size; ++i) {
            // Few distinct values so roughly a quarter of bytes are equal.
            simd.mapping[i] = static_cast<std::byte>(rng() % 4);
            simd.previous[i] = static_cast<std::byte>(rng() % 4);
            simd.heat[i] = static_cast<std::uint8_t>(rng() % (HEAT_MAX + 1));
        }
        Buffers scalar = simd;

        simd.run();
        referenceCopyDiff(scalar.mapping, scalar.current, scalar.previous,
                          scalar.heat);

        EXPECT_EQ(simd.current, scalar.current);
        EXPECT_EQ(simd.heat, scalar.heat);
    }

    TEST_P(CopyDiffSizes, MatchesScalarReference) {
        expectMatchesReference(GetParam());
    }

    TEST(CopyDiff, EveryTargetMatchesScalarReference) {
        for (const std::int64_t target : hwy::SupportedAndGeneratedTargets()) {
            SCOPED_TRACE(hwy::TargetName(target));
            hwy::SetSupportedTargetsForTest(target);
            for (const std::size_t size :
                 std::array<std::size_t, 6>{0, 1, 17, 65, 257, 4099}) {
                expectMatchesReference(size);
            }
        }
        hwy::SetSupportedTargetsForTest(0);
    }

    TEST(CopyDiff, EveryByteChangedIsAllHot) {
        Buffers b(1000);
        for (auto& byte : b.mapping) {
            byte = std::byte{0xff};
        }

        b.run();

        for (const auto h : b.heat) {
            ASSERT_EQ(h, HEAT_MAX);
        }
    }

    TEST(CopyDiff, HeatReachesZeroAfterHeatMaxQuietPolls) {
        Buffers b(100);
        b.mapping[3] = std::byte{1};
        b.run();
        b.previous = b.current;

        for (int poll = 0; poll < HEAT_MAX; ++poll) {
            b.run();
        }

        EXPECT_EQ(b.heat[3], 0);
    }

    TEST(CopyDiff, CopiesMappingIntoCurrent) {
        Buffers b(100);
        for (std::size_t i = 0; i < b.mapping.size(); ++i) {
            b.mapping[i] = static_cast<std::byte>(i);
        }

        b.run();

        EXPECT_EQ(b.current, b.mapping);
    }

    TEST(CopyDiff, ChangedByteGetsFullHeat) {
        Buffers b(100);
        b.mapping[42] = std::byte{1};

        b.run();

        EXPECT_EQ(b.heat[42], HEAT_MAX);
        EXPECT_EQ(b.heat[41], 0);
        EXPECT_EQ(b.heat[43], 0);
    }

    TEST(CopyDiff, UnchangedByteDecaysByOne) {
        Buffers b(100);
        b.heat[10] = HEAT_MAX;
        b.heat[11] = 1;

        b.run();

        EXPECT_EQ(b.heat[10], HEAT_MAX - 1);
        EXPECT_EQ(b.heat[11], 0);
    }

    TEST(CopyDiff, ColdByteStaysAtZero) {
        Buffers b(100);

        b.run();

        for (const auto h : b.heat) {
            ASSERT_EQ(h, 0);
        }
    }

    TEST(CopyDiff, ChangeResetsPartlyDecayedHeat) {
        Buffers b(100);
        b.heat[5] = 3;
        b.mapping[5] = std::byte{9};

        b.run();

        EXPECT_EQ(b.heat[5], HEAT_MAX);
    }

}  // namespace
