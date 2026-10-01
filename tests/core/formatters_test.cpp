#include "shmscope/core/formatters.hpp"

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "shmscope/core/decode.hpp"
#include "shmscope/core/default_formatters.hpp"
#include "shmscope/core/format.hpp"

namespace {

    using shmscope::DefaultFormatters;
    using shmscope::FieldType;
    using shmscope::FormatSpec;
    using shmscope::Formatter;
    using shmscope::OptionsFormatter;
    using shmscope::PlainFormatter;

    static_assert(PlainFormatter<shmscope::DecimalFormatter>);
    static_assert(PlainFormatter<shmscope::HexFormatter>);
    static_assert(OptionsFormatter<shmscope::ScaledFormatter>);
    static_assert(OptionsFormatter<shmscope::EnumFormatter>);
    static_assert(OptionsFormatter<shmscope::TimestampFormatter>);

    static_assert(DefaultFormatters::knows("decimal"));
    static_assert(DefaultFormatters::knows("hex"));
    static_assert(DefaultFormatters::knows("scaled"));
    static_assert(DefaultFormatters::knows("enum"));
    static_assert(DefaultFormatters::knows("timestamp"));
    static_assert(!DefaultFormatters::knows("fixed8"));

    template <typename T>
    std::vector<std::byte> encode(T value) {
        std::vector<std::byte> out(sizeof(T));
        std::memcpy(out.data(), &value, sizeof(T));
        return out;
    }

    template <typename T>
    std::string show(FieldType type, T raw, const FormatSpec& spec) {
        const auto compiled = DefaultFormatters::compile(spec);
        if (!compiled) {
            return "error: " + compiled.error();
        }
        const auto bytes = encode(raw);
        const auto value = shmscope::read(type, bytes, 0);
        if (!value) {
            return "<unreadable>";
        }
        return DefaultFormatters::apply(*compiled, *value);
    }

    std::string error(const FormatSpec& spec) {
        const auto compiled = DefaultFormatters::compile(spec);
        return compiled ? std::string("<compiled>") : compiled.error();
    }

    FormatSpec scaled(std::string digits) {
        return {.kind = "scaled", .options = {{"digits", std::move(digits)}}};
    }

    FormatSpec timestamp(std::string unit) {
        return {.kind = "timestamp", .options = {{"unit", std::move(unit)}}};
    }

    const FormatSpec SIDE{.kind = "enum",
                          .entries = {{"0", "bid"}, {"1", "ask"}}};

    TEST(DecimalFormatterTest, MatchesPlainDecoding) {
        EXPECT_EQ(show(FieldType::U32, std::uint32_t{42}, {.kind = "decimal"}),
                  "42");
        EXPECT_EQ(show(FieldType::I8, std::int8_t{-1}, {.kind = "decimal"}),
                  "-1");
        EXPECT_EQ(show(FieldType::F64, 1.5, {.kind = "decimal"}), "1.5");
    }

    TEST(DecimalFormatterTest, RejectsOptions) {
        EXPECT_EQ(error({.kind = "decimal", .options = {{"digits", "2"}}}),
                  "format 'decimal': takes no options");
    }

    TEST(HexFormatterTest, PadsToTheFieldWidth) {
        EXPECT_EQ(show(FieldType::U8, std::uint8_t{0x0a}, {.kind = "hex"}),
                  "0x0a");
        EXPECT_EQ(show(FieldType::U16, std::uint16_t{0xbeef}, {.kind = "hex"}),
                  "0xbeef");
        EXPECT_EQ(show(FieldType::U32, std::uint32_t{0xbeef}, {.kind = "hex"}),
                  "0x0000beef");
        EXPECT_EQ(show(FieldType::U64, std::uint64_t{1}, {.kind = "hex"}),
                  "0x0000000000000001");
    }

    TEST(HexFormatterTest, SignedValuesShowTheirBitsAtTheirOwnWidth) {
        EXPECT_EQ(show(FieldType::I8, std::int8_t{-1}, {.kind = "hex"}),
                  "0xff");
        EXPECT_EQ(show(FieldType::I16, std::int16_t{-2}, {.kind = "hex"}),
                  "0xfffe");
        EXPECT_EQ(show(FieldType::I32, std::int32_t{-1}, {.kind = "hex"}),
                  "0xffffffff");
        EXPECT_EQ(show(FieldType::I64, std::int64_t{-1}, {.kind = "hex"}),
                  "0xffffffffffffffff");
    }

    TEST(HexFormatterTest, FloatsAndTextFallBackToDecimal) {
        EXPECT_EQ(show(FieldType::F64, 2.5, {.kind = "hex"}), "2.5");
        EXPECT_EQ(show(FieldType::ASCII, std::uint64_t{0x4142434445464748},
                       {.kind = "hex"}),
                  "HGFEDCBA");
    }

    TEST(HexFormatterTest, RejectsOptions) {
        EXPECT_EQ(error({.kind = "hex", .options = {{"width", "4"}}}),
                  "format 'hex': takes no options");
    }

    TEST(ScaledFormatterTest, PlacesTheDecimalPoint) {
        EXPECT_EQ(
            show(FieldType::I64, std::int64_t{271'179'000'000}, scaled("8")),
            "2711.79000000");
        EXPECT_EQ(show(FieldType::I32, std::int32_t{1505}, scaled("2")),
                  "15.05");
        EXPECT_EQ(show(FieldType::U16, std::uint16_t{5}, scaled("3")), "0.005");
    }

    TEST(ScaledFormatterTest, Negative) {
        EXPECT_EQ(show(FieldType::I32, std::int32_t{-1505}, scaled("2")),
                  "-15.05");
        EXPECT_EQ(show(FieldType::I64, std::int64_t{-1}, scaled("8")),
                  "-0.00000001");
    }

    TEST(ScaledFormatterTest, ZeroDigitsIsAPlainInteger) {
        EXPECT_EQ(show(FieldType::U16, std::uint16_t{42}, scaled("0")), "42");
        EXPECT_EQ(show(FieldType::I16, std::int16_t{-42}, scaled("0")), "-42");
    }

    TEST(ScaledFormatterTest, ExtremesDoNotOverflow) {
        EXPECT_EQ(show(FieldType::I64, std::numeric_limits<std::int64_t>::min(),
                       scaled("8")),
                  "-92233720368.54775808");
        EXPECT_EQ(show(FieldType::U64,
                       std::numeric_limits<std::uint64_t>::max(), scaled("18")),
                  "18.446744073709551615");
    }

    TEST(ScaledFormatterTest, IsExactWhereDoubleWouldRound) {
        EXPECT_EQ(show(FieldType::I64, std::int64_t{123'456'789'012'345'678},
                       scaled("8")),
                  "1234567890.12345678");
    }

    TEST(ScaledFormatterTest, FloatsFallBackToDecimal) {
        EXPECT_EQ(show(FieldType::F64, 1.5, scaled("2")), "1.5");
    }

    TEST(ScaledFormatterTest, DigitsAcceptsHex) {
        EXPECT_EQ(show(FieldType::U32, std::uint32_t{1234}, scaled("0x2")),
                  "12.34");
    }

    TEST(ScaledFormatterTest, NeedsDigits) {
        EXPECT_EQ(error({.kind = "scaled"}), "format 'scaled': needs 'digits'");
    }

    TEST(ScaledFormatterTest, DigitsOutOfRange) {
        EXPECT_EQ(error(scaled("19")),
                  "format 'scaled': 'digits' must be 0 to 18, got '19'");
        EXPECT_EQ(error(scaled("-1")),
                  "format 'scaled': 'digits' must be 0 to 18, got '-1'");
        EXPECT_EQ(error(scaled("eight")),
                  "format 'scaled': 'digits' must be 0 to 18, got 'eight'");
        EXPECT_EQ(error(scaled("")),
                  "format 'scaled': 'digits' must be 0 to 18, got ''");
    }

    TEST(ScaledFormatterTest, RejectsUnknownOptionsAndTables) {
        EXPECT_EQ(error({.kind = "scaled",
                         .options = {{"digits", "2"}, {"round", "yes"}}}),
                  "format 'scaled': unknown option 'round'");
        EXPECT_EQ(error({.kind = "scaled",
                         .options = {{"digits", "2"}},
                         .entries = {{"1", "a"}}}),
                  "format 'scaled': does not take a table of values");
    }

    TEST(EnumFormatterTest, NamesKnownValues) {
        EXPECT_EQ(show(FieldType::U8, std::uint8_t{0}, SIDE), "bid");
        EXPECT_EQ(show(FieldType::U8, std::uint8_t{1}, SIDE), "ask");
    }

    TEST(EnumFormatterTest, UnknownValuesShowTheNumber) {
        EXPECT_EQ(show(FieldType::U8, std::uint8_t{7}, SIDE), "7");
    }

    TEST(EnumFormatterTest, WorksOnEveryIntegerWidth) {
        EXPECT_EQ(show(FieldType::U32, std::uint32_t{1}, SIDE), "ask");
        EXPECT_EQ(show(FieldType::U64, std::uint64_t{1}, SIDE), "ask");
        EXPECT_EQ(show(FieldType::I16, std::int16_t{1}, SIDE), "ask");
    }

    TEST(EnumFormatterTest, NegativeKeysMatchSignedFields) {
        const FormatSpec state{.kind = "enum",
                               .entries = {{"-1", "none"}, {"0", "zero"}}};

        EXPECT_EQ(show(FieldType::I8, std::int8_t{-1}, state), "none");
        EXPECT_EQ(show(FieldType::I64, std::int64_t{-1}, state), "none");
        EXPECT_EQ(show(FieldType::I32, std::int32_t{0}, state), "zero");
    }

    TEST(EnumFormatterTest, HexKeys) {
        const FormatSpec kind{.kind = "enum", .entries = {{"0x10", "sixteen"}}};

        EXPECT_EQ(show(FieldType::U8, std::uint8_t{16}, kind), "sixteen");
    }

    TEST(EnumFormatterTest, FloatsFallBackToDecimal) {
        EXPECT_EQ(show(FieldType::F64, 1.0, SIDE), "1");
    }

    TEST(EnumFormatterTest, NeedsValues) {
        EXPECT_EQ(error({.kind = "enum"}),
                  "format 'enum': needs at least one value");
    }

    TEST(EnumFormatterTest, KeysMustBeIntegers) {
        EXPECT_EQ(error({.kind = "enum", .entries = {{"one", "a"}}}),
                  "format 'enum': 'one' is not an integer");
        EXPECT_EQ(error({.kind = "enum", .entries = {{"", "a"}}}),
                  "format 'enum': '' is not an integer");
        EXPECT_EQ(
            error({.kind = "enum", .entries = {{"-9223372036854775809", "a"}}}),
            "format 'enum': '-9223372036854775809' is not an integer");
    }

    TEST(EnumFormatterTest, TheSmallestSignedKeyIsAccepted) {
        const FormatSpec minimum{.kind = "enum",
                                 .entries = {{"-9223372036854775808", "min"}}};

        EXPECT_EQ(show(FieldType::I64, std::numeric_limits<std::int64_t>::min(),
                       minimum),
                  "min");
    }

    TEST(EnumFormatterTest, DuplicateKeysAreRejectedAcrossSpellings) {
        EXPECT_EQ(
            error({.kind = "enum", .entries = {{"1", "a"}, {"0x1", "b"}}}),
            "format 'enum': value '0x1' is listed twice");
    }

    TEST(EnumFormatterTest, RejectsOptions) {
        EXPECT_EQ(error({.kind = "enum",
                         .options = {{"default", "x"}},
                         .entries = {{"1", "a"}}}),
                  "format 'enum': unknown option 'default'");
    }

    TEST(TimestampFormatterTest, NanosecondsByDefault) {
        EXPECT_EQ(show(FieldType::U64, std::uint64_t{1'790'000'000'123'456'789},
                       {.kind = "timestamp"}),
                  "2026-09-21 14:13:20.123456789 UTC");
    }

    TEST(TimestampFormatterTest, EachUnit) {
        EXPECT_EQ(
            show(FieldType::U32, std::uint32_t{1'790'000'000}, timestamp("s")),
            "2026-09-21 14:13:20 UTC");
        EXPECT_EQ(show(FieldType::I64, std::int64_t{1'790'000'000'123},
                       timestamp("ms")),
                  "2026-09-21 14:13:20.123 UTC");
        EXPECT_EQ(show(FieldType::I64, std::int64_t{1'790'000'000'123'456},
                       timestamp("us")),
                  "2026-09-21 14:13:20.123456 UTC");
        EXPECT_EQ(show(FieldType::I64, std::int64_t{1'790'000'000'123'456'789},
                       timestamp("ns")),
                  "2026-09-21 14:13:20.123456789 UTC");
    }

    TEST(TimestampFormatterTest, ZeroIsTheEpoch) {
        EXPECT_EQ(show(FieldType::U64, std::uint64_t{0}, timestamp("s")),
                  "1970-01-01 00:00:00 UTC");
    }

    TEST(TimestampFormatterTest, BeforeTheEpoch) {
        EXPECT_EQ(show(FieldType::I64, std::int64_t{-1}, timestamp("s")),
                  "1969-12-31 23:59:59 UTC");
    }

    TEST(TimestampFormatterTest, OutOfCalendarRangeFallsBackToTheNumber) {
        EXPECT_EQ(show(FieldType::I64, std::numeric_limits<std::int64_t>::max(),
                       timestamp("s")),
                  "9223372036854775807");
        EXPECT_EQ(show(FieldType::I64, std::numeric_limits<std::int64_t>::min(),
                       timestamp("s")),
                  "-9223372036854775808");
    }

    TEST(TimestampFormatterTest, UnsignedAboveSignedRangeFallsBack) {
        EXPECT_EQ(
            show(FieldType::U64, std::numeric_limits<std::uint64_t>::max(),
                 timestamp("ns")),
            "18446744073709551615");
    }

    TEST(TimestampFormatterTest, FloatsFallBackToDecimal) {
        EXPECT_EQ(show(FieldType::F64, 1.5, timestamp("s")), "1.5");
    }

    TEST(TimestampFormatterTest, RejectsBadUnit) {
        EXPECT_EQ(
            error(timestamp("h")),
            "format 'timestamp': 'unit' must be s, ms, us or ns, got 'h'");
        EXPECT_EQ(
            error(timestamp("NS")),
            "format 'timestamp': 'unit' must be s, ms, us or ns, got 'NS'");
    }

    TEST(TimestampFormatterTest, RejectsUnknownOptionsAndTables) {
        EXPECT_EQ(error({.kind = "timestamp", .options = {{"zone", "UTC"}}}),
                  "format 'timestamp': unknown option 'zone'");
        EXPECT_EQ(error({.kind = "timestamp", .entries = {{"0", "epoch"}}}),
                  "format 'timestamp': does not take a table of values");
    }

    TEST(DefaultFormattersTest, UnknownKindListsEveryFormatter) {
        EXPECT_EQ(error({.kind = "fixed8"}),
                  "unknown format 'fixed8' (known: decimal, hex, scaled, enum, "
                  "timestamp)");
    }

}  // namespace
