#include <gtest/gtest.h>

#include <shmscope/version.hpp>
#include <string_view>

TEST(Version, IsNotEmpty) {
    EXPECT_FALSE(std::string_view(SHMSCOPE_VERSION).empty());
}
