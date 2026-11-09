#include "chessgui/core/version.hpp"

#include <gtest/gtest.h>

TEST(Version, IsNotEmpty) {
    EXPECT_FALSE(chessgui::core::version().empty());
}
