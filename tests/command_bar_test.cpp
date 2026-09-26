#include "shmscope/command_bar.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/screen.hpp>
#include <gtest/gtest.h>

namespace {

    using shmscope::CommandBar;

    constexpr int BORDER = 2;

    struct Call {
        std::string name;
        std::string args;
    };

    class CommandBarTest : public ::testing::Test {
    protected:
        void SetUp() override {
            add("jump", "<offset>", "scroll to a hex offset");
            add("jumpy", "", "a longer name sharing the prefix");
            add("freeze", "", "pause updates");
            add("fail", "", "always returns an error", "it broke");
        }

        void add(const std::string& name, const std::string& args,
                 const std::string& help,
                 const std::optional<std::string>& error = std::nullopt) {
            bar_.add({.name = name,
                      .args = args,
                      .help = help,
                      .run = [this, name, error](std::string_view arguments) {
                          calls_.push_back({name, std::string(arguments)});
                          return error;
                      }});
        }

        bool press(const ftxui::Event& event) { return bar_.onEvent(event); }

        void type(std::string_view text) {
            for (const char c : text) {
                bar_.onEvent(ftxui::Event::Character(c));
            }
        }

        void run(std::string_view text) {
            press(ftxui::Event::Character('/'));
            type(text);
            press(ftxui::Event::Return);
        }

        [[nodiscard]] std::string screen() const {
            auto screen = ftxui::Screen(80, 10);
            ftxui::Render(screen, bar_.render());
            return plain(screen.ToString());
        }

        static std::string plain(const std::string& styled) {
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

        [[nodiscard]] bool shows(std::string_view text) const {
            return screen().find(text) != std::string::npos;
        }

        static ftxui::Event wheel() {
            ftxui::Mouse mouse;
            mouse.button = ftxui::Mouse::WheelDown;
            return ftxui::Event::Mouse("", mouse);
        }

        CommandBar bar_;
        std::vector<Call> calls_;
    };

    TEST_F(CommandBarTest, StartsClosed) {
        EXPECT_FALSE(bar_.isOpen());
        EXPECT_EQ(bar_.height(), BORDER + 1);
        EXPECT_TRUE(shows("/ for commands"));
    }

    TEST_F(CommandBarTest, ClosedIgnoresEverythingButSlash) {
        EXPECT_FALSE(press(ftxui::Event::Character('q')));
        EXPECT_FALSE(press(ftxui::Event::Return));
        EXPECT_FALSE(press(ftxui::Event::Escape));
        EXPECT_FALSE(press(ftxui::Event::ArrowDown));
        EXPECT_FALSE(press(wheel()));
        EXPECT_FALSE(bar_.isOpen());
    }

    TEST_F(CommandBarTest, SlashOpens) {
        EXPECT_TRUE(press(ftxui::Event::Character('/')));
        EXPECT_TRUE(bar_.isOpen());
    }

    TEST_F(CommandBarTest, OpenListsEveryCommand) {
        press(ftxui::Event::Character('/'));

        EXPECT_EQ(bar_.height(), BORDER + 5);
        EXPECT_TRUE(shows("/jump <offset>"));
        EXPECT_TRUE(shows("scroll to a hex offset"));
        EXPECT_TRUE(shows("/jumpy"));
        EXPECT_TRUE(shows("/freeze"));
        EXPECT_TRUE(shows("/fail"));
        EXPECT_TRUE(shows("› /"));
    }

    TEST_F(CommandBarTest, TypingFiltersByPrefix) {
        press(ftxui::Event::Character('/'));
        type("fr");

        EXPECT_EQ(bar_.height(), BORDER + 2);
        EXPECT_TRUE(shows("/freeze"));
        EXPECT_FALSE(shows("/jump"));
        EXPECT_TRUE(shows("› /fr"));
    }

    TEST_F(CommandBarTest, NoMatchesLeavesOnlyThePrompt) {
        press(ftxui::Event::Character('/'));
        type("zzz");

        EXPECT_EQ(bar_.height(), BORDER + 1);
        EXPECT_FALSE(shows("/jump"));
    }

    TEST_F(CommandBarTest, AfterASpaceOnlyTheExactCommandIsListed) {
        press(ftxui::Event::Character('/'));
        type("jump ");

        EXPECT_EQ(bar_.height(), BORDER + 2);
        EXPECT_TRUE(shows("/jump <offset>"));
        EXPECT_FALSE(shows("/jumpy"));
    }

    TEST_F(CommandBarTest, CommandWithoutArgsHintRendersNameOnly) {
        press(ftxui::Event::Character('/'));
        type("freeze");

        EXPECT_TRUE(shows("/freeze"));
        EXPECT_FALSE(shows("/freeze <"));
    }

    TEST_F(CommandBarTest, EnterRunsExactMatchWithArgs) {
        run("jump 1f00");

        ASSERT_EQ(calls_.size(), 1U);
        EXPECT_EQ(calls_[0].name, "jump");
        EXPECT_EQ(calls_[0].args, "1f00");
        EXPECT_FALSE(bar_.isOpen());
    }

    TEST_F(CommandBarTest, ExactNameWinsOverLongerPrefixMatch) {
        run("jump");

        EXPECT_TRUE(calls_.empty());
        EXPECT_TRUE(shows("› /jump "));
        EXPECT_FALSE(shows("› /jumpy"));
    }

    TEST_F(CommandBarTest, EnterOnACommandNeedingArgsCompletesIt) {
        press(ftxui::Event::Character('/'));
        type("ju");
        press(ftxui::Event::Return);

        EXPECT_TRUE(calls_.empty());
        EXPECT_TRUE(bar_.isOpen());
        EXPECT_TRUE(shows("› /jump "));

        type("40");
        press(ftxui::Event::Return);

        ASSERT_EQ(calls_.size(), 1U);
        EXPECT_EQ(calls_[0].name, "jump");
        EXPECT_EQ(calls_[0].args, "40");
    }

    TEST_F(CommandBarTest, UnavailableCommandsAreHiddenAndUnknown) {
        bool enabled = false;
        bar_.add({.name = "unfreeze",
                  .help = "resume",
                  .run =
                      [this](std::string_view) {
                          calls_.push_back({"unfreeze", ""});
                          return std::optional<std::string>{};
                      },
                  .available = [&enabled] { return enabled; }});

        press(ftxui::Event::Character('/'));
        EXPECT_FALSE(shows("/unfreeze"));
        type("unfreeze");
        press(ftxui::Event::Return);
        EXPECT_TRUE(calls_.empty());
        EXPECT_TRUE(shows("unknown command /unfreeze"));

        enabled = true;
        press(ftxui::Event::Escape);
        run("unfreeze");
        ASSERT_EQ(calls_.size(), 1U);
        EXPECT_EQ(calls_[0].name, "unfreeze");
    }

    TEST_F(CommandBarTest, PrefixRunsTheHighlightedSuggestion) {
        run("fr");

        ASSERT_EQ(calls_.size(), 1U);
        EXPECT_EQ(calls_[0].name, "freeze");
        EXPECT_EQ(calls_[0].args, "");
    }

    TEST_F(CommandBarTest, ArrowDownChoosesWhichPrefixMatchRuns) {
        press(ftxui::Event::Character('/'));
        type("ju");
        press(ftxui::Event::ArrowDown);
        press(ftxui::Event::Return);

        ASSERT_EQ(calls_.size(), 1U);
        EXPECT_EQ(calls_[0].name, "jumpy");
    }

    TEST_F(CommandBarTest, ExtraSpacesBeforeArgsAreSkipped) {
        run("jump    0x10");

        ASSERT_EQ(calls_.size(), 1U);
        EXPECT_EQ(calls_[0].args, "0x10");
    }

    TEST_F(CommandBarTest, TrailingSpacesOnlyGiveEmptyArgs) {
        run("freeze   ");

        ASSERT_EQ(calls_.size(), 1U);
        EXPECT_EQ(calls_[0].args, "");
    }

    TEST_F(CommandBarTest, TrailingSpacesAfterACommandNeedingArgsDoNotRun) {
        run("jump   ");

        EXPECT_TRUE(calls_.empty());
        EXPECT_TRUE(shows("› /jump "));
    }

    TEST_F(CommandBarTest, EmptyInputPicksTheFirstCommand) {
        run("");

        EXPECT_TRUE(calls_.empty());
        EXPECT_TRUE(shows("› /jump "));
    }

    TEST_F(CommandBarTest, UnknownCommandShowsErrorAndStaysOpen) {
        run("nope");

        EXPECT_TRUE(calls_.empty());
        EXPECT_TRUE(bar_.isOpen());
        EXPECT_TRUE(shows("unknown command /nope"));
        EXPECT_EQ(bar_.height(), BORDER + 2);
    }

    TEST_F(CommandBarTest, UnknownCommandWithArgsNamesOnlyTheWord) {
        run("nope 12");

        EXPECT_TRUE(shows("unknown command /nope"));
        EXPECT_FALSE(shows("unknown command /nope 12"));
        EXPECT_TRUE(shows("› /nope 12"));
    }

    TEST_F(CommandBarTest, FailingCommandShowsItsErrorAndCloses) {
        run("fail");

        ASSERT_EQ(calls_.size(), 1U);
        EXPECT_FALSE(bar_.isOpen());
        EXPECT_TRUE(shows("it broke"));
        EXPECT_TRUE(shows("/ for commands"));
        EXPECT_EQ(bar_.height(), BORDER + 2);
    }

    TEST_F(CommandBarTest, ReopeningClearsTheError) {
        run("fail");
        press(ftxui::Event::Character('/'));

        EXPECT_FALSE(shows("it broke"));
    }

    TEST_F(CommandBarTest, SuccessfulRunLeavesNoError) {
        run("freeze");

        EXPECT_EQ(bar_.height(), BORDER + 1);
    }

    TEST_F(CommandBarTest, InputIsClearedAfterRunning) {
        run("freeze");
        press(ftxui::Event::Character('/'));

        EXPECT_EQ(bar_.height(), BORDER + 5);
        EXPECT_TRUE(shows("› / "));
    }

    TEST_F(CommandBarTest, TabCompletesTheHighlightedCommand) {
        press(ftxui::Event::Character('/'));
        type("fr");
        press(ftxui::Event::Tab);
        type("now");
        press(ftxui::Event::Return);

        ASSERT_EQ(calls_.size(), 1U);
        EXPECT_EQ(calls_[0].name, "freeze");
        EXPECT_EQ(calls_[0].args, "now");
    }

    TEST_F(CommandBarTest, TabKeepsArgumentsAlreadyTyped) {
        press(ftxui::Event::Character('/'));
        type("jump 12");
        press(ftxui::Event::Tab);

        EXPECT_TRUE(shows("› /jump 12"));
        press(ftxui::Event::Return);
        ASSERT_EQ(calls_.size(), 1U);
        EXPECT_EQ(calls_[0].args, "12");
    }

    TEST_F(CommandBarTest, TabAfterTheCommandSpaceChangesNothing) {
        press(ftxui::Event::Character('/'));
        type("jump ");
        press(ftxui::Event::Tab);

        EXPECT_TRUE(shows("› /jump "));
        EXPECT_FALSE(shows("› /jump jump"));
    }

    TEST_F(CommandBarTest, TabCompletesTheSelectedPrefixMatch) {
        press(ftxui::Event::Character('/'));
        type("ju");
        press(ftxui::Event::ArrowDown);
        press(ftxui::Event::Tab);

        EXPECT_TRUE(shows("› /jumpy "));
    }

    TEST_F(CommandBarTest, TabWithNoMatchesChangesNothing) {
        press(ftxui::Event::Character('/'));
        type("zzz");
        press(ftxui::Event::Tab);

        EXPECT_TRUE(shows("› /zzz"));
    }

    TEST_F(CommandBarTest, ArrowUpStopsAtTheFirstSuggestion) {
        press(ftxui::Event::Character('/'));
        press(ftxui::Event::ArrowUp);
        press(ftxui::Event::ArrowUp);
        press(ftxui::Event::Return);

        EXPECT_TRUE(calls_.empty());
        EXPECT_TRUE(shows("› /jump "));
    }

    TEST_F(CommandBarTest, ArrowDownStopsAtTheLastSuggestion) {
        press(ftxui::Event::Character('/'));
        for (int i = 0; i < 10; ++i) {
            press(ftxui::Event::ArrowDown);
        }
        press(ftxui::Event::Return);

        ASSERT_EQ(calls_.size(), 1U);
        EXPECT_EQ(calls_[0].name, "fail");
    }

    TEST_F(CommandBarTest, ArrowDownWithNoMatchesIsHarmless) {
        press(ftxui::Event::Character('/'));
        type("zzz");
        EXPECT_TRUE(press(ftxui::Event::ArrowDown));
        EXPECT_TRUE(bar_.isOpen());
    }

    TEST_F(CommandBarTest, TypingResetsTheSelection) {
        press(ftxui::Event::Character('/'));
        press(ftxui::Event::ArrowDown);
        press(ftxui::Event::ArrowDown);
        type("j");
        press(ftxui::Event::Return);

        EXPECT_TRUE(calls_.empty());
        EXPECT_TRUE(shows("› /jump "));
        EXPECT_FALSE(shows("› /jumpy"));
    }

    TEST_F(CommandBarTest, BackspaceDeletesOneCharacter) {
        press(ftxui::Event::Character('/'));
        type("frx");
        press(ftxui::Event::Backspace);

        EXPECT_TRUE(shows("› /fr"));
        EXPECT_FALSE(shows("› /frx"));
        EXPECT_TRUE(bar_.isOpen());
    }

    TEST_F(CommandBarTest, BackspaceOnEmptyInputCloses) {
        press(ftxui::Event::Character('/'));
        EXPECT_TRUE(press(ftxui::Event::Backspace));

        EXPECT_FALSE(bar_.isOpen());
    }

    TEST_F(CommandBarTest, EscapeClosesWithoutRunning) {
        press(ftxui::Event::Character('/'));
        type("freeze");
        EXPECT_TRUE(press(ftxui::Event::Escape));

        EXPECT_FALSE(bar_.isOpen());
        EXPECT_TRUE(calls_.empty());
    }

    TEST_F(CommandBarTest, EscapeDiscardsTheTypedInput) {
        press(ftxui::Event::Character('/'));
        type("freeze");
        press(ftxui::Event::Escape);
        press(ftxui::Event::Character('/'));

        EXPECT_EQ(bar_.height(), BORDER + 5);
    }

    TEST_F(CommandBarTest, OpenBarSwallowsKeysSoTheyDoNotLeak) {
        press(ftxui::Event::Character('/'));

        EXPECT_TRUE(press(ftxui::Event::Character('q')));
        EXPECT_TRUE(press(ftxui::Event::Character(' ')));
        EXPECT_TRUE(press(ftxui::Event::PageDown));
        EXPECT_TRUE(press(ftxui::Event::F1));
        EXPECT_TRUE(bar_.isOpen());
    }

    TEST_F(CommandBarTest, SecondSlashIsTypedNotReopened) {
        press(ftxui::Event::Character('/'));
        type("/");

        EXPECT_TRUE(shows("› //"));
    }

    TEST_F(CommandBarTest, OpenBarLetsMouseEventsThrough) {
        press(ftxui::Event::Character('/'));

        EXPECT_FALSE(press(wheel()));
        EXPECT_TRUE(bar_.isOpen());
    }

    TEST_F(CommandBarTest, DuplicateNamesRunTheFirstRegistered) {
        add("freeze", "", "second freeze", "from the duplicate");
        run("freeze");

        ASSERT_EQ(calls_.size(), 1U);
        EXPECT_FALSE(shows("from the duplicate"));
    }

    TEST_F(CommandBarTest, SelectionIsClampedAfterFilteringShrinksTheList) {
        press(ftxui::Event::Character('/'));
        press(ftxui::Event::ArrowDown);
        press(ftxui::Event::ArrowDown);
        press(ftxui::Event::ArrowDown);
        type("fr");
        press(ftxui::Event::Tab);

        EXPECT_TRUE(shows("› /freeze "));
    }

    TEST_F(CommandBarTest, CommandNamesAreCaseSensitive) {
        run("JUMP");

        EXPECT_TRUE(calls_.empty());
        EXPECT_TRUE(shows("unknown command /JUMP"));
    }

    TEST_F(CommandBarTest, ArgsKeepInnerSpaces) {
        run("jump a b  c");

        ASSERT_EQ(calls_.size(), 1U);
        EXPECT_EQ(calls_[0].args, "a b  c");
    }

    TEST_F(CommandBarTest, LeadingSpaceChoosesNoCommand) {
        run(" jump");

        EXPECT_TRUE(calls_.empty());
        EXPECT_TRUE(shows("unknown command /"));
    }

    TEST_F(CommandBarTest, CommandCanRunAgainAfterAnError) {
        run("fail");
        run("fail");

        EXPECT_EQ(calls_.size(), 2U);
    }

    TEST_F(CommandBarTest, BackspaceRemovesAWholeMultibyteCharacter) {
        press(ftxui::Event::Character('/'));
        press(ftxui::Event::Character("é"));
        press(ftxui::Event::Backspace);

        EXPECT_TRUE(shows("› / "));
        EXPECT_EQ(bar_.height(), BORDER + 5);
    }

    TEST_F(CommandBarTest, BackspaceRemovesAThreeByteCharacter) {
        press(ftxui::Event::Character('/'));
        type("fr");
        press(ftxui::Event::Character("€"));
        press(ftxui::Event::Backspace);

        EXPECT_TRUE(shows("› /fr "));
        EXPECT_EQ(bar_.height(), BORDER + 2);
    }

    TEST_F(CommandBarTest, BackspaceAfterMultibyteLeavesEarlierAscii) {
        press(ftxui::Event::Character('/'));
        press(ftxui::Event::Character("é"));
        type("x");
        press(ftxui::Event::Backspace);
        press(ftxui::Event::Backspace);
        press(ftxui::Event::Backspace);

        EXPECT_FALSE(bar_.isOpen());
    }

    TEST_F(CommandBarTest, BarWithNoCommandsStillOpensAndReportsUnknown) {
        CommandBar empty;
        empty.onEvent(ftxui::Event::Character('/'));
        empty.onEvent(ftxui::Event::Character('x'));
        empty.onEvent(ftxui::Event::Return);

        EXPECT_TRUE(empty.isOpen());
        EXPECT_EQ(empty.height(), BORDER + 2);
    }

    TEST_F(CommandBarTest, HiddenCommandsAreSkippedByArrowKeys) {
        CommandBar bar;
        std::vector<std::string> ran;
        const auto runner = [&ran](const std::string& name) {
            return [&ran, name](std::string_view) {
                ran.push_back(name);
                return std::optional<std::string>{};
            };
        };
        bar.add({.name = "alpha", .run = runner("alpha")});
        bar.add({.name = "beta", .run = runner("beta"), .available = [] {
                     return false;
                 }});
        bar.add({.name = "gamma", .run = runner("gamma")});

        bar.onEvent(ftxui::Event::Character('/'));
        bar.onEvent(ftxui::Event::ArrowDown);
        bar.onEvent(ftxui::Event::Return);

        ASSERT_EQ(ran.size(), 1U);
        EXPECT_EQ(ran[0], "gamma");
    }

    TEST_F(CommandBarTest, HiddenCommandsDoNotCountTowardsHeight) {
        bool shown = false;
        bar_.add(
            {.name = "extra",
             .run =
                 [](std::string_view) { return std::optional<std::string>{}; },
             .available = [&shown] { return shown; }});

        press(ftxui::Event::Character('/'));
        EXPECT_EQ(bar_.height(), BORDER + 5);

        shown = true;
        EXPECT_EQ(bar_.height(), BORDER + 6);
    }

    TEST_F(CommandBarTest, HeightCountsTheErrorAndTheSuggestions) {
        press(ftxui::Event::Character('/'));
        type("nope");
        press(ftxui::Event::Return);
        EXPECT_EQ(bar_.height(), BORDER + 2);

        press(ftxui::Event::Backspace);
        press(ftxui::Event::Backspace);
        press(ftxui::Event::Backspace);
        press(ftxui::Event::Backspace);
        EXPECT_EQ(bar_.height(), BORDER + 6);
    }

    TEST_F(CommandBarTest, ArrowUpAfterDownReturnsToTheFirst) {
        press(ftxui::Event::Character('/'));
        type("ju");
        press(ftxui::Event::ArrowDown);
        press(ftxui::Event::ArrowUp);
        press(ftxui::Event::Tab);

        EXPECT_TRUE(shows("› /jump "));
        EXPECT_FALSE(shows("› /jumpy"));
    }

    TEST_F(CommandBarTest, SelectedSuggestionIsDrawnInTheAccentColour) {
        press(ftxui::Event::Character('/'));
        press(ftxui::Event::ArrowDown);

        auto screen = ftxui::Screen(80, 10);
        ftxui::Render(screen, bar_.render());
        const auto accent = ftxui::Color::RGB(122, 162, 247);
        EXPECT_EQ(screen.PixelAt(4, 4).foreground_color, accent);
        EXPECT_NE(screen.PixelAt(4, 3).foreground_color, accent);
    }

}  // namespace
