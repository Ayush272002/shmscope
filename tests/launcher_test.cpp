#include "shmscope/launcher.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <gtest/gtest.h>

namespace {

    class LauncherTest : public ::testing::Test {
    protected:
        void type(const std::string& text) {
            for (const char c : text) {
                launcher_.component()->OnEvent(ftxui::Event::Character(c));
            }
        }

        bool press(const ftxui::Event& event) {
            return launcher_.component()->OnEvent(event);
        }

        std::string screen() {
            auto screen = ftxui::Screen(80, 12);
            ftxui::Render(screen, launcher_.component()->Render());
            return screen.ToString();
        }

        std::vector<std::string> submitted_;
        int quits_ = 0;
        shmscope::Launcher launcher_{
            [this](const std::string& name) { submitted_.push_back(name); },
            [this] { ++quits_; }};
    };

    TEST_F(LauncherTest, EnterSubmitsTypedName) {
        type("/fh.test");
        press(ftxui::Event::Return);

        ASSERT_EQ(submitted_.size(), 1U);
        EXPECT_EQ(submitted_.front(), "/fh.test");
    }

    TEST_F(LauncherTest, EnterWithEmptyInputSubmitsNothing) {
        press(ftxui::Event::Return);

        EXPECT_TRUE(submitted_.empty());
    }

    TEST_F(LauncherTest, BackspaceEditsTheName) {
        type("/abc");
        press(ftxui::Event::Backspace);
        press(ftxui::Event::Return);

        ASSERT_EQ(submitted_.size(), 1U);
        EXPECT_EQ(submitted_.front(), "/ab");
    }

    TEST_F(LauncherTest, EscapeQuits) {
        EXPECT_TRUE(press(ftxui::Event::Escape));
        EXPECT_EQ(quits_, 1);
    }

    TEST_F(LauncherTest, QIsTypedNotQuit) {
        type("q");
        press(ftxui::Event::Return);

        EXPECT_EQ(quits_, 0);
        ASSERT_EQ(submitted_.size(), 1U);
        EXPECT_EQ(submitted_.front(), "q");
    }

    TEST_F(LauncherTest, ErrorIsShown) {
        launcher_.setError("no such object");

        EXPECT_NE(screen().find("no such object"), std::string::npos);
    }

    TEST_F(LauncherTest, TypingClearsError) {
        launcher_.setError("no such object");
        type("x");

        EXPECT_EQ(screen().find("no such object"), std::string::npos);
    }

    TEST_F(LauncherTest, BackspaceClearsError) {
        type("/x");
        launcher_.setError("no such object");
        press(ftxui::Event::Backspace);

        EXPECT_EQ(screen().find("no such object"), std::string::npos);
    }

    TEST_F(LauncherTest, EnterAgainResubmitsTheSameName) {
        type("/fh.test");
        press(ftxui::Event::Return);
        press(ftxui::Event::Return);

        ASSERT_EQ(submitted_.size(), 2U);
        EXPECT_EQ(submitted_[1], "/fh.test");
    }

    TEST_F(LauncherTest, EscapeQuitsEvenWithTextTyped) {
        type("/fh.test");
        press(ftxui::Event::Escape);

        EXPECT_EQ(quits_, 1);
        EXPECT_TRUE(submitted_.empty());
    }

    TEST_F(LauncherTest, CursorKeysKeepTheError) {
        type("/x");
        launcher_.setError("no such object");
        press(ftxui::Event::ArrowLeft);
        press(ftxui::Event::ArrowRight);

        EXPECT_NE(screen().find("no such object"), std::string::npos);
    }

    TEST_F(LauncherTest, NewErrorReplacesTheOld) {
        launcher_.setError("first");
        launcher_.setError("second");

        EXPECT_EQ(screen().find("first"), std::string::npos);
        EXPECT_NE(screen().find("second"), std::string::npos);
    }

    TEST_F(LauncherTest, CursorEditsInTheMiddleOfTheName) {
        type("/fhtest");
        for (int i = 0; i < 4; ++i) {
            press(ftxui::Event::ArrowLeft);
        }
        type(".");
        press(ftxui::Event::Return);

        ASSERT_EQ(submitted_.size(), 1U);
        EXPECT_EQ(submitted_.front(), "/fh.test");
    }

    TEST_F(LauncherTest, TitleShowsVersion) {
        EXPECT_NE(screen().find("shmscope"), std::string::npos);
    }

    TEST_F(LauncherTest, ArrowsDoNothingWithoutARecentList) {
        type("/typed");
        press(ftxui::Event::ArrowDown);
        press(ftxui::Event::Return);

        ASSERT_EQ(submitted_.size(), 1U);
        EXPECT_EQ(submitted_.front(), "/typed");
    }

    class LauncherRecentTest : public ::testing::Test {
    protected:
        void SetUp() override {
            for (const auto* name : {"/e", "/d", "/c", "/b", "/a"}) {
                recent_.touch(name);
            }
        }

        void type(const std::string& text) {
            for (const char c : text) {
                launcher_.component()->OnEvent(ftxui::Event::Character(c));
            }
        }

        bool press(const ftxui::Event& event) {
            return launcher_.component()->OnEvent(event);
        }

        std::string submitAfter(int downs) {
            for (int i = 0; i < downs; ++i) {
                press(ftxui::Event::ArrowDown);
            }
            press(ftxui::Event::Return);
            return submitted_.empty() ? std::string() : submitted_.back();
        }

        std::string screen() {
            auto screen = ftxui::Screen(80, 40);
            ftxui::Render(screen, launcher_.component()->Render());
            return screen.ToString();
        }

        bool shows(std::string_view text) {
            return screen().find(text) != std::string::npos;
        }

        shmscope::RecentList recent_{{}};
        std::vector<std::string> submitted_;
        shmscope::Launcher launcher_{
            [this](const std::string& name) { submitted_.push_back(name); },
            [] {}, 15, &recent_};
    };

    TEST_F(LauncherRecentTest, ListsTheRecentNames) {
        EXPECT_TRUE(shows("Recent"));
        for (const auto* name : {"/a", "/b", "/c", "/d", "/e"}) {
            EXPECT_TRUE(shows(name)) << name;
        }
        EXPECT_TRUE(shows("↑↓ recent"));
    }

    TEST_F(LauncherRecentTest, ArrowDownPicksTheNewest) {
        EXPECT_EQ(submitAfter(1), "/a");
    }

    TEST_F(LauncherRecentTest, ArrowDownWalksTheList) {
        EXPECT_EQ(submitAfter(3), "/c");
    }

    TEST_F(LauncherRecentTest, ArrowDownStopsAtTheLastShown) {
        EXPECT_EQ(submitAfter(20), "/e");
    }

    TEST_F(LauncherRecentTest, ArrowUpFromTheFirstEmptiesTheBox) {
        press(ftxui::Event::ArrowDown);
        press(ftxui::Event::ArrowUp);
        press(ftxui::Event::Return);

        EXPECT_TRUE(submitted_.empty());
    }

    TEST_F(LauncherRecentTest, ArrowUpWalksBack) {
        press(ftxui::Event::ArrowDown);
        press(ftxui::Event::ArrowDown);
        press(ftxui::Event::ArrowDown);
        press(ftxui::Event::ArrowUp);

        EXPECT_EQ(submitAfter(0), "/b");
    }

    TEST_F(LauncherRecentTest, APickedNameCanBeEditedBeforeOpening) {
        press(ftxui::Event::ArrowDown);
        type("x");
        press(ftxui::Event::Return);

        ASSERT_EQ(submitted_.size(), 1U);
        EXPECT_EQ(submitted_.front(), "/ax");
    }

    TEST_F(LauncherRecentTest, TypingRestartsTheWalkFromTheTop) {
        press(ftxui::Event::ArrowDown);
        press(ftxui::Event::ArrowDown);
        type("x");

        EXPECT_EQ(submitAfter(1), "/a");
    }

    TEST_F(LauncherRecentTest, PickingClearsTheError) {
        launcher_.setError("/gone: No such file or directory");
        press(ftxui::Event::ArrowDown);

        EXPECT_FALSE(shows("No such file"));
    }

    TEST_F(LauncherRecentTest, OnlyTheFiveNewestAreShown) {
        recent_.touch("/f");

        EXPECT_TRUE(shows("/f"));
        EXPECT_FALSE(shows("/e"));
        EXPECT_EQ(submitAfter(20), "/d");
    }

    TEST_F(LauncherRecentTest, ANameOpenedLaterShowsFirst) {
        recent_.touch("/c");

        EXPECT_EQ(submitAfter(1), "/c");
    }

    TEST_F(LauncherTest, NoRecentSectionWithoutARecentList) {
        auto screen = ftxui::Screen(80, 40);
        ftxui::Render(screen, launcher_.component()->Render());

        EXPECT_EQ(screen.ToString().find("Recent"), std::string::npos);
        EXPECT_EQ(screen.ToString().find("↑↓ recent"), std::string::npos);
    }

}  // namespace
