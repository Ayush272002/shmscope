#include <unistd.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "shmscope/layout/default_readers.hpp"
#include "shmscope/layout/document.hpp"
#include "shmscope/layout/document_reader.hpp"
#include "shmscope/layout/readers/json_reader.hpp"
#include "shmscope/layout/readers/yaml_reader.hpp"

namespace {

    using shmscope::DefaultReaders;
    using shmscope::describe;
    using shmscope::DocumentReader;
    using shmscope::JsonReader;
    using shmscope::MAX_DOCUMENT_BYTES;
    using shmscope::MAX_DOCUMENT_DEPTH;
    using shmscope::Node;
    using shmscope::ReaderSet;
    using shmscope::ReadResult;
    using shmscope::YamlReader;

    struct FakeReader {
        static constexpr std::string_view NAME = "fake";
        static constexpr std::array<std::string_view, 2> EXTENSIONS = {".fake",
                                                                       ".yaml"};

        static ReadResult read(std::string_view text, std::string_view) {
            return Node::scalar("fake:" + std::string(text));
        }
    };

    struct MissingRead {
        static constexpr std::string_view NAME = "missing";
        static constexpr std::array<std::string_view, 1> EXTENSIONS = {".x"};
    };

    struct WrongReturn {
        static constexpr std::string_view NAME = "wrong";
        static constexpr std::array<std::string_view, 1> EXTENSIONS = {".x"};
        static Node read(std::string_view, std::string_view) { return {}; }
    };

    static_assert(DocumentReader<YamlReader>);
    static_assert(DocumentReader<JsonReader>);
    static_assert(DocumentReader<FakeReader>);
    static_assert(!DocumentReader<int>);
    static_assert(!DocumentReader<MissingRead>);
    static_assert(!DocumentReader<WrongReturn>);

    bool sameTree(const Node& a, const Node& b) {
        if (a.kind() != b.kind() || a.text() != b.text() ||
            a.items().size() != b.items().size() ||
            a.entries().size() != b.entries().size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.items().size(); ++i) {
            if (!sameTree(a.items()[i], b.items()[i])) {
                return false;
            }
        }
        for (std::size_t i = 0; i < a.entries().size(); ++i) {
            if (a.entries()[i].key != b.entries()[i].key ||
                !sameTree(a.entries()[i].value, b.entries()[i].value)) {
                return false;
            }
        }
        return true;
    }

    std::string nested(std::size_t depth) {
        return std::string(depth, '[') + std::string(depth, ']');
    }

    Node yaml(std::string_view text) {
        auto result = YamlReader::read(text, "test.yaml");
        EXPECT_TRUE(result.has_value())
            << (result ? "" : describe(result.error()));
        return result ? *result : Node{};
    }

    Node json(std::string_view text) {
        auto result = JsonReader::read(text, "test.json");
        EXPECT_TRUE(result.has_value())
            << (result ? "" : describe(result.error()));
        return result ? *result : Node{};
    }

    constexpr std::string_view QCORE_KSY = R"(meta:
  id: qcore_md_segment
  endian: le
seq:
  - id: header
    type: segment_header
    size: 128
instances:
  slots:
    pos: header.slots_offset + (header.capacity * 8 + 127) / 128 * 128
    size: header.record_bytes
    repeat: expr
    repeat-expr: header.capacity
types:
  segment_header:
    seq:
      - {id: magic, contents: "qcmdseg\0"}
      - {id: capacity, type: u8}
      - {id: bid_price, type: s8, -shmscope-scale: 8}
)";

    constexpr std::string_view QCORE_JSON = R"({
  "meta": {"id": "qcore_md_segment", "endian": "le"},
  "seq": [{"id": "header", "type": "segment_header", "size": 128}],
  "instances": {
    "slots": {
      "pos": "header.slots_offset + (header.capacity * 8 + 127) / 128 * 128",
      "size": "header.record_bytes",
      "repeat": "expr",
      "repeat-expr": "header.capacity"
    }
  },
  "types": {
    "segment_header": {
      "seq": [
        {"id": "magic", "contents": "qcmdseg\u0000"},
        {"id": "capacity", "type": "u8"},
        {"id": "bid_price", "type": "s8", "-shmscope-scale": 8}
      ]
    }
  }
})";

    TEST(YamlReaderTest, ReadsMapsListsAndScalars) {
        const auto root = yaml("name: seg\nsizes: [1, 2, 3]\n");

        ASSERT_TRUE(root.isMap());
        EXPECT_EQ(root.find("name")->text(), "seg");
        const Node* sizes = root.find("sizes");
        ASSERT_TRUE(sizes->isList());
        ASSERT_EQ(sizes->items().size(), 3U);
        EXPECT_EQ(sizes->items()[2].text(), "3");
    }

    TEST(YamlReaderTest, KeepsKeyOrder) {
        const auto root = yaml("z: 1\na: 2\nm: 3\n");

        ASSERT_EQ(root.entries().size(), 3U);
        EXPECT_EQ(root.entries()[0].key, "z");
        EXPECT_EQ(root.entries()[1].key, "a");
        EXPECT_EQ(root.entries()[2].key, "m");
    }

    TEST(YamlReaderTest, LocationsAreOneBased) {
        const auto root = yaml("a: 1\nb:\n  - x\n");

        EXPECT_EQ(root.entries()[0].location.line, 1);
        EXPECT_EQ(root.entries()[0].location.column, 1);
        EXPECT_EQ(root.entries()[1].location.line, 2);
        const Node& item = root.find("b")->items()[0];
        EXPECT_EQ(item.location().line, 3);
        EXPECT_EQ(item.location().column, 5);
    }

    TEST(YamlReaderTest, KeepsANulInADoubleQuotedString) {
        const auto root = yaml("magic: \"qcmdseg\\0\"\n");

        const std::string& magic = root.find("magic")->text();
        ASSERT_EQ(magic.size(), 8U);
        EXPECT_EQ(magic.back(), '\0');
    }

    TEST(YamlReaderTest, NumbersAndHexStayAsText) {
        const auto root = yaml("a: 0x8100\nb: '42'\nc: -3\n");

        EXPECT_EQ(root.find("a")->text(), "0x8100");
        EXPECT_EQ(root.find("b")->text(), "42");
        EXPECT_EQ(root.find("c")->text(), "-3");
    }

    TEST(YamlReaderTest, TildeAndEmptyValuesAreNull) {
        const auto root = yaml("a: ~\nb:\nc: null\n");

        EXPECT_TRUE(root.find("a")->isNull());
        EXPECT_TRUE(root.find("b")->isNull());
        EXPECT_TRUE(root.find("c")->isNull());
    }

    TEST(YamlReaderTest, EmptyDocumentIsNull) {
        EXPECT_TRUE(yaml("").isNull());
    }

    TEST(YamlReaderTest, AliasesAreResolved) {
        const auto root = yaml("a: &size 128\nb: *size\n");

        EXPECT_EQ(root.find("b")->text(), "128");
    }

    TEST(YamlReaderTest, FlowAndBlockStylesGiveTheSameTree) {
        const auto flow = yaml("seq: [{id: a, type: u1}]\n");
        const auto block = yaml("seq:\n  - id: a\n    type: u1\n");

        EXPECT_TRUE(sameTree(flow, block));
    }

    TEST(YamlReaderTest, DuplicateKeyIsAnErrorWithItsLine) {
        const auto result =
            YamlReader::read("meta: 1\nseq: 2\nmeta: 3\n", "d.yaml");

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().message, "duplicate key 'meta'");
        EXPECT_EQ(result.error().location.line, 3);
        EXPECT_EQ(result.error().source, "d.yaml");
    }

    TEST(YamlReaderTest, NonScalarKeyIsAnError) {
        const auto result = YamlReader::read("? [a, b]\n: 1\n", "k.yaml");

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().message, "map keys must be plain text");
    }

    TEST(YamlReaderTest, SyntaxErrorHasALine) {
        const auto result = YamlReader::read("seq:\n  - {id: a\n", "s.yaml");

        ASSERT_FALSE(result.has_value());
        EXPECT_TRUE(result.error().location.known());
        EXPECT_EQ(result.error().source, "s.yaml");
    }

    TEST(YamlReaderTest, NestingUpToTheLimitIsRead) {
        const auto result = YamlReader::read(
            nested(static_cast<std::size_t>(MAX_DOCUMENT_DEPTH)), "n.yaml");

        EXPECT_TRUE(result.has_value());
    }

    TEST(YamlReaderTest, NestingPastTheLimitIsAnError) {
        const auto result = YamlReader::read(
            nested(static_cast<std::size_t>(MAX_DOCUMENT_DEPTH) + 2), "n.yaml");

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().message, "document is nested too deeply");
    }

    TEST(JsonReaderTest, ReadsObjectsArraysAndScalars) {
        const auto root = json(R"({"name": "seg", "sizes": [1, 2, 3]})");

        ASSERT_TRUE(root.isMap());
        EXPECT_EQ(root.find("name")->text(), "seg");
        ASSERT_EQ(root.find("sizes")->items().size(), 3U);
        EXPECT_EQ(root.find("sizes")->items()[1].text(), "2");
    }

    TEST(JsonReaderTest, NumbersBooleansAndNullBecomeText) {
        const auto root = json(
            R"({"i": 42, "n": -1, "f": 1.5, "t": true, "b": false, "z": null,
                "big": 18446744073709551615})");

        EXPECT_EQ(root.find("i")->text(), "42");
        EXPECT_EQ(root.find("n")->text(), "-1");
        EXPECT_EQ(root.find("f")->text(), "1.5");
        EXPECT_EQ(root.find("t")->text(), "true");
        EXPECT_EQ(root.find("b")->text(), "false");
        EXPECT_TRUE(root.find("z")->isNull());
        EXPECT_EQ(root.find("big")->text(), "18446744073709551615");
    }

    TEST(JsonReaderTest, KeepsKeyOrder) {
        const auto root = json(R"({"z": 1, "a": 2, "m": 3})");

        ASSERT_EQ(root.entries().size(), 3U);
        EXPECT_EQ(root.entries()[0].key, "z");
        EXPECT_EQ(root.entries()[1].key, "a");
        EXPECT_EQ(root.entries()[2].key, "m");
    }

    TEST(JsonReaderTest, KeepsAnEscapedNul) {
        const auto root = json(R"({"magic": "qcmdseg\u0000"})");

        const std::string& magic = root.find("magic")->text();
        ASSERT_EQ(magic.size(), 8U);
        EXPECT_EQ(magic.back(), '\0');
    }

    TEST(JsonReaderTest, KeepsUnicodeText) {
        const auto root = json(R"({"title": "prix €"})");

        EXPECT_EQ(root.find("title")->text(), "prix €");
    }

    TEST(JsonReaderTest, AllowsComments) {
        const auto root = json("// layout\n{\"a\": 1 /* one */}\n");

        EXPECT_EQ(root.find("a")->text(), "1");
    }

    TEST(JsonReaderTest, ValuesHaveNoLocation) {
        const auto root = json(R"({"a": 1})");

        EXPECT_FALSE(root.location().known());
        EXPECT_FALSE(root.find("a")->location().known());
    }

    TEST(JsonReaderTest, SyntaxErrorReportsLineAndColumn) {
        const auto result = JsonReader::read(
            "{\n  \"seq\": [\n    {\"id\": \"a\",,}\n  ]\n}\n", "b.json");

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().source, "b.json");
        EXPECT_EQ(result.error().location.line, 3);
        EXPECT_GT(result.error().location.column, 1);
    }

    TEST(JsonReaderTest, SyntaxErrorOnTheFirstLine) {
        const auto result = JsonReader::read("{,}", "b.json");

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().location.line, 1);
    }

    TEST(JsonReaderTest, EmptyInputIsAnError) {
        EXPECT_FALSE(JsonReader::read("", "e.json").has_value());
    }

    TEST(JsonReaderTest, NestingUpToTheLimitIsRead) {
        const auto result = JsonReader::read(
            nested(static_cast<std::size_t>(MAX_DOCUMENT_DEPTH)), "n.json");

        EXPECT_TRUE(result.has_value());
    }

    TEST(JsonReaderTest, NestingPastTheLimitIsAnErrorWithoutAPath) {
        const auto result = JsonReader::read(
            nested(static_cast<std::size_t>(MAX_DOCUMENT_DEPTH) + 2), "n.json");

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().message, "document is nested too deeply");
        EXPECT_TRUE(result.error().path.empty());
    }

    TEST(ReadersTest, KsyAndJsonGiveTheSameTree) {
        const auto fromKsy = yaml(QCORE_KSY);
        const auto fromJson = json(QCORE_JSON);

        EXPECT_TRUE(sameTree(fromKsy, fromJson));
    }

    TEST(ReaderSetTest, DispatchesByExtension) {
        const auto fromYaml = DefaultReaders::read(".ksy", "a: 1\n", "x.ksy");
        const auto fromJson =
            DefaultReaders::read(".json", R"({"a": 1})", "x.json");

        ASSERT_TRUE(fromYaml.has_value());
        ASSERT_TRUE(fromJson.has_value());
        EXPECT_TRUE(sameTree(*fromYaml, *fromJson));
    }

    TEST(ReaderSetTest, ExtensionsAreCaseInsensitive) {
        EXPECT_TRUE(DefaultReaders::read(".KSY", "a: 1\n", "x").has_value());
        EXPECT_TRUE(DefaultReaders::read(".Json", "{}", "x").has_value());
        EXPECT_TRUE(DefaultReaders::supports(".YML"));
    }

    TEST(ReaderSetTest, UnknownExtensionIsAnError) {
        const auto result = DefaultReaders::read(".toml", "a = 1", "x.toml");

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().message, "no reader for '.toml' files");
        EXPECT_EQ(result.error().source, "x.toml");
    }

    TEST(ReaderSetTest, MissingExtensionIsAnError) {
        const auto result = DefaultReaders::read("", "a: 1", "layout");

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().message, "no reader for '(none)' files");
    }

    TEST(ReaderSetTest, SupportsReportsEveryReader) {
        for (const auto* extension : {".yaml", ".yml", ".ksy", ".json"}) {
            EXPECT_TRUE(DefaultReaders::supports(extension)) << extension;
        }
        EXPECT_FALSE(DefaultReaders::supports(".toml"));
        EXPECT_FALSE(DefaultReaders::supports("ksy"));
    }

    TEST(ReaderSetTest, ExtensionsListsEveryReaderInOrder) {
        const auto all = DefaultReaders::extensions();

        ASSERT_EQ(all.size(), 4U);
        EXPECT_EQ(all[0], ".yaml");
        EXPECT_EQ(all[1], ".yml");
        EXPECT_EQ(all[2], ".ksy");
        EXPECT_EQ(all[3], ".json");
    }

    TEST(ReaderSetTest, ANewReaderPlugsIn) {
        using Set = ReaderSet<YamlReader, JsonReader, FakeReader>;

        const auto result = Set::read(".fake", "hello", "x.fake");

        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->text(), "fake:hello");
        EXPECT_EQ(Set::extensions().size(), 6U);
    }

    TEST(ReaderSetTest, TheFirstReaderToClaimAnExtensionWins) {
        const auto fakeFirst =
            ReaderSet<FakeReader, YamlReader>::read(".yaml", "a: 1", "x");
        const auto yamlFirst =
            ReaderSet<YamlReader, FakeReader>::read(".yaml", "a: 1", "x");

        ASSERT_TRUE(fakeFirst.has_value());
        ASSERT_TRUE(yamlFirst.has_value());
        EXPECT_EQ(fakeFirst->text(), "fake:a: 1");
        EXPECT_TRUE(yamlFirst->isMap());
    }

    TEST(ReaderSetTest, AnEmptySetReadsNothing) {
        using Empty = ReaderSet<>;

        EXPECT_FALSE(Empty::supports(".yaml"));
        EXPECT_TRUE(Empty::extensions().empty());
        EXPECT_FALSE(Empty::read(".yaml", "a: 1", "x").has_value());
    }

    class ReaderFileTest : public ::testing::Test {
    protected:
        void SetUp() override {
            static std::atomic<int> counter{0};
            dir_ = std::filesystem::temp_directory_path() /
                   ("shmscope_reader_test_" + std::to_string(::getpid()) + "_" +
                    std::to_string(counter++));
            std::filesystem::create_directories(dir_);
        }

        void TearDown() override {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }

        std::filesystem::path write(const std::string& name,
                                    std::string_view text) {
            const auto path = dir_ / name;
            std::ofstream out(path, std::ios::binary);
            out << text;
            return path;
        }

        std::filesystem::path dir_;
    };

    TEST_F(ReaderFileTest, ReadsAKsyFile) {
        const auto result =
            DefaultReaders::readFile(write("qcore.ksy", QCORE_KSY));

        ASSERT_TRUE(result.has_value()) << describe(result.error());
        EXPECT_EQ(result->find("meta")->find("id")->text(), "qcore_md_segment");
    }

    TEST_F(ReaderFileTest, KsyAndJsonFilesGiveTheSameTree) {
        const auto ksy =
            DefaultReaders::readFile(write("qcore.ksy", QCORE_KSY));
        const auto jsn =
            DefaultReaders::readFile(write("qcore.json", QCORE_JSON));

        ASSERT_TRUE(ksy.has_value());
        ASSERT_TRUE(jsn.has_value());
        EXPECT_TRUE(sameTree(*ksy, *jsn));
    }

    TEST_F(ReaderFileTest, UppercaseExtensionOnDisk) {
        const auto result =
            DefaultReaders::readFile(write("LAYOUT.KSY", "a: 1\n"));

        EXPECT_TRUE(result.has_value());
    }

    TEST_F(ReaderFileTest, ErrorsNameTheFile) {
        const auto path = write("dupe.yaml", "a: 1\na: 2\n");
        const auto result = DefaultReaders::readFile(path);

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().source, path.string());
        EXPECT_EQ(describe(result.error()),
                  path.string() + ":2:1: duplicate key 'a'");
    }

    TEST_F(ReaderFileTest, MissingFileIsAnError) {
        const auto result = DefaultReaders::readFile(dir_ / "missing.ksy");

        ASSERT_FALSE(result.has_value());
        EXPECT_FALSE(result.error().message.empty());
    }

    TEST_F(ReaderFileTest, UnknownExtensionOnDiskIsAnError) {
        const auto result =
            DefaultReaders::readFile(write("layout.toml", "a = 1"));

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().message, "no reader for '.toml' files");
    }

    TEST_F(ReaderFileTest, EmptyYamlFileIsNull) {
        const auto result = DefaultReaders::readFile(write("empty.yaml", ""));

        ASSERT_TRUE(result.has_value());
        EXPECT_TRUE(result->isNull());
    }

    TEST_F(ReaderFileTest, FileAtTheSizeLimitIsRead) {
        std::string text = "a: ";
        text.resize(static_cast<std::size_t>(MAX_DOCUMENT_BYTES) - 1, 'x');
        text += '\n';
        const auto result = DefaultReaders::readFile(write("limit.yaml", text));

        EXPECT_TRUE(result.has_value());
    }

    TEST_F(ReaderFileTest, FileOverTheSizeLimitIsAnError) {
        const std::string text(static_cast<std::size_t>(MAX_DOCUMENT_BYTES) + 1,
                               '#');
        const auto result = DefaultReaders::readFile(write("huge.yaml", text));

        ASSERT_FALSE(result.has_value());
        EXPECT_NE(result.error().message.find("the limit is"),
                  std::string::npos);
    }

    TEST_F(ReaderFileTest, DirectoryIsAnError) {
        std::filesystem::create_directories(dir_ / "folder.ksy");
        const auto result = DefaultReaders::readFile(dir_ / "folder.ksy");

        EXPECT_FALSE(result.has_value());
    }

}  // namespace
