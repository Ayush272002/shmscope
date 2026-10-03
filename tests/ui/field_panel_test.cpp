#include "shmscope/ui/field_panel.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <gtest/gtest.h>

#include "shmscope/layout/dialects/ksy_dialect.hpp"
#include "shmscope/layout/document.hpp"
#include "shmscope/layout/model.hpp"
#include "shmscope/layout/placement.hpp"
#include "shmscope/layout/readers/yaml_reader.hpp"

namespace {

    using shmscope::fieldAt;
    using shmscope::formatValue;
    using shmscope::labelOf;
    using shmscope::Layout;
    using shmscope::PlacedField;
    using shmscope::Placement;
    using shmscope::PlacementProblem;

    using Bytes = std::vector<std::byte>;

    constexpr std::string_view PANEL = R"(meta: {id: panel, endian: le}
-shmscope-formats:
  price: {kind: scaled, digits: 2}
seq:
  - {contents: "AB"}
  - {id: name, type: str, size: 4, encoding: ASCII}
  - {id: price, type: s4, -shmscope-format: price}
  - {id: flags, type: u1, -shmscope-format: hex}
  - {id: pad, size: 12}
  - {id: pair, type: pair}
  - {id: list, type: u1, repeat: expr, repeat-expr: 2}
types:
  pair: {seq: [{id: x, type: u1}, {id: y, type: u2}]}
)";

    Layout loaded(std::string_view text) {
        auto document = shmscope::YamlReader::read(text, "test.ksy");
        if (!document) {
            ADD_FAILURE() << shmscope::describe(document.error());
            return {};
        }
        auto layout = shmscope::KsyDialect::load(*document, "test.ksy");
        if (!layout) {
            ADD_FAILURE() << shmscope::describe(layout.error());
            return {};
        }
        return std::move(*layout);
    }

    Bytes panelBytes() {
        Bytes bytes(32);
        std::memcpy(bytes.data(), "ABring", 6);
        const std::int32_t price = -12345;
        std::memcpy(bytes.data() + 6, &price, sizeof price);
        bytes[10] = std::byte{0x0a};
        for (std::size_t i = 11; i < 23; ++i) {
            bytes[i] = static_cast<std::byte>(i);
        }
        bytes[23] = std::byte{7};
        bytes[26] = std::byte{1};
        bytes[27] = std::byte{2};
        return bytes;
    }

    class FieldPanelTest : public ::testing::Test {
    protected:
        const PlacedField& field(std::string_view path) {
            const auto* found = placement_.find(path);
            if (found == nullptr) {
                ADD_FAILURE() << "no field " << path;
                return missing_;
            }
            return *found;
        }

        std::string value(std::string_view path) {
            return formatValue(layout_, field(path), bytes_);
        }

        std::optional<std::string> pathAt(std::size_t offset) {
            const auto index = fieldAt(placement_, offset);
            if (!index) return std::nullopt;

            return placement_.fields[*index].path;
        }

        static std::vector<std::string> draw(const ftxui::Element& element,
                                             int width = 44, int height = 20) {
            auto screen = ftxui::Screen(width, height);
            ftxui::Render(screen, element);
            std::vector<std::string> lines;
            for (int y = 0; y < height; ++y) {
                std::string line;
                for (int x = 0; x < width; ++x) {
                    const auto& character = screen.PixelAt(x, y).character;
                    line += character.empty() ? " " : character;
                }
                while (!line.empty() && line.back() == ' ') line.pop_back();
                lines.push_back(std::move(line));
            }
            return lines;
        }

        static bool inverted(const ftxui::Element& element, int y,
                             int width = 44, int height = 20) {
            auto screen = ftxui::Screen(width, height);
            ftxui::Render(screen, element);
            return screen.PixelAt(2, y).inverted;
        }

        static std::string joined(const std::vector<std::string>& lines) {
            std::string text;
            for (const auto& line : lines) {
                text += line + "\n";
            }
            return text;
        }

        ftxui::Element panel(std::optional<std::size_t> selected,
                             std::size_t rows = 20) {
            return shmscope::renderFieldPanel(layout_, placement_, bytes_,
                                              selected, rows);
        }

        std::size_t indexOf(std::string_view path) {
            return static_cast<std::size_t>(&field(path) -
                                            placement_.fields.data());
        }

        Layout layout_ = loaded(PANEL);
        Bytes bytes_ = panelBytes();
        Placement placement_ = shmscope::place(layout_, bytes_);
        PlacedField missing_{};
    };

    TEST(LabelOfTest, LastSegmentOfAPath) {
        EXPECT_EQ(labelOf("header"), "header");
        EXPECT_EQ(labelOf("header.count"), "count");
        EXPECT_EQ(labelOf("records[3].price"), "price");
        EXPECT_EQ(labelOf("records[3]"), "[3]");
        EXPECT_EQ(labelOf("a.b[12]"), "[12]");
        EXPECT_EQ(labelOf(""), "");
    }

    TEST_F(FieldPanelTest, PlacesTheFixture) {
        EXPECT_TRUE(placement_.problems.empty());
        EXPECT_EQ(field("pair.y").offset, 24U);
        EXPECT_EQ(field("list[1]").offset, 27U);
    }

    TEST_F(FieldPanelTest, PlainNumbersUseDecimal) {
        EXPECT_EQ(value("pair.x"), "7");
        EXPECT_EQ(value("list[1]"), "2");
    }

    TEST_F(FieldPanelTest, DeclaredFormatsAreApplied) {
        EXPECT_EQ(value("price"), "-123.45");
    }

    TEST_F(FieldPanelTest, InlineFormatsAreApplied) {
        EXPECT_EQ(value("flags"), "0x0a");
    }

    TEST_F(FieldPanelTest, StringsAreQuoted) {
        EXPECT_EQ(value("name"), "\"ring\"");
    }

    TEST_F(FieldPanelTest, ContentsShowTheirBytes) {
        EXPECT_EQ(value("_unnamed0"), "41 42");
    }

    TEST_F(FieldPanelTest, LongBytesArePreviewed) {
        EXPECT_EQ(value("pad"), "0b 0c 0d 0e 0f 10 11 12 …");
    }

    TEST_F(FieldPanelTest, ContainersHaveNoValue) {
        EXPECT_EQ(value("pair"), "");
        EXPECT_EQ(value("list"), "");
    }

    TEST_F(FieldPanelTest, BytesPastTheEndShowNothing) {
        PlacedField outside = field("pad");
        outside.offset = 40;

        EXPECT_EQ(formatValue(layout_, outside, bytes_), "");
    }

    TEST_F(FieldPanelTest, BytesCutByTheMappingStopAtTheEnd) {
        PlacedField tail = field("pad");
        tail.offset = 30;

        bytes_[30] = std::byte{0x1e};
        bytes_[31] = std::byte{0x1f};

        EXPECT_EQ(formatValue(layout_, tail, bytes_), "1e 1f …");
    }

    TEST_F(FieldPanelTest, FieldsWithoutAnAttributeHaveNoValue) {
        EXPECT_EQ(formatValue(layout_, PlacedField{}, bytes_), "");
    }

    TEST_F(FieldPanelTest, UnknownFormatNamesFallBackToDecimal) {
        Layout plain = layout_;
        plain.formats.clear();

        EXPECT_EQ(formatValue(plain, field("price"), bytes_), "-12345");
    }

    TEST_F(FieldPanelTest, FieldAtFindsTheInnermostField) {
        EXPECT_EQ(pathAt(0), "_unnamed0");
        EXPECT_EQ(pathAt(7), "price");
        EXPECT_EQ(pathAt(23), "pair.x");
        EXPECT_EQ(pathAt(25), "pair.y");
        EXPECT_EQ(pathAt(27), "list[1]");
    }

    TEST_F(FieldPanelTest, FieldAtOutsideEveryFieldIsEmpty) {
        EXPECT_EQ(pathAt(28), std::nullopt);
        EXPECT_EQ(pathAt(1000), std::nullopt);
    }

    TEST_F(FieldPanelTest, FieldAtPrefersTheSmallerOfTwoAtTheSameDepth) {
        Placement overlapping;
        overlapping.fields.push_back(PlacedField{.path = "wide", .size = 8});
        overlapping.fields.push_back(
            PlacedField{.path = "narrow", .offset = 2, .size = 2});

        EXPECT_EQ(fieldAt(overlapping, 3), 1U);
        EXPECT_EQ(fieldAt(overlapping, 5), 0U);
    }

    TEST_F(FieldPanelTest, FieldAtSkipsEmptyFields) {
        Placement empty;
        empty.fields.push_back(PlacedField{.path = "nothing", .size = 0});

        EXPECT_EQ(fieldAt(empty, 0), std::nullopt);
    }

    TEST_F(FieldPanelTest, HeaderShowsTheSelectedPath) {
        const auto lines = draw(panel(indexOf("pair.y")));

        EXPECT_EQ(lines[0], " pair.y");
    }

    TEST_F(FieldPanelTest, HeaderWithoutASelection) {
        const auto lines = draw(panel(std::nullopt));

        EXPECT_EQ(lines[0], " no field at the cursor");
    }

    TEST_F(FieldPanelTest, RowsShowLabelValueAndOffset) {
        const auto lines = draw(panel(std::nullopt));

        EXPECT_EQ(lines[4], " price                        -123.45   6")
            << joined(lines);
        EXPECT_EQ(lines[6], " pad        0b 0c 0d 0e 0f 10 11 12 …   b");
        EXPECT_EQ(lines[7], " pair                                  17");
        EXPECT_EQ(lines[9], "   y                                0  18");
        EXPECT_EQ(lines[12], "   [1]                              2  1b");
    }

    TEST_F(FieldPanelTest, WideValuesKeepAGapAfterTheLabel) {
        const auto lines = draw(panel(std::nullopt), 30);

        EXPECT_TRUE(lines[6].starts_with(" pad ")) << lines[6];
        EXPECT_NE(lines[6].find("0b 0c"), std::string::npos) << lines[6];
    }

    TEST_F(FieldPanelTest, ColumnsStayTogetherInAWidePanel) {
        const auto lines = draw(panel(std::nullopt), 120);

        EXPECT_EQ(lines[4], " price                        -123.45   6");
    }

    TEST_F(FieldPanelTest, ColumnsFitTheVisibleRows) {
        const auto lines = draw(panel(indexOf("list[1]"), 5), 44, 5);

        EXPECT_EQ(lines[2], " list      1a");
        EXPECT_EQ(lines[3], "   [0]  1  1a");
        EXPECT_EQ(lines[4], "   [1]  2  1b");
    }

    TEST_F(FieldPanelTest, RowsFollowPlacementOrder) {
        const auto lines = draw(panel(std::nullopt));

        EXPECT_TRUE(lines[2].starts_with(" _unnamed0"));
        EXPECT_TRUE(lines[3].starts_with(" name"));
        EXPECT_TRUE(lines[11].starts_with("   [0]"));
        EXPECT_TRUE(lines[12].starts_with("   [1]"));
    }

    TEST_F(FieldPanelTest, TheSelectedRowIsInverted) {
        const auto element = panel(indexOf("name"));

        EXPECT_TRUE(inverted(element, 3));
        EXPECT_FALSE(inverted(element, 2));
        EXPECT_FALSE(inverted(element, 4));
    }

    TEST_F(FieldPanelTest, ShortPanelsScrollToTheSelection) {
        const auto lines = draw(panel(indexOf("list[1]"), 6), 44, 6);

        EXPECT_EQ(lines[0], " list[1]");
        EXPECT_TRUE(lines[2].starts_with("   y"));
        EXPECT_TRUE(lines[5].starts_with("   [1]"));
    }

    TEST_F(FieldPanelTest, ShortPanelsStartAtTheTopWithoutASelection) {
        const auto lines = draw(panel(std::nullopt, 4), 44, 4);

        EXPECT_TRUE(lines[2].starts_with(" _unnamed0"));
        EXPECT_TRUE(lines[3].starts_with(" name"));
    }

    TEST_F(FieldPanelTest, SelectionNearTheTopDoesNotScroll) {
        const auto lines = draw(panel(indexOf("name"), 6), 44, 6);

        EXPECT_TRUE(lines[2].starts_with(" _unnamed0"));
    }

    TEST_F(FieldPanelTest, NothingPlaced) {
        placement_ = {};
        const auto lines = draw(panel(std::nullopt));

        EXPECT_EQ(lines[2], " nothing placed");
    }

    TEST_F(FieldPanelTest, ProblemsAreListedAtTheBottom) {
        placement_.problems.push_back(
            PlacementProblem{.path = "a", .message = "first"});
        placement_.problems.push_back(
            PlacementProblem{.path = "b", .message = "second"});
        const auto lines = draw(panel(std::nullopt));

        EXPECT_EQ(lines[17], " 2 problems");
        EXPECT_EQ(lines[18], "  a: first");
        EXPECT_EQ(lines[19], "  b: second");
    }

    TEST_F(FieldPanelTest, OnlyTheFirstProblemsAreListed) {
        for (int i = 0; i < 5; ++i) {
            placement_.problems.push_back(
                PlacementProblem{.path = std::to_string(i), .message = "bad"});
        }
        const auto lines = draw(panel(std::nullopt));

        EXPECT_EQ(lines[15], " 5 problems");
        EXPECT_EQ(lines[18], "  2: bad");
        EXPECT_EQ(lines[19], "  … 2 more");
    }

    TEST_F(FieldPanelTest, TruncationIsShown) {
        placement_.truncated = true;
        const auto lines = draw(panel(std::nullopt));

        EXPECT_EQ(lines[19], " stopped after 11 fields");
    }

    TEST_F(FieldPanelTest, ProblemsLeaveRoomForTheSelection) {
        placement_.problems.push_back(
            PlacementProblem{.path = "a", .message = "first"});
        const auto lines = draw(panel(indexOf("list[1]"), 8), 44, 8);

        EXPECT_EQ(lines[0], " list[1]");
        EXPECT_TRUE(lines[5].starts_with("   [1]"));
        EXPECT_EQ(lines[6], " 1 problem");
        EXPECT_EQ(lines[7], "  a: first");
    }

}  // namespace
