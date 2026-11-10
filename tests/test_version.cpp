#include "kiebitz/core/version.hpp"

#include <gtest/gtest.h>

TEST(Version, IsNotEmpty) {
    EXPECT_FALSE(kiebitz::core::version().empty());
}
