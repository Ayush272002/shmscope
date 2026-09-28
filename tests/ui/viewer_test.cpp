#include "shmscope/ui/viewer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
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

namespace {

    using shmscope::Frame;
    using shmscope::HEAT_MAX;
    using shmscope::Source;
    using shmscope::Viewer;

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

        int polls = 0;
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

        bool byteInverted(std::size_t col, int row = 0) {
            const int gap = col >= 8 ? 1 : 0;
            const int x = 13 + static_cast<int>(col * 3) + gap;
            return draw().PixelAt(x, 2 + row).inverted;
        }

        int closes_ = 0;
        bool destroyed_ = false;
        Viewer viewer_{HZ, [this] { ++closes_; }};
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

        EXPECT_FALSE(shows(rowLabel((visibleRows() - 7) * 16)));
        EXPECT_TRUE(shows(rowLabel((visibleRows() - 8) * 16)));
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

    TEST_F(ViewerTest, MouseEventsAreLeftToTheTerminal) {
        attach(4096);
        draw();
        for (const auto button : {ftxui::Mouse::Left, ftxui::Mouse::WheelDown,
                                  ftxui::Mouse::WheelUp, ftxui::Mouse::Right}) {
            EXPECT_FALSE(press(mouse(button)));
        }
        EXPECT_EQ(firstRow(), 0U);
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

        for (const auto* name :
             {"u8 ", "u16 ", "u32 ", "u64 ", "i8 ", "i16 ", "i32 ", "i64 ",
              "f32 ", "f64 ", "fixed8 ", "ascii "}) {
            EXPECT_TRUE(shows(name)) << name;
        }
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

}  // namespace
