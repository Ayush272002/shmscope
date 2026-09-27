#include "shmscope/decode.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

    using shmscope::ALL_FIELD_TYPES;
    using shmscope::decode;
    using shmscope::FieldType;
    using shmscope::nameOf;
    using shmscope::widthOf;

    std::vector<std::byte> bytes(std::initializer_list<unsigned> values) {
        std::vector<std::byte> out;
        out.reserve(values.size());
        for (const unsigned v : values) {
            out.push_back(static_cast<std::byte>(v));
        }
        return out;
    }

    template <typename T>
    std::vector<std::byte> encode(T value) {
        std::vector<std::byte> out(sizeof(T));
        std::memcpy(out.data(), &value, sizeof(T));
        return out;
    }

    std::string at(FieldType type, std::span<const std::byte> data,
                   std::size_t offset = 0) {
        return decode(type, data, offset).value_or("<none>");
    }

    TEST(DecodeTest, EveryTypeHasAUniqueName) {
        std::set<std::string_view> names;
        for (const FieldType type : ALL_FIELD_TYPES) {
            EXPECT_NE(nameOf(type), "?");
            names.insert(nameOf(type));
        }
        EXPECT_EQ(names.size(), ALL_FIELD_TYPES.size());
    }

    TEST(DecodeTest, WidthsMatchTheTypes) {
        EXPECT_EQ(widthOf(FieldType::U8), 1U);
        EXPECT_EQ(widthOf(FieldType::I8), 1U);
        EXPECT_EQ(widthOf(FieldType::U16), 2U);
        EXPECT_EQ(widthOf(FieldType::I16), 2U);
        EXPECT_EQ(widthOf(FieldType::U32), 4U);
        EXPECT_EQ(widthOf(FieldType::I32), 4U);
        EXPECT_EQ(widthOf(FieldType::F32), 4U);
        EXPECT_EQ(widthOf(FieldType::U64), 8U);
        EXPECT_EQ(widthOf(FieldType::I64), 8U);
        EXPECT_EQ(widthOf(FieldType::F64), 8U);
        EXPECT_EQ(widthOf(FieldType::FIXED8), 8U);
        EXPECT_EQ(widthOf(FieldType::ASCII), shmscope::ASCII_WIDTH);
    }

    TEST(DecodeTest, UnsignedIntegersAreLittleEndian) {
        const auto data =
            bytes({0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08});

        EXPECT_EQ(at(FieldType::U8, data), "1");
        EXPECT_EQ(at(FieldType::U16, data), "513");
        EXPECT_EQ(at(FieldType::U32, data), "67305985");
        EXPECT_EQ(at(FieldType::U64, data), "578437695752307201");
    }

    TEST(DecodeTest, AllOnesIsMaxUnsignedAndMinusOneSigned) {
        const auto data =
            bytes({0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});

        EXPECT_EQ(at(FieldType::U8, data), "255");
        EXPECT_EQ(at(FieldType::U16, data), "65535");
        EXPECT_EQ(at(FieldType::U32, data), "4294967295");
        EXPECT_EQ(at(FieldType::U64, data), "18446744073709551615");
        EXPECT_EQ(at(FieldType::I8, data), "-1");
        EXPECT_EQ(at(FieldType::I16, data), "-1");
        EXPECT_EQ(at(FieldType::I32, data), "-1");
        EXPECT_EQ(at(FieldType::I64, data), "-1");
    }

    TEST(DecodeTest, SingleBytesPrintAsNumbersNotCharacters) {
        const auto data = bytes({0x41});

        EXPECT_EQ(at(FieldType::U8, data), "65");
        EXPECT_EQ(at(FieldType::I8, data), "65");
    }

    TEST(DecodeTest, SignedExtremes) {
        EXPECT_EQ(
            at(FieldType::I8, encode(std::numeric_limits<std::int8_t>::min())),
            "-128");
        EXPECT_EQ(at(FieldType::I64,
                     encode(std::numeric_limits<std::int64_t>::min())),
                  "-9223372036854775808");
        EXPECT_EQ(at(FieldType::I64,
                     encode(std::numeric_limits<std::int64_t>::max())),
                  "9223372036854775807");
    }

    TEST(DecodeTest, WriterSequenceFromTheFeed) {
        const auto data =
            bytes({0xcc, 0x20, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00});

        EXPECT_EQ(at(FieldType::U64, data), "139468");
    }

    TEST(DecodeTest, Fixed8ScalesByOneHundredMillion) {
        EXPECT_EQ(
            at(FieldType::FIXED8, encode<std::int64_t>(8'145'200'000'000)),
            "81452.00000000");
        EXPECT_EQ(at(FieldType::FIXED8, encode<std::int64_t>(271'179'000'000)),
                  "2711.79000000");
        EXPECT_EQ(at(FieldType::FIXED8, encode<std::int64_t>(1)), "0.00000001");
        EXPECT_EQ(at(FieldType::FIXED8, encode<std::int64_t>(0)), "0.00000000");
    }

    TEST(DecodeTest, Fixed8Negative) {
        EXPECT_EQ(at(FieldType::FIXED8, encode<std::int64_t>(-150'000'000)),
                  "-1.50000000");
        EXPECT_EQ(at(FieldType::FIXED8, encode<std::int64_t>(-1)),
                  "-0.00000001");
    }

    TEST(DecodeTest, Fixed8MinimumDoesNotOverflow) {
        EXPECT_EQ(at(FieldType::FIXED8,
                     encode(std::numeric_limits<std::int64_t>::min())),
                  "-92233720368.54775808");
    }

    TEST(DecodeTest, Fixed8IsExactWhereDoubleWouldRound) {
        EXPECT_EQ(at(FieldType::FIXED8,
                     encode<std::int64_t>(123'456'789'012'345'678)),
                  "1234567890.12345678");
    }

    TEST(DecodeTest, Floats) {
        EXPECT_EQ(at(FieldType::F64, encode(1.5)), "1.5");
        EXPECT_EQ(at(FieldType::F64, encode(-0.25)), "-0.25");
        EXPECT_EQ(at(FieldType::F32, encode(3.0F)), "3");
        EXPECT_EQ(at(FieldType::F64, encode(1e-300)), "1e-300");
    }

    TEST(DecodeTest, FloatSpecialValues) {
        EXPECT_EQ(
            at(FieldType::F64, encode(std::numeric_limits<double>::infinity())),
            "inf");
        EXPECT_EQ(at(FieldType::F64,
                     encode(-std::numeric_limits<double>::infinity())),
                  "-inf");
        EXPECT_EQ(at(FieldType::F64,
                     encode(std::numeric_limits<double>::quiet_NaN())),
                  "nan");
    }

    TEST(DecodeTest, AsciiShowsPrintableAndDotsTheRest) {
        const auto data =
            bytes({'q', 'c', 'm', 'd', 's', 'e', 'g', 0x00, 'x', 'y'});

        EXPECT_EQ(at(FieldType::ASCII, data), "qcmdseg.");
    }

    TEST(DecodeTest, AsciiDotsControlAndHighBytes) {
        const auto data = bytes({0x1f, 0x20, 0x7e, 0x7f, 0x80, 0xff, 'A', 'z'});

        EXPECT_EQ(at(FieldType::ASCII, data), ". ~...Az");
    }

    TEST(DecodeTest, ReadsAtAnUnalignedOffset) {
        const auto data =
            bytes({0xaa, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

        EXPECT_EQ(at(FieldType::U64, data, 1), "1");
        EXPECT_EQ(at(FieldType::U16, data, 1), "1");
    }

    TEST(DecodeTest, AValueEndingExactlyAtTheEndIsRead) {
        const auto data = bytes({0x00, 0x00, 0x34, 0x12});

        EXPECT_EQ(at(FieldType::U16, data, 2), "4660");
        EXPECT_EQ(at(FieldType::U8, data, 3), "18");
    }

    TEST(DecodeTest, AValueRunningPastTheEndIsNotRead) {
        const auto data = bytes({0x01, 0x02, 0x03});

        EXPECT_FALSE(decode(FieldType::U32, data, 0).has_value());
        EXPECT_FALSE(decode(FieldType::U16, data, 2).has_value());
        EXPECT_FALSE(decode(FieldType::ASCII, data, 0).has_value());
    }

    TEST(DecodeTest, OffsetAtOrPastTheEndIsNotRead) {
        const auto data = bytes({0x01, 0x02});

        EXPECT_FALSE(decode(FieldType::U8, data, 2).has_value());
        EXPECT_FALSE(decode(FieldType::U8, data, 100).has_value());
        EXPECT_FALSE(
            decode(FieldType::U8, data, std::numeric_limits<std::size_t>::max())
                .has_value());
    }

    TEST(DecodeTest, EmptyBytesReadNothing) {
        const std::vector<std::byte> empty;

        for (const FieldType type : ALL_FIELD_TYPES) {
            EXPECT_FALSE(decode(type, empty, 0).has_value()) << nameOf(type);
        }
    }

}  // namespace
