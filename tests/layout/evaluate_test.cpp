#include "shmscope/layout/evaluate.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <ostream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "shmscope/layout/expression.hpp"

namespace {

    using shmscope::compile;
    using shmscope::evaluate;
    using shmscope::Instruction;
    using shmscope::Lookup;
    using shmscope::OpCode;
    using shmscope::Program;
    using shmscope::Resolver;

    constexpr std::int64_t MAX = std::numeric_limits<std::int64_t>::max();
    constexpr std::int64_t MIN = std::numeric_limits<std::int64_t>::min();

    struct FakeResolver {
        struct Ref {
            std::string path;
        };

        std::map<std::string, std::int64_t, std::less<>> numbers{
            {"header.count", 3},       {"header.size", 64},
            {"header.capacity", 8192}, {"header.slots_offset", 256},
            {"items[0].price", 10},    {"items[1].price", 20},
            {"items[2].price", 30},    {"field", 7},
        };
        int names = 0;
        int members = 0;
        int elements = 0;
        int reads = 0;

        Lookup<Ref> name(std::string_view text) {
            ++names;
            if (text == "n") return std::int64_t{5};
            if (text == "big") return MAX;
            if (text == "small") return MIN;
            if (text == "zero") return std::int64_t{0};
            if (text == "header" || text == "items" || text == "field") {
                return Ref{std::string(text)};
            }
            return std::unexpected(std::format("unknown name '{}'", text));
        }

        Lookup<Ref> member(const Ref& ref, std::string_view text) {
            ++members;
            return Ref{ref.path + "." + std::string(text)};
        }

        Lookup<Ref> element(const Ref& ref, std::int64_t index) {
            ++elements;
            if (ref.path != "items") {
                return std::unexpected(
                    std::format("'{}' is not a list", ref.path));
            }
            if (index < 0 || index > 2) {
                return std::unexpected(
                    std::format("index {} is out of range (3 items)", index));
            }
            return Ref{std::format("items[{}]", index)};
        }

        std::expected<std::int64_t, std::string> number(const Ref& ref) {
            ++reads;
            const auto found = numbers.find(ref.path);
            if (found == numbers.end()) {
                return std::unexpected(
                    std::format("'{}' is not a number", ref.path));
            }
            return found->second;
        }
    };

    struct MissingRef {};

    struct IntegerRef {
        using Ref = std::int64_t;
        Lookup<Ref> name(std::string_view);
        Lookup<Ref> member(const Ref&, std::string_view);
        Lookup<Ref> element(const Ref&, std::int64_t);
        std::expected<std::int64_t, std::string> number(const Ref&);
    };

    struct NoNumber {
        struct Ref {};
        Lookup<Ref> name(std::string_view);
        Lookup<Ref> member(const Ref&, std::string_view);
        Lookup<Ref> element(const Ref&, std::int64_t);
    };

    struct WrongNameReturn {
        struct Ref {};
        std::int64_t name(std::string_view);
        Lookup<Ref> member(const Ref&, std::string_view);
        Lookup<Ref> element(const Ref&, std::int64_t);
        std::expected<std::int64_t, std::string> number(const Ref&);
    };

    struct MoveOnlyRef {
        struct Ref {
            Ref() = default;
            Ref(const Ref&) = delete;
            Ref(Ref&&) = default;
            Ref& operator=(const Ref&) = delete;
            Ref& operator=(Ref&&) = default;
        };
        Lookup<Ref> name(std::string_view);
        Lookup<Ref> member(const Ref&, std::string_view);
        Lookup<Ref> element(const Ref&, std::int64_t);
        std::expected<std::int64_t, std::string> number(const Ref&);
    };

    static_assert(Resolver<FakeResolver>);
    static_assert(!Resolver<MissingRef>);
    static_assert(!Resolver<IntegerRef>);
    static_assert(!Resolver<NoNumber>);
    static_assert(!Resolver<WrongNameReturn>);
    static_assert(!Resolver<MoveOnlyRef>);
    static_assert(!Resolver<int>);

    std::string run(std::string_view text, FakeResolver& resolver) {
        const auto program = compile(text);
        if (!program) {
            return "compile error: " + program.error().message;
        }
        const auto value = evaluate(*program, resolver);
        if (!value) {
            return std::format("{}: {}", value.error().column,
                               value.error().message);
        }
        return std::to_string(*value);
    }

    std::string run(std::string_view text) {
        FakeResolver resolver;
        return run(text, resolver);
    }

    struct Case {
        std::string_view text;
        std::string_view expected;
    };

    void PrintTo(const Case& value, std::ostream* out) {
        *out << '"' << value.text << '"';
    }

    class EvaluatesTo : public ::testing::TestWithParam<Case> {};

    TEST_P(EvaluatesTo, TheExpectedResult) {
        EXPECT_EQ(run(GetParam().text), GetParam().expected);
    }

    INSTANTIATE_TEST_SUITE_P(
        Arithmetic, EvaluatesTo,
        ::testing::Values(Case{"0", "0"}, Case{"42", "42"}, Case{"1 + 2", "3"},
                          Case{"2 - 5", "-3"}, Case{"6 * 7", "42"},
                          Case{"1 + 2 * 3", "7"}, Case{"(1 + 2) * 3", "9"},
                          Case{"10 - 4 - 3", "3"}, Case{"100 / 10 / 5", "2"},
                          Case{"-5", "-5"}, Case{"--5", "5"},
                          Case{"-(2 + 3)", "-5"},
                          Case{"0x10 + 0b11 + 0o7", "26"},
                          Case{"9223372036854775807", "9223372036854775807"}));

    INSTANTIATE_TEST_SUITE_P(
        FloorDivision, EvaluatesTo,
        ::testing::Values(Case{"7 / 2", "3"}, Case{"-7 / 2", "-4"},
                          Case{"7 / -2", "-4"}, Case{"-7 / -2", "3"},
                          Case{"6 / 3", "2"}, Case{"-6 / 3", "-2"},
                          Case{"0 / -5", "0"},
                          Case{"small / 1", "-9223372036854775808"},
                          Case{"small / 2", "-4611686018427387904"}));

    INSTANTIATE_TEST_SUITE_P(
        FloorModulo, EvaluatesTo,
        ::testing::Values(Case{"7 % 3", "1"}, Case{"-7 % 3", "2"},
                          Case{"7 % -3", "-2"}, Case{"-7 % -3", "-1"},
                          Case{"6 % 3", "0"}, Case{"-6 % 3", "0"},
                          Case{"small % -1", "0"}, Case{"big % -1", "0"},
                          Case{"small % 7", "6"}, Case{"big % 7", "0"}));

    INSTANTIATE_TEST_SUITE_P(
        Bitwise, EvaluatesTo,
        ::testing::Values(Case{"0xf0 & 0x3c", "48"}, Case{"0xf0 | 0x0f", "255"},
                          Case{"0xff ^ 0x0f", "240"}, Case{"~0", "-1"},
                          Case{"~-1", "0"}, Case{"1 << 0", "1"},
                          Case{"1 << 10", "1024"},
                          Case{"1 << 62", "4611686018427387904"},
                          Case{"-1 << 63", "-9223372036854775808"},
                          Case{"1024 >> 3", "128"}, Case{"-8 >> 1", "-4"},
                          Case{"-1 >> 63", "-1"}, Case{"big >> 63", "0"},
                          Case{"0xff & 0x0f | 0x100 ^ 1", "271"}));

    INSTANTIATE_TEST_SUITE_P(
        Resolved, EvaluatesTo,
        ::testing::Values(
            Case{"n", "5"}, Case{"n * n", "25"}, Case{"field", "7"},
            Case{"field + 1", "8"}, Case{"header.count", "3"},
            Case{"header.count * 8", "24"},
            Case{"header.size - header.count", "61"},
            Case{"items[0].price", "10"}, Case{"items[2].price", "30"},
            Case{"items[header.count - 1].price", "30"},
            Case{"items[n - 4].price + items[0].price", "30"},
            Case{"items[field - 7].price", "10"},
            Case{"header.slots_offset + (header.capacity * 8 + 127) / 128 * "
                 "128",
                 "65792"}));

    INSTANTIATE_TEST_SUITE_P(
        Overflow, EvaluatesTo,
        ::testing::Values(Case{"big + 1",
                               "5: the result does not fit in a signed 64 bit "
                               "integer"},
                          Case{"small - 1",
                               "7: the result does not fit in a signed 64 bit "
                               "integer"},
                          Case{"big * 2",
                               "5: the result does not fit in a signed 64 bit "
                               "integer"},
                          Case{"small * -1",
                               "7: the result does not fit in a signed 64 bit "
                               "integer"},
                          Case{"-small",
                               "1: the result does not fit in a signed 64 bit "
                               "integer"},
                          Case{"small / -1",
                               "7: the result does not fit in a signed 64 bit "
                               "integer"},
                          Case{"big << 1",
                               "5: the result does not fit in a signed 64 bit "
                               "integer"},
                          Case{"2 << 62",
                               "3: the result does not fit in a signed 64 bit "
                               "integer"},
                          Case{"-2 << 63",
                               "4: the result does not fit in a signed 64 bit "
                               "integer"}));

    INSTANTIATE_TEST_SUITE_P(
        BadOperands, EvaluatesTo,
        ::testing::Values(
            Case{"1 / 0", "3: division by zero"},
            Case{"1 / zero", "3: division by zero"},
            Case{"1 % 0", "3: modulo by zero"},
            Case{"1 << 64", "3: a shift by 64 is out of range; use 0 to 63"},
            Case{"1 << -1", "3: a shift by -1 is out of range; use 0 to 63"},
            Case{"1 >> 64", "3: a shift by 64 is out of range; use 0 to 63"},
            Case{"1 >> big",
                 "3: a shift by 9223372036854775807 is out of range; use 0 "
                 "to 63"}));

    INSTANTIATE_TEST_SUITE_P(
        BadReferences, EvaluatesTo,
        ::testing::Values(
            Case{"nope", "1: unknown name 'nope'"},
            Case{"1 + nope", "5: unknown name 'nope'"},
            Case{"header", "1: 'header' is not a number"},
            Case{"header.missing", "1: 'header.missing' is not a number"},
            Case{"3 * header", "3: 'header' is not a number"},
            Case{"items[3].price", "6: index 3 is out of range (3 items)"},
            Case{"items[-1]", "6: index -1 is out of range (3 items)"},
            Case{"header[0]", "7: 'header' is not a list"},
            Case{"items[header].price", "6: 'header' is not a number"},
            Case{"5.x", "3: a number has no member 'x'"},
            Case{"n.x", "3: a number has no member 'x'"},
            Case{"n[0]", "2: a number has no elements"},
            Case{"(1 + 2)[0]", "8: a number has no elements"}));

    TEST(EvaluateTest, ReferencesAreReadOnlyWhenNeeded) {
        FakeResolver resolver;

        EXPECT_EQ(run("items[1].price", resolver), "20");
        EXPECT_EQ(resolver.names, 1);
        EXPECT_EQ(resolver.elements, 1);
        EXPECT_EQ(resolver.members, 1);
        EXPECT_EQ(resolver.reads, 1);
    }

    TEST(EvaluateTest, EachOperandIsReadOnce) {
        FakeResolver resolver;

        EXPECT_EQ(run("header.count + header.count", resolver), "6");
        EXPECT_EQ(resolver.reads, 2);
        EXPECT_EQ(resolver.members, 2);
    }

    TEST(EvaluateTest, LiteralsNeverTouchTheResolver) {
        FakeResolver resolver;

        EXPECT_EQ(run("(1 + 2) * 3 - 4 / 2", resolver), "7");
        EXPECT_EQ(resolver.names + resolver.members + resolver.elements +
                      resolver.reads,
                  0);
    }

    TEST(EvaluateTest, AnErrorStopsBeforeLaterLookups) {
        FakeResolver resolver;

        EXPECT_EQ(run("nope + header.count", resolver),
                  "1: unknown name 'nope'");
        EXPECT_EQ(resolver.names, 1);
        EXPECT_EQ(resolver.reads, 0);
    }

    TEST(EvaluateTest, TheResolverSeesLiveChanges) {
        FakeResolver resolver;
        const auto program = compile("header.count * 2");
        ASSERT_TRUE(program.has_value());

        EXPECT_EQ(evaluate(*program, resolver), 6);
        resolver.numbers["header.count"] = 50;
        EXPECT_EQ(evaluate(*program, resolver), 100);
    }

    TEST(EvaluateTest, OneProgramEvaluatesManyTimes) {
        FakeResolver resolver;
        const auto program = compile("items[1].price + n");
        ASSERT_TRUE(program.has_value());

        for (int i = 0; i < 100; ++i) {
            ASSERT_EQ(evaluate(*program, resolver), 25);
        }
    }

    TEST(EvaluateTest, LongChainsEvaluate) {
        std::string text = "1";
        for (int i = 0; i < 2000; ++i) {
            text += " + 1";
        }

        EXPECT_EQ(run(text), "2001");
    }

    TEST(EvaluateTest, EmptyProgramIsAnError) {
        FakeResolver resolver;
        const auto value = evaluate(Program{}, resolver);

        ASSERT_FALSE(value.has_value());
        EXPECT_EQ(value.error().message, "the program is empty");
    }

    TEST(EvaluateTest, StackUnderflowIsAnError) {
        FakeResolver resolver;
        const Program program{.code = {Instruction{.code = OpCode::ADD}}};
        const auto value = evaluate(program, resolver);

        ASSERT_FALSE(value.has_value());
        EXPECT_EQ(value.error().message, "the program is malformed");
    }

    TEST(EvaluateTest, LeftoverValuesAreAnError) {
        FakeResolver resolver;
        const Program program{
            .code = {Instruction{.code = OpCode::PUSH, .value = 1},
                     Instruction{.code = OpCode::PUSH, .value = 2}}};
        const auto value = evaluate(program, resolver);

        ASSERT_FALSE(value.has_value());
        EXPECT_EQ(value.error().message,
                  "the program left more than one value");
    }

    TEST(EvaluateTest, AnOutOfRangeNameIndexIsAnEmptyName) {
        FakeResolver resolver;
        const Program program{
            .code = {Instruction{.code = OpCode::NAME, .name = 9}}};
        const auto value = evaluate(program, resolver);

        ASSERT_FALSE(value.has_value());
        EXPECT_EQ(value.error().message, "unknown name ''");
    }

    TEST(EvaluateTest, ProgramsWithoutADepthStillRun) {
        FakeResolver resolver;
        const Program program{
            .code = {Instruction{.code = OpCode::PUSH, .value = 2},
                     Instruction{.code = OpCode::PUSH, .value = 3},
                     Instruction{.code = OpCode::MULTIPLY}},
            .depth = 0};

        EXPECT_EQ(evaluate(program, resolver), 6);
    }

}  // namespace
