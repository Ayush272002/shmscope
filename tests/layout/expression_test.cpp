#include "shmscope/layout/expression.hpp"

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace {

    using shmscope::compile;
    using shmscope::disassemble;
    using shmscope::OpCode;
    using shmscope::Program;

    std::string program(std::string_view text) {
        const auto compiled = compile(text);
        if (!compiled) {
            return "error: " + compiled.error().message;
        }
        return disassemble(*compiled);
    }

    std::string error(std::string_view text) {
        const auto compiled = compile(text);
        if (compiled) {
            return "<compiled>";
        }
        return std::to_string(compiled.error().column) + ": " +
               compiled.error().message;
    }

    struct Case {
        std::string_view text;
        std::string_view expected;
    };

    void PrintTo(const Case& value, std::ostream* out) {
        *out << '"' << value.text << '"';
    }

    class CompilesTo : public ::testing::TestWithParam<Case> {};

    TEST_P(CompilesTo, TheExpectedProgram) {
        EXPECT_EQ(program(GetParam().text), GetParam().expected);
    }

    INSTANTIATE_TEST_SUITE_P(
        Literals, CompilesTo,
        ::testing::Values(
            Case{"0", "PUSH 0"}, Case{"8", "PUSH 8"}, Case{"007", "PUSH 7"},
            Case{"0x10", "PUSH 16"}, Case{"0XfF", "PUSH 255"},
            Case{"0b1010", "PUSH 10"}, Case{"0B1", "PUSH 1"},
            Case{"0o17", "PUSH 15"}, Case{"0O7", "PUSH 7"},
            Case{"1_000_000", "PUSH 1000000"}, Case{"0x_ff", "PUSH 255"},
            Case{"9223372036854775807", "PUSH 9223372036854775807"},
            Case{"0x7fffffffffffffff", "PUSH 9223372036854775807"}));

    INSTANTIATE_TEST_SUITE_P(
        Names, CompilesTo,
        ::testing::Values(
            Case{"count", "NAME count"}, Case{"_root", "NAME _root"},
            Case{"_index", "NAME _index"}, Case{"a1_b2", "NAME a1_b2"},
            Case{"Header", "NAME Header"},
            Case{"header.count", "NAME header; MEMBER count"},
            Case{"_root.header._parent.x",
                 "NAME _root; MEMBER header; MEMBER _parent; MEMBER x"},
            Case{"a . b", "NAME a; MEMBER b"},
            Case{"items[3]", "NAME items; PUSH 3; INDEX"},
            Case{"items[3].price", "NAME items; PUSH 3; INDEX; MEMBER price"},
            Case{"a[1][2]", "NAME a; PUSH 1; INDEX; PUSH 2; INDEX"},
            Case{"records[_index + 1].size",
                 "NAME records; NAME _index; PUSH 1; ADD; INDEX; MEMBER "
                 "size"},
            Case{"(a).b", "NAME a; MEMBER b"},
            Case{"(a + b).c", "NAME a; NAME b; ADD; MEMBER c"}));

    INSTANTIATE_TEST_SUITE_P(
        Operators, CompilesTo,
        ::testing::Values(Case{"a + b", "NAME a; NAME b; ADD"},
                          Case{"a - b", "NAME a; NAME b; SUBTRACT"},
                          Case{"a * b", "NAME a; NAME b; MULTIPLY"},
                          Case{"a / b", "NAME a; NAME b; DIVIDE"},
                          Case{"a % b", "NAME a; NAME b; MODULO"},
                          Case{"a << b", "NAME a; NAME b; SHIFT_LEFT"},
                          Case{"a >> b", "NAME a; NAME b; SHIFT_RIGHT"},
                          Case{"a & b", "NAME a; NAME b; BIT_AND"},
                          Case{"a | b", "NAME a; NAME b; BIT_OR"},
                          Case{"a ^ b", "NAME a; NAME b; BIT_XOR"},
                          Case{"-a", "NAME a; NEGATE"},
                          Case{"~a", "NAME a; COMPLEMENT"},
                          Case{"--a", "NAME a; NEGATE; NEGATE"},
                          Case{"-~a", "NAME a; COMPLEMENT; NEGATE"},
                          Case{"-a.b", "NAME a; MEMBER b; NEGATE"},
                          Case{"-a[0]", "NAME a; PUSH 0; INDEX; NEGATE"}));

    INSTANTIATE_TEST_SUITE_P(
        Precedence, CompilesTo,
        ::testing::Values(
            Case{"a + b * c", "NAME a; NAME b; NAME c; MULTIPLY; ADD"},
            Case{"a * b + c", "NAME a; NAME b; MULTIPLY; NAME c; ADD"},
            Case{"(a + b) * c", "NAME a; NAME b; ADD; NAME c; MULTIPLY"},
            Case{"a + b << c", "NAME a; NAME b; ADD; NAME c; SHIFT_LEFT"},
            Case{"a << b & c", "NAME a; NAME b; SHIFT_LEFT; NAME c; BIT_AND"},
            Case{"a & b ^ c", "NAME a; NAME b; BIT_AND; NAME c; BIT_XOR"},
            Case{"a ^ b | c", "NAME a; NAME b; BIT_XOR; NAME c; BIT_OR"},
            Case{"a | b ^ c & d << e + f * g",
                 "NAME a; NAME b; NAME c; NAME d; NAME e; NAME f; NAME g; "
                 "MULTIPLY; ADD; SHIFT_LEFT; BIT_AND; BIT_XOR; BIT_OR"},
            Case{"-a * b", "NAME a; NEGATE; NAME b; MULTIPLY"},
            Case{"a * -b", "NAME a; NAME b; NEGATE; MULTIPLY"},
            Case{"a - -b", "NAME a; NAME b; NEGATE; SUBTRACT"}));

    INSTANTIATE_TEST_SUITE_P(
        LeftAssociative, CompilesTo,
        ::testing::Values(
            Case{"a - b - c", "NAME a; NAME b; SUBTRACT; NAME c; SUBTRACT"},
            Case{"a / b / c", "NAME a; NAME b; DIVIDE; NAME c; DIVIDE"},
            Case{"a % b * c", "NAME a; NAME b; MODULO; NAME c; MULTIPLY"},
            Case{"a << b >> c",
                 "NAME a; NAME b; SHIFT_LEFT; NAME c; SHIFT_RIGHT"},
            Case{"a - (b - c)", "NAME a; NAME b; NAME c; SUBTRACT; SUBTRACT"}));

    INSTANTIATE_TEST_SUITE_P(
        Whitespace, CompilesTo,
        ::testing::Values(Case{"  a  +  b  ", "NAME a; NAME b; ADD"},
                          Case{"a+b", "NAME a; NAME b; ADD"},
                          Case{"a\t+\nb\r", "NAME a; NAME b; ADD"},
                          Case{"( ( a ) )", "NAME a"}));

    INSTANTIATE_TEST_SUITE_P(
        RealLayouts, CompilesTo,
        ::testing::Values(
            Case{"header.slots_offset + (header.capacity * 8 + 127) / 128 * "
                 "128",
                 "NAME header; MEMBER slots_offset; NAME header; MEMBER "
                 "capacity; PUSH 8; MULTIPLY; PUSH 127; ADD; PUSH 128; "
                 "DIVIDE; PUSH 128; MULTIPLY; ADD"},
            Case{"_root.header.record_size * _index",
                 "NAME _root; MEMBER header; MEMBER record_size; NAME _index; "
                 "MULTIPLY"},
            Case{"flags & 0x0f", "NAME flags; PUSH 15; BIT_AND"}));

    class FailsWith : public ::testing::TestWithParam<Case> {};

    TEST_P(FailsWith, TheExpectedColumnAndMessage) {
        EXPECT_EQ(error(GetParam().text), GetParam().expected);
    }

    INSTANTIATE_TEST_SUITE_P(
        Empty, FailsWith,
        ::testing::Values(Case{"", "1: the expression is empty"},
                          Case{"   ", "4: the expression is empty"}));

    INSTANTIATE_TEST_SUITE_P(
        Structure, FailsWith,
        ::testing::Values(
            Case{"a +", "4: the expression ends too early"},
            Case{"-", "2: the expression ends too early"},
            Case{"(a", "3: expected ')'"}, Case{"a)", "2: unexpected ')'"},
            Case{"a b", "3: unexpected 'b'"}, Case{"1 2", "3: unexpected '2'"},
            Case{"a[1", "4: expected ']'"},
            Case{"a[]",
                 "3: expected a number, "
                 "a name or '(', got "
                 "']'"},
            Case{"()", "2: expected a number, a name or '(', got ')'"},
            Case{"a.", "3: expected a name after '.'"},
            Case{"a. 3", "4: expected a name after '.'"},
            Case{"a.1", "3: expected a name after '.'"},
            Case{".a", "1: expected a number, a name or '(', got '.'"},
            Case{"+a", "1: expected a number, a name or '(', got '+'"},
            Case{"a * * b", "5: expected a number, a name or '(', got '*'"},
            Case{"]", "1: expected a number, a name or '(', got ']'"}));

    INSTANTIATE_TEST_SUITE_P(
        Numbers, FailsWith,
        ::testing::Values(
            Case{"0x", "1: a number needs digits after its prefix"},
            Case{"0xZ", "1: a number needs digits after its prefix"},
            Case{"0b", "1: a number needs digits after its prefix"},
            Case{"0b2", "3: '2' is not a base 2 digit"},
            Case{"0o8", "3: '8' is not a base 8 digit"},
            Case{"0b1a", "4: 'a' is not a base 2 digit"},
            Case{"12abc", "3: unexpected 'a' after a number"},
            Case{"1x", "2: unexpected 'x' after a number"},
            Case{"1.5",
                 "1: fractional numbers are not supported in shmscope "
                 "expressions"},
            Case{"9223372036854775808",
                 "1: '9223372036854775808' does not fit in a signed 64 bit "
                 "integer"},
            Case{"99999999999999999999",
                 "1: '99999999999999999999' does not fit in a signed 64 bit "
                 "integer"},
            Case{"0xffffffffffffffffff",
                 "1: '0xffffffffffffffffff' does not fit in a signed 64 bit "
                 "integer"},
            Case{"a + 9223372036854775808",
                 "5: '9223372036854775808' does not fit in a signed 64 bit "
                 "integer"}));

    INSTANTIATE_TEST_SUITE_P(
        Unsupported, FailsWith,
        ::testing::Values(
            Case{"a == b",
                 "3: '==' is not supported in shmscope expressions "
                 "yet"},
            Case{"a != b",
                 "3: '!=' is not supported in shmscope expressions "
                 "yet"},
            Case{"a <= b",
                 "3: '<=' is not supported in shmscope expressions "
                 "yet"},
            Case{"a >= b",
                 "3: '>=' is not supported in shmscope expressions "
                 "yet"},
            Case{"a < b",
                 "3: '<' is not supported in shmscope expressions "
                 "yet"},
            Case{"a > b",
                 "3: '>' is not supported in shmscope expressions "
                 "yet"},
            Case{"a ? b : c",
                 "3: '?' is not supported in shmscope expressions "
                 "yet"},
            Case{"!a", "1: '!' is not supported in shmscope expressions yet"},
            Case{"a and b",
                 "3: 'and' is not supported in shmscope expressions "
                 "yet"},
            Case{"a or b",
                 "3: 'or' is not supported in shmscope expressions "
                 "yet"},
            Case{"not a",
                 "1: 'not' is not supported in shmscope expressions "
                 "yet"},
            Case{"true",
                 "1: 'true' is not supported in shmscope expressions "
                 "yet"},
            Case{"false",
                 "1: 'false' is not supported in shmscope "
                 "expressions yet"},
            Case{"'s'",
                 "1: strings are not supported in shmscope expressions "
                 "yet"},
            Case{"\"s\"",
                 "1: strings are not supported in shmscope "
                 "expressions yet"}));

    INSTANTIATE_TEST_SUITE_P(
        Characters, FailsWith,
        ::testing::Values(Case{"a $ b", "3: unexpected '$'"},
                          Case{"a @ b", "3: unexpected '@'"},
                          Case{"a, b", "2: unexpected ','"},
                          Case{"a;", "2: unexpected ';'"}));

    TEST(ExpressionTest, KeywordsAreWholeWordsOnly) {
        EXPECT_EQ(program("android"), "NAME android");
        EXPECT_EQ(program("order + notes"), "NAME order; NAME notes; ADD");
        EXPECT_EQ(program("true_count"), "NAME true_count");
    }

    TEST(ExpressionTest, NamesAreInternedOnce) {
        const auto compiled = compile("a + a * b - a");

        ASSERT_TRUE(compiled.has_value());
        ASSERT_EQ(compiled->names.size(), 2U);
        EXPECT_EQ(compiled->names[0], "a");
        EXPECT_EQ(compiled->names[1], "b");
        EXPECT_EQ(compiled->code[0].name, 0U);
        EXPECT_EQ(compiled->code[1].name, 0U);
        EXPECT_EQ(compiled->code[2].name, 1U);
    }

    TEST(ExpressionTest, MembersShareTheNameTable) {
        const auto compiled = compile("count + header.count");

        ASSERT_TRUE(compiled.has_value());
        EXPECT_EQ(compiled->names.size(), 2U);
        EXPECT_EQ(compiled->code[0].name, compiled->code[2].name);
    }

    TEST(ExpressionTest, PushCarriesTheValue) {
        const auto compiled = compile("0x7fffffffffffffff");

        ASSERT_TRUE(compiled.has_value());
        ASSERT_EQ(compiled->code.size(), 1U);
        EXPECT_EQ(compiled->code[0].code, OpCode::PUSH);
        EXPECT_EQ(compiled->code[0].value, INT64_MAX);
    }

    TEST(ExpressionTest, InstructionsKnowTheirColumns) {
        const auto compiled = compile("ab + header.count * 2");

        ASSERT_TRUE(compiled.has_value());
        ASSERT_EQ(compiled->code.size(), 6U);
        EXPECT_EQ(compiled->code[0].column, 1U);
        EXPECT_EQ(compiled->code[1].column, 6U);
        EXPECT_EQ(compiled->code[2].column, 13U);
        EXPECT_EQ(compiled->code[3].column, 21U);
        EXPECT_EQ(compiled->code[4].column, 19U);
        EXPECT_EQ(compiled->code[5].column, 4U);
    }

    TEST(ExpressionTest, IndexInstructionPointsAtTheBracket) {
        const auto compiled = compile("items[3]");

        ASSERT_TRUE(compiled.has_value());
        EXPECT_EQ(compiled->code.back().code, OpCode::INDEX);
        EXPECT_EQ(compiled->code.back().column, 6U);
    }

    struct DepthCase {
        std::string_view text;
        std::size_t depth;
    };

    class StackDepth : public ::testing::TestWithParam<DepthCase> {};

    TEST_P(StackDepth, IsTheMostValuesLiveAtOnce) {
        const auto compiled = compile(GetParam().text);

        ASSERT_TRUE(compiled.has_value());
        EXPECT_EQ(compiled->depth, GetParam().depth);
    }

    INSTANTIATE_TEST_SUITE_P(
        Programs, StackDepth,
        ::testing::Values(
            DepthCase{"1", 1}, DepthCase{"-~1", 1}, DepthCase{"a.b.c", 1},
            DepthCase{"a + b", 2}, DepthCase{"a + b + c + d", 2},
            DepthCase{"a + (b + (c + d))", 4}, DepthCase{"a + b * c", 3},
            DepthCase{"items[i + 1]", 3}, DepthCase{"a[b[c[d]]]", 4}));

    std::string repeat(std::string_view text, std::size_t times) {
        std::string out;
        for (std::size_t i = 0; i < times; ++i) {
            out += text;
        }
        return out;
    }

    TEST(ExpressionTest, NestingUpToTheLimitCompiles) {
        const std::size_t limit = shmscope::MAX_EXPRESSION_NESTING;
        const auto text = repeat("(", limit) + "1" + repeat(")", limit);

        EXPECT_TRUE(compile(text).has_value());
    }

    TEST(ExpressionTest, DeepParenthesesAreRejected) {
        const auto text = repeat("(", 100) + "1" + repeat(")", 100);

        EXPECT_EQ(compile(text).error().message,
                  "the expression is nested too deeply");
    }

    TEST(ExpressionTest, DeepUnaryOperatorsAreRejected) {
        EXPECT_EQ(compile(repeat("-", 100) + "1").error().message,
                  "the expression is nested too deeply");
    }

    TEST(ExpressionTest, DeepIndexingIsRejected) {
        const auto text = "a" + repeat("[a", 100) + repeat("]", 100);

        EXPECT_EQ(compile(text).error().message,
                  "the expression is nested too deeply");
    }

    TEST(ExpressionTest, LongFlatChainsAreFine) {
        std::string text = "1";
        for (int i = 0; i < 2000; ++i) {
            text += " + 1";
        }
        const auto compiled = compile(text);

        ASSERT_TRUE(compiled.has_value());
        EXPECT_EQ(compiled->code.size(), 4001U);
        EXPECT_EQ(compiled->depth, 2U);
    }

    TEST(ExpressionTest, EveryOpCodeHasADistinctName) {
        std::set<std::string_view> names;
        for (auto code = static_cast<int>(OpCode::PUSH);
             code <= static_cast<int>(OpCode::BIT_XOR); ++code) {
            const auto name = shmscope::nameOf(static_cast<OpCode>(code));
            EXPECT_NE(name, "?");
            names.insert(name);
        }
        EXPECT_EQ(names.size(), 16U);
    }

    TEST(ExpressionTest, DisassembleOfAnEmptyProgramIsEmpty) {
        EXPECT_EQ(disassemble(Program{}), "");
    }

}  // namespace
