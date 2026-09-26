#include "shmscope/diff.hpp"

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "shmscope/source.hpp"

namespace {

    using shmscope::copyDiff;
    using shmscope::HEAT_MAX;

    // Byte at a time reference the SIMD version must agree with.
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

    // Buffers sized exactly, so ASan flags any read or write past the end.
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

    // Sizes straddle every loop boundary: empty, the scalar tail, one vector
    // (16 on NEON, 32 on AVX2, 64 on AVX-512), the 4x unrolled block, and
    // a page plus a few bytes.
    INSTANTIATE_TEST_SUITE_P(Boundaries, CopyDiffSizes,
                             ::testing::Values(0, 1, 7, 15, 16, 17, 31, 32, 33,
                                               63, 64, 65, 127, 128, 129, 255,
                                               256, 257, 1000, 4099, 65536));

    TEST_P(CopyDiffSizes, MatchesScalarReference) {
        const std::size_t size = GetParam();
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
