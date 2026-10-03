#include "shmscope/ui/paths.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

    using shmscope::commonPrefix;
    using shmscope::completePath;
    using shmscope::expandHome;
    using shmscope::LAYOUT_EXTENSIONS;

    using Choices = std::vector<std::string>;

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

    class CompletePathTest : public ::testing::Test {
    protected:
        void SetUp() override {
            dir_ = std::filesystem::temp_directory_path() /
                   std::format("shmscope-paths-{}",
                               ::testing::UnitTest::GetInstance()
                                   ->current_test_info()
                                   ->name());
            std::filesystem::remove_all(dir_);
            std::filesystem::create_directories(dir_ / "sub");
            std::filesystem::create_directories(dir_ / "sum.ksy.d");
            for (const auto* name :
                 {"a.ksy", "ab.yaml", "c.json", "d.yml", "notes.txt",
                  ".hidden.ksy", "sub/inner.ksy"}) {
                std::ofstream(dir_ / name) << "meta: {}\n";
            }
            base_ = dir_.string() + "/";
        }

        void TearDown() override { std::filesystem::remove_all(dir_); }

        std::filesystem::path dir_{};
        std::string base_{};
    };

    TEST_F(CompletePathTest, ListsLayoutFilesAndDirectories) {
        EXPECT_EQ(
            completePath(base_, LAYOUT_EXTENSIONS),
            (Choices{base_ + "a.ksy", base_ + "ab.yaml", base_ + "c.json",
                     base_ + "d.yml", base_ + "sub/", base_ + "sum.ksy.d/"}));
    }

    TEST_F(CompletePathTest, WithoutExtensionsEveryFileIsListed) {
        const auto choices = completePath(base_ + "n");

        EXPECT_EQ(choices, (Choices{base_ + "notes.txt"}));
    }

    TEST_F(CompletePathTest, FiltersByPrefix) {
        EXPECT_EQ(completePath(base_ + "a", LAYOUT_EXTENSIONS),
                  (Choices{base_ + "a.ksy", base_ + "ab.yaml"}));
        EXPECT_EQ(completePath(base_ + "su", LAYOUT_EXTENSIONS),
                  (Choices{base_ + "sub/", base_ + "sum.ksy.d/"}));
    }

    TEST_F(CompletePathTest, LooksInsideSubdirectories) {
        EXPECT_EQ(completePath(base_ + "sub/", LAYOUT_EXTENSIONS),
                  (Choices{base_ + "sub/inner.ksy"}));
    }

    TEST_F(CompletePathTest, HiddenEntriesNeedADot) {
        EXPECT_EQ(completePath(base_ + ".h", LAYOUT_EXTENSIONS),
                  (Choices{base_ + ".hidden.ksy"}));
    }

    TEST_F(CompletePathTest, DotsCompleteToTheParent) {
        EXPECT_EQ(completePath(base_ + "..", LAYOUT_EXTENSIONS),
                  (Choices{base_ + "../"}));
        EXPECT_EQ(
            completePath(base_ + ".", LAYOUT_EXTENSIONS),
            (Choices{base_ + "../", base_ + "./", base_ + ".hidden.ksy"}));
    }

    TEST_F(CompletePathTest, ParentDirectoriesCanBeListed) {
        const auto choices =
            completePath(base_ + "sub/../a", LAYOUT_EXTENSIONS);

        EXPECT_EQ(choices,
                  (Choices{base_ + "sub/../a.ksy", base_ + "sub/../ab.yaml"}));
    }

    TEST_F(CompletePathTest, RelativePathsStartFromTheWorkingDirectory) {
        const ScopedDirectory inside(dir_);

        EXPECT_EQ(completePath("su", LAYOUT_EXTENSIONS),
                  (Choices{"sub/", "sum.ksy.d/"}));
        EXPECT_EQ(completePath("sub/i", LAYOUT_EXTENSIONS),
                  (Choices{"sub/inner.ksy"}));
    }

    TEST_F(CompletePathTest, AnEmptyPartialListsTheWorkingDirectory) {
        const ScopedDirectory inside(dir_);

        EXPECT_EQ(completePath("", LAYOUT_EXTENSIONS),
                  (Choices{"a.ksy", "ab.yaml", "c.json", "d.yml", "sub/",
                           "sum.ksy.d/"}));
    }

    TEST_F(CompletePathTest, TildeKeepsItsSpelling) {
        const ScopedEnv home("HOME", dir_.string());

        EXPECT_EQ(completePath("~/su", LAYOUT_EXTENSIONS),
                  (Choices{"~/sub/", "~/sum.ksy.d/"}));
    }

    TEST_F(CompletePathTest, MissingDirectoriesHaveNoChoices) {
        EXPECT_TRUE(completePath(base_ + "nope/a", LAYOUT_EXTENSIONS).empty());
    }

    TEST_F(CompletePathTest, NoMatchHasNoChoices) {
        EXPECT_TRUE(completePath(base_ + "zzz", LAYOUT_EXTENSIONS).empty());
    }

    TEST(CommonPrefixTest, SharedStart) {
        EXPECT_EQ(commonPrefix({"alpha/", "alpine.ksy"}), "alp");
        EXPECT_EQ(commonPrefix({"same", "same"}), "same");
        EXPECT_EQ(commonPrefix({"one"}), "one");
        EXPECT_EQ(commonPrefix({"a", "b"}), "");
        EXPECT_EQ(commonPrefix({}), "");
        EXPECT_EQ(commonPrefix({"abc", "ab"}), "ab");
    }

    TEST(ExpandHomeTest, TildeIsTheHomeDirectory) {
        const ScopedEnv home("HOME", "/home/someone");

        EXPECT_EQ(expandHome("~"), std::filesystem::path("/home/someone"));
        EXPECT_EQ(expandHome("~/x.ksy"),
                  std::filesystem::path("/home/someone/x.ksy"));
    }

    TEST(ExpandHomeTest, OtherPathsAreUnchanged) {
        const ScopedEnv home("HOME", "/home/someone");

        EXPECT_EQ(expandHome("x.ksy"), std::filesystem::path("x.ksy"));
        EXPECT_EQ(expandHome("/abs/x.ksy"),
                  std::filesystem::path("/abs/x.ksy"));
        EXPECT_EQ(expandHome("~other/x"), std::filesystem::path("~other/x"));
        EXPECT_EQ(expandHome("a/~/b"), std::filesystem::path("a/~/b"));
    }

}  // namespace
