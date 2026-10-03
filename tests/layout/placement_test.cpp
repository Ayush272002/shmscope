#include "shmscope/layout/placement.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "shmscope/core/decode.hpp"
#include "shmscope/layout/dialects/ksy_dialect.hpp"
#include "shmscope/layout/document.hpp"
#include "shmscope/layout/model.hpp"
#include "shmscope/layout/readers/yaml_reader.hpp"

namespace {

    using shmscope::KsyDialect;
    using shmscope::Layout;
    using shmscope::Placement;
    using shmscope::PlacementLimits;
    using shmscope::YamlReader;

    using Bytes = std::vector<std::byte>;

    constexpr std::string_view RING = R"(meta: {id: ring, endian: le}
seq:
  - {id: header, type: header}
  - {id: sequence, type: u8}
instances:
  records:
    pos: header.records_at
    size: header.record_size
    type:
      switch-on: header.kind
      cases: {1: trade, 2: quote, _: raw}
    repeat: expr
    repeat-expr: header.count
  last_price:
    pos: header.records_at + (header.count - 1) * header.record_size + 8
    type: s8
types:
  header:
    seq:
      - {contents: "RING0001"}
      - {id: kind, type: u4}
      - {id: record_size, type: u4}
      - {id: count, type: u8}
      - {id: records_at, type: u8}
      - {id: name, type: str, size: 16, encoding: ASCII}
  trade:
    seq:
      - {id: sent_at, type: u8}
      - {id: price, type: s8}
      - {id: side, type: s1}
    instances:
      first_byte: {pos: 0, type: u1}
  quote:
    seq:
      - {id: bid, type: s8}
      - {id: ask, type: s8}
  raw:
    seq:
      - {id: bytes, size: 16}
)";

    Layout loaded(std::string_view text) {
        auto document = YamlReader::read(text, "test.ksy");
        if (!document) {
            ADD_FAILURE() << shmscope::describe(document.error());
            return {};
        }
        auto layout = KsyDialect::load(*document, "test.ksy");
        if (!layout) {
            ADD_FAILURE() << shmscope::describe(layout.error());
            return {};
        }
        return std::move(*layout);
    }

    std::string with(std::string_view body) {
        return std::format("meta: {{id: x, endian: le}}\n{}", body);
    }

    template <typename T>
    void put(Bytes& bytes, const std::size_t at, const T value) {
        std::memcpy(bytes.data() + at, &value, sizeof value);
    }

    void putText(Bytes& bytes, const std::size_t at, std::string_view text) {
        std::memcpy(bytes.data() + at, text.data(), text.size());
    }

    Bytes ring(const std::uint32_t kind, const std::uint64_t count,
               const std::size_t length = 256) {
        Bytes bytes(length);
        putText(bytes, 0, "RING0001");
        put<std::uint32_t>(bytes, 8, kind);
        put<std::uint32_t>(bytes, 12, 24);
        put<std::uint64_t>(bytes, 16, count);
        put<std::uint64_t>(bytes, 24, 128);
        putText(bytes, 32, "ring-a");
        put<std::uint64_t>(bytes, 48, 77);
        for (std::size_t i = 0; i < 4; ++i) {
            const auto at = 128 + i * 24;
            put<std::uint64_t>(bytes, at, 1000 + i);
            put<std::int64_t>(bytes, at + 8, -5 * static_cast<std::int64_t>(i));
            put<std::int8_t>(bytes, at + 16, 1);
        }
        return bytes;
    }

    Placement placed(std::string_view text, const Bytes& bytes,
                     const PlacementLimits& limits = {}) {
        const auto layout = loaded(text);
        return shmscope::place(layout, bytes, limits);
    }

    std::string at(const Placement& placement, std::string_view path) {
        const auto* field = placement.find(path);
        if (field == nullptr) return "<missing>";

        return std::format("@{}+{}", field->offset, field->size);
    }

    std::string valueOf(const Placement& placement, std::string_view path) {
        const auto* field = placement.find(path);
        if (field == nullptr) return "<missing>";
        if (!field->value) return "<none>";

        return shmscope::toText(*field->value);
    }

    std::string problems(const Placement& placement) {
        std::string text;
        for (const auto& problem : placement.problems) {
            if (!text.empty()) text += "\n";

            text += std::format("{}: {}", problem.path, problem.message);
        }
        return text;
    }

    std::string paths(const Placement& placement) {
        std::string text;
        for (const auto& field : placement.fields) {
            if (!text.empty()) text += " ";

            text += field.path;
        }
        return text;
    }

    TEST(PlacementRingTest, PlacesTheHeaderInOrder) {
        const auto placement = placed(RING, ring(1, 3));

        EXPECT_EQ(at(placement, "header"), "@0+48");
        EXPECT_EQ(at(placement, "header._unnamed0"), "@0+8");
        EXPECT_EQ(at(placement, "header.kind"), "@8+4");
        EXPECT_EQ(at(placement, "header.record_size"), "@12+4");
        EXPECT_EQ(at(placement, "header.count"), "@16+8");
        EXPECT_EQ(at(placement, "header.records_at"), "@24+8");
        EXPECT_EQ(at(placement, "header.name"), "@32+16");
        EXPECT_EQ(at(placement, "sequence"), "@48+8");
        EXPECT_EQ(problems(placement), "");
        EXPECT_FALSE(placement.truncated);
    }

    TEST(PlacementRingTest, ReadsScalarAndStringValues) {
        const auto placement = placed(RING, ring(1, 3));

        EXPECT_EQ(valueOf(placement, "header.kind"), "1");
        EXPECT_EQ(valueOf(placement, "header.count"), "3");
        EXPECT_EQ(valueOf(placement, "header.name"), "ring-a");
        EXPECT_EQ(valueOf(placement, "sequence"), "77");
        EXPECT_EQ(valueOf(placement, "header"), "<none>");
        EXPECT_EQ(valueOf(placement, "header._unnamed0"), "<none>");
    }

    TEST(PlacementRingTest, PlacesRepeatedSwitchRecords) {
        const auto placement = placed(RING, ring(1, 3));

        EXPECT_EQ(at(placement, "records"), "@128+72");
        EXPECT_EQ(at(placement, "records[0]"), "@128+24");
        EXPECT_EQ(at(placement, "records[2]"), "@176+24");
        EXPECT_EQ(at(placement, "records[3]"), "<missing>");
        EXPECT_EQ(valueOf(placement, "records[1].sent_at"), "1001");
        EXPECT_EQ(valueOf(placement, "records[2].price"), "-10");
        EXPECT_EQ(valueOf(placement, "records[0].side"), "1");
    }

    TEST(PlacementRingTest, InstancesUseOtherFields) {
        const auto placement = placed(RING, ring(1, 3));

        EXPECT_EQ(at(placement, "last_price"), "@184+8");
        EXPECT_EQ(valueOf(placement, "last_price"), "-10");
    }

    TEST(PlacementRingTest, PosInASizedTypeIsRelativeToIt) {
        const auto placement = placed(RING, ring(1, 3));

        EXPECT_EQ(at(placement, "records[1].first_byte"), "@152+1");
        EXPECT_EQ(valueOf(placement, "records[1].first_byte"), "233");
    }

    TEST(PlacementRingTest, SwitchPicksAnotherCase) {
        const auto placement = placed(RING, ring(2, 2));

        EXPECT_EQ(at(placement, "records[0]"), "@128+24");
        EXPECT_EQ(at(placement, "records[0].bid"), "@128+8");
        EXPECT_EQ(at(placement, "records[0].ask"), "@136+8");
        EXPECT_EQ(valueOf(placement, "records[0].bid"), "1000");
        EXPECT_EQ(at(placement, "records[0].sent_at"), "<missing>");
    }

    TEST(PlacementRingTest, SwitchFallsBackToTheDefault) {
        const auto placement = placed(RING, ring(9, 1));

        EXPECT_EQ(at(placement, "records[0].bytes"), "@128+16");
        EXPECT_EQ(at(placement, "records[0]"), "@128+24");
    }

    TEST(PlacementRingTest, ParentLinksFormATree) {
        const auto placement = placed(RING, ring(1, 2));
        const auto* header = placement.find("header");
        const auto* kind = placement.find("header.kind");
        const auto* records = placement.find("records");
        const auto* record = placement.find("records[1]");
        const auto* price = placement.find("records[1].price");

        ASSERT_NE(kind, nullptr);
        ASSERT_NE(record, nullptr);
        ASSERT_NE(price, nullptr);
        EXPECT_FALSE(header->parent.has_value());
        EXPECT_EQ(&placement.fields[*kind->parent], header);
        EXPECT_EQ(&placement.fields[*record->parent], records);
        EXPECT_EQ(&placement.fields[*price->parent], record);
    }

    TEST(PlacementRingTest, ParentsComeBeforeChildren) {
        const auto placement = placed(RING, ring(1, 3));

        for (std::size_t i = 0; i < placement.fields.size(); ++i) {
            const auto& parent = placement.fields[i].parent;
            if (parent) EXPECT_LT(*parent, i) << placement.fields[i].path;
        }
    }

    TEST(PlacementRingTest, DepthFollowsNesting) {
        const auto placement = placed(RING, ring(1, 1));

        EXPECT_EQ(placement.find("header")->depth, 0U);
        EXPECT_EQ(placement.find("header.kind")->depth, 1U);
        EXPECT_EQ(placement.find("records")->depth, 0U);
        EXPECT_EQ(placement.find("records[0]")->depth, 1U);
        EXPECT_EQ(placement.find("records[0].price")->depth, 2U);
    }

    TEST(PlacementRingTest, FieldsPointAtTheirAttributes) {
        const auto layout = loaded(RING);
        const auto placement = shmscope::place(layout, ring(1, 1));

        EXPECT_EQ(placement.find("header.count")->attribute,
                  layout.findType("header")->find("count"));
        EXPECT_EQ(placement.find("records")->attribute,
                  layout.root.find("records"));
        EXPECT_EQ(placement.find("records[0]")->attribute,
                  layout.root.find("records"));
    }

    TEST(PlacementRingTest, ACountPastTheEndKeepsWhatFits) {
        const auto placement = placed(RING, ring(1, 9));

        EXPECT_EQ(at(placement, "records[4]"), "@224+24");
        EXPECT_EQ(at(placement, "records[5]"), "<missing>");
        EXPECT_EQ(at(placement, "records"), "@128+120");
        EXPECT_EQ(problems(placement),
                  "records[5]: 24 bytes at offset 248 go past the end at 256\n"
                  "last_price: pos 328 is outside the 256 bytes available");
    }

    TEST(PlacementRingTest, WrongMagicIsReportedButPlaced) {
        auto bytes = ring(1, 1);
        putText(bytes, 0, "XING");
        const auto placement = placed(RING, bytes);

        EXPECT_EQ(at(placement, "header._unnamed0"), "@0+8");
        EXPECT_EQ(valueOf(placement, "header.count"), "1");
        EXPECT_EQ(problems(placement),
                  "header._unnamed0: the bytes do not match the expected "
                  "contents");
    }

    TEST(PlacementRingTest, ProblemsKnowTheirLines) {
        const auto placement = placed(RING, ring(1, 9));

        ASSERT_EQ(placement.problems.size(), 2U);
        EXPECT_EQ(placement.problems[0].location.line, 7);
        EXPECT_EQ(placement.problems[1].location.line, 15);
    }

    TEST(PlacementRingTest, FindReturnsNullForUnknownPaths) {
        const auto placement = placed(RING, ring(1, 1));

        EXPECT_EQ(placement.find("nope"), nullptr);
        EXPECT_EQ(placement.find(""), nullptr);
    }

    TEST(PlacementSeqTest, EmptyBytesPlaceNothing) {
        const auto placement = placed(with("seq: [{id: a, type: u4}]"), {});

        EXPECT_TRUE(placement.fields.empty());
        EXPECT_EQ(problems(placement),
                  "a: 4 bytes at offset 0 go past the end at 0");
    }

    TEST(PlacementSeqTest, AnEmptyLayoutPlacesNothing) {
        const auto placement = placed(with(""), Bytes(16));

        EXPECT_TRUE(placement.fields.empty());
        EXPECT_TRUE(placement.problems.empty());
    }

    TEST(PlacementSeqTest, StopsAtTheFirstFieldThatDoesNotFit) {
        const auto placement = placed(
            with("seq: [{id: a, type: u4}, {id: b, type: u8}, {id: c, type: "
                 "u1}]"),
            Bytes(8));

        EXPECT_EQ(paths(placement), "a");
        EXPECT_EQ(problems(placement),
                  "b: 8 bytes at offset 4 go past the end at 8");
    }

    TEST(PlacementSeqTest, EveryScalarWidth) {
        Bytes bytes(64);
        put<std::int8_t>(bytes, 0, -1);
        put<std::int16_t>(bytes, 1, -300);
        put<std::uint32_t>(bytes, 3, 70000);
        put<double>(bytes, 7, 1.5);
        put<float>(bytes, 15, 2.5F);
        const auto placement = placed(
            with("seq: [{id: a, type: s1}, {id: b, type: s2}, {id: c, type: "
                 "u4}, {id: d, type: f8}, {id: e, type: f4}]"),
            bytes);

        EXPECT_EQ(valueOf(placement, "a"), "-1");
        EXPECT_EQ(valueOf(placement, "b"), "-300");
        EXPECT_EQ(valueOf(placement, "c"), "70000");
        EXPECT_EQ(valueOf(placement, "d"), "1.5");
        EXPECT_EQ(valueOf(placement, "e"), "2.5");
        EXPECT_EQ(at(placement, "e"), "@15+4");
    }

    TEST(PlacementSeqTest, StringsStopAtNulAndHideControlBytes) {
        Bytes bytes(16);
        putText(bytes, 0,
                "ab\x01"
                "c");
        putText(bytes, 8, "zzzz");
        const auto placement = placed(
            with("seq: [{id: s, type: str, size: 8, encoding: ASCII}, {id: t, "
                 "type: str, size: 4, encoding: ASCII}]"),
            bytes);

        EXPECT_EQ(valueOf(placement, "s"), "ab.c");
        EXPECT_EQ(valueOf(placement, "t"), "zzzz");
        EXPECT_EQ(at(placement, "s"), "@0+8");
    }

    TEST(PlacementSeqTest, BytesHaveNoValue) {
        const auto placement = placed(
            with("seq: [{id: pad, size: 6}, {id: v, type: u1}]"), Bytes(8));

        EXPECT_EQ(at(placement, "pad"), "@0+6");
        EXPECT_EQ(valueOf(placement, "pad"), "<none>");
        EXPECT_EQ(at(placement, "v"), "@6+1");
    }

    TEST(PlacementSeqTest, AnonymousFieldsAreNumberedBySeqPosition) {
        const auto placement = placed(
            with("seq: [{size: 2}, {id: a, type: u1}, {size: 1}]"), Bytes(4));

        EXPECT_EQ(paths(placement), "_unnamed0 a _unnamed2");
    }

    TEST(PlacementSeqTest, SizeFromAnEarlierField) {
        Bytes bytes(16);
        put<std::uint8_t>(bytes, 0, 5);
        const auto placement = placed(
            with("seq: [{id: n, type: u1}, {id: body, size: n * 2}, {id: t, "
                 "type: u1}]"),
            bytes);

        EXPECT_EQ(at(placement, "body"), "@1+10");
        EXPECT_EQ(at(placement, "t"), "@11+1");
    }

    TEST(PlacementSeqTest, NegativeSizeIsAProblem) {
        const auto placement =
            placed(with("seq: [{id: body, size: 0 - 1}]"), Bytes(8));

        EXPECT_EQ(problems(placement), "body: size -1 is negative");
    }

    TEST(PlacementSeqTest, LaterFieldsAreNotAvailableYet) {
        const auto placement = placed(
            with("seq: [{id: body, size: n}, {id: n, type: u1}]"), Bytes(8));

        EXPECT_EQ(problems(placement),
                  "body: size \"n\": 'n' is not placed yet; a field can only "
                  "use the fields before it");
    }

    TEST(PlacementSeqTest, UnknownNamesAreProblems) {
        const auto placement =
            placed(with("seq: [{id: body, size: nope}]"), Bytes(8));

        EXPECT_EQ(problems(placement),
                  "body: size \"nope\": 'x' has no field 'nope'");
    }

    TEST(PlacementUserTypeTest, UnsizedTypesTakeTheirSeqLength) {
        const auto placement = placed(with(R"(seq:
  - {id: a, type: pair}
  - {id: b, type: u1}
types:
  pair: {seq: [{id: x, type: u2}, {id: y, type: u4}]}
)"),
                                      Bytes(8));

        EXPECT_EQ(at(placement, "a"), "@0+6");
        EXPECT_EQ(at(placement, "a.y"), "@2+4");
        EXPECT_EQ(at(placement, "b"), "@6+1");
    }

    TEST(PlacementUserTypeTest, SizedTypesTakeTheirSize) {
        const auto placement = placed(with(R"(seq:
  - {id: a, type: pair, size: 10}
  - {id: b, type: u1}
types:
  pair: {seq: [{id: x, type: u2}]}
)"),
                                      Bytes(16));

        EXPECT_EQ(at(placement, "a"), "@0+10");
        EXPECT_EQ(at(placement, "b"), "@10+1");
    }

    TEST(PlacementUserTypeTest, ASizedTypeLimitsItsFields) {
        const auto placement = placed(with(R"(seq:
  - {id: a, type: pair, size: 3}
  - {id: b, type: u1}
types:
  pair: {seq: [{id: x, type: u2}, {id: y, type: u2}]}
)"),
                                      Bytes(16));

        EXPECT_EQ(at(placement, "a.x"), "@0+2");
        EXPECT_EQ(at(placement, "a.y"), "<missing>");
        EXPECT_EQ(at(placement, "b"), "@3+1");
        EXPECT_EQ(problems(placement),
                  "a.y: 2 bytes at offset 2 go past the end at 3");
    }

    TEST(PlacementUserTypeTest, AnIncompleteUnsizedTypeStopsItsParent) {
        const auto placement = placed(with(R"(seq:
  - {id: a, type: pair}
  - {id: b, type: u1}
types:
  pair: {seq: [{id: x, type: u2}, {id: y, type: u8}]}
)"),
                                      Bytes(8));

        EXPECT_EQ(at(placement, "a.x"), "@0+2");
        EXPECT_EQ(at(placement, "a"), "@0+0");
        EXPECT_EQ(at(placement, "b"), "<missing>");
    }

    TEST(PlacementUserTypeTest, ASizedTypeMustFit) {
        const auto placement = placed(with(R"(seq:
  - {id: a, type: pair, size: 32}
types:
  pair: {seq: [{id: x, type: u2}]}
)"),
                                      Bytes(8));

        EXPECT_TRUE(placement.fields.empty());
        EXPECT_EQ(problems(placement),
                  "a: 32 bytes at offset 0 go past the end at 8");
    }

    TEST(PlacementUserTypeTest, MembersOfNestedTypes) {
        Bytes bytes(8);
        put<std::uint8_t>(bytes, 1, 3);
        const auto placement = placed(with(R"(seq:
  - {id: h, type: head}
  - {id: body, size: h.inner.n}
types:
  head: {seq: [{size: 1}, {id: inner, type: inner}]}
  inner: {seq: [{id: n, type: u1}]}
)"),
                                      bytes);

        EXPECT_EQ(at(placement, "body"), "@2+3");
    }

    TEST(PlacementSwitchTest, NegativeCasesMatchSignedValues) {
        Bytes bytes(8);
        put<std::int8_t>(bytes, 0, -1);
        const auto placement = placed(with(R"(seq:
  - {id: k, type: s1}
  - id: body
    type: {switch-on: k, cases: {-1: neg, 1: pos}}
types:
  neg: {seq: [{id: n, type: u2}]}
  pos: {seq: [{id: p, type: u4}]}
)"),
                                      bytes);

        EXPECT_EQ(at(placement, "body.n"), "@1+2");
    }

    TEST(PlacementSwitchTest, NoMatchWithASizeIsBytes) {
        const auto placement = placed(with(R"(seq:
  - {id: k, type: u1}
  - id: body
    size: 4
    type: {switch-on: k, cases: {1: a}}
  - {id: t, type: u1}
types:
  a: {seq: [{id: v, type: u1}]}
)"),
                                      Bytes(8));

        EXPECT_EQ(at(placement, "body"), "@1+4");
        EXPECT_EQ(valueOf(placement, "body"), "<none>");
        EXPECT_EQ(at(placement, "t"), "@5+1");
        EXPECT_EQ(problems(placement), "");
    }

    TEST(PlacementSwitchTest, NoMatchWithoutASizeIsAProblem) {
        const auto placement = placed(with(R"(seq:
  - {id: k, type: u1}
  - id: body
    type: {switch-on: k, cases: {1: a}}
types:
  a: {seq: [{id: v, type: u1}]}
)"),
                                      Bytes(8));

        EXPECT_EQ(problems(placement),
                  "body: no case matches 0 and there is no '_' case");
    }

    TEST(PlacementRepeatTest, RepeatsScalars) {
        Bytes bytes(8);
        put<std::uint8_t>(bytes, 0, 3);
        put<std::uint16_t>(bytes, 3, 9);
        const auto placement = placed(
            with("seq: [{id: n, type: u1}, {id: v, type: u2, repeat: expr, "
                 "repeat-expr: n}]"),
            bytes);

        EXPECT_EQ(at(placement, "v"), "@1+6");
        EXPECT_EQ(at(placement, "v[1]"), "@3+2");
        EXPECT_EQ(valueOf(placement, "v[1]"), "9");
        EXPECT_EQ(valueOf(placement, "v"), "<none>");
    }

    TEST(PlacementRepeatTest, ZeroElements) {
        const auto placement = placed(
            with("seq: [{id: v, type: u2, repeat: expr, repeat-expr: 0}, {id: "
                 "t, type: u1}]"),
            Bytes(4));

        EXPECT_EQ(at(placement, "v"), "@0+0");
        EXPECT_EQ(at(placement, "t"), "@0+1");
    }

    TEST(PlacementRepeatTest, NegativeCountIsAProblem) {
        const auto placement = placed(
            with("seq: [{id: v, type: u1, repeat: expr, repeat-expr: -2}]"),
            Bytes(4));

        EXPECT_TRUE(placement.fields.empty());
        EXPECT_EQ(problems(placement),
                  "v: repeat-expr -2 is outside 0 to 1048576");
    }

    TEST(PlacementRepeatTest, CountOverTheLimitIsAProblem) {
        const auto placement = placed(
            with("seq: [{id: v, type: u1, repeat: expr, repeat-expr: 11}]"),
            Bytes(32), PlacementLimits{.maxRepeat = 10});

        EXPECT_EQ(problems(placement), "v: repeat-expr 11 is outside 0 to 10");
    }

    TEST(PlacementRepeatTest, IndexIsAvailableInsideTheRepeat) {
        const auto placement = placed(
            with("seq: [{id: v, size: _index + 1, repeat: expr, repeat-expr: "
                 "3}]"),
            Bytes(8));

        EXPECT_EQ(at(placement, "v[0]"), "@0+1");
        EXPECT_EQ(at(placement, "v[1]"), "@1+2");
        EXPECT_EQ(at(placement, "v[2]"), "@3+3");
    }

    TEST(PlacementRepeatTest, IndexOutsideARepeatIsAProblem) {
        const auto placement =
            placed(with("seq: [{id: v, size: _index}]"), Bytes(8));

        EXPECT_EQ(problems(placement),
                  "v: size \"_index\": '_index' is only available inside a "
                  "repeat");
    }

    TEST(PlacementRepeatTest, ElementsCanBeIndexed) {
        Bytes bytes(16);
        put<std::uint8_t>(bytes, 1, 4);
        const auto placement = placed(
            with("seq: [{id: v, type: u1, repeat: expr, repeat-expr: 2}, {id: "
                 "body, size: 'v[1]'}]"),
            bytes);

        EXPECT_EQ(at(placement, "body"), "@2+4");
    }

    TEST(PlacementRepeatTest, IndexOutOfRangeIsAProblem) {
        const auto placement = placed(
            with("seq: [{id: v, type: u1, repeat: expr, repeat-expr: 2}, {id: "
                 "body, size: 'v[2]'}]"),
            Bytes(8));

        EXPECT_EQ(problems(placement),
                  "body: size \"v[2]\": index 2 is out of range; 'v' has 2 "
                  "elements");
    }

    TEST(PlacementRepeatTest, MembersOfRepeatedTypes) {
        Bytes bytes(8);
        put<std::uint8_t>(bytes, 1, 2);
        const auto placement = placed(with(R"(seq:
  - {id: items, type: item, repeat: expr, repeat-expr: 2}
  - {id: body, size: 'items[1].v'}
types:
  item: {seq: [{id: v, type: u1}]}
)"),
                                      bytes);

        EXPECT_EQ(at(placement, "items[1].v"), "@1+1");
        EXPECT_EQ(at(placement, "body"), "@2+2");
    }

    TEST(PlacementInstanceTest, PosFromTheStartOfTheMapping) {
        Bytes bytes(16);
        put<std::uint16_t>(bytes, 10, 513);
        const auto placement =
            placed(with("instances: {v: {pos: 10, type: u2}}"), bytes);

        EXPECT_EQ(at(placement, "v"), "@10+2");
        EXPECT_EQ(valueOf(placement, "v"), "513");
    }

    TEST(PlacementInstanceTest, PosInAnUnsizedTypeUsesTheParentStream) {
        const auto placement = placed(with(R"(seq:
  - {size: 4}
  - {id: a, type: holder}
types:
  holder:
    seq: [{id: x, type: u1}]
    instances: {v: {pos: 2, type: u1}}
)"),
                                      Bytes(8));

        EXPECT_EQ(at(placement, "a.v"), "@2+1");
    }

    TEST(PlacementInstanceTest, PosAtTheEndIsAllowedButTheFieldMustFit) {
        const auto placement =
            placed(with("instances: {v: {pos: 8, type: u1}}"), Bytes(8));

        EXPECT_EQ(problems(placement),
                  "v: 1 bytes at offset 8 go past the end at 8");
    }

    TEST(PlacementInstanceTest, NegativePosIsAProblem) {
        const auto placement =
            placed(with("instances: {v: {pos: -1, type: u1}}"), Bytes(8));

        EXPECT_EQ(problems(placement),
                  "v: pos -1 is outside the 8 bytes available");
    }

    TEST(PlacementInstanceTest, SeqFieldsCanUseInstances) {
        Bytes bytes(16);
        put<std::uint8_t>(bytes, 15, 3);
        const auto placement = placed(with(R"(seq:
  - {id: body, size: n}
instances:
  n: {pos: 15, type: u1}
)"),
                                      bytes);

        EXPECT_EQ(at(placement, "body"), "@0+3");
        EXPECT_EQ(at(placement, "n"), "@15+1");
        EXPECT_EQ(paths(placement), "n body");
    }

    TEST(PlacementInstanceTest, InstancesAreOnlyPlacedOnce) {
        const auto placement = placed(with(R"(seq:
  - {id: a, size: n}
  - {id: b, size: n}
instances:
  n: {pos: 7, type: u1}
)"),
                                      Bytes(8));

        EXPECT_EQ(paths(placement), "n a b");
    }

    TEST(PlacementInstanceTest, CyclesAreProblems) {
        const auto placement = placed(with(R"(instances:
  a: {pos: b, type: u1}
  b: {pos: a, type: u1}
)"),
                                      Bytes(8));

        EXPECT_TRUE(placement.fields.empty());
        EXPECT_EQ(problems(placement),
                  "b: pos \"a\": 'a' depends on itself\n"
                  "a: pos \"b\": 'b' could not be placed");
    }

    TEST(PlacementInstanceTest, AFailedInstanceIsReportedOnce) {
        const auto placement = placed(with(R"(seq:
  - {id: a, size: n}
instances:
  n: {pos: 99, type: u1}
)"),
                                      Bytes(8));

        EXPECT_EQ(problems(placement),
                  "n: pos 99 is outside the 8 bytes available\n"
                  "a: size \"n\": 'n' could not be placed");
    }

    TEST(PlacementNameTest, RootAndParent) {
        Bytes bytes(16);
        put<std::uint8_t>(bytes, 0, 2);
        const auto placement = placed(with(R"(seq:
  - {id: n, type: u1}
  - {id: a, type: inner}
types:
  inner:
    seq:
      - {id: p, size: _parent.n}
      - {id: r, size: _root.n + 1}
)"),
                                      bytes);

        EXPECT_EQ(at(placement, "a.p"), "@1+2");
        EXPECT_EQ(at(placement, "a.r"), "@3+3");
    }

    TEST(PlacementNameTest, RootHasNoParent) {
        const auto placement =
            placed(with("seq: [{id: v, size: _parent.n}]"), Bytes(8));

        EXPECT_EQ(problems(placement),
                  "v: size \"_parent.n\": '_root' has no '_parent'");
    }

    TEST(PlacementNameTest, OtherSpecialNamesAreNotSupported) {
        const auto placement =
            placed(with("seq: [{id: v, size: _io.size}]"), Bytes(8));

        EXPECT_EQ(problems(placement),
                  "v: size \"_io.size\": '_io' is not supported by shmscope "
                  "yet");
    }

    TEST(PlacementNameTest, ScalarsHaveNoFields) {
        const auto placement = placed(
            with("seq: [{id: n, type: u1}, {id: v, size: n.x}]"), Bytes(8));

        EXPECT_EQ(problems(placement), "v: size \"n.x\": 'n' has no fields");
    }

    TEST(PlacementNameTest, ScalarsAreNotLists) {
        const auto placement = placed(
            with("seq: [{id: n, type: u1}, {id: v, size: 'n[0]'}]"), Bytes(8));

        EXPECT_EQ(problems(placement), "v: size \"n[0]\": 'n' is not a list");
    }

    TEST(PlacementNumberTest, TypesAreNotNumbers) {
        const auto placement = placed(with(R"(seq:
  - {id: a, type: inner}
  - {id: v, size: a}
types:
  inner: {seq: [{id: x, type: u1}]}
)"),
                                      Bytes(8));

        EXPECT_EQ(problems(placement), "v: size \"a\": 'a' is not a number");
    }

    TEST(PlacementNumberTest, RootIsATypeNotANumber) {
        const auto placement =
            placed(with("seq: [{id: v, size: _root}]"), Bytes(8));

        EXPECT_EQ(problems(placement),
                  "v: size \"_root\": '_root' is a type, not a number");
    }

    TEST(PlacementNumberTest, ListsAreNotNumbers) {
        const auto placement = placed(
            with("seq: [{id: n, type: u1, repeat: expr, repeat-expr: 1}, {id: "
                 "v, size: n}]"),
            Bytes(8));

        EXPECT_EQ(problems(placement),
                  "v: size \"n\": 'n' is a list, not a number");
    }

    TEST(PlacementNumberTest, StringsAreNotNumbers) {
        const auto placement = placed(
            with("seq: [{id: s, type: str, size: 2, encoding: ASCII}, {id: v, "
                 "size: s}]"),
            Bytes(8));

        EXPECT_EQ(problems(placement),
                  "v: size \"s\": 's' is a string, not a number");
    }

    TEST(PlacementNumberTest, FloatsAreNotNumbers) {
        const auto placement = placed(
            with("seq: [{id: f, type: f4}, {id: v, size: f}]"), Bytes(8));

        EXPECT_EQ(problems(placement),
                  "v: size \"f\": 'f' is a floating point number; expressions "
                  "only use integers");
    }

    TEST(PlacementNumberTest, HugeUnsignedValuesAreProblems) {
        Bytes bytes(16);
        put<std::uint64_t>(bytes, 0, std::numeric_limits<std::uint64_t>::max());
        const auto placement =
            placed(with("seq: [{id: n, type: u8}, {id: v, size: n}]"), bytes);

        EXPECT_EQ(problems(placement),
                  "v: size \"n\": 'n' is 18446744073709551615, which does not "
                  "fit in a signed 64 bit integer");
    }

    TEST(PlacementNumberTest, EvaluationErrorsAreProblems) {
        const auto placement = placed(
            with("seq: [{id: n, type: u1}, {id: v, size: 4 / n}]"), Bytes(8));

        EXPECT_EQ(problems(placement), "v: size \"4 / n\": division by zero");
    }

    TEST(PlacementLimitTest, MaxFieldsTruncates) {
        const auto placement = placed(
            with("seq: [{id: v, type: u1, repeat: expr, repeat-expr: 100}]"),
            Bytes(200), PlacementLimits{.maxFields = 5});

        EXPECT_TRUE(placement.truncated);
        EXPECT_EQ(placement.fields.size(), 5U);
        EXPECT_EQ(at(placement, "v[3]"), "@3+1");
        EXPECT_TRUE(placement.problems.empty());
    }

    TEST(PlacementLimitTest, RecursiveTypesStopAtMaxDepth) {
        const auto placement =
            placed(with(R"(seq:
  - {id: c, type: node}
types:
  node: {seq: [{id: v, type: u1}, {id: next, type: node}]}
)"),
                   Bytes(64), PlacementLimits{.maxDepth = 3});

        EXPECT_EQ(at(placement, "c.next.next.v"), "@2+1");
        EXPECT_EQ(at(placement, "c.next.next.next"), "<missing>");
        EXPECT_EQ(problems(placement),
                  "c.next.next.next: types are nested more than 3 deep");
    }

}  // namespace
