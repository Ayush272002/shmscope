#include "shmscope/layout/dialects/ksy_dialect.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "shmscope/core/decode.hpp"
#include "shmscope/core/default_formatters.hpp"
#include "shmscope/layout/document.hpp"
#include "shmscope/layout/model.hpp"
#include "shmscope/layout/readers/json_reader.hpp"
#include "shmscope/layout/readers/yaml_reader.hpp"

namespace {

    using shmscope::Attribute;
    using shmscope::AttributeKind;
    using shmscope::DefaultFormatters;
    using shmscope::FieldType;
    using shmscope::JsonReader;
    using shmscope::KsyDialect;
    using shmscope::Layout;
    using shmscope::LayoutResult;
    using shmscope::Node;
    using shmscope::Type;
    using shmscope::Value;
    using shmscope::YamlReader;

    constexpr std::string_view SAMPLE = R"(meta:
  id: sample_ring
  title: sample ring buffer
  endian: le
  encoding: ASCII
-shmscope-formats:
  price: {kind: scaled, digits: 4}
  side: {kind: enum, values: {0: buy, 1: sell, -1: none}}
  stamp: {kind: timestamp, unit: ns}
-webide-representation: '{magic}'
seq:
  - id: header
    type: header
  - id: writer_sequence
    type: u8
    -shmscope-format: hex
instances:
  records:
    pos: header.records_at
    size: header.record_size
    type:
      switch-on: header.kind
      cases:
        1: trade
        2: quote
        _: raw
    repeat: expr
    repeat-expr: header.count
types:
  header:
    seq:
      - {contents: "SAMPLE01"}
      - {id: kind, type: u4}
      - {id: record_size, type: u4}
      - {id: count, type: u8}
      - {id: records_at, type: u8}
      - {id: name, type: str, size: 16}
      - size: 24
  trade:
    seq:
      - {id: sent_at, type: u8, -shmscope-format: stamp}
      - {id: price, type: s8, -shmscope-format: price}
      - {id: side, type: s1, -shmscope-format: side}
      - {id: qty, type: u4le}
  quote:
    seq:
      - {id: bid, type: s8, -shmscope-format: price}
      - {id: ask, type: s8, -shmscope-format: price}
  raw:
    seq:
      - {id: bytes, size: 16}
)";

    constexpr std::string_view SAMPLE_JSON = R"({
 "meta": {"id": "sample_ring", "title": "sample ring buffer", "endian": "le",
          "encoding": "ASCII"},
 "-shmscope-formats": {
  "price": {"kind": "scaled", "digits": 4},
  "side": {"kind": "enum", "values": {"0": "buy", "1": "sell", "-1": "none"}},
  "stamp": {"kind": "timestamp", "unit": "ns"}
 },
 "-webide-representation": "{magic}",
 "seq": [
  {"id": "header", "type": "header"},
  {"id": "writer_sequence", "type": "u8", "-shmscope-format": "hex"}
 ],
 "instances": {
  "records": {
   "pos": "header.records_at", "size": "header.record_size",
   "type": {"switch-on": "header.kind",
            "cases": {"1": "trade", "2": "quote", "_": "raw"}},
   "repeat": "expr", "repeat-expr": "header.count"
  }
 },
 "types": {
  "header": {"seq": [
   {"contents": "SAMPLE01"}, {"id": "kind", "type": "u4"},
   {"id": "record_size", "type": "u4"}, {"id": "count", "type": "u8"},
   {"id": "records_at", "type": "u8"},
   {"id": "name", "type": "str", "size": 16}, {"size": 24}]},
  "trade": {"seq": [
   {"id": "sent_at", "type": "u8", "-shmscope-format": "stamp"},
   {"id": "price", "type": "s8", "-shmscope-format": "price"},
   {"id": "side", "type": "s1", "-shmscope-format": "side"},
   {"id": "qty", "type": "u4le"}]},
  "quote": {"seq": [
   {"id": "bid", "type": "s8", "-shmscope-format": "price"},
   {"id": "ask", "type": "s8", "-shmscope-format": "price"}]},
  "raw": {"seq": [{"id": "bytes", "size": 16}]}
 }
})";

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

    std::string failure(std::string_view text) {
        const auto layout = loadYaml(text);
        if (layout) {
            return "<loaded>";
        }
        const auto& error = layout.error();
        return error.path.empty() ? error.message
                                  : error.path + ": " + error.message;
    }

    std::string with(std::string_view body) {
        return std::format("meta: {{id: x, endian: le}}\n{}", body);
    }

    std::string bytesText(const std::vector<std::byte>& bytes) {
        std::string text;
        for (const std::byte b : bytes) {
            text += static_cast<char>(b);
        }
        return text;
    }

    std::string summary(const Attribute& attribute) {
        std::string text = std::format("{}:{}", attribute.id,
                                       shmscope::nameOf(attribute.kind));
        if (attribute.kind == AttributeKind::SCALAR) {
            text += std::format(":{}", shmscope::nameOf(attribute.scalar));
        }
        if (attribute.kind == AttributeKind::USER) {
            text += ":" + attribute.userType;
        }
        if (attribute.switchOn) {
            text += ":on=" + attribute.switchOn->on.text;
            for (const auto& switchCase : attribute.switchOn->cases) {
                text += std::format(
                    ",{}->{}",
                    switchCase.value ? std::to_string(*switchCase.value) : "_",
                    switchCase.type);
            }
        }
        if (attribute.size) {
            text += ":size=" + attribute.size->text;
        }
        if (attribute.repeatCount) {
            text += ":repeat=" + attribute.repeatCount->text;
        }
        if (attribute.pos) {
            text += ":pos=" + attribute.pos->text;
        }
        if (!attribute.format.empty()) {
            text += ":format=" + attribute.format;
        }
        if (!attribute.contents.empty()) {
            text += ":contents=" + bytesText(attribute.contents);
        }
        return text;
    }

    std::string summary(const Type& type) {
        std::string text = type.name + "{";
        for (const auto& attribute : type.seq) {
            text += summary(attribute) + ";";
        }
        text += "|";
        for (const auto& attribute : type.instances) {
            text += summary(attribute) + ";";
        }
        return text + "}";
    }

    std::string summary(const Layout& layout) {
        std::string text = std::format("{}|{}|{}|", layout.id, layout.title,
                                       bytesText(layout.magic));
        for (const auto& [name, format] : layout.formats) {
            text += name + ",";
        }
        text += summary(layout.root);
        for (const auto& [name, type] : layout.types) {
            text += summary(type);
        }
        return text;
    }

    std::string formatted(const Layout& layout, std::string_view name,
                          Value value) {
        const auto found = layout.formats.find(name);
        if (found == layout.formats.end()) {
            return "<no format>";
        }
        return DefaultFormatters::apply(found->second, value);
    }

    TEST(KsyMatchesTest, RecognisesKsyRoots) {
        EXPECT_TRUE(
            KsyDialect::matches(*YamlReader::read("meta: {id: x}", "")));
        EXPECT_TRUE(KsyDialect::matches(*YamlReader::read("seq: []", "")));
    }

    TEST(KsyMatchesTest, RejectsOtherDocuments) {
        EXPECT_FALSE(KsyDialect::matches(*YamlReader::read("other: 1", "")));
        EXPECT_FALSE(KsyDialect::matches(*YamlReader::read("- meta", "")));
        EXPECT_FALSE(KsyDialect::matches(*YamlReader::read("meta", "")));
        EXPECT_FALSE(KsyDialect::matches(Node()));
    }

    TEST(KsySampleTest, LoadsMetaAndMagic) {
        const auto layout = loaded(SAMPLE);

        EXPECT_EQ(layout.id, "sample_ring");
        EXPECT_EQ(layout.title, "sample ring buffer");
        EXPECT_EQ(bytesText(layout.magic), "SAMPLE01");
        EXPECT_EQ(layout.root.name, "sample_ring");
    }

    TEST(KsySampleTest, LoadsEveryType) {
        const auto layout = loaded(SAMPLE);

        ASSERT_EQ(layout.types.size(), 4U);
        EXPECT_NE(layout.findType("header"), nullptr);
        EXPECT_NE(layout.findType("trade"), nullptr);
        EXPECT_NE(layout.findType("quote"), nullptr);
        EXPECT_NE(layout.findType("raw"), nullptr);
        EXPECT_EQ(layout.findType("sample_ring"), nullptr);
    }

    TEST(KsySampleTest, RootSeqAndInstances) {
        const auto layout = loaded(SAMPLE);

        EXPECT_EQ(summary(layout.root),
                  "sample_ring{header:user type:header;"
                  "writer_sequence:scalar:u64:format=hex;|"
                  "records:switch:on=header.kind,1->trade,2->quote,_->raw:"
                  "size=header.record_size:repeat=header.count:"
                  "pos=header.records_at;}");
    }

    TEST(KsySampleTest, HeaderFieldsInOrder) {
        const auto layout = loaded(SAMPLE);
        const Type* header = layout.findType("header");
        ASSERT_NE(header, nullptr);

        EXPECT_EQ(summary(*header),
                  "header{:contents:contents=SAMPLE01;kind:scalar:u32;"
                  "record_size:scalar:u32;count:scalar:u64;"
                  "records_at:scalar:u64;name:string:size=16;"
                  ":bytes:size=24;|}");
    }

    TEST(KsySampleTest, RecordTypes) {
        const auto layout = loaded(SAMPLE);

        EXPECT_EQ(summary(*layout.findType("trade")),
                  "trade{sent_at:scalar:u64:format=stamp;"
                  "price:scalar:i64:format=price;side:scalar:i8:format=side;"
                  "qty:scalar:u32;|}");
        EXPECT_EQ(summary(*layout.findType("quote")),
                  "quote{bid:scalar:i64:format=price;"
                  "ask:scalar:i64:format=price;|}");
        EXPECT_EQ(summary(*layout.findType("raw")),
                  "raw{bytes:bytes:size=16;|}");
    }

    TEST(KsySampleTest, CompilesDeclaredAndInlineFormats) {
        const auto layout = loaded(SAMPLE);

        ASSERT_EQ(layout.formats.size(), 4U);
        EXPECT_EQ(formatted(layout, "price",
                            Value{.data = std::int64_t{1234567}, .width = 8}),
                  "123.4567");
        EXPECT_EQ(formatted(layout, "side",
                            Value{.data = std::int64_t{-1}, .width = 1}),
                  "none");
        EXPECT_EQ(formatted(layout, "side",
                            Value{.data = std::int64_t{1}, .width = 1}),
                  "sell");
        EXPECT_EQ(formatted(layout, "hex",
                            Value{.data = std::uint64_t{255}, .width = 8}),
                  "0x00000000000000ff");
        EXPECT_EQ(formatted(layout, "stamp",
                            Value{.data = std::uint64_t{0}, .width = 8}),
                  "1970-01-01 00:00:00.000000000 UTC");
    }

    TEST(KsySampleTest, AttributesKnowTheirLines) {
        const auto layout = loaded(SAMPLE);

        EXPECT_EQ(layout.root.seq[0].location.line, 12);
        EXPECT_EQ(layout.root.instances[0].location.line, 19);
        ASSERT_TRUE(layout.root.instances[0].pos.has_value());
        EXPECT_EQ(layout.root.instances[0].pos->location.line, 19);
        EXPECT_EQ(layout.findType("trade")->seq[1].location.line, 42);
    }

    TEST(KsySampleTest, JsonGivesTheSameLayout) {
        const auto fromYaml = loadYaml(SAMPLE);
        const auto fromJson = loadJson(SAMPLE_JSON);

        ASSERT_TRUE(fromYaml.has_value());
        ASSERT_TRUE(fromJson.has_value())
            << shmscope::describe(fromJson.error());
        EXPECT_EQ(summary(*fromYaml), summary(*fromJson));
    }

    TEST(KsyMinimalTest, OnlyMetaIsAnEmptyLayout) {
        const auto layout = loaded("meta: {id: empty}");

        EXPECT_EQ(layout.id, "empty");
        EXPECT_TRUE(layout.root.seq.empty());
        EXPECT_TRUE(layout.root.instances.empty());
        EXPECT_TRUE(layout.types.empty());
        EXPECT_TRUE(layout.formats.empty());
        EXPECT_TRUE(layout.magic.empty());
    }

    struct PrimitiveCase {
        std::string_view kaitai;
        FieldType type;
    };

    class KsyPrimitiveTest : public ::testing::TestWithParam<PrimitiveCase> {};

    TEST_P(KsyPrimitiveTest, MapsToTheStorageType) {
        const auto& [kaitai, type] = GetParam();
        const auto layout =
            loaded(with(std::format("seq: [{{id: a, type: {}}}]", kaitai)));

        ASSERT_EQ(layout.root.seq.size(), 1U);
        EXPECT_EQ(layout.root.seq[0].kind, AttributeKind::SCALAR);
        EXPECT_EQ(layout.root.seq[0].scalar, type);
    }

    INSTANTIATE_TEST_SUITE_P(
        AllPrimitives, KsyPrimitiveTest,
        ::testing::Values(PrimitiveCase{"u1", FieldType::U8},
                          PrimitiveCase{"u2", FieldType::U16},
                          PrimitiveCase{"u4", FieldType::U32},
                          PrimitiveCase{"u8", FieldType::U64},
                          PrimitiveCase{"s1", FieldType::I8},
                          PrimitiveCase{"s2", FieldType::I16},
                          PrimitiveCase{"s4", FieldType::I32},
                          PrimitiveCase{"s8", FieldType::I64},
                          PrimitiveCase{"f4", FieldType::F32},
                          PrimitiveCase{"f8", FieldType::F64},
                          PrimitiveCase{"u2le", FieldType::U16},
                          PrimitiveCase{"u4le", FieldType::U32},
                          PrimitiveCase{"u8le", FieldType::U64},
                          PrimitiveCase{"s2le", FieldType::I16},
                          PrimitiveCase{"s4le", FieldType::I32},
                          PrimitiveCase{"s8le", FieldType::I64},
                          PrimitiveCase{"f4le", FieldType::F32},
                          PrimitiveCase{"f8le", FieldType::F64}));

    TEST(KsyEndianTest, SingleBytesNeedNoEndian) {
        const auto layout = loaded(
            "meta: {id: x}\nseq: [{id: a, type: u1}, "
            "{id: b, type: s1}]");

        EXPECT_EQ(layout.root.seq.size(), 2U);
    }

    TEST(KsyEndianTest, ExplicitLittleEndianNeedsNoMeta) {
        const auto layout = loaded("meta: {id: x}\nseq: [{id: a, type: u4le}]");

        EXPECT_EQ(layout.root.seq[0].scalar, FieldType::U32);
    }

    TEST(KsyEndianTest, MultiByteWithoutEndianIsAnError) {
        EXPECT_EQ(
            failure("meta: {id: x}\nseq: [{id: a, type: u4}]"),
            "seq[0].type: 'u4' needs 'meta: endian: le', or write 'u4le'");
        EXPECT_EQ(
            failure("meta: {id: x}\nseq: [{id: a, type: f8}]"),
            "seq[0].type: 'f8' needs 'meta: endian: le', or write 'f8le'");
    }

    TEST(KsyEndianTest, BigEndianTypesAreRejected) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: u4be}]")),
                  "seq[0].type: big-endian types are not supported by "
                  "shmscope yet");
        EXPECT_EQ(failure(with("seq: [{id: a, type: f8be}]")),
                  "seq[0].type: big-endian types are not supported by "
                  "shmscope yet");
    }

    TEST(KsyEndianTest, SingleBytesTakeNoEndianSuffix) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: u1le}]")),
                  "seq[0].type: 'u1' has one byte, so it takes no endianness");
    }

    TEST(KsyEndianTest, BigEndianMetaIsRejected) {
        EXPECT_EQ(failure("meta: {id: x, endian: be}"),
                  "meta.endian: big-endian layouts are not supported by "
                  "shmscope yet");
    }

    TEST(KsyEndianTest, UnknownEndianIsAnError) {
        EXPECT_EQ(failure("meta: {id: x, endian: middle}"),
                  "meta.endian: 'endian' must be le or be, got 'middle'");
    }

    TEST(KsyEndianTest, SwitchingEndianIsRejected) {
        EXPECT_EQ(failure("meta: {id: x, endian: {switch-on: a, cases: {}}}"),
                  "meta.endian: switching endianness is not supported by "
                  "shmscope yet");
    }

    TEST(KsyEndianTest, TypeNamesEndingInLeOrBeAreUserTypes) {
        const auto layout =
            loaded(with("seq: [{id: a, type: table}, {id: b, type: maybe}]\n"
                        "types: {table: {}, maybe: {}}"));

        EXPECT_EQ(layout.root.seq[0].kind, AttributeKind::USER);
        EXPECT_EQ(layout.root.seq[0].userType, "table");
        EXPECT_EQ(layout.root.seq[1].userType, "maybe");
    }

    TEST(KsyMetaTest, MissingMeta) {
        EXPECT_EQ(failure("seq: []"), "missing 'meta'");
    }

    TEST(KsyMetaTest, MetaMustBeAMap) {
        EXPECT_EQ(failure("meta: x"), "meta: 'meta' must be a map");
    }

    TEST(KsyMetaTest, MissingId) {
        EXPECT_EQ(failure("meta: {title: x}"), "meta: missing 'id'");
    }

    TEST(KsyMetaTest, IdMustBeLowerSnakeCase) {
        EXPECT_EQ(failure("meta: {id: Bad-Id}"),
                  "meta.id: 'Bad-Id' is not a valid id; use lower_snake_case");
        EXPECT_EQ(failure("meta: {id: 9lives}"),
                  "meta.id: '9lives' is not a valid id; use lower_snake_case");
        EXPECT_EQ(failure("meta: {id: ''}"),
                  "meta.id: '' is not a valid id; use lower_snake_case");
    }

    TEST(KsyMetaTest, IdMustBeAScalar) {
        EXPECT_EQ(failure("meta: {id: [a]}"),
                  "meta.id: 'id' must be a single value, not a list");
    }

    TEST(KsyMetaTest, KnownKaitaiMetaKeysAreAccepted) {
        const auto layout = loaded(
            "meta: {id: x, file-extension: bin, ks-version: 0.10, license: "
            "MIT, application: demo, xref: {}, tags: [a], bit-endian: le}");

        EXPECT_EQ(layout.id, "x");
    }

    TEST(KsyMetaTest, ImportsAreNotSupported) {
        EXPECT_EQ(failure("meta: {id: x, imports: [y]}"),
                  "meta.imports: 'imports' is not supported by shmscope yet");
    }

    TEST(KsyMetaTest, UnknownMetaKey) {
        EXPECT_EQ(failure("meta: {id: x, colour: red}"),
                  "meta.colour: unknown key 'colour'");
    }

    TEST(KsyRootTest, MustBeAMap) {
        const auto document = YamlReader::read("- a", "test.ksy");
        ASSERT_TRUE(document.has_value());
        const auto layout = KsyDialect::load(*document, "test.ksy");

        ASSERT_FALSE(layout.has_value());
        EXPECT_EQ(layout.error().message, "a layout must be a map of keys");
    }

    TEST(KsyRootTest, UnknownKey) {
        EXPECT_EQ(failure(with("foo: 1")), "foo: unknown key 'foo'");
    }

    TEST(KsyRootTest, UnsupportedKaitaiKeys) {
        EXPECT_EQ(failure(with("enums: {a: {0: x}}")),
                  "enums: 'enums' is not supported by shmscope yet");
        EXPECT_EQ(failure(with("params: []")),
                  "params: 'params' is not supported by shmscope yet");
    }

    TEST(KsyRootTest, ToolExtensionKeysAreIgnored) {
        const auto layout =
            loaded(with("-webide-representation: x\n"
                        "-orig-id: y\ndoc: about\ndoc-ref: z"));

        EXPECT_EQ(layout.id, "x");
    }

    TEST(KsyRootTest, MisspelledShmscopeKeysAreErrors) {
        EXPECT_EQ(failure(with("-shmscope-fromats: {}")),
                  "-shmscope-fromats: unknown shmscope key "
                  "'-shmscope-fromats'");
    }

    TEST(KsyRootTest, ErrorsCarrySourceLineAndColumn) {
        const auto layout = loadYaml("meta: {id: x}\nseq: [{id: a, type: u4}]");

        ASSERT_FALSE(layout.has_value());
        EXPECT_EQ(shmscope::describe(layout.error()),
                  "test.ksy:2:21: seq[0].type: 'u4' needs 'meta: endian: le', "
                  "or write 'u4le'");
    }

    TEST(KsyRootTest, JsonErrorsCarryAPath) {
        const auto layout = loadJson(
            R"({"meta": {"id": "x"}, "seq": [{"id": "a", "type": "u4"}]})");

        ASSERT_FALSE(layout.has_value());
        EXPECT_EQ(layout.error().path, "seq[0].type");
        EXPECT_EQ(layout.error().source, "test.json");
    }

    TEST(KsyTypesTest, MustBeAMap) {
        EXPECT_EQ(failure(with("types: [a]")),
                  "types: 'types' must be a map of names to types");
    }

    TEST(KsyTypesTest, NamesMustBeLowerSnakeCase) {
        EXPECT_EQ(failure(with("types: {Header: {}}")),
                  "types.Header: 'Header' is not a valid type name; use "
                  "lower_snake_case");
    }

    TEST(KsyTypesTest, CannotShareTheLayoutName) {
        EXPECT_EQ(failure(with("types: {x: {}}")),
                  "types.x: type 'x' has the same name as the layout");
    }

    TEST(KsyTypesTest, MustBeMaps) {
        EXPECT_EQ(failure(with("types: {t: [1]}")),
                  "types.t: a type must be a map");
    }

    TEST(KsyTypesTest, NestedTypesAreNotSupported) {
        EXPECT_EQ(failure(with("types: {t: {types: {u: {}}}}")),
                  "types.t.types: 'types' is not supported by shmscope yet");
    }

    TEST(KsyTypesTest, UnknownKeyInAType) {
        EXPECT_EQ(failure(with("types: {t: {fields: []}}")),
                  "types.t.fields: unknown key 'fields'");
    }

    TEST(KsyTypesTest, TypesMayReferToEachOtherInAnyOrder) {
        const auto layout =
            loaded(with("seq: [{id: a, type: first}]\n"
                        "types:\n"
                        "  first: {seq: [{id: b, type: second}]}\n"
                        "  second: {seq: [{id: c, type: u1}]}"));

        EXPECT_EQ(layout.findType("first")->seq[0].userType, "second");
    }

    TEST(KsyTypesTest, ATypeMayReferToItself) {
        const auto layout = loaded(
            with("types: {node: {seq: [{id: next, type: node, size: 8}]}}"));

        EXPECT_EQ(layout.findType("node")->seq[0].userType, "node");
    }

    TEST(KsyTypesTest, TheLayoutIdNamesTheRootType) {
        const auto layout =
            loaded(with("seq: [{id: again, type: x, size: 4}]"));

        EXPECT_EQ(layout.root.seq[0].userType, "x");
    }

    TEST(KsyTypesTest, UnknownTypeReference) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: header}]")),
                  "seq[0].type: unknown type 'header'");
        EXPECT_EQ(failure(with("types: {t: {seq: [{id: a, type: nope}]}}")),
                  "types.t.seq[0].type: unknown type 'nope'");
        EXPECT_EQ(failure(with("instances: {a: {pos: 0, type: nope}}")),
                  "instances.a.type: unknown type 'nope'");
    }

    TEST(KsyAttributeTest, MustBeAMap) {
        EXPECT_EQ(failure(with("seq: [a]")),
                  "seq[0]: an attribute must be a map");
    }

    TEST(KsyAttributeTest, SeqMustBeAList) {
        EXPECT_EQ(failure(with("seq: {a: 1}")),
                  "seq: 'seq' must be a list of attributes");
    }

    TEST(KsyAttributeTest, NeedsTypeContentsOrSize) {
        EXPECT_EQ(failure(with("seq: [{id: a}]")),
                  "seq[0]: needs a 'type', 'contents' or 'size'");
    }

    TEST(KsyAttributeTest, AnonymousPaddingIsBytes) {
        const auto layout = loaded(with("seq: [{size: 24}]"));

        ASSERT_EQ(layout.root.seq.size(), 1U);
        EXPECT_TRUE(layout.root.seq[0].id.empty());
        EXPECT_EQ(layout.root.seq[0].kind, AttributeKind::BYTES);
        EXPECT_EQ(layout.root.seq[0].size->text, "24");
    }

    TEST(KsyAttributeTest, SizeMayBeAnExpression) {
        const auto layout =
            loaded(with("seq: [{id: n, type: u4}, {id: body, size: n * 2}]"));

        EXPECT_EQ(layout.root.seq[1].size->text, "n * 2");
    }

    TEST(KsyAttributeTest, SizeMustNotBeEmpty) {
        EXPECT_EQ(failure(with("seq: [{id: a, size: ''}]")),
                  "seq[0].size: 'size' is empty");
    }

    TEST(KsyAttributeTest, SizeMustBeAScalar) {
        EXPECT_EQ(failure(with("seq: [{id: a, size: [1]}]")),
                  "seq[0].size: 'size' must be a single value, not a list");
    }

    TEST(KsyAttributeTest, IdsMustBeLowerSnakeCase) {
        EXPECT_EQ(failure(with("seq: [{id: Count, type: u1}]")),
                  "seq[0].id: 'Count' is not a valid id; use lower_snake_case");
    }

    TEST(KsyAttributeTest, DuplicateIdsInSeq) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: u1}, {id: a, type: u2}]")),
                  "seq[1]: 'a' is defined twice in 'x'");
    }

    TEST(KsyAttributeTest, DuplicateIdsAcrossSeqAndInstances) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: u1}]\n"
                               "instances: {a: {pos: 0, type: u1}}")),
                  "instances.a: 'a' is defined twice in 'x'");
    }

    TEST(KsyAttributeTest, AnonymousAttributesMayRepeat) {
        const auto layout = loaded(with("seq: [{size: 1}, {size: 2}]"));

        EXPECT_EQ(layout.root.seq.size(), 2U);
    }

    TEST(KsyAttributeTest, UnknownKey) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: u1, colour: red}]")),
                  "seq[0].colour: unknown key 'colour'");
    }

    TEST(KsyAttributeTest, UnsupportedKaitaiKeys) {
        for (const std::string_view key :
             {"if", "process", "enum", "terminator", "consume", "include",
              "eos-error", "pad-right", "repeat-until", "size-eos", "io",
              "value", "valid", "parent"}) {
            EXPECT_EQ(failure(with(std::format(
                          "seq: [{{id: a, type: u1, {}: x}}]", key))),
                      std::format("seq[0].{}: '{}' is not supported by "
                                  "shmscope yet",
                                  key, key))
                << key;
        }
    }

    TEST(KsyAttributeTest, DocKeysAreAllowed) {
        const auto layout =
            loaded(with("seq: [{id: a, type: u1, doc: the a, doc-ref: b}]"));

        EXPECT_EQ(layout.root.seq.size(), 1U);
    }

    TEST(KsyAttributeTest, SizeCannotBeUsedWithAPrimitive) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: u4, size: 4}]")),
                  "seq[0].size: 'size' cannot be used with 'u4'");
    }

    TEST(KsyAttributeTest, UserTypesMayHaveASize) {
        const auto layout =
            loaded(with("seq: [{id: a, type: t, size: 64}]\n"
                        "types: {t: {}}"));

        EXPECT_EQ(layout.root.seq[0].size->text, "64");
    }

    TEST(KsyAttributeTest, TypeMustBeAScalarOrSwitch) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: [u1]}]")),
                  "seq[0].type: 'type' must be a single value, not a list");
    }

    TEST(KsyAttributeTest, UnsupportedTypeForms) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: strz}]")),
                  "seq[0].type: 'strz' is not supported by shmscope yet; use "
                  "'str' with a 'size'");
        EXPECT_EQ(failure(with("seq: [{id: a, type: b4}]")),
                  "seq[0].type: bit fields are not supported by shmscope yet");
        EXPECT_EQ(failure(with("seq: [{id: a, type: 'rec(1)'}]")),
                  "seq[0].type: parameterised types are not supported by "
                  "shmscope yet");
        EXPECT_EQ(failure(with("seq: [{id: a, type: 'outer::inner'}]")),
                  "seq[0].type: nested type paths are not supported by "
                  "shmscope yet");
        EXPECT_EQ(failure(with("seq: [{id: a, type: Header}]")),
                  "seq[0].type: unknown type 'Header'");
    }

    TEST(KsyAttributeTest, BeOnlyMeansBigEndianAfterAPrimitive) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: b}]")),
                  "seq[0].type: unknown type 'b'");
    }

    TEST(KsyStringTest, UsesTheMetaEncoding) {
        const auto layout = loaded(
            "meta: {id: x, encoding: UTF-8}\nseq: [{id: s, type: str, size: "
            "8}]");

        EXPECT_EQ(layout.root.seq[0].kind, AttributeKind::STRING);
        EXPECT_EQ(layout.root.seq[0].size->text, "8");
    }

    TEST(KsyStringTest, OwnEncodingOverridesMeta) {
        const auto layout = loaded(
            "meta: {id: x, encoding: UTF-16LE}\n"
            "seq: [{id: s, type: str, size: 8, encoding: ascii}]");

        EXPECT_EQ(layout.root.seq[0].kind, AttributeKind::STRING);
    }

    TEST(KsyStringTest, EncodingNamesAreCaseInsensitive) {
        for (const std::string_view encoding :
             {"ASCII", "ascii", "UTF-8", "utf-8", "UTF8", "utf8"}) {
            EXPECT_EQ(failure(with(std::format(
                          "seq: [{{id: s, type: str, size: 4, encoding: {}}}]",
                          encoding))),
                      "<loaded>")
                << encoding;
        }
    }

    TEST(KsyStringTest, NeedsASize) {
        EXPECT_EQ(failure(with("seq: [{id: s, type: str, encoding: ASCII}]")),
                  "seq[0].type: 'str' needs a 'size'");
    }

    TEST(KsyStringTest, NeedsAnEncoding) {
        EXPECT_EQ(failure(with("seq: [{id: s, type: str, size: 4}]")),
                  "seq[0].type: 'str' needs an 'encoding' here or in 'meta'");
    }

    TEST(KsyStringTest, OtherEncodingsAreNotSupported) {
        EXPECT_EQ(
            failure(with("seq: [{id: s, type: str, size: 4, encoding: "
                         "UTF-16LE}]")),
            "seq[0].type: encoding 'UTF-16LE' is not supported by shmscope "
            "yet; use ASCII or UTF-8");
    }

    TEST(KsyStringTest, EncodingOnlyAppliesToStr) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: u1, encoding: ASCII}]")),
                  "seq[0].encoding: 'encoding' only applies to 'str'");
    }

    TEST(KsyContentsTest, TextBecomesBytes) {
        const auto layout = loaded(with("seq: [{contents: \"AB\\0\"}]"));

        EXPECT_EQ(layout.root.seq[0].kind, AttributeKind::CONTENTS);
        ASSERT_EQ(layout.root.seq[0].contents.size(), 3U);
        EXPECT_EQ(layout.root.seq[0].contents[2], std::byte{0});
    }

    TEST(KsyContentsTest, ListOfNumbersAndText) {
        const auto layout =
            loaded(with("seq: [{contents: [0x7f, 69, LF, 255]}]"));

        EXPECT_EQ(bytesText(layout.root.seq[0].contents),
                  std::string("\x7f"
                              "ELF\xff"));
    }

    TEST(KsyContentsTest, NumbersMustFitInAByte) {
        EXPECT_EQ(failure(with("seq: [{contents: [256]}]")),
                  "seq[0].contents[0]: 256 does not fit in a byte");
    }

    TEST(KsyContentsTest, MustNotBeEmpty) {
        EXPECT_EQ(failure(with("seq: [{contents: []}]")),
                  "seq[0].contents: 'contents' is empty");
        EXPECT_EQ(failure(with("seq: [{contents: ''}]")),
                  "seq[0].contents: 'contents' is empty");
    }

    TEST(KsyContentsTest, ItemsMustBeScalars) {
        EXPECT_EQ(failure(with("seq: [{contents: [[1]]}]")),
                  "seq[0].contents[0]: contents items must be numbers or text");
    }

    TEST(KsyContentsTest, MustBeTextOrAList) {
        EXPECT_EQ(failure(with("seq: [{contents: {a: 1}}]")),
                  "seq[0].contents: 'contents' must be text or a list");
    }

    TEST(KsyContentsTest, CannotBeCombinedWithTypeOrSize) {
        EXPECT_EQ(failure(with("seq: [{contents: [1], type: u1}]")),
                  "seq[0].contents: 'contents' cannot be combined with 'type' "
                  "or 'size'");
        EXPECT_EQ(failure(with("seq: [{contents: [1], size: 1}]")),
                  "seq[0].contents: 'contents' cannot be combined with 'type' "
                  "or 'size'");
    }

    TEST(KsyRepeatTest, ExprWithCount) {
        const auto layout = loaded(
            with("seq: [{id: n, type: u4}, {id: items, type: u2, repeat: "
                 "expr, repeat-expr: n}]"));

        ASSERT_TRUE(layout.root.seq[1].repeatCount.has_value());
        EXPECT_EQ(layout.root.seq[1].repeatCount->text, "n");
    }

    TEST(KsyRepeatTest, EosAndUntilAreNotSupported) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: u1, repeat: eos}]")),
                  "seq[0].repeat: 'repeat: eos' is not supported by shmscope "
                  "yet; use 'repeat: expr'");
        EXPECT_EQ(failure(with("seq: [{id: a, type: u1, repeat: until}]")),
                  "seq[0].repeat: 'repeat: until' is not supported by "
                  "shmscope yet; use 'repeat: expr'");
    }

    TEST(KsyRepeatTest, UnknownRepeatKind) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: u1, repeat: always}]")),
                  "seq[0].repeat: 'repeat' must be expr, got 'always'");
    }

    TEST(KsyRepeatTest, ExprNeedsACount) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: u1, repeat: expr}]")),
                  "seq[0].repeat: 'repeat: expr' needs 'repeat-expr'");
    }

    TEST(KsyRepeatTest, CountNeedsRepeat) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: u1, repeat-expr: 3}]")),
                  "seq[0].repeat-expr: 'repeat-expr' needs 'repeat: expr'");
    }

    TEST(KsyInstanceTest, TakesItsIdFromTheKey) {
        const auto layout =
            loaded(with("instances: {tail: {pos: 0x100, type: u4}}"));

        ASSERT_EQ(layout.root.instances.size(), 1U);
        EXPECT_EQ(layout.root.instances[0].id, "tail");
        EXPECT_EQ(layout.root.instances[0].pos->text, "0x100");
    }

    TEST(KsyInstanceTest, KeepsFileOrder) {
        const auto layout = loaded(
            with("instances: {zeta: {pos: 0, type: u1}, alpha: {pos: 1, type: "
                 "u1}}"));

        ASSERT_EQ(layout.root.instances.size(), 2U);
        EXPECT_EQ(layout.root.instances[0].id, "zeta");
        EXPECT_EQ(layout.root.instances[1].id, "alpha");
    }

    TEST(KsyInstanceTest, NeedsAPos) {
        EXPECT_EQ(failure(with("instances: {a: {type: u1}}")),
                  "instances.a: instances need a 'pos'");
    }

    TEST(KsyInstanceTest, PosIsOnlyForInstances) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: u1, pos: 4}]")),
                  "seq[0].pos: 'pos' is only allowed in instances");
    }

    TEST(KsyInstanceTest, IdKeyIsRejected) {
        EXPECT_EQ(failure(with("instances: {a: {id: b, pos: 0, type: u1}}")),
                  "instances.a.id: instances take their id from their key");
    }

    TEST(KsyInstanceTest, KeysMustBeLowerSnakeCase) {
        EXPECT_EQ(failure(with("instances: {Tail: {pos: 0, type: u1}}")),
                  "instances.Tail: 'Tail' is not a valid id; use "
                  "lower_snake_case");
    }

    TEST(KsyInstanceTest, MustBeAMap) {
        EXPECT_EQ(failure(with("instances: [a]")),
                  "instances: 'instances' must be a map of names to "
                  "attributes");
    }

    TEST(KsySwitchTest, IntegerHexNegativeAndDefaultCases) {
        const auto layout = loaded(
            with("seq: [{id: k, type: s4}, {id: body, size: 8, type: "
                 "{switch-on: k, cases: {1: a, 0x10: b, -1: c, _: d}}}]\n"
                 "types: {a: {}, b: {}, c: {}, d: {}}"));
        const auto& cases = layout.root.seq[1].switchOn->cases;

        ASSERT_EQ(cases.size(), 4U);
        EXPECT_EQ(cases[0].value, 1U);
        EXPECT_EQ(cases[1].value, 16U);
        EXPECT_EQ(cases[2].value, static_cast<std::uint64_t>(-1));
        EXPECT_FALSE(cases[3].value.has_value());
        EXPECT_EQ(cases[3].type, "d");
    }

    TEST(KsySwitchTest, NeedsSwitchOnAndCases) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: {cases: {1: t}}}]")),
                  "seq[0].type: a switch needs 'switch-on' and 'cases'");
        EXPECT_EQ(failure(with("seq: [{id: a, type: {switch-on: k}}]")),
                  "seq[0].type: a switch needs 'switch-on' and 'cases'");
    }

    TEST(KsySwitchTest, CasesMustBeANonEmptyMap) {
        EXPECT_EQ(
            failure(with("seq: [{id: a, type: {switch-on: k, cases: {}}}]")),
            "seq[0].type.cases: 'cases' must map values to type names");
        EXPECT_EQ(
            failure(with("seq: [{id: a, type: {switch-on: k, cases: [t]}}]")),
            "seq[0].type.cases: 'cases' must map values to type names");
    }

    TEST(KsySwitchTest, CaseKeysMustBeIntegers) {
        EXPECT_EQ(failure(with(
                      "seq: [{id: a, type: {switch-on: k, cases: {one: t}}}]\n"
                      "types: {t: {}}")),
                  "seq[0].type.cases.one: case 'one' must be an integer or _");
    }

    TEST(KsySwitchTest, DuplicateCasesAcrossSpellings) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: {switch-on: k, cases: "
                               "{1: t, 0x1: t}}}]\ntypes: {t: {}}")),
                  "seq[0].type.cases.0x1: case '0x1' is listed twice");
    }

    TEST(KsySwitchTest, ADefaultCaseAlone) {
        const auto document = YamlReader::read(
            with("seq: [{id: a, type: {switch-on: k, cases: {_: t}}}]\n"
                 "types: {t: {}}"),
            "test.ksy");
        ASSERT_TRUE(document.has_value());
        EXPECT_TRUE(KsyDialect::load(*document, "test.ksy").has_value());
    }

    TEST(KsySwitchTest, CaseTypesMustBeUserTypes) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: {switch-on: k, cases: "
                               "{1: u4}}}]")),
                  "seq[0].type.cases.1: case type 'u4' must name a user type");
    }

    TEST(KsySwitchTest, CaseTypesMustExist) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: {switch-on: k, cases: "
                               "{1: nope}}}]")),
                  "seq[0].type.cases: unknown type 'nope'");
    }

    TEST(KsySwitchTest, UnknownSwitchKey) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: {switch-on: k, cases: "
                               "{1: t}, by: x}}]\ntypes: {t: {}}")),
                  "seq[0].type.by: unknown key 'by'");
    }

    TEST(KsyFormatTest, DeclaredFormatsAreCompiled) {
        const auto layout = loaded(
            with("-shmscope-formats: {cents: {kind: scaled, digits: 2}}\n"
                 "seq: [{id: amount, type: s4, -shmscope-format: cents}]"));

        EXPECT_EQ(layout.root.seq[0].format, "cents");
        EXPECT_EQ(formatted(layout, "cents",
                            Value{.data = std::int64_t{-1505}, .width = 4}),
                  "-15.05");
    }

    TEST(KsyFormatTest, BuiltInFormatsWithoutOptionsCanBeUsedInline) {
        const auto layout =
            loaded(with("seq: [{id: a, type: u2, -shmscope-format: hex}, "
                        "{id: b, type: u2, -shmscope-format: decimal}]"));

        EXPECT_EQ(layout.formats.size(), 2U);
        EXPECT_EQ(formatted(layout, "hex",
                            Value{.data = std::uint64_t{10}, .width = 2}),
                  "0x000a");
    }

    TEST(KsyFormatTest, InlineFormatsAreCompiledOnce) {
        const auto layout =
            loaded(with("seq: [{id: a, type: u2, -shmscope-format: hex}, "
                        "{id: b, type: u4, -shmscope-format: hex}]"));

        EXPECT_EQ(layout.formats.size(), 1U);
    }

    TEST(KsyFormatTest, DeclaredNamesShadowBuiltIns) {
        const auto layout =
            loaded(with("-shmscope-formats: {hex: {kind: scaled, digits: 1}}\n"
                        "seq: [{id: a, type: u2, -shmscope-format: hex}]"));

        EXPECT_EQ(formatted(layout, "hex",
                            Value{.data = std::uint64_t{15}, .width = 2}),
                  "1.5");
    }

    TEST(KsyFormatTest, UnknownFormat) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: u1, -shmscope-format: "
                               "money}]")),
                  "seq[0].-shmscope-format: unknown format 'money'; declare it "
                  "under -shmscope-formats");
    }

    TEST(KsyFormatTest, BuiltInsNeedingOptionsMustBeDeclared) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: u1, -shmscope-format: "
                               "scaled}]")),
                  "seq[0].-shmscope-format: format 'scaled': needs 'digits'; "
                  "declare it with its options under -shmscope-formats");
    }

    TEST(KsyFormatTest, OnlyNumbersTakeFormats) {
        EXPECT_EQ(
            failure(with("seq: [{id: a, size: 4, -shmscope-format: hex}]")),
            "seq[0].-shmscope-format: formats only apply to number types "
            "(this attribute is bytes)");
        EXPECT_EQ(
            failure(with("seq: [{id: a, type: t, -shmscope-format: hex}]\n"
                         "types: {t: {}}")),
            "seq[0].-shmscope-format: formats only apply to number types "
            "(this attribute is user type)");
    }

    TEST(KsyFormatTest, FormatNameMustBeAScalar) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: u1, -shmscope-format: "
                               "[hex]}]")),
                  "seq[0].-shmscope-format: a format name must be a single "
                  "value, not a list");
    }

    TEST(KsyFormatTest, FormatsMustBeAMap) {
        EXPECT_EQ(failure(with("-shmscope-formats: [a]")),
                  "-shmscope-formats: formats must be a map of names to "
                  "formats");
    }

    TEST(KsyFormatTest, FormatNamesMustBeLowerSnakeCase) {
        EXPECT_EQ(failure(with("-shmscope-formats: {Price: {kind: hex}}")),
                  "-shmscope-formats.Price: 'Price' is not a valid format "
                  "name; use lower_snake_case");
    }

    TEST(KsyFormatTest, SpecMustBeAMapWithAKind) {
        EXPECT_EQ(failure(with("-shmscope-formats: {p: scaled}")),
                  "-shmscope-formats.p: a format must be a map with a 'kind'");
        EXPECT_EQ(failure(with("-shmscope-formats: {p: {digits: 2}}")),
                  "-shmscope-formats.p: missing 'kind'");
    }

    TEST(KsyFormatTest, FormatterErrorsAreReported) {
        EXPECT_EQ(failure(with("-shmscope-formats: {p: {kind: scaled, "
                               "digits: 30}}")),
                  "-shmscope-formats.p: format 'scaled': 'digits' must be 0 "
                  "to 18, got '30'");
        EXPECT_EQ(failure(with("-shmscope-formats: {p: {kind: rainbow}}")),
                  "-shmscope-formats.p: unknown format 'rainbow' (known: "
                  "decimal, hex, scaled, enum, timestamp)");
    }

    TEST(KsyFormatTest, ValuesMustBeAMapOfScalars) {
        EXPECT_EQ(failure(with("-shmscope-formats: {s: {kind: enum, values: "
                               "[a]}}")),
                  "-shmscope-formats.s.values: 'values' must be a map of "
                  "numbers to names");
        EXPECT_EQ(failure(with("-shmscope-formats: {s: {kind: enum, values: "
                               "{0: [a]}}}")),
                  "-shmscope-formats.s.values.0: a name must be a single "
                  "value, not a list");
    }

    TEST(KsyFormatTest, OptionsMustBeScalars) {
        EXPECT_EQ(failure(with("-shmscope-formats: {p: {kind: scaled, "
                               "digits: [2]}}")),
                  "-shmscope-formats.p.digits: 'digits' must be a single "
                  "value, not a list");
    }

    TEST(KsyFormatTest, UnknownShmscopeKeyOnAnAttribute) {
        EXPECT_EQ(failure(with("seq: [{id: a, type: u1, -shmscope-scale: 8}]")),
                  "seq[0].-shmscope-scale: unknown shmscope key "
                  "'-shmscope-scale'");
    }

    TEST(KsyMagicTest, FromTheFirstRootAttribute) {
        const auto layout =
            loaded(with("seq: [{contents: MAGIC}, {id: a, type: "
                        "u1}]"));

        EXPECT_EQ(bytesText(layout.magic), "MAGIC");
    }

    TEST(KsyMagicTest, ThroughNestedTypes) {
        const auto layout =
            loaded(with("seq: [{id: outer, type: a}]\n"
                        "types:\n"
                        "  a: {seq: [{id: inner, type: b}]}\n"
                        "  b: {seq: [{contents: [0xca, 0xfe]}]}"));

        EXPECT_EQ(layout.magic.size(), 2U);
        EXPECT_EQ(layout.magic[0], std::byte{0xca});
        EXPECT_EQ(layout.magic[1], std::byte{0xfe});
    }

    TEST(KsyMagicTest, NoneWhenTheFirstAttributeIsData) {
        const auto layout =
            loaded(with("seq: [{id: a, type: u1}, {contents: LATE}]"));

        EXPECT_TRUE(layout.magic.empty());
    }

    TEST(KsyMagicTest, NoneWithoutASeq) {
        const auto layout =
            loaded(with("instances: {a: {pos: 0, contents: "
                        "X}}"));

        EXPECT_TRUE(layout.magic.empty());
    }

    TEST(KsyMagicTest, ASelfReferencingFirstTypeStops) {
        const auto layout =
            loaded(with("seq: [{id: a, type: loop}]\n"
                        "types: {loop: {seq: [{id: b, type: "
                        "loop}]}}"));

        EXPECT_TRUE(layout.magic.empty());
    }

}  // namespace
