#include "kiebitz/training/log.hpp"
#include "kiebitz/training/session.hpp"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

using namespace kiebitz::training;

TEST(TrainingSession, RunsThePartsThatAreOnInTheirOrder) {
    // Listed out of order, one off, one at 0 minutes: Openings, then Tactics, then Self-study.
    Session session({{Part::SelfStudy, 5, true},
                     {Part::Tactics, 2, true},
                     {Part::Openings, 1, true},
                     {Part::Middlegame, 10, false},
                     {Part::Endgames, 0, true}});
    ASSERT_EQ(session.count(), 3U);
    EXPECT_EQ(session.part(), Part::Openings);
    ASSERT_NE(session.upcoming(), nullptr);
    EXPECT_EQ(session.upcoming()->part, Part::Tactics);
    EXPECT_EQ(session.leftMs(), 60'000);
    session.elapse(45'000);
    EXPECT_EQ(session.leftMs(), 15'000);
    EXPECT_FALSE(session.timeUp());
    session.elapse(20'000); // past its minute: up, but it counts on until Next
    EXPECT_TRUE(session.timeUp());
    EXPECT_EQ(session.leftMs(), 0);
    EXPECT_EQ(session.usedMs(), 65'000);
    session.next();
    EXPECT_EQ(session.part(), Part::Tactics);
    EXPECT_EQ(session.usedMs(), 0);
    session.next();
    EXPECT_EQ(session.part(), Part::SelfStudy);
    EXPECT_EQ(session.upcoming(), nullptr);
    session.next();
    EXPECT_TRUE(session.finished());
    EXPECT_FALSE(session.timeUp());
}

TEST(TrainingSession, NothingToDoWhenEverythingIsOff) {
    EXPECT_TRUE(Session({{Part::Tactics, 10, false}, {Part::Endgames, 0, true}}).empty());
    EXPECT_FALSE(Session(defaultPlan()).empty());
    EXPECT_EQ(Session(defaultPlan()).count(), kParts);
}

TEST(TrainingStreak, CountsDaysInARowUpToToday) {
    EXPECT_EQ(streak({}, 100), 0);
    EXPECT_EQ(streak({98, 99, 100}, 100), 3);
    // Nothing yet today: the streak isn't broken until the day is over.
    EXPECT_EQ(streak({97, 98, 99}, 100), 3);
    EXPECT_EQ(streak({97, 98}, 100), 0); // yesterday missed
    EXPECT_EQ(streak({90, 91, 92, 99, 100}, 100), 2);
    EXPECT_EQ(bestStreak({90, 91, 92, 99, 100}), 3);
    EXPECT_EQ(bestStreak({}), 0);
}

TEST(TrainingLog, AddsUpTimePerDayAndPart) {
    const auto path = std::filesystem::temp_directory_path() / "kiebitz-training-test.sqlite";
    std::filesystem::remove(path);
    {
        auto log = Log::open(path.string());
        ASSERT_TRUE(log.ok()) << log.error().message;
        (*log)->add(100, Part::Tactics, 600);
        (*log)->add(100, Part::Tactics, 300);
        (*log)->add(100, Part::Openings, 120);
        (*log)->add(101, Part::SelfStudy, 60);
        (*log)->add(99, Part::Endgames, 0); // nothing: not a day trained
    }
    auto log = Log::open(path.string()); // kept across opens
    ASSERT_TRUE(log.ok());
    const auto days = (*log)->days(100);
    ASSERT_EQ(days.size(), 2U);
    EXPECT_EQ(days.at(100)[2], 900); // Tactics, third in kAllParts
    EXPECT_EQ(days.at(100)[0], 120);
    EXPECT_EQ(days.at(101)[4], 60);
    EXPECT_EQ((*log)->days(101).size(), 1U);
    EXPECT_EQ((*log)->trainedDays(), (std::set<Day>{100, 101}));
    (*log).reset();
    std::filesystem::remove(path);
}

} // namespace
