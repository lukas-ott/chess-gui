#include "chessgui/core/annotations.hpp"

#include <gtest/gtest.h>

namespace {

using namespace chessgui::core;

Square sq(std::string_view name) {
    return Square::parse(name).value();
}

TEST(Comment, ExtractsArrowsAndSquares) {
    const auto parts = parseComment("[%csl Gd4,Rf5][%cal Ge2e4,Rd1d8] Controls the centre");
    EXPECT_EQ(parts.text, "Controls the centre");
    ASSERT_EQ(parts.squareMarks.size(), 2U);
    EXPECT_EQ(parts.squareMarks[0], (SquareMark{sq("d4"), MarkColor::Green}));
    EXPECT_EQ(parts.squareMarks[1], (SquareMark{sq("f5"), MarkColor::Red}));
    ASSERT_EQ(parts.arrows.size(), 2U);
    EXPECT_EQ(parts.arrows[0], (Arrow{sq("e2"), sq("e4"), MarkColor::Green}));
    EXPECT_EQ(parts.arrows[1], (Arrow{sq("d1"), sq("d8"), MarkColor::Red}));
}

TEST(Comment, TextAroundCommandsIsKept) {
    EXPECT_EQ(parseComment("Good move [%cal Bb1c3] here").text, "Good move here");
    EXPECT_EQ(parseComment("[%cal Yh1h8]").text, "");
    EXPECT_EQ(parseComment("plain text").text, "plain text");
}

TEST(Comment, OtherCommandsStayInText) {
    const auto parts = parseComment("[%clk 0:05:00] [%cal Ga1a8] Time trouble");
    EXPECT_EQ(parts.text, "[%clk 0:05:00] Time trouble");
    EXPECT_EQ(parts.arrows.size(), 1U);
}

TEST(Comment, MalformedEntriesAreDropped) {
    const auto parts = parseComment("[%cal Xe2e4,Ge2,Ge2e4] [%csl Gz9, Yh8]");
    ASSERT_EQ(parts.arrows.size(), 1U);
    EXPECT_EQ(parts.arrows[0], (Arrow{sq("e2"), sq("e4"), MarkColor::Green}));
    ASSERT_EQ(parts.squareMarks.size(), 1U);
    EXPECT_EQ(parts.squareMarks[0], (SquareMark{sq("h8"), MarkColor::Yellow}));
}

TEST(Comment, UnclosedBracketIsText) {
    EXPECT_EQ(parseComment("see [%cal Ge2e4").text, "see [%cal Ge2e4");
}

TEST(Comment, FormatRoundTrips) {
    const CommentParts parts{"Controls the centre",
                             {{sq("e2"), sq("e4"), MarkColor::Green}},
                             {{sq("d4"), MarkColor::Blue}}};
    const std::string text = formatComment(parts);
    EXPECT_EQ(text, "[%csl Bd4] [%cal Ge2e4] Controls the centre");
    EXPECT_EQ(parseComment(text), parts);
    EXPECT_EQ(formatComment({"just text", {}, {}}), "just text");
    EXPECT_EQ(formatComment({}), "");
}

TEST(Nag, Symbols) {
    EXPECT_EQ(nagSymbol(nag::kGood), "!");
    EXPECT_EQ(nagSymbol(nag::kInteresting), "!?");
    EXPECT_EQ(nagSymbol(nag::kWhiteSlightly), "⩲");
    EXPECT_EQ(nagSymbol(nag::kNovelty), "N");
    EXPECT_EQ(nagSymbol(250), "");
}

TEST(Nag, FromSymbol) {
    EXPECT_EQ(nagFromSymbol("!!"), nag::kBrilliant);
    EXPECT_EQ(nagFromSymbol("?!"), nag::kDubious);
    EXPECT_EQ(nagFromSymbol("±"), nag::kWhiteBetter);
    EXPECT_EQ(nagFromSymbol("+/-"), nag::kWhiteBetter);
    EXPECT_EQ(nagFromSymbol("-+"), nag::kBlackWinning);
    EXPECT_FALSE(nagFromSymbol("!!!").has_value());
}

} // namespace
