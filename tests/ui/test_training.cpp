// The Training page: the plan, a session through its parts (each on its own page), the time
// kept, and the days as a grid.

#include "MainWindow.hpp"
#include "kiebitz/training/log.hpp"
#include "library/Library.hpp"
#include "notify/Notifications.hpp"
#include "puzzles/PuzzlesView.hpp"
#include "style/AppStyle.hpp"
#include "training/ActivityGrid.hpp"
#include "training/TrainingHistory.hpp"
#include "training/TrainingSession.hpp"
#include "training/TrainingView.hpp"
#include "widgets/Stepper.hpp"
#include "widgets/Toggle.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>
#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <numeric>

namespace {

using kiebitz::app::MainWindow;
using kiebitz::app::TrainingSession;
using kiebitz::training::Part;
using namespace kiebitz;

constexpr qint64 kMinute = 60'000;

/// This run's training log (its folder made first, as the app does).
core::Result<std::unique_ptr<training::Log>> openLog() {
    const QString file = app::TrainingHistory::defaultFile();
    QDir().mkpath(QFileInfo(file).path());
    return training::Log::open(file.toStdString());
}

/// Seconds trained today, as the log has them.
int secondsToday() {
    auto log = openLog();
    if (!log.ok()) {
        return 0;
    }
    const training::Day today = app::TrainingHistory::dayOf(QDate::currentDate());
    const auto days = (*log)->days(today);
    const auto found = days.find(today);
    return found == days.end() ? 0 : std::accumulate(found->second.begin(), found->second.end(), 0);
}

class TrainingTest : public ::testing::Test {
protected:
    void SetUp() override {
        QSettings().remove(QStringLiteral("training"));
        window = std::make_unique<MainWindow>(style);
        window->show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window.get()));
        session = window->findChild<TrainingSession*>();
        bar = window->findChild<app::TrainingBar*>(QStringLiteral("trainingBar"));
        ASSERT_NE(session, nullptr);
        ASSERT_NE(bar, nullptr);
        window->showSection(MainWindow::Section::Training);
    }
    void TearDown() override {
        session->stop();
        QSettings().remove(QStringLiteral("training"));
    }
    /// A part's row on the page: its toggle and its minutes.
    [[nodiscard]] QWidget* row(Part part) const {
        const auto rows = window->findChildren<QWidget*>(QStringLiteral("trainingPart"));
        return rows.value(static_cast<int>(part));
    }
    void click(const char* name) const {
        auto* button = window->findChild<QAbstractButton*>(QString::fromLatin1(name));
        ASSERT_NE(button, nullptr) << name;
        QTest::mouseClick(button, Qt::LeftButton);
    }

    app::AppStyle style{*qApp};
    std::unique_ptr<MainWindow> window;
    TrainingSession* session = nullptr;
    app::TrainingBar* bar = nullptr;
};

TEST_F(TrainingTest, ThePlanIsKeptAsYouSetIt) {
    // The default: every part on, an hour in all.
    const auto* total = window->findChild<QLabel*>(QStringLiteral("trainingTotal"));
    EXPECT_EQ(total->text(), QStringLiteral("1 h in all"));
    // Middlegame off, Tactics up to 30 minutes.
    row(Part::Middlegame)->findChild<app::Toggle*>()->click();
    auto* tactics = row(Part::Tactics)->findChild<app::Stepper*>();
    tactics->setValue(30);
    emit tactics->valueChanged(30);
    EXPECT_EQ(total->text(), QStringLiteral("1 h 5 min in all"));
    const auto plan = TrainingSession::plan();
    EXPECT_FALSE(plan[1].on);
    EXPECT_EQ(plan[2].minutes, 30);
    EXPECT_EQ(training::Session(plan).count(), 4U); // the middlegame left out
}

TEST_F(TrainingTest, ASessionGoesThroughThePartsAndKeepsTheTime) {
    // Openings and Tactics only.
    for (const Part part : {Part::Middlegame, Part::Endgames, Part::SelfStudy}) {
        row(part)->findChild<app::Toggle*>()->click();
    }
    EXPECT_FALSE(bar->isVisible()); // nothing running
    window->findChild<app::TrainingHistory*>()->wait();
    const int before = secondsToday();
    click("trainingStart");
    ASSERT_TRUE(session->running());
    EXPECT_EQ(session->session()->part(), Part::Openings);
    EXPECT_TRUE(bar->isVisible());
    EXPECT_EQ(bar->text(), QStringLiteral("Openings  ·  15:00 left"));
    EXPECT_EQ(window->findChild<QPushButton*>(QStringLiteral("trainingStart"))->text(),
              QStringLiteral("Stop Training"));

    // Time passes: a minute, then the rest; it says so and offers the next part.
    session->advance(kMinute);
    EXPECT_EQ(bar->text(), QStringLiteral("Openings  ·  14:00 left"));
    const QSignalSpy up(session, &TrainingSession::timeUp);
    QStringList notified;
    app::notifications::setSinkForTests(
        [&](const QString& title, const QString& body) { notified << title + " / " + body; });
    session->advance(14 * kMinute);
    app::notifications::setSinkForTests({});
    ASSERT_EQ(up.size(), 1);
    EXPECT_EQ(bar->text(), QStringLiteral("Openings  ·  time's up  ·  next: Tactics"));
    // A notification too, for when you're in another app.
    EXPECT_EQ(notified, QStringList{QStringLiteral("Openings: time's up / Next: Tactics")});

    // Paused, time stands still.
    click("trainingBarPause");
    EXPECT_TRUE(session->paused());
    EXPECT_TRUE(bar->text().endsWith(u"paused"));
    session->advance(10 * kMinute);
    EXPECT_LT(session->session()->usedMs(), 16 * kMinute); // the 10 minutes didn't count
    click("trainingBarPause");                             // Resume

    // Next: the tactics, on the Puzzles page (the Lichess puzzles, all themes).
    click("trainingBarNext");
    EXPECT_EQ(session->session()->part(), Part::Tactics);
    EXPECT_EQ(window->section(), MainWindow::Section::Puzzles);
    EXPECT_EQ(window->findChild<app::PuzzlesView*>()->source(),
              QString::fromLatin1(app::PuzzlesView::kLichess));
    EXPECT_EQ(window->findChild<QToolButton*>(QStringLiteral("trainingBarNext"))->text(),
              QStringLiteral("Finish")); // the last part

    // Finish: back on the Training page, the 20 minutes in the log and in today's square.
    session->advance(5 * kMinute);
    const QSignalSpy ended(session, &TrainingSession::ended);
    click("trainingBarNext");
    ASSERT_EQ(ended.size(), 1);
    EXPECT_FALSE(session->running());
    EXPECT_FALSE(bar->isVisible());
    EXPECT_EQ(window->section(), MainWindow::Section::Training);
    window->findChild<app::TrainingHistory*>()->wait();
    const int added = secondsToday() - before;
    EXPECT_GE(added, 20 * 60);
    EXPECT_LT(added, (20 * 60) + 5); // and not the paused 10 minutes
    const auto* streak = window->findChild<QLabel*>(QStringLiteral("trainingStreak"));
    ASSERT_TRUE(QTest::qWaitFor(
        [&] { return streak->text().endsWith(u"day") || streak->text().endsWith(u"days"); }, 5000));
    const auto* grid = window->findChild<app::ActivityGrid*>();
    const training::Day day = app::TrainingHistory::dayOf(QDate::currentDate());
    EXPECT_FALSE(grid->cellOf(day).isEmpty());
    ASSERT_TRUE(QTest::qWaitFor([&] { return grid->levelOf(day) >= 2; }, 5000)); // 15+ minutes
}

TEST_F(TrainingTest, EachPartOpensItsPage) {
    click("trainingStart"); // the default plan: every part
    // Openings first: the Library, with your repertoire files to pick one from; then
    // middlegame, tactics and endgame puzzles, then self-study on the Training page.
    EXPECT_EQ(window->section(), MainWindow::Section::Library);
    EXPECT_EQ(window->findChild<QComboBox*>(QStringLiteral("libraryTypeFilter"))->currentText(),
              QStringLiteral("Repertoires"));
    for (const auto& [part, section] :
         {std::pair{Part::Middlegame, MainWindow::Section::Puzzles},
          std::pair{Part::Tactics, MainWindow::Section::Puzzles},
          std::pair{Part::Endgames, MainWindow::Section::Puzzles},
          std::pair{Part::SelfStudy, MainWindow::Section::Training}}) {
        click("trainingBarNext");
        ASSERT_TRUE(session->running());
        EXPECT_EQ(session->session()->part(), part);
        EXPECT_EQ(window->section(), section) << TrainingSession::name(part).toStdString();
    }
    click("trainingBarStop");
    EXPECT_FALSE(session->running());
}

TEST(TrainingSnapshot, ThePageWithAFewMonthsOfTraining) {
    // Only for looking at (it writes days into this run's training log).
    const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR");
    if (dir.isEmpty()) {
        GTEST_SKIP() << "set KIEBITZ_SNAPSHOT_DIR";
    }
    {
        auto log = openLog();
        ASSERT_TRUE(log.ok());
        const training::Day today = app::TrainingHistory::dayOf(QDate::currentDate());
        for (training::Day day = today - 150; day < today; ++day) {
            if ((day * 7) % 5 == 0) {
                continue; // a day off now and then
            }
            const int minutes = ((day * 37) % 70) + 5;
            (*log)->add(day, Part::Tactics, minutes * 40);
            (*log)->add(day, Part::Openings, minutes * 20);
        }
    }
    app::AppStyle style{*qApp};
    MainWindow window(style);
    window.show();
    window.setWindowState(Qt::WindowNoState);
    window.resize(1280, 800);
    window.showSection(MainWindow::Section::Training);
    const auto* streak = window.findChild<QLabel*>(QStringLiteral("trainingStreak"));
    ASSERT_TRUE(QTest::qWaitFor([&] { return streak->text() != u"–"; }, 5000));
    QTest::qWait(100);
    window.grab().save(QDir(dir).filePath(QStringLiteral("training.png")));
}

TEST_F(TrainingTest, OpeningsShowsOneSidesRepertoireFilesInTheirFolders) {
    QTemporaryDir folder;
    QDir(folder.path()).mkpath(QStringLiteral("White/e4"));
    QDir(folder.path()).mkpath(QStringLiteral("Black"));
    const auto write = [&](const char* name, const char* pgn) {
        std::ofstream(folder.filePath(QString::fromLatin1(name)).toStdString()) << pgn;
    };
    write("White/e4/italian.pgn",
          "[FileType \"Repertoire\"]\n[Orientation \"white\"]\n\n1. e4 e5 2. Nf3 *\n");
    write("Black/caro.pgn", "[FileType \"Repertoire\"]\n[Orientation \"black\"]\n\n1. e4 c6 *\n");
    write("White/club.pgn", "[White \"A\"]\n[Black \"B\"]\n\n1. d4 d5 *\n");
    QSignalSpy scanned(&window->library(), &app::Library::scanned);
    window->library().setFolder(folder.path());
    ASSERT_TRUE(scanned.wait(5000));
    // White's repertoire files: the Italian only, in its folders.
    for (const Part part : {Part::Middlegame, Part::Tactics, Part::Endgames, Part::SelfStudy}) {
        row(part)->findChild<app::Toggle*>()->click();
    }
    auto* side = window->findChild<QComboBox*>(QStringLiteral("trainingSide"));
    side->setCurrentIndex(side->findText(QStringLiteral("White repertoires")));
    emit side->activated(side->currentIndex());
    click("trainingStart");
    EXPECT_EQ(window->section(), MainWindow::Section::Library);
    const auto* tree = window->findChild<QTreeWidget*>(QStringLiteral("libraryFiles"));
    QStringList shown;
    const std::function<void(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* item) {
        if (!item->isHidden()) {
            shown << item->text(0);
            for (int i = 0; i < item->childCount(); ++i) {
                visit(item->child(i));
            }
        }
    };
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        visit(tree->topLevelItem(i));
    }
    EXPECT_EQ(shown, (QStringList{QStringLiteral("White"), QStringLiteral("e4"),
                                  QStringLiteral("italian")}));
    session->stop();
    // Back to every file afterwards.
    auto* filter = window->findChild<QComboBox*>(QStringLiteral("libraryTypeFilter"));
    filter->setCurrentIndex(0);
}

TEST_F(TrainingTest, TheYearsAreAChoiceOnceThereAreSeveral) {
    auto* year = window->findChild<QComboBox*>(QStringLiteral("trainingYear"));
    const auto* grid = window->findChild<app::ActivityGrid*>();
    ASSERT_NE(year, nullptr);
    // Training in an earlier year, and today.
    const int earlier = QDate::currentDate().year() - 2;
    const training::Day then = app::TrainingHistory::dayOf(QDate(earlier, 3, 10));
    window->findChild<app::TrainingHistory*>()->wait(); // its own first look at the log first
    {
        auto log = openLog();
        ASSERT_TRUE(log.ok()) << log.error().message;
        (*log)->add(then, Part::Endgames, 45 * 60);
        (*log)->add(app::TrainingHistory::dayOf(QDate::currentDate()), Part::Tactics, 60);
    }
    window->showSection(MainWindow::Section::Library);
    window->showSection(MainWindow::Section::Training); // read again
    ASSERT_TRUE(QTest::qWaitFor([&] { return year->isVisible(); }, 5000));
    EXPECT_EQ(year->itemText(0), QStringLiteral("Last 12 months"));
    EXPECT_GE(year->findText(QString::number(earlier)), 1);
    EXPECT_TRUE(grid->cellOf(then).isEmpty()); // not in the last 12 months
    const QRect size = grid->geometry();
    const int columns = grid->weeks();

    // That year: January to December, the March day in it; the grid as big as before.
    year->setCurrentIndex(year->findText(QString::number(earlier)));
    emit year->activated(year->currentIndex());
    EXPECT_EQ(grid->year(), earlier);
    EXPECT_FALSE(grid->cellOf(then).isEmpty());
    EXPECT_EQ(grid->levelOf(then), 3); // 45 minutes
    EXPECT_FALSE(grid->cellOf(app::TrainingHistory::dayOf(QDate(earlier, 1, 1))).isEmpty());
    EXPECT_FALSE(grid->cellOf(app::TrainingHistory::dayOf(QDate(earlier, 12, 31))).isEmpty());
    EXPECT_GE(grid->weeks(), columns);
    QApplication::processEvents();
    EXPECT_EQ(grid->geometry(), size);
}

TEST(TrainingClock, ReadsLikeAClock) {
    EXPECT_EQ(app::clockText(15 * kMinute), QStringLiteral("15:00"));
    EXPECT_EQ(app::clockText(61'000), QStringLiteral("1:01"));
    EXPECT_EQ(app::clockText(400), QStringLiteral("0:01")); // not 0:00 until it's over
    EXPECT_EQ(app::clockText(0), QStringLiteral("0:00"));
    EXPECT_EQ(app::clockText(3'723'000), QStringLiteral("1:02:03"));
}

} // namespace
