#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "shmscope/layout/dialects/ksy_dialect.hpp"
#include "shmscope/layout/document.hpp"
#include "shmscope/layout/evaluate.hpp"
#include "shmscope/layout/expression.hpp"
#include "shmscope/layout/model.hpp"
#include "shmscope/layout/readers/json_reader.hpp"
#include "shmscope/layout/readers/yaml_reader.hpp"

namespace {

    using shmscope::disassemble;
    using shmscope::Expression;
    using shmscope::JsonReader;
    using shmscope::KsyDialect;
    using shmscope::Layout;
    using shmscope::LayoutResult;
    using shmscope::Lookup;
    using shmscope::YamlReader;

    struct PathResolver {
        struct Ref {
            std::string path;
        };

        std::map<std::string, std::int64_t, std::less<>> numbers{};

        Lookup<Ref> name(std::string_view text) {
            return Ref{std::string(text)};
        }

        Lookup<Ref> member(const Ref& ref, std::string_view text) {
            return Ref{std::format("{}.{}", ref.path, text)};
        }

        Lookup<Ref> element(const Ref& ref, std::int64_t index) {
            return Ref{std::format("{}[{}]", ref.path, index)};
        }

        std::expected<std::int64_t, std::string> number(const Ref& ref) {
            const auto found = numbers.find(ref.path);
            if (found == numbers.end()) {
                return std::unexpected(
                    std::format("no value for '{}'", ref.path));
            }
            return found->second;
        }
    };

    static_assert(shmscope::Resolver<PathResolver>);

    LayoutResult loadYaml(std::string_view text) {
        auto document = YamlReader::read(text, "test.ksy");
        if (!document) {
            return std::unexpected(std::move(document.error()));
        }
        return KsyDialect::load(*document, "test.ksy");
    }

    LayoutResult loadJson(std::string_view text) {
        auto document = JsonReader::read(text, "test.json");
        if (!document) {
            return std::unexpected(std::move(document.error()));
        }
        return KsyDialect::load(*document, "test.json");
    }

    Layout loaded(std::string_view text) {
        auto layout = loadYaml(text);
        EXPECT_TRUE(layout.has_value())
            << (layout ? "" : shmscope::describe(layout.error()));
        return layout ? std::move(*layout) : Layout{};
    }

    std::string failure(const LayoutResult& layout) {
        if (layout) {
            return "<loaded>";
        }
        return layout.error().path + ": " + layout.error().message;
    }

    std::string failure(std::string_view text) {
        return failure(loadYaml(text));
    }

    std::string described(std::string_view text) {
        const auto layout = loadYaml(text);
        return layout ? "<loaded>" : shmscope::describe(layout.error());
    }

    std::string with(std::string_view body) {
        return std::format("meta: {{id: x, endian: le}}\n{}", body);
    }

    std::string code(const std::optional<Expression>& expression) {
        return expression ? disassemble(expression->program) : "<none>";
    }

    std::int64_t valueOf(const Expression& expression, PathResolver& resolver) {
        const auto result = shmscope::evaluate(expression.program, resolver);
        EXPECT_TRUE(result.has_value())
            << (result ? "" : result.error().message);
        return result.value_or(-1);
    }

    constexpr std::string_view INSTANCE = R"(meta: {id: x, endian: le}
seq:
  - {id: header, type: header}
instances:
  records:
    pos: header.records_at + header.count * 8
    size: 16
    repeat: expr
    repeat-expr: header.count - 1
types:
  header:
    seq:
      - {id: count, type: u4}
      - {id: records_at, type: u4}
)";

    TEST(KsyExpressionTest, SizeIsCompiled) {
        const auto layout = loaded(
            with("seq: [{id: n, type: u4}, {id: body, size: n * 2 + 1}]"));

        EXPECT_EQ(layout.root.seq[1].size->text, "n * 2 + 1");
        EXPECT_EQ(code(layout.root.seq[1].size),
                  "NAME n; PUSH 2; MULTIPLY; PUSH 1; ADD");
    }

    TEST(KsyExpressionTest, APlainNumberIsASinglePush) {
        const auto layout = loaded(with("seq: [{id: pad, size: 128}]"));

        EXPECT_EQ(code(layout.root.seq[0].size), "PUSH 128");
    }

    TEST(KsyExpressionTest, HexNumbersAreCompiled) {
        const auto layout = loaded(with("seq: [{id: pad, size: 0x40}]"));

        EXPECT_EQ(code(layout.root.seq[0].size), "PUSH 64");
    }

    TEST(KsyExpressionTest, PosAndRepeatAreCompiled) {
        const auto layout = loaded(INSTANCE);
        const auto& records = layout.root.instances[0];

        EXPECT_EQ(code(records.pos),
                  "NAME header; MEMBER records_at; NAME header; MEMBER count; "
                  "PUSH 8; MULTIPLY; ADD");
        EXPECT_EQ(code(records.repeatCount),
                  "NAME header; MEMBER count; PUSH 1; SUBTRACT");
        EXPECT_EQ(code(records.size), "PUSH 16");
    }

    TEST(KsyExpressionTest, SwitchOnIsCompiled) {
        const auto layout = loaded(with(R"(seq:
  - {id: kind, type: u1}
  - id: body
    type:
      switch-on: kind & 0x0f
      cases: {1: a, _: b}
types:
  a: {seq: [{id: v, type: u1}]}
  b: {seq: [{id: v, type: u2}]}
)"));

        EXPECT_EQ(disassemble(layout.root.seq[1].switchOn->on.program),
                  "NAME kind; PUSH 15; BIT_AND");
    }

    TEST(KsyExpressionTest, IndexingIsCompiled) {
        const auto layout =
            loaded(with("seq: [{id: a, size: 'lens[2].value'}]"));

        EXPECT_EQ(code(layout.root.seq[0].size),
                  "NAME lens; PUSH 2; INDEX; MEMBER value");
    }

    TEST(KsyExpressionTest, UnknownNamesAreLeftForEvaluation) {
        const auto layout = loaded(with("seq: [{id: a, size: nowhere * 2}]"));

        EXPECT_EQ(code(layout.root.seq[0].size),
                  "NAME nowhere; PUSH 2; MULTIPLY");
    }

    TEST(KsyExpressionTest, ProgramsEvaluateAgainstAResolver) {
        const auto layout = loaded(INSTANCE);
        const auto& records = layout.root.instances[0];
        PathResolver resolver{
            .numbers = {{"header.count", 4}, {"header.records_at", 256}}};

        EXPECT_EQ(valueOf(*records.pos, resolver), 288);
        EXPECT_EQ(valueOf(*records.repeatCount, resolver), 3);
        EXPECT_EQ(valueOf(*records.size, resolver), 16);
    }

    TEST(KsyExpressionTest, ProgramsKeepTheirDepth) {
        const auto layout = loaded(INSTANCE);

        EXPECT_EQ(layout.root.instances[0].pos->program.depth, 3U);
        EXPECT_EQ(layout.root.instances[0].size->program.depth, 1U);
    }

    TEST(KsyExpressionTest, TheSampleFixtureCompiles) {
        const auto layout = loaded(R"(meta: {id: sample_ring, endian: le}
seq:
  - {id: header, type: header}
instances:
  records:
    pos: header.records_at
    size: header.record_size
    type:
      switch-on: header.kind
      cases: {1: trade, _: raw}
    repeat: expr
    repeat-expr: header.count
types:
  header:
    seq:
      - {id: kind, type: u4}
      - {id: record_size, type: u4}
      - {id: count, type: u8}
      - {id: records_at, type: u8}
  trade: {seq: [{id: price, type: s8}]}
  raw: {seq: [{id: bytes, size: 16}]}
)");
        const auto& records = layout.root.instances[0];
        PathResolver resolver{.numbers = {{"header.kind", 1},
                                          {"header.record_size", 24},
                                          {"header.count", 10},
                                          {"header.records_at", 4096}}};

        EXPECT_EQ(valueOf(*records.pos, resolver), 4096);
        EXPECT_EQ(valueOf(*records.size, resolver), 24);
        EXPECT_EQ(valueOf(records.switchOn->on, resolver), 1);
        EXPECT_EQ(valueOf(*records.repeatCount, resolver), 10);
    }

    TEST(KsyExpressionTest, JsonExpressionsAreCompiled) {
        const auto layout = loadJson(
            R"({"meta": {"id": "x", "endian": "le"},
 "seq": [{"id": "n", "type": "u4"}, {"id": "body", "size": "n << 2"}]})");

        ASSERT_TRUE(layout.has_value()) << shmscope::describe(layout.error());
        EXPECT_EQ(code(layout->root.seq[1].size), "NAME n; PUSH 2; SHIFT_LEFT");
    }

    TEST(KsyExpressionErrorTest, SizeReportsTheColumn) {
        EXPECT_EQ(failure(with("seq: [{id: a, size: 1.5}]")),
                  "seq[0].size: in \"1.5\" at column 1: fractional numbers are "
                  "not supported in shmscope expressions");
    }

    TEST(KsyExpressionErrorTest, RepeatExprEndsTooEarly) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: u1, repeat: expr, "
                               "repeat-expr: 3 +}]")),
                  "seq[0].repeat-expr: in \"3 +\" at column 4: the expression "
                  "ends too early");
    }

    TEST(KsyExpressionErrorTest, PosWithAnUnclosedParenthesis) {
        EXPECT_EQ(failure(R"(meta: {id: x, endian: le}
instances:
  records:
    pos: header.x + (b
    size: 4
)"),
                  "instances.records.pos: in \"header.x + (b\" at column 14: "
                  "expected ')'");
    }

    TEST(KsyExpressionErrorTest, SwitchOnWithAComparison) {
        EXPECT_EQ(failure(with(R"(seq:
  - id: body
    type: {switch-on: kind < 2, cases: {_: a}}
types:
  a: {seq: [{id: v, type: u1}]}
)")),
                  "seq[0].type.switch-on: in \"kind < 2\" at column 6: '<' is "
                  "not supported in shmscope expressions yet");
    }

    TEST(KsyExpressionErrorTest, ErrorsInsideNamedTypes) {
        EXPECT_EQ(failure(with(R"(seq: [{id: h, type: h}]
types:
  h:
    seq: [{id: a, size: 2 * * 3}]
)")),
                  "types.h.seq[0].size: in \"2 * * 3\" at column 5: expected a "
                  "number, a name or '(', got '*'");
    }

    TEST(KsyExpressionErrorTest, JsonReportsTheSameError) {
        EXPECT_EQ(
            failure(loadJson(
                R"({"meta": {"id": "x", "endian": "le"},
 "seq": [{"id": "a", "size": "4 +"}]})")),
            "seq[0].size: in \"4 +\" at column 4: the expression ends too "
            "early");
    }

    TEST(KsyExpressionLocationTest, PlainScalarsPointAtTheBadCharacter) {
        EXPECT_EQ(
            described(R"(meta: {id: x, endian: le}
instances:
  records:
    pos: header.x + (b
    size: 4
)"),
            "test.ksy:4:23: instances.records.pos: in \"header.x + (b\" at "
            "column 14: expected ')'");
    }

    TEST(KsyExpressionLocationTest, FlowScalarsPointAtTheBadCharacter) {
        EXPECT_EQ(described(with("seq: [{id: a, size: 3 +}]")),
                  "test.ksy:2:24: seq[0].size: in \"3 +\" at column 4: the "
                  "expression ends too early");
    }

    TEST(KsyExpressionLocationTest, ColumnOneIsTheValueItself) {
        EXPECT_EQ(described(with("seq: [{id: a, size: 1.5}]")),
                  "test.ksy:2:21: seq[0].size: in \"1.5\" at column 1: "
                  "fractional numbers are not supported in shmscope "
                  "expressions");
    }

}  // namespace
