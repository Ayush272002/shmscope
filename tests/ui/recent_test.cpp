#include "shmscope/ui/recent.hpp"

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

    using shmscope::RecentList;

    class RecentListTest : public ::testing::Test {
    protected:
        void SetUp() override {
            const auto* info =
                ::testing::UnitTest::GetInstance()->current_test_info();
            dir_ =
                std::filesystem::temp_directory_path() /
                std::format("shmscope-recent-{}-{}", info->name(), ::getpid());
            std::filesystem::remove_all(dir_);
            std::filesystem::create_directories(dir_);
        }

        void TearDown() override { std::filesystem::remove_all(dir_); }

        [[nodiscard]] std::filesystem::path file() const {
            return dir_ / "recent";
        }

        void write(const std::vector<std::string>& lines) const {
            std::ofstream out(file(), std::ios::trunc);
            for (const auto& line : lines) {
                out << line << '\n';
            }
        }

        [[nodiscard]] std::vector<std::string> lines() const {
            std::vector<std::string> result;
            std::ifstream in(file());
            for (std::string line; std::getline(in, line);) {
                result.push_back(line);
            }
            return result;
        }

        static std::vector<std::string> entries(const RecentList& recent) {
            return {recent.entries().begin(), recent.entries().end()};
        }

        static std::vector<std::string> names(int from, int to) {
            std::vector<std::string> result;
            for (int i = from; i < to; ++i) {
                result.push_back(std::format("/n{}", i));
            }
            return result;
        }

        std::filesystem::path dir_;
    };

    class ScopedEnv {
    public:
        explicit ScopedEnv(const char* name) : name_(name) {
            if (const char* value = std::getenv(name)) {
                saved_ = value;
            }
        }

        ScopedEnv(const ScopedEnv&) = delete;
        ScopedEnv& operator=(const ScopedEnv&) = delete;
        ScopedEnv(ScopedEnv&&) = delete;
        ScopedEnv& operator=(ScopedEnv&&) = delete;

        ~ScopedEnv() {
            if (saved_) {
                ::setenv(name_, saved_->c_str(), 1);
            } else {
                ::unsetenv(name_);
            }
        }

        void set(const char* value) const { ::setenv(name_, value, 1); }
        void unset() const { ::unsetenv(name_); }

    private:
        const char* name_;
        std::optional<std::string> saved_;
    };

    TEST_F(RecentListTest, MissingFileStartsEmpty) {
        const RecentList recent(file());
        EXPECT_TRUE(recent.entries().empty());
    }

    TEST_F(RecentListTest, TouchPutsTheNewestFirst) {
        RecentList recent(file());
        recent.touch("/a");
        recent.touch("/b");

        EXPECT_EQ(entries(recent), (std::vector<std::string>{"/b", "/a"}));
    }

    TEST_F(RecentListTest, TouchingAKnownNameMovesItToTheFront) {
        RecentList recent(file());
        recent.touch("/a");
        recent.touch("/b");
        recent.touch("/c");
        recent.touch("/a");

        EXPECT_EQ(entries(recent),
                  (std::vector<std::string>{"/a", "/c", "/b"}));
    }

    TEST_F(RecentListTest, TouchKeepsOnlyTheTenNewest) {
        RecentList recent(file());
        for (const auto& name : names(0, 12)) {
            recent.touch(name);
        }

        ASSERT_EQ(recent.entries().size(), 10U);
        EXPECT_EQ(recent.entries().front(), "/n11");
        EXPECT_EQ(recent.entries().back(), "/n2");
        EXPECT_EQ(lines().size(), 10U);
    }

    TEST_F(RecentListTest, FileListsTheNewestOnTheFirstLine) {
        RecentList recent(file());
        recent.touch("/old");
        recent.touch("/new");

        EXPECT_EQ(lines(), (std::vector<std::string>{"/new", "/old"}));
    }

    TEST_F(RecentListTest, ReloadingGivesTheSameOrder) {
        {
            RecentList recent(file());
            recent.touch("/a");
            recent.touch("/b");
            recent.touch("/c");
        }

        const RecentList reloaded(file());
        EXPECT_EQ(entries(reloaded),
                  (std::vector<std::string>{"/c", "/b", "/a"}));
    }

    TEST_F(RecentListTest, LongerFileLoadsTheFirstTenLines) {
        write(names(0, 20));

        const RecentList recent(file());
        EXPECT_EQ(entries(recent), names(0, 10));
    }

    TEST_F(RecentListTest, NextTouchTrimsALongerFileToTen) {
        write(names(0, 20));

        RecentList recent(file());
        recent.touch("/x");

        auto expected = names(0, 9);
        expected.insert(expected.begin(), "/x");
        EXPECT_EQ(lines(), expected);
    }

    TEST_F(RecentListTest, LoadingAFileLeavesItUntouched) {
        write(names(0, 20));

        const RecentList recent(file());
        EXPECT_EQ(lines().size(), 20U);
    }

    TEST_F(RecentListTest, DuplicateLinesKeepTheFirstCopy) {
        write({"/a", "/b", "/a", "/c", "/b"});

        const RecentList recent(file());
        EXPECT_EQ(entries(recent),
                  (std::vector<std::string>{"/a", "/b", "/c"}));
    }

    TEST_F(RecentListTest, DuplicatesDoNotCountTowardsTheLimit) {
        auto content = names(0, 10);
        content.insert(content.begin() + 3, "/n0");
        content.insert(content.begin() + 5, "/n1");
        write(content);

        const RecentList recent(file());
        EXPECT_EQ(entries(recent), names(0, 10));
    }

    TEST_F(RecentListTest, BlankLinesAreSkipped) {
        write({"", "/a", "", "", "/b", ""});

        const RecentList recent(file());
        EXPECT_EQ(entries(recent), (std::vector<std::string>{"/a", "/b"}));
    }

    TEST_F(RecentListTest, MissingDirectoriesAreCreated) {
        const auto nested = dir_ / "a" / "b" / "recent";
        RecentList recent(nested);
        recent.touch("/a");

        EXPECT_TRUE(std::filesystem::exists(nested));
    }

    TEST_F(RecentListTest, EmptyPathWorksInMemoryOnly) {
        RecentList recent({});
        recent.touch("/a");
        recent.touch("/b");

        EXPECT_EQ(entries(recent), (std::vector<std::string>{"/b", "/a"}));
    }

    TEST_F(RecentListTest, UnwritableLocationStillWorksInMemory) {
        write({"/a"});
        RecentList recent(file() / "recent");
        recent.touch("/b");

        EXPECT_EQ(entries(recent), (std::vector<std::string>{"/b"}));
        EXPECT_EQ(lines(), (std::vector<std::string>{"/a"}));
    }

    TEST(RecentListPath, UsesXdgStateHome) {
        const ScopedEnv xdg("XDG_STATE_HOME");
        xdg.set("/state");

        EXPECT_EQ(RecentList::defaultPath(),
                  std::filesystem::path("/state/shmscope/recent"));
    }

    TEST(RecentListPath, FallsBackToHomeWhenXdgIsEmpty) {
        const ScopedEnv xdg("XDG_STATE_HOME");
        const ScopedEnv home("HOME");
        xdg.set("");
        home.set("/home/me");

        EXPECT_EQ(
            RecentList::defaultPath(),
            std::filesystem::path("/home/me/.local/state/shmscope/recent"));
    }

    TEST(RecentListPath, FallsBackToHomeWhenXdgIsUnset) {
        const ScopedEnv xdg("XDG_STATE_HOME");
        const ScopedEnv home("HOME");
        xdg.unset();
        home.set("/home/me");

        EXPECT_EQ(
            RecentList::defaultPath(),
            std::filesystem::path("/home/me/.local/state/shmscope/recent"));
    }

    TEST(RecentListPath, EmptyWithoutEitherVariable) {
        const ScopedEnv xdg("XDG_STATE_HOME");
        const ScopedEnv home("HOME");
        xdg.unset();
        home.unset();

        EXPECT_TRUE(RecentList::defaultPath().empty());
    }

}  // namespace
