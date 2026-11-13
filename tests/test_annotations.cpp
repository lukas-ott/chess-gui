#include "kiebitz/core/annotations.hpp"

#include <gtest/gtest.h>

namespace {

using namespace kiebitz::core;

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

TEST(Comment, LineBreakInsideACommand) {
    const auto parts = parseComment("[%csl\nRe5] [%cal\r\nGe2e4] text");
    EXPECT_EQ(parts.squareMarks.size(), 1U);
    EXPECT_EQ(parts.arrows.size(), 1U);
    EXPECT_EQ(parts.text, "text");
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

TEST(Drawing, ToggleArrow) {
    std::vector<Arrow> arrows;
    const Arrow green{sq("e2"), sq("e4"), MarkColor::Green};
    toggleArrow(arrows, green);
    ASSERT_EQ(arrows.size(), 1U);

    toggleArrow(arrows, {sq("e2"), sq("e4"), MarkColor::Red}); // same squares, new colour
    ASSERT_EQ(arrows.size(), 1U);
    EXPECT_EQ(arrows[0].color, MarkColor::Red);

    toggleArrow(arrows, {sq("e4"), sq("e2"), MarkColor::Red}); // other direction: a new arrow
    EXPECT_EQ(arrows.size(), 2U);

    toggleArrow(arrows, {sq("e2"), sq("e4"), MarkColor::Red}); // same again: removed
    ASSERT_EQ(arrows.size(), 1U);
    EXPECT_EQ(arrows[0].from, sq("e4"));
}

TEST(Drawing, ToggleSquareMark) {
    std::vector<SquareMark> marks;
    toggleSquareMark(marks, {sq("d4"), MarkColor::Green});
    toggleSquareMark(marks, {sq("d4"), MarkColor::Blue});
    ASSERT_EQ(marks.size(), 1U);
    EXPECT_EQ(marks[0].color, MarkColor::Blue);
    toggleSquareMark(marks, {sq("d4"), MarkColor::Blue});
    EXPECT_TRUE(marks.empty());
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

TEST(CommentDisplay, ClockIsKeptApartAndShortened) {
    EXPECT_EQ(displayComment("[%clk 1:24:00]"), (CommentDisplay{"", "1:24:00", ""}));
    EXPECT_EQ(displayComment("[%clk 0:47:00]").clock, "47:00");
    EXPECT_EQ(displayComment("[%clk 0:05:00]").clock, "5:00");
    EXPECT_EQ(displayComment("[%clk 0:00:09.2]").clock, "0:09");
}

TEST(CommentDisplay, OtherCommandsAreLeftOut) {
    EXPECT_EQ(displayComment("[%eval 0.31] [%emt 0:00:05] Good idea [%evp 1,2]"),
              (CommentDisplay{"Good idea", "", ""}));
}

TEST(CommentDisplay, AuthorFromAnno) {
    // As in a study exported from Lichess: who wrote the text, then the text, then the clock.
    const auto shown = displayComment("[%anno \"jonathanlund123\", jonathanlund123] trengte du å "
                                      "bruke en halvtime på a6? [%clk 1:05:00]");
    EXPECT_EQ(shown.author, "jonathanlund123");
    EXPECT_EQ(shown.text, "trengte du å bruke en halvtime på a6?");
    EXPECT_EQ(shown.clock, "1:05:00");
    EXPECT_EQ(displayComment("[%anno Anna] hi").author, "Anna");
}

TEST(CommentDisplay, PlainTextIsTidied) {
    EXPECT_EQ(displayComment("  A plan\n worth   knowing "),
              (CommentDisplay{"A plan worth knowing", "", ""}));
    EXPECT_EQ(displayComment("Unfinished [%clk 1:00"), // no closing bracket: shown as written
              (CommentDisplay{"Unfinished [%clk 1:00", "", ""}));
    EXPECT_EQ(displayComment(""), CommentDisplay{});
}

TEST(Annotations, CommentTextLeavesOutCommandsAndKeepsLineBreaks) {
    EXPECT_EQ(commentText("[%clk 0:03:00]"), "");
    EXPECT_EQ(commentText("[%clk 0:03:00] Best by test"), "Best by test");
    EXPECT_EQ(commentText("Best by test [%clk 0:05:00]"), "Best by test");
    EXPECT_EQ(commentText("Good [%eval 0.3] idea"), "Good idea");
    EXPECT_EQ(commentText("First line\nsecond [%clk 0:01:00]"), "First line\nsecond");
    EXPECT_EQ(commentText("No commands at all"), "No commands at all");
}

TEST(Annotations, EditedTextKeepsTheCommands) {
    EXPECT_EQ(withCommentText("[%clk 0:03:00]", "Best by test"), "[%clk 0:03:00] Best by test");
    EXPECT_EQ(withCommentText("Old words [%clk 0:05:00] [%eval 0.3]", "New"),
              "[%clk 0:05:00] [%eval 0.3] New");
    EXPECT_EQ(withCommentText("[%clk 0:05:00] Old", ""), "[%clk 0:05:00]"); // text cleared
    EXPECT_EQ(withCommentText("Old", "New\nlines"), "New\nlines");
    // Round trip: the text shown, edited back unchanged, is the same comment's text.
    const std::string comment = "[%clk 1:24:00] Keeps its clock";
    EXPECT_EQ(commentText(withCommentText(comment, commentText(comment))), "Keeps its clock");
    EXPECT_EQ(displayComment(withCommentText(comment, "Edited")).clock, "1:24:00");
}
