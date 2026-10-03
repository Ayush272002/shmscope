#include "shmscope/layout/load_layout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "shmscope/core/decode.hpp"
#include "shmscope/core/default_formatters.hpp"
#include "shmscope/layout/document.hpp"
#include "shmscope/layout/document_reader.hpp"
#include "shmscope/layout/model.hpp"
#include "shmscope/layout/readers/yaml_reader.hpp"

namespace {

    using shmscope::AttributeKind;
    using shmscope::DefaultFormatters;
    using shmscope::FieldType;
    using shmscope::Layout;
    using shmscope::LayoutResult;
    using shmscope::LoadError;
    using shmscope::Node;
    using shmscope::ReadResult;
    using shmscope::Type;
    using shmscope::Value;

    std::filesystem::path fixture(std::string_view name) {
        return std::filesystem::path(__FILE__).parent_path().parent_path() /
               "fixtures" / "layouts" / name;
    }

    std::string bytesText(const std::vector<std::byte>& bytes) {
        std::string text;
        for (const std::byte b : bytes) {
            text += static_cast<char>(b);
        }
        return text;
    }

    std::string summary(const Type& type) {
        std::string text = type.name + "{";
        for (const auto* list : {&type.seq, &type.instances}) {
            for (const auto& attribute : *list) {
                text += std::format(
                    "{}:{}:{}:{}:{}:{}:{};", attribute.id,
                    shmscope::nameOf(attribute.kind),
                    shmscope::nameOf(attribute.scalar), attribute.userType,
                    attribute.size ? attribute.size->text : "",
                    attribute.pos ? attribute.pos->text : "", attribute.format);
            }
            text += "|";
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

    std::string failure(const LayoutResult& result) {
        return result ? std::string("<loaded>")
                      : shmscope::describe(result.error());
    }

    TEST(LoadLayoutTest, FixturesExist) {
        EXPECT_TRUE(std::filesystem::exists(fixture("sample.ksy")));
        EXPECT_TRUE(std::filesystem::exists(fixture("sample.json")));
    }

    TEST(LoadLayoutTest, LoadsAKsyFile) {
        const auto layout = shmscope::loadLayout(fixture("sample.ksy"));

        ASSERT_TRUE(layout.has_value()) << failure(layout);
        EXPECT_EQ(layout->id, "sample_ring");
        EXPECT_EQ(layout->title, "sample ring buffer");
        EXPECT_EQ(bytesText(layout->magic), "SAMPLE01");
        EXPECT_EQ(layout->types.size(), 4U);
        EXPECT_EQ(layout->formats.size(), 4U);
    }

    TEST(LoadLayoutTest, LoadsAJsonFile) {
        const auto layout = shmscope::loadLayout(fixture("sample.json"));

        ASSERT_TRUE(layout.has_value()) << failure(layout);
        EXPECT_EQ(layout->id, "sample_ring");
        EXPECT_EQ(bytesText(layout->magic), "SAMPLE01");
    }

    TEST(LoadLayoutTest, KsyAndJsonFilesGiveTheSameLayout) {
        const auto fromKsy = shmscope::loadLayout(fixture("sample.ksy"));
        const auto fromJson = shmscope::loadLayout(fixture("sample.json"));

        ASSERT_TRUE(fromKsy.has_value()) << failure(fromKsy);
        ASSERT_TRUE(fromJson.has_value()) << failure(fromJson);
        EXPECT_EQ(summary(*fromKsy), summary(*fromJson));
    }

    TEST(LoadLayoutTest, TheSampleIsFullyTyped) {
        const auto layout = shmscope::loadLayout(fixture("sample.ksy"));
        ASSERT_TRUE(layout.has_value()) << failure(layout);

        const Type* trade = layout->findType("trade");
        ASSERT_NE(trade, nullptr);
        ASSERT_EQ(trade->seq.size(), 4U);
        EXPECT_EQ(trade->seq[1].id, "price");
        EXPECT_EQ(trade->seq[1].kind, AttributeKind::SCALAR);
        EXPECT_EQ(trade->seq[1].scalar, FieldType::I64);
        EXPECT_EQ(trade->seq[1].format, "price");

        const auto& records = layout->root.instances.at(0);
        EXPECT_EQ(records.kind, AttributeKind::SWITCH);
        EXPECT_EQ(records.pos->text, "header.records_at");
        EXPECT_EQ(records.repeatCount->text, "header.count");
        EXPECT_EQ(records.switchOn->cases.size(), 3U);
    }

    TEST(LoadLayoutTest, TheSampleFormatsWork) {
        const auto layout = shmscope::loadLayout(fixture("sample.ksy"));
        ASSERT_TRUE(layout.has_value()) << failure(layout);

        EXPECT_EQ(DefaultFormatters::apply(
                      layout->formats.at("price"),
                      Value{.data = std::int64_t{271'179'000}, .width = 8}),
                  "27117.9000");
        EXPECT_EQ(DefaultFormatters::apply(
                      layout->formats.at("side"),
                      Value{.data = std::int64_t{0}, .width = 1}),
                  "buy");
    }

    TEST(LoadLayoutTest, TheSampleKnowsItsLines) {
        const auto layout = shmscope::loadLayout(fixture("sample.ksy"));
        ASSERT_TRUE(layout.has_value()) << failure(layout);

        EXPECT_EQ(layout->root.seq.at(0).location.line, 11);
        EXPECT_EQ(layout->findType("trade")->seq.at(1).location.line, 41);
    }

    TEST(LoadLayoutTest, YamlExtensionWorksToo) {
        const auto layout = shmscope::loadLayout(fixture("minimal.yaml"));

        ASSERT_TRUE(layout.has_value()) << failure(layout);
        EXPECT_EQ(layout->id, "minimal");
        EXPECT_TRUE(layout->root.seq.empty());
    }

    TEST(LoadLayoutTest, LayoutErrorsNameTheFileLineAndPath) {
        const auto layout = shmscope::loadLayout(fixture("missing_endian.ksy"));

        ASSERT_FALSE(layout.has_value());
        EXPECT_EQ(layout.error().source,
                  fixture("missing_endian.ksy").string());
        EXPECT_EQ(layout.error().location.line, 5);
        EXPECT_EQ(layout.error().path, "seq[0].type");
        EXPECT_EQ(layout.error().message,
                  "'u4' needs 'meta: endian: le', or write 'u4le'");
    }

    TEST(LoadLayoutTest, YamlSyntaxErrorsNameTheFileAndLine) {
        const auto layout = shmscope::loadLayout(fixture("broken_syntax.ksy"));

        ASSERT_FALSE(layout.has_value());
        EXPECT_EQ(layout.error().source, fixture("broken_syntax.ksy").string());
        EXPECT_TRUE(layout.error().location.known());
    }

    TEST(LoadLayoutTest, JsonSyntaxErrorsNameTheFileAndLine) {
        const auto layout = shmscope::loadLayout(fixture("broken_syntax.json"));

        ASSERT_FALSE(layout.has_value());
        EXPECT_EQ(layout.error().source,
                  fixture("broken_syntax.json").string());
        EXPECT_EQ(layout.error().location.line, 3);
    }

    TEST(LoadLayoutTest, ADocumentThatIsNoLayout) {
        const auto layout = shmscope::loadLayout(fixture("not_a_layout.json"));

        ASSERT_FALSE(layout.has_value());
        EXPECT_EQ(layout.error().message,
                  "not a layout this build understands (dialects: ksy)");
    }

    TEST(LoadLayoutTest, AnEmptyFileIsNoLayout) {
        const auto layout = shmscope::loadLayout(fixture("empty.ksy"));

        ASSERT_FALSE(layout.has_value());
        EXPECT_EQ(layout.error().message,
                  "not a layout this build understands (dialects: ksy)");
    }

    TEST(LoadLayoutTest, UnknownExtension) {
        const auto layout = shmscope::loadLayout(fixture("layout.toml"));

        ASSERT_FALSE(layout.has_value());
        EXPECT_EQ(layout.error().message, "no reader for '.toml' files");
    }

    TEST(LoadLayoutTest, MissingFile) {
        const auto layout = shmscope::loadLayout(fixture("does_not_exist.ksy"));

        ASSERT_FALSE(layout.has_value());
        EXPECT_EQ(layout.error().source,
                  fixture("does_not_exist.ksy").string());
    }

    TEST(LoadLayoutTest, ADirectoryIsAnError) {
        const auto layout =
            shmscope::loadLayout(fixture("sample.ksy").parent_path());

        EXPECT_FALSE(layout.has_value());
    }

    struct FakeReaders {
        static ReadResult readFile(const std::filesystem::path& file) {
            if (file.filename() == "fail") {
                return std::unexpected(LoadError{.message = "fake read failed",
                                                 .source = file.string()});
            }
            return Node::scalar(file.filename().string());
        }
    };

    struct EchoDialects {
        static LayoutResult load(const Node& root, std::string_view source) {
            return Layout{.id = root.text(), .title = std::string(source)};
        }
    };

    TEST(LoadLayoutTest, ReadersAndDialectsCanBeReplaced) {
        const auto layout =
            shmscope::loadLayout<FakeReaders, EchoDialects>("dir/thing");

        ASSERT_TRUE(layout.has_value());
        EXPECT_EQ(layout->id, "thing");
        EXPECT_EQ(layout->title, "dir/thing");
    }

    TEST(LoadLayoutTest, AReadErrorStopsBeforeTheDialect) {
        const auto layout =
            shmscope::loadLayout<FakeReaders, EchoDialects>("dir/fail");

        ASSERT_FALSE(layout.has_value());
        EXPECT_EQ(layout.error().message, "fake read failed");
    }

    TEST(LoadLayoutTest, CustomDialectsWithTheRealReaders) {
        const auto layout =
            shmscope::loadLayout<shmscope::DefaultReaders, EchoDialects>(
                fixture("minimal.yaml"));

        ASSERT_TRUE(layout.has_value());
        EXPECT_EQ(layout->title, fixture("minimal.yaml").string());
    }

}  // namespace
