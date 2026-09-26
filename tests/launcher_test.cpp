#include "shmscope/launcher.hpp"

#include <string>
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

}  // namespace
