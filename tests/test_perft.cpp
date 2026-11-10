// Perft reference values: https://www.chessprogramming.org/Perft_Results

#include "kiebitz/core/perft.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>

namespace {

using kiebitz::core::kStartFen;
using kiebitz::core::perft;

constexpr std::string_view kKiwipete =
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";

constexpr std::string_view kPosition3 = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";

TEST(Perft, StartPosition) {
    EXPECT_EQ(perft(kStartFen, 1), 20U);
    EXPECT_EQ(perft(kStartFen, 2), 400U);
    EXPECT_EQ(perft(kStartFen, 3), 8'902U);
    EXPECT_EQ(perft(kStartFen, 4), 197'281U);
}

TEST(Perft, Kiwipete) {
    EXPECT_EQ(perft(kKiwipete, 1), 48U);
    EXPECT_EQ(perft(kKiwipete, 2), 2'039U);
    EXPECT_EQ(perft(kKiwipete, 3), 97'862U);
}

TEST(Perft, EnPassantAndPinsEndgame) {
    EXPECT_EQ(perft(kPosition3, 1), 14U);
    EXPECT_EQ(perft(kPosition3, 2), 191U);
    EXPECT_EQ(perft(kPosition3, 3), 2'812U);
    EXPECT_EQ(perft(kPosition3, 4), 43'238U);
}

TEST(Perft, DepthZeroIsOneNode) {
    EXPECT_EQ(perft(kStartFen, 0), 1U);
}

} // namespace
