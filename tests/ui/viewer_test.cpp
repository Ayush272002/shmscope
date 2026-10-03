#include "shmscope/ui/viewer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/terminal.hpp>
#include <gtest/gtest.h>

#include "shmscope/core/source.hpp"
#include "shmscope/layout/dialects/ksy_dialect.hpp"
#include "shmscope/layout/document.hpp"
#include "shmscope/layout/load_layout.hpp"
#include "shmscope/layout/model.hpp"
#include "shmscope/layout/readers/yaml_reader.hpp"

namespace {

    using shmscope::Frame;
    using shmscope::HEAT_MAX;
    using shmscope::Layout;
    using shmscope::Source;
    using shmscope::SourceState;
    using shmscope::Viewer;

    constexpr std::string_view COUNTED = R"(meta: {id: counted, endian: le}
seq:
  - {id: n, type: u1}
  - {id: v, type: u1, repeat: expr, repeat-expr: n}
)";

    constexpr std::string_view TOO_BIG = R"(meta: {id: too_big, endian: le}
seq:
  - {id: a, type: u1}
  - {id: big, size: 1000}
)";

    constexpr std::string_view WIDE = R"(meta: {id: wide, endian: le}
seq:
  - {id: a, type: u2}
  - {id: b, type: u4}
  - {id: pad, size: 1}
  - {id: c, size: 10}
)";

    constexpr std::string_view FAR = R"(meta: {id: far_away, endian: le}
seq:
  - {id: a, type: u1}
instances:
  far: {pos: 0x9000, type: u8}
)";

    constexpr std::string_view MOVING = R"(meta: {id: moving, endian: le}
seq:
  - {id: at, type: u2}
instances:
  item: {pos: at * 16, type: u2}
)";

    constexpr std::string_view MANY = R"(meta: {id: many, endian: le}
seq:
  - {id: v, type: u1, repeat: expr, repeat-expr: 60}
)";

    const auto FIELD_BACKGROUND = ftxui::Color::RGB(45, 55, 85);

    Layout layoutFrom(std::string_view text) {
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

    constexpr int HZ = 15;
    constexpr int SCREEN_WIDTH = 100;
    constexpr int CHROME_WITH_CLOSED_BAR = 6;

    class FakeSource final : public Source {
    public:
        FakeSource(std::string name, std::size_t size, bool* destroyed)
            : name_(std::move(name)),
              bytes_(size),
              heat_(size),
              destroyed_(destroyed) {
            for (std::size_t i = 0; i < size; ++i) {
                bytes_[i] = static_cast<std::byte>(i & 0xff);
            }
        }

        ~FakeSource() override {
            if (destroyed_ != nullptr) {
                *destroyed_ = true;
            }
        }

        FakeSource(const FakeSource&) = delete;
        FakeSource& operator=(const FakeSource&) = delete;
        FakeSource(FakeSource&&) = delete;
        FakeSource& operator=(FakeSource&&) = delete;

        [[nodiscard]] std::string_view name() const noexcept override {
            return name_;
        }

        [[nodiscard]] Frame poll() noexcept override {
            ++polls;
            return Frame{.bytes = bytes_,
                         .heat = heat_,
                         .sequence = static_cast<std::uint64_t>(polls)};
        }

        [[nodiscard]] SourceState state() const noexcept override {
            return currentState;
        }

        int polls = 0;
        SourceState currentState = SourceState::LIVE;
        std::vector<std::uint8_t>& heat() { return heat_; }
        std::vector<std::byte>& bytes() { return bytes_; }

    private:
        std::string name_;
        std::vector<std::byte> bytes_;
        std::vector<std::uint8_t> heat_;
        bool* destroyed_;
    };

    class ViewerTest : public ::testing::Test {
    protected:
        FakeSource& attach(std::size_t size, std::string name = "/fake") {
            auto source = std::make_unique<FakeSource>(std::move(name), size,
                                                       &destroyed_);
            FakeSource& fake = *source;
            viewer_.attach(std::move(source));
            return fake;
        }

        bool press(const ftxui::Event& event) {
            return viewer_.component()->OnEvent(event);
        }

        void type(std::string_view text) {
            for (const char c : text) {
                press(ftxui::Event::Character(c));
            }
        }

        void command(std::string_view text) {
            press(ftxui::Event::Character('/'));
            type(text);
            press(ftxui::Event::Return);
        }

        static ftxui::Event mouse(ftxui::Mouse::Button button) {
            ftxui::Mouse state;
            state.button = button;
            return ftxui::Event::Mouse("", state);
        }

        static ftxui::Event mouseAt(
            ftxui::Mouse::Button button, int x, int y,
            ftxui::Mouse::Motion motion = ftxui::Mouse::Pressed) {
            ftxui::Mouse state;
            state.button = button;
            state.motion = motion;
            state.x = x;
            state.y = y;
            return ftxui::Event::Mouse("", state);
        }

        static int height() { return ftxui::Terminal::Size().dimy; }

        static std::size_t visibleRows() {
            return static_cast<std::size_t>(
                std::max(1, height() - CHROME_WITH_CLOSED_BAR));
        }

        ftxui::Screen draw() {
            auto screen = ftxui::Screen(SCREEN_WIDTH, height());
            ftxui::Render(screen, viewer_.component()->Render());
            return screen;
        }

        std::string screen() {
            const std::string styled = draw().ToString();
            std::string text;
            for (std::size_t i = 0; i < styled.size(); ++i) {
                if (styled[i] == '\x1b') {
                    i = styled.find_first_of("mABCDHJK", i);
                    if (i == std::string::npos) break;
                    continue;
                }
                text += styled[i];
            }
            return text;
        }

        bool shows(std::string_view text) {
            return screen().find(text) != std::string::npos;
        }

        static std::string rowLabel(std::size_t offset) {
            return std::format("{:08x}", offset);
        }

        std::size_t firstRow() {
            const auto screen = draw();
            std::string label;
            for (int x = 3; x < 11; ++x) {
                label += screen.PixelAt(x, 2).character;
            }
            return std::stoul(label, nullptr, 16);
        }

        ftxui::Color byteColor(std::size_t col) {
            const int gap = col >= 8 ? 1 : 0;
            const int x = 13 + static_cast<int>(col * 3) + gap;
            return draw().PixelAt(x, 2).foreground_color;
        }

        ftxui::Color byteBackground(std::size_t col, int row = 0) {
            const int gap = col >= 8 ? 1 : 0;
            const int x = 13 + static_cast<int>(col * 3) + gap;
            return draw().PixelAt(x, 2 + row).background_color;
        }

        ftxui::Color gapBackground(std::size_t col, int row = 0) {
            const int gap = col >= 8 ? 1 : 0;
            const int x = 13 + static_cast<int>(col * 3) + gap + 2;
            return draw().PixelAt(x, 2 + row).background_color;
        }

        bool byteInverted(std::size_t col, int row = 0) {
            const int gap = col >= 8 ? 1 : 0;
            const int x = 13 + static_cast<int>(col * 3) + gap;
            return draw().PixelAt(x, 2 + row).inverted;
        }

        int closes_ = 0;
        bool destroyed_ = false;
        Viewer::LayoutLoader loadWith_{};
        shmscope::SourceOpener openWith_{};
        Viewer viewer_{HZ, [this] { ++closes_; },
                       [this](const std::filesystem::path& file)
                           -> std::expected<Layout, std::string> {
                           if (!loadWith_) {
                               return std::unexpected(
                                   std::string("no loader in this test"));
                           }
                           return loadWith_(file);
                       },
                       [this](std::string_view name) -> shmscope::OpenResult {
                           if (!openWith_) {
                               return std::unexpected(
                                   std::string("no opener in this test"));
                           }
                           return openWith_(name);
                       }};
    };

    TEST_F(ViewerTest, AttachPollsOnce) {
        auto& fake = attach(64);
        EXPECT_EQ(fake.polls, 1);
    }

    TEST_F(ViewerTest, TickPolls) {
        auto& fake = attach(64);
        viewer_.tick();
        viewer_.tick();
        EXPECT_EQ(fake.polls, 3);
    }

    TEST_F(ViewerTest, TickWithoutSourceIsHarmless) {
        viewer_.tick();
        EXPECT_TRUE(shows(" - "));
        EXPECT_TRUE(shows("0 bytes"));
    }

    TEST_F(ViewerTest, SpaceFreezesAndResumes) {
        auto& fake = attach(64);

        EXPECT_TRUE(press(ftxui::Event::Character(' ')));
        viewer_.tick();
        EXPECT_EQ(fake.polls, 1);
        EXPECT_TRUE(shows("frozen"));

        press(ftxui::Event::Character(' '));
        viewer_.tick();
        EXPECT_EQ(fake.polls, 2);
        EXPECT_TRUE(shows("live"));
    }

    TEST_F(ViewerTest, FreezeAndUnfreezeCommands) {
        auto& fake = attach(64);

        command("freeze");
        viewer_.tick();
        EXPECT_EQ(fake.polls, 1);

        command("freeze");
        viewer_.tick();
        EXPECT_EQ(fake.polls, 1);
        EXPECT_TRUE(shows("unknown command /freeze"));

        press(ftxui::Event::Escape);
        command("unfreeze");
        viewer_.tick();
        EXPECT_EQ(fake.polls, 2);
    }

    TEST_F(ViewerTest, FollowMovesToTheFreshlyWrittenRegion) {
        auto& fake = attach(64 * 1024);
        command("follow");
        EXPECT_TRUE(shows("following"));

        constexpr std::size_t writeAt = 0x9000;
        std::fill_n(fake.heat().begin() + writeAt, 64, HEAT_MAX);
        viewer_.tick();
        EXPECT_TRUE(shows(std::format("{:08x}", writeAt)));
        EXPECT_FALSE(shows("00000000  "));

        press(ftxui::Event::ArrowUp);
        EXPECT_FALSE(shows("following"));
    }

    TEST_F(ViewerTest, DetachReleasesTheSource) {
        attach(64);
        viewer_.detach();

        EXPECT_TRUE(destroyed_);
        EXPECT_TRUE(shows("0 bytes"));
    }

    TEST_F(ViewerTest, AttachReplacesThePreviousSource) {
        attach(64, "/first");
        attach(64, "/second");

        EXPECT_TRUE(destroyed_);
        EXPECT_TRUE(shows("/second"));
    }

    TEST_F(ViewerTest, AttachResetsScrollAndFreeze) {
        attach(4096);
        press(ftxui::Event::End);
        press(ftxui::Event::Character(' '));

        auto& fake = attach(4096);
        viewer_.tick();

        EXPECT_EQ(firstRow(), 0U);
        EXPECT_EQ(fake.polls, 2);
    }

    TEST_F(ViewerTest, TitleShowsNameSizeRateAndState) {
        attach(1234, "/fh.test");

        EXPECT_TRUE(shows("/fh.test"));
        EXPECT_TRUE(shows("1234 bytes"));
        EXPECT_TRUE(shows(std::format("{} Hz", HZ)));
        EXPECT_TRUE(shows("live"));
    }

    TEST_F(ViewerTest, FooterWarnsAboutTornReadsAndOffersCommands) {
        attach(64);

        EXPECT_TRUE(shows("values may tear mid write"));
        EXPECT_TRUE(shows("/ for commands"));
    }
    TEST_F(ViewerTest, RowsShowOffsetAndBytesWithMidRowGap) {
        attach(64);

        EXPECT_TRUE(shows("00000000  00 01 02 03 04 05 06 07  08 09 0a 0b"));
        EXPECT_TRUE(shows("00000030  30 31"));
    }

    TEST_F(ViewerTest, PartialLastRowStopsAtTheEnd) {
        attach(20);

        EXPECT_TRUE(shows("00000010  10 11 12 13"));
        EXPECT_FALSE(shows("10 11 12 13 14"));
        EXPECT_FALSE(shows(rowLabel(0x20)));
    }

    TEST_F(ViewerTest, EmptySourceDrawsNoRows) {
        attach(0);

        EXPECT_FALSE(shows(rowLabel(0)));
        EXPECT_TRUE(shows("0 bytes"));
    }

    TEST_F(ViewerTest, OnlyVisibleRowsAreDrawn) {
        attach(1 << 20);

        EXPECT_TRUE(shows(rowLabel((visibleRows() - 1) * 16)));
        EXPECT_FALSE(shows(rowLabel(visibleRows() * 16)));
    }

    TEST_F(ViewerTest, HotByteIsYellow) {
        auto& fake = attach(64);
        fake.heat()[3] = HEAT_MAX;
        viewer_.tick();

        EXPECT_EQ(byteColor(3), ftxui::Color::Interpolate(
                                    1.0F, ftxui::Color::RGB(170, 170, 170),
                                    ftxui::Color::RGB(255, 200, 0)));
        EXPECT_NE(byteColor(3), ftxui::Color::RGB(170, 170, 170));
    }

    TEST_F(ViewerTest, HotZeroByteIsAlsoHighlighted) {
        auto& fake = attach(64);
        fake.heat()[0] = HEAT_MAX;
        viewer_.tick();

        EXPECT_NE(byteColor(0), ftxui::Color::RGB(90, 90, 90));
    }

    TEST_F(ViewerTest, HeatInTheSecondHalfOfTheRowIsDrawnPastTheGap) {
        auto& fake = attach(64);
        fake.heat()[8] = HEAT_MAX;
        viewer_.tick();

        EXPECT_NE(byteColor(8), ftxui::Color::RGB(170, 170, 170));
        EXPECT_EQ(byteColor(7), ftxui::Color::RGB(170, 170, 170));
    }

    TEST_F(ViewerTest, ColdZeroIsDimmerThanColdNonZero) {
        attach(64);

        EXPECT_EQ(byteColor(0), ftxui::Color::RGB(90, 90, 90));
        EXPECT_EQ(byteColor(1), ftxui::Color::RGB(170, 170, 170));
    }

    TEST_F(ViewerTest, FadingByteIsBetweenColdAndHot) {
        auto& fake = attach(64);
        fake.heat()[9] = HEAT_MAX / 2;
        viewer_.tick();

        const auto color = byteColor(9);
        EXPECT_NE(color, ftxui::Color::RGB(255, 200, 0));
        EXPECT_NE(color, ftxui::Color::RGB(170, 170, 170));
    }

    TEST_F(ViewerTest, ArrowDownScrollsOneRow) {
        attach(4096);
        draw();

        EXPECT_TRUE(press(ftxui::Event::ArrowDown));
        EXPECT_EQ(firstRow(), 0x10U);
        press(ftxui::Event::ArrowDown);
        EXPECT_EQ(firstRow(), 0x20U);
    }

    TEST_F(ViewerTest, ArrowUpScrollsBack) {
        attach(4096);
        draw();
        press(ftxui::Event::ArrowDown);
        press(ftxui::Event::ArrowDown);

        press(ftxui::Event::ArrowUp);
        EXPECT_EQ(firstRow(), 0x10U);
        press(ftxui::Event::ArrowUp);
        EXPECT_EQ(firstRow(), 0U);
    }

    TEST_F(ViewerTest, VimKeysAreNotShortcuts) {
        attach(4096);
        draw();

        for (const char key : {'j', 'k', 'g', 'G'}) {
            EXPECT_FALSE(press(ftxui::Event::Character(key))) << key;
        }
        EXPECT_EQ(firstRow(), 0U);
        EXPECT_EQ(closes_, 0);
    }

    TEST_F(ViewerTest, ScrollingUpStopsAtTheTop) {
        attach(4096);
        draw();
        press(ftxui::Event::ArrowUp);
        press(ftxui::Event::PageUp);

        EXPECT_EQ(firstRow(), 0U);
    }

    TEST_F(ViewerTest, PageDownAndUpMoveByAScreen) {
        attach(1 << 16);
        draw();

        press(ftxui::Event::PageDown);
        EXPECT_EQ(firstRow(), visibleRows() * 16);
        press(ftxui::Event::PageUp);
        EXPECT_EQ(firstRow(), 0U);
    }

    TEST_F(ViewerTest, EndGoesToTheLastScreen) {
        attach(4096);
        draw();

        press(ftxui::Event::End);
        EXPECT_EQ(firstRow(), (256 - visibleRows()) * 16);
        EXPECT_TRUE(shows(rowLabel(0xff0)));
    }

    TEST_F(ViewerTest, HomeGoesToTheTop) {
        attach(4096);
        draw();
        press(ftxui::Event::End);

        press(ftxui::Event::Home);
        EXPECT_EQ(firstRow(), 0U);
    }

    TEST_F(ViewerTest, ScrollingDownStopsAtTheLastScreen) {
        attach(4096);
        draw();
        for (int i = 0; i < 1000; ++i) {
            press(ftxui::Event::PageDown);
        }

        EXPECT_EQ(firstRow(), (256 - visibleRows()) * 16);
    }

    TEST_F(ViewerTest, ObjectSmallerThanTheScreenNeverScrolls) {
        attach(32);
        draw();
        press(ftxui::Event::End);
        press(ftxui::Event::PageDown);
        press(ftxui::Event::ArrowDown);

        EXPECT_EQ(firstRow(), 0U);
    }

    TEST_F(ViewerTest, QCloses) {
        attach(64);
        EXPECT_TRUE(press(ftxui::Event::Character('q')));
        EXPECT_EQ(closes_, 1);
    }

    TEST_F(ViewerTest, EscapeCloses) {
        attach(64);
        EXPECT_TRUE(press(ftxui::Event::Escape));
        EXPECT_EQ(closes_, 1);
    }

    TEST_F(ViewerTest, UnknownKeyIsNotHandled) {
        attach(64);
        EXPECT_FALSE(press(ftxui::Event::Character('x')));
        EXPECT_EQ(closes_, 0);
    }

    TEST_F(ViewerTest, ScrollKeysNeverClose) {
        attach(4096);
        for (const auto& event :
             {ftxui::Event::ArrowDown, ftxui::Event::ArrowUp,
              ftxui::Event::PageDown, ftxui::Event::PageUp, ftxui::Event::Home,
              ftxui::Event::End, ftxui::Event::Character(' ')}) {
            press(event);
        }
        EXPECT_EQ(closes_, 0);
    }

    TEST_F(ViewerTest, EscapeWithTheBarOpenClosesOnlyTheBar) {
        attach(64);
        press(ftxui::Event::Character('/'));

        press(ftxui::Event::Escape);
        EXPECT_EQ(closes_, 0);

        press(ftxui::Event::Escape);
        EXPECT_EQ(closes_, 1);
    }

    TEST_F(ViewerTest, KeysTypedIntoTheBarDoNotReachTheViewer) {
        attach(4096);
        draw();
        press(ftxui::Event::Character('/'));
        type("qjG ");

        EXPECT_EQ(closes_, 0);
        EXPECT_EQ(firstRow(), 0U);
        EXPECT_TRUE(shows("live"));
    }

    TEST_F(ViewerTest, CloseCommandCloses) {
        attach(64);
        command("close");
        EXPECT_EQ(closes_, 1);
    }

    TEST_F(ViewerTest, BarSuggestionsShrinkTheHexPane) {
        attach(1 << 20);
        press(ftxui::Event::Character('/'));

        EXPECT_FALSE(shows(rowLabel((visibleRows() - 9) * 16)));
        EXPECT_TRUE(shows(rowLabel((visibleRows() - 10) * 16)));
    }

    TEST_F(ViewerTest, ListsAllCommands) {
        attach(64);
        press(ftxui::Event::Character('/'));

        for (const auto* name :
             {"/jump", "/freeze", "/top", "/bottom", "/close"}) {
            EXPECT_TRUE(shows(name)) << name;
        }
    }

    TEST_F(ViewerTest, JumpScrollsToTheRowHoldingTheOffset) {
        attach(1 << 16);
        draw();

        command("jump 1234");
        EXPECT_EQ(firstRow(), 0x1230U);
    }

    TEST_F(ViewerTest, JumpAcceptsHexPrefixAndUppercase) {
        attach(1 << 16);
        draw();

        command("jump 0x100");
        EXPECT_EQ(firstRow(), 0x100U);
        command("jump 0XAB0");
        EXPECT_EQ(firstRow(), 0xab0U);
        command("jump FF0");
        EXPECT_EQ(firstRow(), 0xff0U);
    }

    TEST_F(ViewerTest, JumpNearTheEndStopsAtTheLastScreen) {
        attach(4096);
        draw();

        command("jump ff0");
        EXPECT_EQ(firstRow(), (256 - visibleRows()) * 16);
    }

    TEST_F(ViewerTest, JumpToTheLastByteIsAllowed) {
        attach(4096);
        draw();

        command("jump fff");
        EXPECT_FALSE(shows("past the end"));
    }

    TEST_F(ViewerTest, JumpPastTheEndIsAnError) {
        attach(4096);
        draw();

        command("jump 1000");
        EXPECT_TRUE(shows("0x1000 is past the end (0x1000 bytes)"));
        EXPECT_EQ(firstRow(), 0U);
    }

    TEST_F(ViewerTest, JumpRejectsNonHex) {
        attach(4096);

        command("jump xyz");
        EXPECT_TRUE(shows("/jump needs a hex offset, got 'xyz'"));
    }

    TEST_F(ViewerTest, JumpRejectsTrailingJunk) {
        attach(4096);

        command("jump 10g");
        EXPECT_TRUE(shows("got '10g'"));
    }

    TEST_F(ViewerTest, JumpRejectsABarePrefix) {
        attach(4096);

        command("jump 0x");
        EXPECT_TRUE(shows("got '0x'"));
    }

    TEST_F(ViewerTest, JumpWithoutAnOffsetWaitsForOne) {
        attach(4096);

        command("jump");
        EXPECT_TRUE(shows("› /jump "));
        EXPECT_FALSE(shows("needs a hex offset"));
    }

    TEST_F(ViewerTest, JumpWithABlankOffsetIsAnError) {
        attach(4096);

        command("jump zz");
        EXPECT_TRUE(shows("/jump needs a hex offset, got 'zz'"));
    }

    TEST_F(ViewerTest, JumpRejectsAnOverflowingOffset) {
        attach(4096);

        command("jump ffffffffffffffffff");
        EXPECT_TRUE(shows("needs a hex offset"));
    }

    TEST_F(ViewerTest, JumpWithNoSourceIsPastTheEnd) {
        command("jump 0");
        EXPECT_TRUE(shows("past the end"));
    }

    TEST_F(ViewerTest, ScrollingWorksWhileFrozen) {
        auto& fake = attach(4096);
        draw();
        press(ftxui::Event::Character(' '));

        press(ftxui::Event::PageDown);
        command("jump 800");

        EXPECT_EQ(firstRow(), 0x800U);
        EXPECT_EQ(fake.polls, 1);
    }

    TEST_F(ViewerTest, ExactlyOneRowObjectHasNoEmptySecondRow) {
        attach(16);

        EXPECT_TRUE(shows("0f"));
        EXPECT_FALSE(shows(rowLabel(0x10)));
    }

    TEST_F(ViewerTest, OffsetsBeyondThirtyTwoBitsKeepAllDigits) {
        attach(1 << 16);
        draw();
        command("jump fff0");

        EXPECT_TRUE(shows("0000fff0"));
    }

    TEST_F(ViewerTest, ErrorFromACommandShrinksThePaneByOneRow) {
        attach(1 << 20);
        command("jump xyz");

        EXPECT_FALSE(shows(rowLabel((visibleRows() - 1) * 16)));
        EXPECT_TRUE(shows(rowLabel((visibleRows() - 2) * 16)));
    }

    TEST_F(ViewerTest, DetachWhileFrozenThenAttachIsLive) {
        attach(64);
        press(ftxui::Event::Character(' '));
        viewer_.detach();

        auto& fake = attach(64);
        viewer_.tick();

        EXPECT_EQ(fake.polls, 2);
        EXPECT_TRUE(shows("live"));
    }

    TEST_F(ViewerTest, TopAndBottomCommands) {
        attach(4096);
        draw();

        command("bottom");
        EXPECT_EQ(firstRow(), (256 - visibleRows()) * 16);
        command("top");
        EXPECT_EQ(firstRow(), 0U);
    }

    TEST_F(ViewerTest, LiveShowsOnlyChangedRowsWithGapsFolded) {
        auto& fake = attach(64 * 1024);
        fake.heat()[0x85] = HEAT_MAX;
        fake.heat()[0x9003] = 1;
        viewer_.tick();

        command("live");
        EXPECT_TRUE(shows("changes only"));
        EXPECT_TRUE(shows(rowLabel(0x80)));
        EXPECT_TRUE(shows(rowLabel(0x9000)));
        EXPECT_FALSE(shows(rowLabel(0x90)));
        EXPECT_TRUE(shows("⋯ 0x80 bytes unchanged"));
        EXPECT_TRUE(shows("⋯ 0x8f70 bytes unchanged"));
    }

    TEST_F(ViewerTest, LiveKeepsARegionWholeAcrossShortQuietStretches) {
        auto& fake = attach(4096);
        fake.heat()[0x100] = HEAT_MAX;
        fake.heat()[0x130] = HEAT_MAX;
        viewer_.tick();

        command("live");
        EXPECT_TRUE(shows(rowLabel(0x110)));
        EXPECT_TRUE(shows(rowLabel(0x120)));
        EXPECT_FALSE(shows("⋯ 0x20 bytes unchanged"));
    }

    TEST_F(ViewerTest, LiveFoldsTheTailOfALongRegion) {
        auto& fake = attach(4096);
        std::fill_n(fake.heat().begin() + 0x200, 48 * 16, HEAT_MAX);
        viewer_.tick();

        command("live");
        const std::size_t shown = visibleRows() - 2;
        EXPECT_TRUE(shows(rowLabel(0x200 + ((shown - 1) * 16))));
        EXPECT_FALSE(shows(rowLabel(0x200 + (shown * 16))));
        EXPECT_TRUE(shows(std::format("⋯ {} more rows", 48 - shown)));
    }

    TEST_F(ViewerTest, LiveKeepsTheMinimumPerRegionWhenCrowded) {
        auto& fake = attach(64 * 1024);
        for (std::ptrdiff_t region = 1; region <= 8; ++region) {
            std::fill_n(fake.heat().begin() + (0x1000 * region), 48 * 16,
                        HEAT_MAX);
        }
        viewer_.tick();

        command("live");
        EXPECT_TRUE(shows(rowLabel(0x1050)));
        EXPECT_FALSE(shows(rowLabel(0x1060)));
        EXPECT_TRUE(shows("⋯ 42 more rows"));
    }

    TEST_F(ViewerTest, LiveWithNothingChangingSaysSo) {
        attach(4096);
        command("live");
        EXPECT_TRUE(shows("nothing is changing"));
    }

    TEST_F(ViewerTest, LiveAndHexSwapInTheCommandList) {
        attach(4096);
        command("live");
        press(ftxui::Event::Character('/'));
        EXPECT_TRUE(shows("/hex"));
        EXPECT_FALSE(shows("/live"));
        press(ftxui::Event::Escape);

        command("hex");
        EXPECT_FALSE(shows("changes only"));
    }

    TEST_F(ViewerTest, JumpAndFollowLeaveLive) {
        attach(64 * 1024);
        command("live");
        command("jump 4000");
        EXPECT_FALSE(shows("changes only"));
        EXPECT_EQ(firstRow(), 0x4000U);

        command("live");
        command("follow");
        EXPECT_FALSE(shows("changes only"));
        EXPECT_TRUE(shows("following"));
    }

    TEST_F(ViewerTest, FollowStaysOnTheRecordWhileTheHeaderAlsoTicks) {
        auto& fake = attach(64 * 1024);
        command("follow");
        std::fill_n(fake.heat().begin() + 0x9000, 64, HEAT_MAX);
        viewer_.tick();
        ASSERT_TRUE(shows(rowLabel(0x9000)));

        std::ranges::fill(fake.heat(), 0);
        std::fill_n(fake.heat().begin() + 0x80, 5, HEAT_MAX);
        std::fill_n(fake.heat().begin() + 0x9040, 8, HEAT_MAX);
        viewer_.tick();
        EXPECT_TRUE(shows(rowLabel(0x9040)));
        EXPECT_FALSE(shows(rowLabel(0x80)));
    }

    TEST_F(ViewerTest, FollowMovesOnWhenTheTrackedRegionGoesQuiet) {
        auto& fake = attach(64 * 1024);
        command("follow");
        std::fill_n(fake.heat().begin() + 0x9000, 64, HEAT_MAX);
        viewer_.tick();

        std::ranges::fill(fake.heat(), 0);
        std::fill_n(fake.heat().begin() + 0xc000, 16, HEAT_MAX);
        viewer_.tick();
        EXPECT_TRUE(shows(rowLabel(0xc000)));
        EXPECT_FALSE(shows(rowLabel(0x9000)));
    }

    TEST_F(ViewerTest, FollowHoldsStillWhenNothingIsWritten) {
        auto& fake = attach(64 * 1024);
        command("follow");
        std::fill_n(fake.heat().begin() + 0x9000, 64, HEAT_MAX);
        viewer_.tick();

        std::ranges::fill(fake.heat(), 0);
        viewer_.tick();
        EXPECT_TRUE(shows(rowLabel(0x9000)));
        EXPECT_TRUE(shows("following"));
    }

    TEST_F(ViewerTest, FollowIgnoresBytesThatAreOnlyFading) {
        auto& fake = attach(64 * 1024);
        command("follow");
        std::fill_n(fake.heat().begin() + 0x9000, 64, HEAT_MAX - 1);
        viewer_.tick();
        EXPECT_EQ(firstRow(), 0U);
    }

    TEST_F(ViewerTest, FollowPausesWhileFrozen) {
        auto& fake = attach(64 * 1024);
        command("follow");
        command("freeze");
        std::fill_n(fake.heat().begin() + 0x9000, 64, HEAT_MAX);
        viewer_.tick();
        EXPECT_EQ(firstRow(), 0U);

        command("unfreeze");
        viewer_.tick();
        EXPECT_TRUE(shows(rowLabel(0x9000)));
    }

    TEST_F(ViewerTest, FollowPlacesTheWriteAQuarterDownTheScreen) {
        auto& fake = attach(64 * 1024);
        draw();
        command("follow");
        std::fill_n(fake.heat().begin() + 0x9000, 16, HEAT_MAX);
        viewer_.tick();
        EXPECT_EQ(firstRow(), 0x9000 - ((visibleRows() / 4) * 16));
    }

    TEST_F(ViewerTest, FKeyTogglesFollowAndLeavesLive) {
        attach(4096);
        command("live");
        press(ftxui::Event::Character('f'));
        EXPECT_TRUE(shows("following"));
        EXPECT_FALSE(shows("changes only"));

        press(ftxui::Event::Character('f'));
        EXPECT_FALSE(shows("following"));
    }

    TEST_F(ViewerTest, UnfollowCommandStopsFollowing) {
        attach(4096);
        command("follow");
        command("unfollow");
        EXPECT_FALSE(shows("following"));

        press(ftxui::Event::Character('/'));
        EXPECT_TRUE(shows("/follow"));
        EXPECT_FALSE(shows("/unfollow"));
    }

    TEST_F(ViewerTest, ArrowDownStopsFollowing) {
        attach(64 * 1024);
        command("follow");
        press(ftxui::Event::ArrowDown);
        EXPECT_FALSE(shows("following"));
    }

    TEST_F(ViewerTest, LiveScrollsThroughChangedRowsNotTheMapping) {
        auto& fake = attach(64 * 1024);
        for (std::size_t row = 0; row < 1000; row += 4) {
            fake.heat()[row * 16] = 1;
        }
        viewer_.tick();
        command("live");
        ASSERT_TRUE(shows(rowLabel(0)));

        press(ftxui::Event::ArrowDown);
        EXPECT_FALSE(shows(rowLabel(0)));
        EXPECT_TRUE(shows("changes only"));

        press(ftxui::Event::End);
        EXPECT_TRUE(shows(rowLabel(996 * 16)));

        press(ftxui::Event::Home);
        EXPECT_TRUE(shows(rowLabel(0)));
        EXPECT_FALSE(shows(rowLabel(996 * 16)));
    }

    TEST_F(ViewerTest, LiveBottomUsesTheCurrentListNotTheLastDrawn) {
        auto& fake = attach(64 * 1024);
        viewer_.tick();
        command("live");
        draw();

        for (std::size_t row = 0; row < 1000; row += 4) {
            fake.heat()[row * 16] = 1;
        }
        viewer_.tick();
        command("bottom");
        EXPECT_TRUE(shows(rowLabel(996 * 16)));
    }

    TEST_F(ViewerTest, TopAndBottomCommandsWorkInLive) {
        auto& fake = attach(64 * 1024);
        for (std::size_t row = 0; row < 1000; row += 4) {
            fake.heat()[row * 16] = 1;
        }
        viewer_.tick();
        command("live");
        draw();

        command("bottom");
        EXPECT_TRUE(shows(rowLabel(996 * 16)));
        EXPECT_TRUE(shows("changes only"));

        command("top");
        EXPECT_TRUE(shows(rowLabel(0)));
    }

    TEST_F(ViewerTest, LiveDropsRowsOnceTheirHeatIsGone) {
        auto& fake = attach(4096);
        fake.heat()[0x100] = 1;
        viewer_.tick();
        command("live");
        ASSERT_TRUE(shows(rowLabel(0x100)));

        std::ranges::fill(fake.heat(), 0);
        viewer_.tick();
        EXPECT_FALSE(shows(rowLabel(0x100)));
        EXPECT_TRUE(shows("nothing is changing"));
    }

    TEST_F(ViewerTest, LiveShowsTheLastPartialRow) {
        auto& fake = attach(100);
        fake.heat()[99] = HEAT_MAX;
        viewer_.tick();
        command("live");
        EXPECT_TRUE(shows(rowLabel(0x60)));
        EXPECT_TRUE(shows("⋯ 0x60 bytes unchanged"));
    }

    TEST_F(ViewerTest, AttachLeavesLive) {
        attach(4096);
        command("live");
        attach(4096);
        EXPECT_FALSE(shows("changes only"));
    }

    TEST_F(ViewerTest, CtrlCIsLeftToTheTerminal) {
        attach(4096);
        draw();
        EXPECT_FALSE(press(ftxui::Event::CtrlC));
        EXPECT_EQ(closes_, 0);
    }

    TEST_F(ViewerTest, WheelOverTheHexScrollsLikeTheArrows) {
        attach(4096);
        draw();

        EXPECT_TRUE(press(mouseAt(ftxui::Mouse::WheelDown, 20, 5)));
        EXPECT_TRUE(press(mouseAt(ftxui::Mouse::WheelDown, 20, 5)));
        EXPECT_TRUE(shows("cursor 0x20 "));

        EXPECT_TRUE(press(mouseAt(ftxui::Mouse::WheelUp, 20, 5)));
        EXPECT_TRUE(shows("cursor 0x10 "));
    }

    TEST_F(ViewerTest, OtherButtonsAreLeftToTheTerminal) {
        attach(4096);
        draw();

        EXPECT_FALSE(press(mouseAt(ftxui::Mouse::Right, 20, 5)));
        EXPECT_FALSE(press(mouseAt(ftxui::Mouse::Middle, 20, 5)));
        EXPECT_FALSE(
            press(mouseAt(ftxui::Mouse::Left, 20, 5, ftxui::Mouse::Released)));
        EXPECT_TRUE(shows("cursor 0x0 "));
    }

    TEST_F(ViewerTest, ClickingAByteMovesTheCursor) {
        attach(4096);
        draw();

        EXPECT_TRUE(press(mouseAt(ftxui::Mouse::Left, 13 + 5 * 3, 4)));
        EXPECT_TRUE(shows("cursor 0x25 "));
        EXPECT_TRUE(byteInverted(5, 2));

        EXPECT_TRUE(press(mouseAt(ftxui::Mouse::Left, 13 + 9 * 3 + 1 + 1, 3)));
        EXPECT_TRUE(shows("cursor 0x19 "));
    }

    TEST_F(ViewerTest, ClicksOutsideTheBytesAreIgnored) {
        attach(4096);
        draw();

        EXPECT_FALSE(press(mouseAt(ftxui::Mouse::Left, 5, 4)));
        EXPECT_FALSE(press(mouseAt(ftxui::Mouse::Left, 13 + 8 * 3, 4)));
        EXPECT_FALSE(press(mouseAt(ftxui::Mouse::Left, 13 + 16 * 3 + 2, 4)));
        EXPECT_FALSE(press(mouseAt(ftxui::Mouse::Left, 20, 1)));
        EXPECT_TRUE(shows("cursor 0x0 "));
    }

    TEST_F(ViewerTest, ClickingPastTheLastByteIsIgnored) {
        attach(20);
        draw();

        EXPECT_FALSE(press(mouseAt(ftxui::Mouse::Left, 13 + 6 * 3, 3)));
        EXPECT_TRUE(shows("cursor 0x0 "));
    }

    TEST_F(ViewerTest, ArrowKeysFromTheWheelScrollInLive) {
        auto& fake = attach(64 * 1024);
        for (std::size_t row = 0; row < 1000; row += 4) {
            fake.heat()[row * 16] = 1;
        }
        viewer_.tick();
        command("live");
        draw();

        for (int i = 0; i < 3; ++i) {
            press(ftxui::Event::ArrowDown);
        }
        EXPECT_FALSE(shows(rowLabel(0x40)));
        EXPECT_TRUE(shows(rowLabel(0x80)));
    }

    TEST_F(ViewerTest, CursorStartsOnTheFirstByte) {
        attach(4096);

        EXPECT_TRUE(shows("cursor 0x0 "));
        EXPECT_TRUE(byteInverted(0));
        EXPECT_FALSE(byteInverted(1));
    }

    TEST_F(ViewerTest, InspectorDecodesTheBytesAtTheCursor) {
        attach(4096);

        EXPECT_TRUE(shows("u8      0 "));
        EXPECT_TRUE(shows("u16     256 "));
        EXPECT_TRUE(shows("u64     506097522914230528"));
        EXPECT_TRUE(shows("ascii   ........"));
    }

    TEST_F(ViewerTest, InspectorListsEveryType) {
        attach(4096);

        for (const auto* name : {"u8 ", "u16 ", "u32 ", "u64 ", "i8 ", "i16 ",
                                 "i32 ", "i64 ", "f32 ", "f64 ", "ascii "}) {
            EXPECT_TRUE(shows(name)) << name;
        }
        EXPECT_FALSE(shows("fixed8"));
    }

    TEST_F(ViewerTest, ArrowRightAndLeftMoveTheCursorOneByte) {
        attach(4096);

        press(ftxui::Event::ArrowRight);
        EXPECT_TRUE(shows("cursor 0x1 "));
        EXPECT_TRUE(shows("u8      1 "));
        EXPECT_TRUE(byteInverted(1));
        EXPECT_FALSE(byteInverted(0));

        press(ftxui::Event::ArrowLeft);
        EXPECT_TRUE(shows("cursor 0x0 "));
    }

    TEST_F(ViewerTest, ArrowRightCrossesIntoTheSecondHalfOfTheRow) {
        attach(4096);

        for (int i = 0; i < 8; ++i) {
            press(ftxui::Event::ArrowRight);
        }
        EXPECT_TRUE(byteInverted(8));
        EXPECT_TRUE(shows("u8      8 "));
    }

    TEST_F(ViewerTest, ArrowRightWrapsToTheNextRow) {
        attach(4096);
        draw();

        for (int i = 0; i < 16; ++i) {
            press(ftxui::Event::ArrowRight);
        }
        EXPECT_TRUE(shows("cursor 0x10 "));
        EXPECT_TRUE(byteInverted(0, 1));
    }

    TEST_F(ViewerTest, ArrowLeftAtTheStartStaysPut) {
        attach(4096);

        press(ftxui::Event::ArrowLeft);
        EXPECT_TRUE(shows("cursor 0x0 "));
        EXPECT_TRUE(byteInverted(0));
    }

    TEST_F(ViewerTest, ArrowDownScrollsAndCarriesTheCursor) {
        attach(1 << 16);
        draw();
        press(ftxui::Event::ArrowRight);
        press(ftxui::Event::ArrowRight);

        press(ftxui::Event::ArrowDown);
        EXPECT_EQ(firstRow(), 0x10U);
        EXPECT_TRUE(shows("cursor 0x12 "));
        EXPECT_TRUE(byteInverted(2));
    }

    TEST_F(ViewerTest, ArrowUpAtTheTopMovesTheCursorUpARow) {
        attach(1 << 16);
        draw();
        for (int i = 0; i < 0x23; ++i) {
            press(ftxui::Event::ArrowRight);
        }

        press(ftxui::Event::ArrowUp);
        EXPECT_EQ(firstRow(), 0U);
        EXPECT_TRUE(shows("cursor 0x13 "));
        EXPECT_TRUE(byteInverted(3, 1));
    }

    TEST_F(ViewerTest, PageDownCarriesTheCursorAPage) {
        attach(1 << 20);
        draw();

        press(ftxui::Event::PageDown);
        EXPECT_EQ(firstRow(), visibleRows() * 16);
        EXPECT_TRUE(shows(std::format("cursor 0x{:x} ", visibleRows() * 16)));
        EXPECT_TRUE(byteInverted(0));
    }

    TEST_F(ViewerTest, ArrowDownAtTheEndStopsOnTheLastRow) {
        attach(64);
        draw();

        for (int i = 0; i < 10; ++i) {
            press(ftxui::Event::ArrowDown);
        }
        EXPECT_TRUE(shows("cursor 0x30 "));
    }

    TEST_F(ViewerTest, ArrowDownOnTheLastRowKeepsTheColumn) {
        attach(64);
        draw();
        command("jump 35");

        press(ftxui::Event::ArrowDown);
        EXPECT_TRUE(shows("cursor 0x35 "));
    }

    TEST_F(ViewerTest, ArrowUpOnTheFirstRowKeepsTheColumn) {
        attach(64);
        draw();
        command("jump 5");

        press(ftxui::Event::ArrowUp);
        EXPECT_TRUE(shows("cursor 0x5 "));
    }

    TEST_F(ViewerTest, ArrowDownIntoAShortLastRowStopsOnTheLastByte) {
        attach(40);
        draw();
        command("jump 1c");

        press(ftxui::Event::ArrowDown);
        EXPECT_TRUE(shows("cursor 0x27 "));
    }

    TEST_F(ViewerTest, EndPutsTheCursorOnTheLastByte) {
        attach(4096);
        draw();

        press(ftxui::Event::End);
        EXPECT_TRUE(shows("cursor 0xfff "));
        EXPECT_TRUE(shows("u8      255 "));
    }

    TEST_F(ViewerTest, ValuesRunningPastTheEndShowADash) {
        attach(4096);
        draw();

        press(ftxui::Event::End);
        EXPECT_TRUE(shows("u16     –"));
        EXPECT_TRUE(shows("u64     –"));
        EXPECT_TRUE(shows("ascii   –"));
    }

    TEST_F(ViewerTest, HomeReturnsTheCursorToTheStart) {
        attach(1 << 16);
        draw();
        press(ftxui::Event::End);

        press(ftxui::Event::Home);
        EXPECT_EQ(firstRow(), 0U);
        EXPECT_TRUE(shows("cursor 0x0 "));
    }

    TEST_F(ViewerTest, JumpPutsTheCursorOnTheExactByte) {
        attach(1 << 16);
        draw();

        command("jump 1234");
        EXPECT_EQ(firstRow(), 0x1230U);
        EXPECT_TRUE(shows("cursor 0x1234 "));
        EXPECT_TRUE(shows("u8      52 "));
        EXPECT_TRUE(byteInverted(4));
    }

    TEST_F(ViewerTest, JumpNearTheEndKeepsTheCursorOnScreen) {
        attach(1 << 16);
        draw();

        command("jump fff8");
        EXPECT_TRUE(shows("cursor 0xfff8 "));
        EXPECT_TRUE(shows(rowLabel(0xfff0)));
    }

    TEST_F(ViewerTest, MovingTheCursorStopsFollowing) {
        attach(4096);
        command("follow");
        EXPECT_TRUE(shows("following"));

        press(ftxui::Event::ArrowRight);
        EXPECT_FALSE(shows("following"));
    }

    TEST_F(ViewerTest, InspectorTracksLiveChangesAtTheCursor) {
        auto& fake = attach(4096);
        EXPECT_TRUE(shows("u8      0 "));

        fake.bytes()[0] = std::byte{0x2a};
        viewer_.tick();
        EXPECT_TRUE(shows("u8      42 "));
    }

    TEST_F(ViewerTest, LiveHidesTheCursorAndInspector) {
        auto& fake = attach(4096);
        fake.heat()[0] = HEAT_MAX;
        viewer_.tick();

        command("live");
        EXPECT_FALSE(shows("cursor 0x"));
        EXPECT_FALSE(byteInverted(0));
    }

    TEST_F(ViewerTest, LeftAndRightDoNothingInLive) {
        attach(4096);
        command("live");

        EXPECT_FALSE(press(ftxui::Event::ArrowRight));
        EXPECT_FALSE(press(ftxui::Event::ArrowLeft));

        command("hex");
        EXPECT_TRUE(shows("cursor 0x0 "));
    }

    TEST_F(ViewerTest, AttachingResetsTheCursor) {
        attach(4096);
        command("jump 100");
        EXPECT_TRUE(shows("cursor 0x100 "));

        attach(4096);
        EXPECT_TRUE(shows("cursor 0x0 "));
    }

    TEST_F(ViewerTest, EmptySourceShowsNoValues) {
        attach(0);

        press(ftxui::Event::ArrowRight);
        press(ftxui::Event::End);
        EXPECT_TRUE(shows("cursor 0x0 "));
        EXPECT_TRUE(shows("u8      –"));
    }

    TEST_F(ViewerTest, NoLayoutShowsNoLayoutState) {
        attach(64);

        EXPECT_FALSE(shows("fields"));
    }

    TEST_F(ViewerTest, LayoutShowsIdAndFieldCount) {
        auto& fake = attach(64);
        fake.bytes()[0] = std::byte{3};
        viewer_.tick();
        viewer_.setLayout(layoutFrom(COUNTED));

        EXPECT_TRUE(shows(" counted · 5 fields "));
        EXPECT_FALSE(shows("problems"));
    }

    TEST_F(ViewerTest, LayoutSetBeforeAttachIsApplied) {
        viewer_.setLayout(layoutFrom(COUNTED));
        EXPECT_TRUE(shows(" counted · 0 fields "));

        attach(64);

        EXPECT_TRUE(shows(" counted · 2 fields "));
    }

    TEST_F(ViewerTest, TickPlacesTheNewBytes) {
        auto& fake = attach(64);
        viewer_.setLayout(layoutFrom(COUNTED));
        EXPECT_TRUE(shows(" counted · 2 fields "));

        fake.bytes()[0] = std::byte{4};
        viewer_.tick();

        EXPECT_TRUE(shows(" counted · 6 fields "));
    }

    TEST_F(ViewerTest, FrozenKeepsThePlacement) {
        auto& fake = attach(64);
        viewer_.setLayout(layoutFrom(COUNTED));
        press(ftxui::Event::Character(' '));

        fake.bytes()[0] = std::byte{4};
        viewer_.tick();
        EXPECT_TRUE(shows(" counted · 2 fields "));

        press(ftxui::Event::Character(' '));
        viewer_.tick();
        EXPECT_TRUE(shows(" counted · 6 fields "));
    }

    TEST_F(ViewerTest, ProblemsAreCounted) {
        attach(64);
        viewer_.setLayout(layoutFrom(TOO_BIG));

        EXPECT_TRUE(shows(" too_big · 1 field · 1 problem "));
    }

    TEST_F(ViewerTest, DetachKeepsTheLayoutButNotTheFields) {
        attach(64);
        viewer_.setLayout(layoutFrom(COUNTED));
        viewer_.detach();

        EXPECT_TRUE(shows(" counted · 0 fields "));
    }

    TEST_F(ViewerTest, ClearingTheLayoutHidesIt) {
        attach(64);
        viewer_.setLayout(layoutFrom(COUNTED));
        viewer_.setLayout(std::nullopt);

        EXPECT_FALSE(shows("counted"));
        EXPECT_FALSE(shows("fields"));
    }

    TEST_F(ViewerTest, ReplacingTheLayoutPlacesAgain) {
        attach(64);
        viewer_.setLayout(layoutFrom(TOO_BIG));
        viewer_.setLayout(layoutFrom(COUNTED));

        EXPECT_TRUE(shows(" counted · 2 fields "));
        EXPECT_FALSE(shows("problems"));
    }

    TEST_F(ViewerTest, TheSidePaneStartsRightAfterTheHex) {
        attach(64);
        const auto screen = draw();

        EXPECT_EQ(screen.PixelAt(1 + 61, 2).character, "│");
    }

    TEST_F(ViewerTest, TheFieldPanelLeavesRoomForWholeHexRows) {
        attach(64);
        viewer_.setLayout(layoutFrom(COUNTED));

        EXPECT_TRUE(shows("08 09 0a 0b 0c 0d 0e 0f"));
        EXPECT_TRUE(
            shows("00000010  10 11 12 13 14 15 16 17  "
                  "18 19 1a 1b 1c 1d 1e 1f"));
    }

    TEST_F(ViewerTest, LayoutShowsTheFieldPanelInsteadOfTheInspector) {
        attach(64);
        EXPECT_TRUE(shows("cursor 0x0"));

        viewer_.setLayout(layoutFrom(COUNTED));

        EXPECT_FALSE(shows("cursor 0x0"));
        EXPECT_TRUE(shows(" n  "));
        EXPECT_TRUE(shows(" v  "));
    }

    TEST_F(ViewerTest, FieldPanelFollowsTheCursor) {
        auto& fake = attach(64);
        fake.bytes()[0] = std::byte{3};
        viewer_.tick();
        viewer_.setLayout(layoutFrom(COUNTED));
        EXPECT_TRUE(shows(" n  "));

        press(ftxui::Event::ArrowRight);
        press(ftxui::Event::ArrowRight);

        EXPECT_TRUE(shows(" v[1]  "));
    }

    TEST_F(ViewerTest, FieldPanelSaysWhenTheCursorIsOutsideEveryField) {
        attach(64);
        viewer_.setLayout(layoutFrom(COUNTED));
        press(ftxui::Event::ArrowRight);

        EXPECT_TRUE(shows("no field at the cursor"));
    }

    TEST_F(ViewerTest, FieldPanelListsProblems) {
        attach(64);
        viewer_.setLayout(layoutFrom(TOO_BIG));

        EXPECT_TRUE(shows(" 1 problem "));
        EXPECT_TRUE(shows("big: 1000 bytes at offset 1"));
    }

    TEST_F(ViewerTest, IKeySwitchesBetweenFieldsAndInspector) {
        attach(64);
        viewer_.setLayout(layoutFrom(COUNTED));

        EXPECT_TRUE(press(ftxui::Event::Character('i')));
        EXPECT_TRUE(shows("cursor 0x0"));

        EXPECT_TRUE(press(ftxui::Event::Character('i')));
        EXPECT_FALSE(shows("cursor 0x0"));
    }

    TEST_F(ViewerTest, IKeyDoesNothingWithoutALayout) {
        attach(64);

        EXPECT_FALSE(press(ftxui::Event::Character('i')));
        EXPECT_TRUE(shows("cursor 0x0"));
    }

    TEST_F(ViewerTest, InspectorAndFieldsCommands) {
        attach(64);
        viewer_.setLayout(layoutFrom(COUNTED));

        command("inspector");
        EXPECT_TRUE(shows("cursor 0x0"));

        command("fields");
        EXPECT_FALSE(shows("cursor 0x0"));
    }

    TEST_F(ViewerTest, FieldsCommandsNeedALayout) {
        attach(64);

        command("fields");

        EXPECT_TRUE(shows("unknown command /fields"));
    }

    TEST_F(ViewerTest, SelectedFieldBytesAreHighlighted) {
        attach(64);
        viewer_.setLayout(layoutFrom(WIDE));
        press(ftxui::Event::ArrowRight);
        press(ftxui::Event::ArrowRight);
        press(ftxui::Event::ArrowRight);

        EXPECT_NE(byteBackground(1), FIELD_BACKGROUND);
        for (std::size_t col = 2; col <= 5; ++col) {
            EXPECT_EQ(byteBackground(col), FIELD_BACKGROUND) << col;
        }
        EXPECT_NE(byteBackground(6), FIELD_BACKGROUND);
    }

    TEST_F(ViewerTest, GapsInsideTheFieldAreHighlighted) {
        attach(64);
        viewer_.setLayout(layoutFrom(WIDE));
        press(ftxui::Event::ArrowRight);
        press(ftxui::Event::ArrowRight);

        EXPECT_NE(gapBackground(1), FIELD_BACKGROUND);
        EXPECT_EQ(gapBackground(2), FIELD_BACKGROUND);
        EXPECT_EQ(gapBackground(4), FIELD_BACKGROUND);
        EXPECT_NE(gapBackground(5), FIELD_BACKGROUND);
    }

    TEST_F(ViewerTest, HighlightStopsAtTheMiddleAndEndOfARow) {
        attach(64);
        viewer_.setLayout(layoutFrom(WIDE));
        command("field c");

        EXPECT_EQ(byteBackground(7), FIELD_BACKGROUND);
        EXPECT_EQ(byteBackground(8), FIELD_BACKGROUND);
        EXPECT_NE(gapBackground(7), FIELD_BACKGROUND);
        EXPECT_EQ(byteBackground(15), FIELD_BACKGROUND);
        EXPECT_NE(gapBackground(15), FIELD_BACKGROUND);
        EXPECT_EQ(byteBackground(0, 1), FIELD_BACKGROUND);
        EXPECT_NE(byteBackground(1, 1), FIELD_BACKGROUND);
    }

    TEST_F(ViewerTest, TheCursorByteStaysInverted) {
        attach(64);
        viewer_.setLayout(layoutFrom(WIDE));
        press(ftxui::Event::ArrowRight);
        press(ftxui::Event::ArrowRight);

        EXPECT_TRUE(byteInverted(2));
        EXPECT_FALSE(byteInverted(3));
    }

    TEST_F(ViewerTest, NothingIsHighlightedWithoutALayout) {
        attach(64);

        for (std::size_t col = 0; col < 16; ++col) {
            EXPECT_NE(byteBackground(col), FIELD_BACKGROUND) << col;
        }
    }

    TEST_F(ViewerTest, NothingIsHighlightedOutsideEveryField) {
        attach(64);
        viewer_.setLayout(layoutFrom(WIDE));
        command("jump 30");

        for (std::size_t col = 0; col < 16; ++col) {
            EXPECT_NE(byteBackground(col), FIELD_BACKGROUND) << col;
        }
    }

    TEST_F(ViewerTest, InspectorKeepsTheHighlight) {
        attach(64);
        viewer_.setLayout(layoutFrom(WIDE));
        press(ftxui::Event::Character('i'));

        EXPECT_EQ(byteBackground(0), FIELD_BACKGROUND);
        EXPECT_EQ(byteBackground(1), FIELD_BACKGROUND);
    }

    TEST_F(ViewerTest, FieldCommandMovesTheCursor) {
        attach(64);
        viewer_.setLayout(layoutFrom(WIDE));

        command("field b");

        EXPECT_TRUE(byteInverted(2));
        EXPECT_TRUE(shows(" b  "));
    }

    TEST_F(ViewerTest, FieldCommandScrollsToFarFields) {
        attach(64 * 1024);
        viewer_.setLayout(layoutFrom(FAR));

        command("field far");

        EXPECT_TRUE(shows(rowLabel(0x9000)));
        EXPECT_TRUE(shows(" far  "));
        EXPECT_FALSE(shows("00000000  "));
    }

    TEST_F(ViewerTest, FieldCommandIgnoresTrailingSpaces) {
        attach(64);
        viewer_.setLayout(layoutFrom(WIDE));

        command("field b  ");

        EXPECT_TRUE(byteInverted(2));
    }

    TEST_F(ViewerTest, FieldCommandRejectsUnknownPaths) {
        attach(64);
        viewer_.setLayout(layoutFrom(WIDE));

        command("field nope");

        EXPECT_TRUE(shows("no field 'nope' is placed"));
        EXPECT_TRUE(byteInverted(0));
    }

    TEST_F(ViewerTest, FieldCommandNeedsALayout) {
        attach(64);

        command("field a");

        EXPECT_TRUE(shows("unknown command /field"));
    }

    class LayoutCommandTest : public ViewerTest {
    protected:
        void SetUp() override {
            dir_ = std::filesystem::temp_directory_path() /
                   std::format("shmscope-layouts-{}",
                               ::testing::UnitTest::GetInstance()
                                   ->current_test_info()
                                   ->name());
            std::filesystem::remove_all(dir_);
            std::filesystem::create_directories(dir_);
            loadWith_ = [](const std::filesystem::path& file)
                -> std::expected<Layout, std::string> {
                auto layout = shmscope::loadLayout(file);
                if (!layout) {
                    return std::unexpected(shmscope::describe(layout.error()));
                }
                return std::move(*layout);
            };
            attach(64);
        }

        void TearDown() override { std::filesystem::remove_all(dir_); }

        std::filesystem::path write(std::string_view name,
                                    std::string_view text) {
            const auto file = dir_ / name;
            std::ofstream(file) << text;
            return file;
        }

        void load(const std::filesystem::path& file) {
            command("layout " + file.string());
        }

        std::filesystem::path dir_{};
    };

    class ScopedEnv {
    public:
        ScopedEnv(const char* name, const std::string& value) : name_(name) {
            if (const char* old = std::getenv(name)) old_ = old;
            ::setenv(name, value.c_str(), 1);
        }

        ScopedEnv(const ScopedEnv&) = delete;
        ScopedEnv& operator=(const ScopedEnv&) = delete;
        ScopedEnv(ScopedEnv&&) = delete;
        ScopedEnv& operator=(ScopedEnv&&) = delete;

        ~ScopedEnv() {
            if (old_) {
                ::setenv(name_, old_->c_str(), 1);
            } else {
                ::unsetenv(name_);
            }
        }

    private:
        const char* name_;
        std::optional<std::string> old_{};
    };

    class ScopedDirectory {
    public:
        explicit ScopedDirectory(const std::filesystem::path& to)
            : old_(std::filesystem::current_path()) {
            std::filesystem::current_path(to);
        }

        ScopedDirectory(const ScopedDirectory&) = delete;
        ScopedDirectory& operator=(const ScopedDirectory&) = delete;
        ScopedDirectory(ScopedDirectory&&) = delete;
        ScopedDirectory& operator=(ScopedDirectory&&) = delete;

        ~ScopedDirectory() { std::filesystem::current_path(old_); }

    private:
        std::filesystem::path old_;
    };

    TEST_F(LayoutCommandTest, LoadsAFile) {
        load(write("counted.ksy", COUNTED));

        EXPECT_TRUE(shows(" counted · 2 fields "));
        EXPECT_FALSE(shows("cursor 0x0"));
    }

    TEST_F(LayoutCommandTest, SwapsOneLayoutForAnother) {
        load(write("counted.ksy", COUNTED));
        load(write("wide.ksy", WIDE));

        EXPECT_TRUE(shows(" wide · 4 fields "));
        EXPECT_FALSE(shows("counted"));
    }

    TEST_F(LayoutCommandTest, ABadFileKeepsTheCurrentLayout) {
        load(write("counted.ksy", COUNTED));
        write("bad.ksy",
              "meta: {id: bad, endian: le}\nseq: [{id: a, "
              "type: u3}]\n");
        {
            const ScopedDirectory inside(dir_);
            command("layout bad.ksy");
        }

        EXPECT_TRUE(shows("   bad.ksy:2:"));
        EXPECT_TRUE(shows(" counted · 2 fields "));
    }

    TEST_F(LayoutCommandTest, ErrorsNameTheFileAsTyped) {
        write("bad.ksy", "meta: [");
        {
            const ScopedDirectory inside(dir_);
            command("layout bad.ksy");
        }

        EXPECT_TRUE(shows("   bad.ksy:"));
        EXPECT_FALSE(shows(dir_.string()));
    }

    TEST_F(LayoutCommandTest, AMissingFileIsAnError) {
        {
            const ScopedDirectory inside(dir_);
            command("layout missing.ksy");
        }

        EXPECT_TRUE(shows("missing.ksy"));
        EXPECT_TRUE(shows("cursor 0x0"));
    }

    TEST_F(LayoutCommandTest, OffRemovesTheLayout) {
        load(write("counted.ksy", COUNTED));

        command("layout off");

        EXPECT_FALSE(shows("counted"));
        EXPECT_TRUE(shows("cursor 0x0"));
    }

    TEST_F(LayoutCommandTest, OffWithoutALayoutIsAnError) {
        command("layout off");

        EXPECT_TRUE(shows("no layout is loaded"));
    }

    TEST_F(LayoutCommandTest, ReloadReadsTheFileAgain) {
        const auto file = write("live.ksy", COUNTED);
        load(file);

        write("live.ksy", WIDE);
        command("layout reload");

        EXPECT_TRUE(shows(" wide · 4 fields "));
    }

    TEST_F(LayoutCommandTest, ABrokenReloadKeepsTheCurrentLayout) {
        const auto file = write("live.ksy", COUNTED);
        load(file);

        write("live.ksy", "meta: [");
        command("layout reload");

        EXPECT_TRUE(shows("   /"));
        EXPECT_TRUE(shows(" counted · 2 fields "));

        write("live.ksy", WIDE);
        press(ftxui::Event::Escape);
        command("layout reload");
        EXPECT_TRUE(shows(" wide · 4 fields "));
    }

    TEST_F(LayoutCommandTest, ReloadWithoutAFileIsAnError) {
        command("layout reload");

        EXPECT_TRUE(shows("no layout file to reload"));
    }

    TEST_F(LayoutCommandTest, OffForgetsTheFile) {
        load(write("counted.ksy", COUNTED));
        command("layout off");

        command("layout reload");

        EXPECT_TRUE(shows("no layout file to reload"));
    }

    TEST_F(LayoutCommandTest, RelativePathsStartFromTheWorkingDirectory) {
        write("counted.ksy", COUNTED);
        {
            const ScopedDirectory inside(dir_);
            command("layout counted.ksy");
        }

        EXPECT_TRUE(shows(" counted · 2 fields "));

        write("counted.ksy", WIDE);
        command("layout reload");
        EXPECT_TRUE(shows(" wide · 4 fields "));
    }

    TEST_F(LayoutCommandTest, TildeIsTheHomeDirectory) {
        write("counted.ksy", COUNTED);
        const ScopedEnv home("HOME", dir_.string());

        command("layout ~/counted.ksy");

        EXPECT_TRUE(shows(" counted · 2 fields "));
    }

    TEST_F(LayoutCommandTest, TabCompletesLayoutFiles) {
        write("counted.ksy", COUNTED);
        write("notes.txt", "not a layout");
        std::filesystem::create_directories(dir_ / "more");
        {
            const ScopedDirectory inside(dir_);
            press(ftxui::Event::Character('/'));
            type("layout c");
            press(ftxui::Event::Tab);
            EXPECT_TRUE(shows("› /layout counted.ksy"));
            press(ftxui::Event::Return);
        }

        EXPECT_TRUE(shows(" counted · 2 fields "));
    }

    TEST_F(LayoutCommandTest, TabListsDirectoriesAndLayoutsOnly) {
        write("counted.ksy", COUNTED);
        write("notes.txt", "not a layout");
        std::filesystem::create_directories(dir_ / "more");

        const ScopedDirectory inside(dir_);
        press(ftxui::Event::Character('/'));
        type("layout ");
        press(ftxui::Event::Tab);

        EXPECT_TRUE(shows("   counted.ksy"));
        EXPECT_TRUE(shows("   more/"));
        EXPECT_FALSE(shows("notes.txt"));
    }

    TEST_F(LayoutCommandTest, TabCompletesTheKeywords) {
        load(write("counted.ksy", COUNTED));

        press(ftxui::Event::Character('/'));
        type("layout rel");
        press(ftxui::Event::Tab);

        EXPECT_TRUE(shows("› /layout reload"));
    }

    TEST_F(LayoutCommandTest, TrailingSpacesAreIgnored) {
        load(write("counted.ksy", COUNTED));

        command("layout off   ");

        EXPECT_FALSE(shows("counted"));
    }

    TEST_F(LayoutCommandTest, LoadingStopsTrackingAndKeepsTheSource) {
        load(write("counted.ksy", COUNTED));
        command("field v");
        EXPECT_TRUE(shows("tracking"));

        load(write("wide.ksy", WIDE));

        EXPECT_FALSE(shows("tracking"));
        EXPECT_TRUE(shows("/fake"));
    }

    TEST_F(ViewerTest, LayoutNeedsALoader) {
        Viewer bare{HZ, [] {}};
        bare.component()->OnEvent(ftxui::Event::Character('/'));
        for (const char c : std::string_view("layout x.ksy")) {
            bare.component()->OnEvent(ftxui::Event::Character(c));
        }
        bare.component()->OnEvent(ftxui::Event::Return);

        auto screen = ftxui::Screen(SCREEN_WIDTH, height());
        ftxui::Render(screen, bare.component()->Render());

        EXPECT_NE(screen.ToString().find("loading layouts is not available"),
                  std::string::npos);
    }

    class PanelMouseTest : public ViewerTest {
    protected:
        static constexpr int PANEL_X = 85;
        static constexpr int FIRST_ROW_Y = 3;

        void SetUp() override {
            attach(256);
            viewer_.setLayout(layoutFrom(MANY));
            draw();
        }

        void wheel(ftxui::Mouse::Button button, int times, int x = PANEL_X) {
            for (int i = 0; i < times; ++i) {
                press(mouseAt(button, x, 10));
            }
        }
    };

    TEST_F(PanelMouseTest, TheListStartsAtTheTop) {
        EXPECT_TRUE(shows("   [0] "));
        EXPECT_TRUE(shows(" v[0] "));
    }

    TEST_F(PanelMouseTest, WheelOverThePanelScrollsOnlyThePanel) {
        wheel(ftxui::Mouse::WheelDown, 5);

        EXPECT_FALSE(shows("   [0] "));
        EXPECT_TRUE(shows("   [5] "));
        EXPECT_EQ(firstRow(), 0U);
        EXPECT_TRUE(byteInverted(0));
    }

    TEST_F(PanelMouseTest, WheelUpStopsAtTheTop) {
        wheel(ftxui::Mouse::WheelDown, 2);
        wheel(ftxui::Mouse::WheelUp, 10);

        EXPECT_TRUE(shows(" v      "));
        EXPECT_TRUE(shows("   [0] "));
    }

    TEST_F(PanelMouseTest, WheelDownStopsAtTheEnd) {
        wheel(ftxui::Mouse::WheelDown, 500);

        EXPECT_TRUE(shows("   [59] "));
        EXPECT_FALSE(shows("   [0] "));
    }

    TEST_F(PanelMouseTest, TheScrolledPanelSurvivesATick) {
        wheel(ftxui::Mouse::WheelDown, 5);
        viewer_.tick();

        EXPECT_FALSE(shows("   [0] "));
    }

    TEST_F(PanelMouseTest, WheelOverTheHexStillScrollsTheHex) {
        wheel(ftxui::Mouse::WheelDown, 2, 20);

        EXPECT_TRUE(byteInverted(0, 2));
        EXPECT_TRUE(shows(" v[32] "));
    }

    TEST_F(PanelMouseTest, KeysBringThePanelBackToTheCursor) {
        wheel(ftxui::Mouse::WheelDown, 20);
        EXPECT_FALSE(shows("   [0] "));

        press(ftxui::Event::ArrowRight);

        EXPECT_TRUE(shows("   [0] "));
        EXPECT_TRUE(shows(" v[1] "));
    }

    TEST_F(PanelMouseTest, ClickingARowSelectsThatField) {
        EXPECT_TRUE(
            press(mouseAt(ftxui::Mouse::Left, PANEL_X, FIRST_ROW_Y + 3)));

        EXPECT_TRUE(shows(" v[2] "));
        EXPECT_TRUE(byteInverted(2));
    }

    TEST_F(PanelMouseTest, ClickingAfterScrollingKeepsTheList) {
        wheel(ftxui::Mouse::WheelDown, 10);
        press(mouseAt(ftxui::Mouse::Left, PANEL_X, FIRST_ROW_Y));

        EXPECT_TRUE(shows(" v[9] "));
        EXPECT_TRUE(byteInverted(9));
        EXPECT_FALSE(shows("   [0] "));
    }

    TEST_F(PanelMouseTest, ClicksOnTheHeaderAreIgnored) {
        EXPECT_FALSE(press(mouseAt(ftxui::Mouse::Left, PANEL_X, 1)));
        EXPECT_FALSE(press(mouseAt(ftxui::Mouse::Left, PANEL_X, 2)));
    }

    TEST_F(PanelMouseTest, ClickingStopsTracking) {
        command("field v[40]");
        EXPECT_TRUE(shows("tracking"));

        press(mouseAt(ftxui::Mouse::Left, PANEL_X, FIRST_ROW_Y + 1));

        EXPECT_FALSE(shows("tracking"));
    }

    TEST_F(PanelMouseTest, TheInspectorTakesNoPanelScrolling) {
        press(ftxui::Event::Character('i'));

        wheel(ftxui::Mouse::WheelDown, 2);

        EXPECT_TRUE(shows("cursor 0x20 "));
    }

    class TrackingTest : public ViewerTest {
    protected:
        void SetUp() override {
            fake_ = &attach(64 * 1024);
            moveItemTo(2);
            viewer_.setLayout(layoutFrom(MOVING));
            draw();
        }

        void moveItemTo(std::uint16_t row) {
            fake_->bytes()[0] = static_cast<std::byte>(row & 0xff);
            fake_->bytes()[1] = static_cast<std::byte>(row >> 8);
            viewer_.tick();
        }

        FakeSource* fake_ = nullptr;
    };

    TEST_F(TrackingTest, FieldCommandStartsTracking) {
        command("field item");

        EXPECT_EQ(firstRow(), 0x20U);
        EXPECT_TRUE(byteInverted(0));
        EXPECT_TRUE(shows(" tracking item "));
    }

    TEST_F(TrackingTest, TheCursorFollowsTheFieldEachTick) {
        command("field item");

        moveItemTo(3);

        EXPECT_EQ(firstRow(), 0x20U);
        EXPECT_TRUE(byteInverted(0, 1));
        EXPECT_FALSE(byteInverted(0));
        EXPECT_TRUE(shows(" item  "));
    }

    TEST_F(TrackingTest, FarMovesScrollTheView) {
        command("field item");

        moveItemTo(0x900);

        EXPECT_TRUE(shows(rowLabel(0x9000)));
        EXPECT_FALSE(shows("00000000  "));
        EXPECT_TRUE(byteInverted(0));
    }

    TEST_F(TrackingTest, MovingTheCursorStopsTracking) {
        command("field item");
        press(ftxui::Event::ArrowRight);
        EXPECT_FALSE(shows("tracking"));

        moveItemTo(3);

        EXPECT_TRUE(byteInverted(1));
        EXPECT_FALSE(byteInverted(0, 1));
    }

    TEST_F(TrackingTest, OtherNavigationStopsTracking) {
        for (const auto& stop :
             {ftxui::Event::ArrowDown, ftxui::Event::PageUp, ftxui::Event::Home,
              ftxui::Event::End, ftxui::Event::Character('f')}) {
            command("field item");
            press(stop);
            EXPECT_FALSE(shows("tracking")) << stop.input();
            press(ftxui::Event::Character('f'));
            press(ftxui::Event::Character('f'));
        }
    }

    TEST_F(TrackingTest, CommandsStopTracking) {
        for (const auto* stop : {"jump 40", "follow", "live", "untrack"}) {
            command("field item");
            command(stop);
            EXPECT_FALSE(shows("tracking")) << stop;
            command("hex");
            command("unfollow");
        }
    }

    TEST_F(TrackingTest, UntrackIsOnlyOfferedWhileTracking) {
        command("untrack");

        EXPECT_TRUE(shows("unknown command /untrack"));
    }

    TEST_F(TrackingTest, AMissingFieldKeepsTheCursorAndTheTracking) {
        command("field item");

        moveItemTo(0xffff);

        EXPECT_TRUE(shows(" tracking item "));
        EXPECT_TRUE(byteInverted(0));
    }

    TEST_F(TrackingTest, FrozenViewsDoNotMove) {
        command("field item");
        press(ftxui::Event::Character(' '));

        moveItemTo(3);

        EXPECT_TRUE(byteInverted(0));
        EXPECT_FALSE(byteInverted(0, 1));
    }

    TEST_F(TrackingTest, ReplacingTheLayoutStopsTracking) {
        command("field item");
        viewer_.setLayout(layoutFrom(MOVING));

        EXPECT_FALSE(shows("tracking"));
    }

    TEST_F(TrackingTest, ReattachingStopsTracking) {
        command("field item");
        attach(64);

        EXPECT_FALSE(shows("tracking"));
    }

    TEST_F(ViewerTest, ALiveSegmentShowsNoWarning) {
        attach(64);

        EXPECT_FALSE(shows("removed"));
        EXPECT_FALSE(shows("recreated"));
    }

    TEST_F(ViewerTest, ARemovedSegmentSaysSo) {
        auto& fake = attach(64);
        fake.currentState = SourceState::REMOVED;
        viewer_.tick();

        EXPECT_TRUE(shows(" removed · showing the last data "));
        EXPECT_TRUE(shows("live"));
    }

    TEST_F(ViewerTest, ARecreatedSegmentOffersReopen) {
        auto& fake = attach(64);
        fake.currentState = SourceState::REPLACED;
        viewer_.tick();

        EXPECT_TRUE(shows(" recreated · /reopen "));
    }

    TEST_F(ViewerTest, ReopenAttachesWhatTheNameOpensNow) {
        attach(64, "/seg");
        std::string asked;
        openWith_ = [&](std::string_view name) -> shmscope::OpenResult {
            asked = std::string(name);
            return std::make_unique<FakeSource>("/seg", 128, nullptr);
        };

        command("reopen");

        EXPECT_EQ(asked, "/seg");
        EXPECT_TRUE(destroyed_);
        EXPECT_TRUE(shows("128 bytes"));
    }

    TEST_F(ViewerTest, ReopenKeepsTheCursor) {
        attach(4096);
        draw();
        command("jump 123");
        openWith_ = [](std::string_view name) -> shmscope::OpenResult {
            return std::make_unique<FakeSource>(std::string(name), 4096,
                                                nullptr);
        };

        command("reopen");

        EXPECT_TRUE(shows("cursor 0x123 "));
    }

    TEST_F(ViewerTest, ReopenClampsTheCursorToASmallerSegment) {
        attach(4096);
        draw();
        command("jump ff0");
        openWith_ = [](std::string_view name) -> shmscope::OpenResult {
            return std::make_unique<FakeSource>(std::string(name), 32, nullptr);
        };

        command("reopen");

        EXPECT_TRUE(shows("cursor 0x1f "));
    }

    TEST_F(ViewerTest, ReopenKeepsTheLayoutAndTracking) {
        attach(64 * 1024);
        viewer_.setLayout(layoutFrom(MOVING));
        command("field item");
        openWith_ = [](std::string_view name) -> shmscope::OpenResult {
            return std::make_unique<FakeSource>(std::string(name), 64 * 1024,
                                                nullptr);
        };

        command("reopen");

        EXPECT_TRUE(shows(" moving · "));
        EXPECT_TRUE(shows(" tracking item "));
    }

    TEST_F(ViewerTest, AFailedReopenKeepsTheOldSegment) {
        attach(64, "/kept");
        openWith_ = [](std::string_view) -> shmscope::OpenResult {
            return std::unexpected(std::string("/kept: No such file"));
        };

        command("reopen");

        EXPECT_TRUE(shows("/kept: No such file"));
        EXPECT_FALSE(destroyed_);
        EXPECT_TRUE(shows("64 bytes"));
    }

    TEST_F(ViewerTest, ReopenNeedsAnOpenSegment) {
        command("reopen");

        EXPECT_TRUE(shows("unknown command /reopen"));
    }

    TEST_F(ViewerTest, ReopenNeedsAnOpener) {
        Viewer bare{HZ, [] {}};
        bare.attach(std::make_unique<FakeSource>("/bare", 64, nullptr));
        bare.component()->OnEvent(ftxui::Event::Character('/'));
        for (const char c : std::string_view("reopen")) {
            bare.component()->OnEvent(ftxui::Event::Character(c));
        }
        bare.component()->OnEvent(ftxui::Event::Return);

        auto screen = ftxui::Screen(SCREEN_WIDTH, height());
        ftxui::Render(screen, bare.component()->Render());

        EXPECT_NE(screen.ToString().find("unknown command /reopen"),
                  std::string::npos);
    }

    TEST_F(ViewerTest, AShrinkingSourceClampsTheCursor) {
        auto& fake = attach(4096);
        draw();
        command("jump f00");

        fake.bytes().resize(256);
        fake.heat().resize(256);
        viewer_.tick();

        EXPECT_TRUE(shows("cursor 0xff "));
    }

    TEST_F(ViewerTest, ClearingTheLayoutBringsBackTheInspector) {
        attach(64);
        viewer_.setLayout(layoutFrom(COUNTED));
        viewer_.setLayout(std::nullopt);

        EXPECT_TRUE(shows("cursor 0x0"));
    }

}  // namespace
