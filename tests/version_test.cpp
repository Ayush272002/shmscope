#include "shmscope/version.hpp"

#include <string_view>

#include <gtest/gtest.h>

TEST(Version, IsNotEmpty) {
    EXPECT_FALSE(std::string_view(SHMSCOPE_VERSION).empty());
}
