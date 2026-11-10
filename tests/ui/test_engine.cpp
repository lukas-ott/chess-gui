// M4 in the window: engine analysis with a fake UCI engine (fake_engine.cpp), the eval bar and
// arrows, playing lines, the Lichess cloud's answers, and installing a downloaded Stockfish.
// Nothing here goes over the network.

#include "MainWindow.hpp"
#include "board/BoardView.hpp"
#include "board/EvalBar.hpp"
#include "engine/CloudEval.hpp"
#include "engine/EnginePanel.hpp"
#include "engine/EngineRegistry.hpp"
#include "engine/EngineSection.hpp"
#include "engine/EngineSettings.hpp"
#include "engine/EnginesView.hpp"
#include "engine/StockfishDownload.hpp"
#include "engine/UciEngine.hpp"
#include "style/AppStyle.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>

#include <memory>

namespace {

using chessgui::app::AppStyle;
using chessgui::app::BoardView;
using chessgui::app::CloudEval;
using chessgui::app::EngineEntry;
using chessgui::app::EnginePanel;
using chessgui::app::EngineSection;
using chessgui::app::EvalBar;
using chessgui::app::MainWindow;
using chessgui::app::StockfishDownload;
using namespace chessgui;

/// The name the fake engine is added under.
QString fake() {
    return QStringLiteral("FakeFish 1");
}

EngineEntry fakeEngine(const QString& name = fake()) {
    EngineEntry entry;
    entry.name = name;
    entry.path = QStringLiteral(CHESSGUI_FAKE_ENGINE);
    entry.threads = 1;
    entry.hashMb = 16;
    return entry;
}

class EngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Start without engines (the settings file is shared by every test in this run).
        QSettings settings;
        settings.remove(QStringLiteral("engines"));
        settings.remove(QStringLiteral("engine"));
        window = std::make_unique<MainWindow>(style);
        window->show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window.get()));
        board = window->findChild<BoardView*>();
        panel = window->findChild<EnginePanel*>(QStringLiteral("enginePanel"));
        bar = window->findChild<EvalBar*>(QStringLiteral("evalBar"));
        tabs = window->findChild<QTabBar*>(QStringLiteral("documentTabs"));
        ASSERT_NE(board, nullptr);
        ASSERT_NE(panel, nullptr);
        ASSERT_NE(bar, nullptr);
        ASSERT_NE(tabs, nullptr);
        window->showSection(MainWindow::Section::Board);
    }

    void play(std::string_view san) {
        emit board->moveMade(board->position().parseSan(san).value());
    }
    /// A line as shown by one engine (the fake by default); empty if that row is hidden.
    [[nodiscard]] QString lineText(int line, const QString& engineName = fake()) const {
        const EngineSection* section = panel->section(engineName);
        const auto* label = section == nullptr
                                ? nullptr
                                : section->findChild<QLabel*>(QStringLiteral("pvLine%1").arg(line));
        return label != nullptr && label->isVisibleTo(panel) ? label->text() : QString();
    }
    [[nodiscard]] QPushButton* toggleOf(const QString& engineName) const {
        const EngineSection* section = panel->section(engineName);
        return section == nullptr
                   ? nullptr
                   : section->findChild<QPushButton*>(QStringLiteral("engineToggle"));
    }
    /// Waits until the engine shows lines for the position on the board.
    [[nodiscard]] bool waitForLines(std::size_t count = 1) const {
        return QTest::qWaitFor(
            [&] {
                return panel->lines().size() >= count &&
                       !engine::pvMoves(board->position(), panel->lines().front().pv).empty();
            },
            5000);
    }

    AppStyle style{*qApp};
    std::unique_ptr<MainWindow> window;
    BoardView* board = nullptr;
    EnginePanel* panel = nullptr;
    EvalBar* bar = nullptr;
    QTabBar* tabs = nullptr;
};

TEST_F(EngineTest, WithoutAnEngineTheCloudIsThereAndStockfishOnOffer) {
    // No engine yet: the Lichess cloud has a section (nothing runs until asked) and the tab
    // offers the way to get Stockfish, which leads to the Engines page.
    ASSERT_NE(panel->section(EnginePanel::cloudName()), nullptr);
    EXPECT_FALSE(panel->isRunning());
    auto* noEngine = panel->findChild<QWidget*>(QStringLiteral("noEngine"));
    ASSERT_NE(noEngine, nullptr);
    EXPECT_TRUE(noEngine->isVisible());
    auto* get = panel->findChild<QPushButton*>(QStringLiteral("getStockfish"));
    ASSERT_NE(get, nullptr);
    QTest::mouseClick(get, Qt::LeftButton);
    EXPECT_EQ(window->section(), MainWindow::Section::Engines);
    EXPECT_NE(window->findChild<QWidget*>(QStringLiteral("getStockfishCard")), nullptr);

    // An engine added (here directly; in the app via download, file or the Mac's Stockfish)
    // gets a section of its own above the cloud's, and is remembered.
    window->engines().add(fakeEngine());
    ASSERT_NE(panel->section(fake()), nullptr);
    EXPECT_FALSE(panel->isRunning());
    EXPECT_FALSE(noEngine->isVisibleTo(panel));
    ASSERT_EQ(chessgui::app::engines::load().size(), 1U);
    EXPECT_EQ(chessgui::app::engines::load().front().name, fake());
}

TEST_F(EngineTest, TheEnginesPageShowsCardsAndDetails) {
    window->engines().add(fakeEngine());
    window->showSection(MainWindow::Section::Engines);
    window->setWindowState(Qt::WindowNoState);
    window->resize(1280, 800);
    QApplication::processEvents();
    auto* view = window->findChild<chessgui::app::EnginesView*>(QStringLiteral("enginesView"));
    ASSERT_NE(view, nullptr);
    const auto cardFor = [&](const QString& name) -> QFrame* {
        for (auto* card : view->findChildren<QFrame*>(QStringLiteral("engineCard"))) {
            if (card->accessibleName() == name && card->isVisible()) {
                return card;
            }
        }
        return nullptr;
    };
    // A card per engine, plus the Lichess cloud.
    ASSERT_NE(cardFor(fake()), nullptr);
    ASSERT_NE(cardFor(EnginePanel::cloudName()), nullptr);

    // Clicking a card selects it, through the window as a real click arrives. (Selecting used
    // to rebuild the cards, the clicked one too: deleting it mid-click crashed the app.)
    const auto click = [&](QWidget* target) {
        ASSERT_NE(target, nullptr);
        const QPoint at = target->mapTo(window.get(), target->rect().center());
        QTest::mouseMove(window->windowHandle(), at);
        QTest::mouseClick(window->windowHandle(), Qt::LeftButton, {}, at);
        QApplication::processEvents();
    };
    click(cardFor(EnginePanel::cloudName()));
    EXPECT_EQ(view->selected(), EnginePanel::cloudName());
    click(cardFor(fake()));
    EXPECT_EQ(view->selected(), fake());

    // Settings change in the details and are kept.
    auto* threads = view->findChild<QSpinBox*>(QStringLiteral("engineThreads"));
    ASSERT_NE(threads, nullptr);
    threads->setValue(2);
    EXPECT_EQ(window->engines().find(fake())->threads, 2);
    EXPECT_EQ(chessgui::app::engines::load().front().threads, 2);
    window->showSection(MainWindow::Section::Board);
    panel->setRunning(fake(), true);
    ASSERT_TRUE(waitForLines());

    // Removing a running engine stops it and takes its section away.
    window->showSection(MainWindow::Section::Engines);
    view->select(fake());
    auto* remove = view->findChild<QPushButton*>(QStringLiteral("removeEngine"));
    ASSERT_NE(remove, nullptr);
    QTest::mouseClick(remove, Qt::LeftButton);
    EXPECT_TRUE(window->engines().engines().empty());
    EXPECT_EQ(panel->section(fake()), nullptr);
    EXPECT_FALSE(panel->isRunning());
}

TEST_F(EngineTest, StartsOnlyWhenSwitchedOnAndShowsTheBestLine) {
    window->engines().add(fakeEngine());
    ASSERT_NE(panel->section(fake()), nullptr);
    EXPECT_EQ(panel->section(fake())->uciEngine(), nullptr); // not started (spec: lazily)
    EXPECT_FALSE(bar->isVisible());

    const QRect boardBefore = board->geometry();
    QPushButton* toggle = toggleOf(fake());
    ASSERT_NE(toggle, nullptr);
    QTest::mouseClick(toggle, Qt::LeftButton);
    ASSERT_TRUE(panel->isRunning(fake()));
    ASSERT_NE(panel->section(fake())->uciEngine(), nullptr);
    ASSERT_TRUE(waitForLines());

    // Start position: the fake's best line is its first legal move in UCI order, a2a3, then the
    // reply that sorts first, a7a5.
    EXPECT_TRUE(lineText(1).contains(QStringLiteral(">a3<"))) << lineText(1).toStdString();
    EXPECT_TRUE(lineText(1).contains(QStringLiteral(">a5<")));
    EXPECT_TRUE(bar->isVisible());
    QApplication::processEvents();
    EXPECT_EQ(board->geometry(), boardBefore); // the bar's space was kept: the board stays put
    // To the right of the board, centred between its squares and the moves panel.
    const QWidget* side = window->findChild<QSplitter*>(QStringLiteral("mainSplitter"))->widget(1);
    const int squaresRight =
        board->mapTo(window.get(), board->squareRect(*core::Square::parse("h1")).topRight()).x();
    const int barLeft = bar->mapTo(window.get(), QPoint(0, 0)).x();
    const int barRight = barLeft + bar->width() - 1;
    const int panelLeft = side->mapTo(window.get(), QPoint(0, 0)).x();
    EXPECT_GT(barLeft, squaresRight);
    EXPECT_NEAR(barLeft - squaresRight, panelLeft - barRight, 1);
    ASSERT_TRUE(bar->score().has_value());
    EXPECT_EQ(bar->score()->value, 50); // White to move, +0.50
    ASSERT_FALSE(board->engineMoves().empty());
    EXPECT_EQ(board->engineMoves().front().uci(), "a2a3");

    QTest::mouseClick(toggle, Qt::LeftButton); // off again
    EXPECT_FALSE(panel->isRunning());
    EXPECT_FALSE(bar->isVisible());
    EXPECT_TRUE(board->engineMoves().empty());
}

TEST_F(EngineTest, SeveralEnginesRunAtOnce) {
    window->engines().add(fakeEngine());
    window->engines().add(fakeEngine(QStringLiteral("FakeFish 2")));
    panel->setRunning(fake(), true);
    panel->setRunning(QStringLiteral("FakeFish 2"), true);
    ASSERT_TRUE(QTest::qWaitFor(
        [&] {
            return !lineText(1).isEmpty() && !lineText(1, QStringLiteral("FakeFish 2")).isEmpty();
        },
        5000));
    EXPECT_NE(panel->section(fake())->uciEngine(),
              panel->section(QStringLiteral("FakeFish 2"))->uciEngine()); // two processes
    // The board follows the topmost running engine; when it stops, the next one takes over.
    EXPECT_EQ(panel->primary(), panel->section(fake()));
    panel->setRunning(fake(), false);
    EXPECT_EQ(panel->primary(), panel->section(QStringLiteral("FakeFish 2")));
    EXPECT_TRUE(bar->isVisible());
    ASSERT_TRUE(QTest::qWaitFor([&] { return !board->engineMoves().empty(); }, 5000));

    // Both follow the board.
    panel->setRunning(fake(), true);
    play("e4");
    ASSERT_TRUE(QTest::qWaitFor(
        [&] {
            return lineText(1).contains(QStringLiteral("1…")) &&
                   lineText(1, QStringLiteral("FakeFish 2")).contains(QStringLiteral("1…"));
        },
        5000));

    // Space stops them all, and starts the same ones again.
    board->setFocus();
    QTest::keyClick(window.get(), Qt::Key_Space);
    EXPECT_FALSE(panel->isRunning());
    EXPECT_FALSE(bar->isVisible());
    QTest::keyClick(window.get(), Qt::Key_Space);
    EXPECT_TRUE(panel->isRunning(fake()));
    EXPECT_TRUE(panel->isRunning(QStringLiteral("FakeFish 2")));
}

TEST_F(EngineTest, FollowsTheBoardAndScoresForWhite) {
    window->engines().add(fakeEngine());
    panel->setRunning(fake(), true);
    ASSERT_TRUE(waitForLines());
    play("e4");
    // Black to move now: the fake says +0.50 for Black, which is -0.50 for White.
    ASSERT_TRUE(QTest::qWaitFor(
        [&] {
            return !board->engineMoves().empty() && board->engineMoves().front().uci() == "a7a5";
        },
        5000));
    EXPECT_TRUE(lineText(1).contains(QStringLiteral("1…")));
    ASSERT_TRUE(bar->score().has_value());
    EXPECT_EQ(bar->score()->value, -50);
}

TEST_F(EngineTest, MoreLinesAndArrowsForEach) {
    window->engines().add(fakeEngine());
    panel->setLineCount(3);
    panel->setRunning(fake(), true);
    ASSERT_TRUE(waitForLines(3));
    EXPECT_FALSE(lineText(3).isEmpty());
    EXPECT_TRUE(lineText(4).isEmpty());
    EXPECT_EQ(board->engineMoves().size(), 3U);
    // Fewer lines while it runs: the engine is told, and the extra rows go.
    panel->setLineCount(1);
    ASSERT_TRUE(QTest::qWaitFor([&] { return lineText(2).isEmpty(); }, 5000));
    EXPECT_EQ(panel->lineCount(), 1);
}

TEST_F(EngineTest, ClickingAMoveInALinePlaysItIntoTheGame) {
    window->engines().add(fakeEngine());
    panel->setRunning(fake(), true);
    ASSERT_TRUE(waitForLines());
    panel->section(fake())->playLine(1, 2); // a3, then a5
    core::Position expected;
    expected.play(expected.parseUci("a2a3").value());
    expected.play(expected.parseUci("a7a5").value());
    EXPECT_EQ(board->position().fen(), expected.fen());
    // And the engine moves on with the board.
    ASSERT_TRUE(QTest::qWaitFor([&] { return lineText(1).contains(QStringLiteral("2.")); }, 5000));
}

TEST_F(EngineTest, ARealClickOnAMoveInALinePlaysIt) {
    // Clicking replaces the clicked line's own text (the board moved on): that must wait until
    // the label has finished handling the click.
    window->engines().add(fakeEngine());
    panel->setRunning(fake(), true);
    ASSERT_TRUE(waitForLines());
    auto* line = panel->section(fake())->findChild<QLabel*>(QStringLiteral("pvLine1"));
    ASSERT_NE(line, nullptr);
    // "1. a3 a5": the first move starts after "1. ".
    const QFontMetrics metrics(line->font());
    const QPoint at(metrics.horizontalAdvance(QStringLiteral("1. a")), line->height() / 2);
    QTest::mouseMove(line, at);
    QTest::mouseClick(line, Qt::LeftButton, {}, at);
    QApplication::processEvents();
    core::Position expected;
    expected.play(expected.parseUci("a2a3").value());
    EXPECT_EQ(board->position().fen(), expected.fen());
    ASSERT_TRUE(waitForLines());
}

TEST_F(EngineTest, SpaceStartsTheFirstEngineTheFirstTime) {
    window->engines().add(fakeEngine());
    board->setFocus();
    QTest::keyClick(window.get(), Qt::Key_Space);
    EXPECT_TRUE(panel->isRunning(fake()));
    EXPECT_FALSE(panel->isRunning(EnginePanel::cloudName()));
    ASSERT_TRUE(waitForLines());
    QTest::keyClick(window.get(), Qt::Key_Space);
    EXPECT_FALSE(panel->isRunning());
}

TEST_F(EngineTest, PausesInTheLibraryAndGameOverNeedsNoEngine) {
    window->engines().add(fakeEngine());
    panel->setRunning(fake(), true);
    ASSERT_TRUE(waitForLines());
    window->showSection(MainWindow::Section::Library); // nothing to analyse
    EXPECT_TRUE(panel->lines().empty());
    window->showSection(MainWindow::Section::Board);
    ASSERT_TRUE(waitForLines());

    // Fool's mate on the board: no engine lines, and the bar goes all the way to Black.
    for (const char* move : {"f3", "e5", "g4", "Qh4#"}) {
        play(move);
    }
    const auto* status = panel->section(fake())->findChild<QLabel*>(QStringLiteral("engineStatus"));
    ASSERT_NE(status, nullptr);
    EXPECT_EQ(status->text(), QStringLiteral("Checkmate"));
    EXPECT_TRUE(lineText(1).isEmpty());
    ASSERT_TRUE(bar->score().has_value());
    EXPECT_EQ(bar->score()->kind, engine::Score::Kind::Over);
    EXPECT_EQ(bar->score()->value, -1); // 0-1
    EXPECT_DOUBLE_EQ(bar->whiteShare(), 0.0);
}

TEST_F(EngineTest, AnEngineThatWontStartIsReported) {
    EngineEntry broken = fakeEngine(QStringLiteral("Broken"));
    broken.path = QStringLiteral("/nonexistent/engine");
    window->engines().add(broken);
    panel->setRunning(QStringLiteral("Broken"), true);
    ASSERT_TRUE(QTest::qWaitFor([&] { return !panel->isRunning(); }, 5000));
    const auto* status = panel->section(QStringLiteral("Broken"))
                             ->findChild<QLabel*>(QStringLiteral("engineStatus"));
    ASSERT_NE(status, nullptr);
    EXPECT_TRUE(status->text().contains(QStringLiteral("couldn't be started")))
        << status->text().toStdString();
}

/// Design review: PNGs of the engine panel and the Engines page when CHESSGUI_SNAPSHOT_DIR is
/// set.
TEST_F(EngineTest, Snapshots) {
    const QString dir = qEnvironmentVariable("CHESSGUI_SNAPSHOT_DIR");
    window->setWindowState(Qt::WindowNoState);
    window->resize(1280, 800);
    auto* panels = window->findChild<QTabWidget*>(QStringLiteral("panels"));
    ASSERT_NE(panels, nullptr);
    panels->setCurrentWidget(panel);
    const auto save = [&](const QString& name) {
        QApplication::processEvents();
        const QImage image = window->grab().toImage();
        EXPECT_FALSE(image.isNull());
        if (!dir.isEmpty()) {
            image.save(QDir(dir).filePath(name + QStringLiteral(".png")));
        }
    };
    save(QStringLiteral("engine-none"));
    window->showSection(MainWindow::Section::Engines);
    save(QStringLiteral("engines-page-empty"));
    window->engines().add(fakeEngine());
    window->engines().add(fakeEngine(QStringLiteral("FakeFish 2")));
    window->findChild<chessgui::app::EnginesView*>()->select(fake());
    save(QStringLiteral("engines-page"));
    window->showSection(MainWindow::Section::Board);
    panel->setLineCount(2);
    play("e4");
    play("e5");
    panel->setRunning(fake(), true);
    panel->setRunning(QStringLiteral("FakeFish 2"), true);
    ASSERT_TRUE(waitForLines(2));
    QTest::qWait(300); // the eval bar's animation
    save(QStringLiteral("engine-running"));
}

TEST(CloudEvalParse, ScoresForTheSideToMoveAndCastling) {
    // Real answers from lichess.org/api/cloud-eval (trimmed). Scores there are White's view.
    const auto afterE4 =
        core::Position::fromFen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1")
            .value();
    const QByteArray json = R"({"fen":"x","knodes":119133,"depth":60,"pvs":[
        {"moves":"e7e5 g1f3 b8c6 f1b5 g8f6 e1h1 f6e4","cp":22},
        {"moves":"c7c6 d2d4","cp":27}]})";
    const auto lines = CloudEval::parse(json, afterE4);
    ASSERT_EQ(lines.size(), 2U);
    EXPECT_EQ(lines[0].multiPv, 1);
    EXPECT_EQ(lines[0].depth, 60);
    EXPECT_EQ(lines[0].nodes, 119133000U);
    EXPECT_EQ(lines[0].score.value, -22); // Black to move: +0.22 for White is -22 for Black
    ASSERT_EQ(lines[0].pv.size(), 7U);
    EXPECT_EQ(lines[0].pv[5], "e1g1"); // castling, written as king-takes-rook by Lichess

    // Black mates in one: White's view says -1, which is +1 for Black, the side to move.
    const QByteArray mate = R"({"depth":40,"knodes":5,"pvs":[{"moves":"d8h4","mate":-1}]})";
    const auto mateLines = CloudEval::parse(
        mate,
        core::Position::fromFen("rnbqkbnr/pppp1ppp/8/4p3/6P1/5P2/PPPPP2P/RNBQKBNR b KQkq - 0 2")
            .value());
    ASSERT_EQ(mateLines.size(), 1U);
    EXPECT_EQ(mateLines[0].score.kind, engine::Score::Kind::Mate);
    EXPECT_EQ(mateLines[0].score.value, 1);

    EXPECT_TRUE(CloudEval::parse("not json", afterE4).empty());
}

TEST(StockfishInstall, UnpacksAndFindsTheEngine) {
    const QTemporaryDir work;
    ASSERT_TRUE(work.isValid());
    // An archive shaped like Stockfish's: a folder with the engine and some text files.
    const QString inner = work.filePath(QStringLiteral("pack/stockfish"));
    ASSERT_TRUE(QDir().mkpath(inner));
    ASSERT_TRUE(QFile::copy(QStringLiteral(CHESSGUI_FAKE_ENGINE),
                            inner + QStringLiteral("/stockfish-macos-universal")));
    QFile readme(inner + QStringLiteral("/README.md"));
    ASSERT_TRUE(readme.open(QIODevice::WriteOnly));
    readme.write("Stockfish");
    readme.close();
    const QString archive = work.filePath(QStringLiteral("stockfish-macos-universal.tar.gz"));
    ASSERT_EQ(
        QProcess::execute(QStringLiteral("/usr/bin/tar"),
                          {QStringLiteral("-czf"), archive, QStringLiteral("-C"),
                           work.filePath(QStringLiteral("pack")), QStringLiteral("stockfish")}),
        0);

    const QString engines = work.filePath(QStringLiteral("engines"));
    StockfishDownload download(engines);
    QSignalSpy finished(&download, &StockfishDownload::finished);
    download.install(archive, QStringLiteral("sf_test"));
    ASSERT_TRUE(finished.wait(5000));
    const QString path = finished.front().at(1).toString();
    EXPECT_TRUE(path.endsWith(QStringLiteral("stockfish-macos-universal")));
    EXPECT_TRUE(path.startsWith(engines + QStringLiteral("/sf_test")));
    EXPECT_TRUE(QFileInfo(path).isExecutable());
    EXPECT_FALSE(QFile::exists(archive)); // the download itself is cleaned up

    // A broken archive fails cleanly.
    QFile junk(work.filePath(QStringLiteral("junk.tar.gz")));
    ASSERT_TRUE(junk.open(QIODevice::WriteOnly));
    junk.write("not an archive");
    junk.close();
    QSignalSpy failed(&download, &StockfishDownload::failed);
    download.install(junk.fileName(), QStringLiteral("sf_junk"));
    ASSERT_TRUE(failed.wait(5000));
}

/// The real Lichess cloud. Tests stay off the network, so this runs only when asked:
///   CHESSGUI_LIVE_NETWORK=1 ./build/dev/tests/chessgui_ui_tests --gtest_filter='Live*'
TEST(LiveNetwork, LichessCloudKnowsTheStartPosition) {
    if (qEnvironmentVariableIsEmpty("CHESSGUI_LIVE_NETWORK")) {
        GTEST_SKIP() << "set CHESSGUI_LIVE_NETWORK=1 to run";
    }
    CloudEval cloud;
    cloud.setMultiPv(2);
    chessgui::app::AnalysisUpdate last;
    QObject::connect(&cloud, &chessgui::app::AnalysisSource::updated,
                     [&](const chessgui::app::AnalysisUpdate& update) { last = update; });
    const core::Position start;
    cloud.analyse(start);
    ASSERT_TRUE(QTest::qWaitFor([&] { return last.lines.size() == 2; }, 10000));
    EXPECT_EQ(last.position.fen(), start.fen());
    EXPECT_GT(last.lines.front().depth, 20);
    EXPECT_FALSE(engine::pvMoves(start, last.lines.front().pv).empty());
}

/// The real thing, when Stockfish is installed (Homebrew); skipped otherwise (e.g. in CI).
TEST(RealEngine, StockfishAnalysesAndFollowsPositions) {
    const QString path = chessgui::app::engines::findInstalledStockfish();
    if (path.isEmpty()) {
        GTEST_SKIP() << "no Stockfish installed";
    }
    EngineEntry entry;
    entry.name = QStringLiteral("Stockfish");
    entry.path = path;
    entry.threads = 2;
    entry.hashMb = 32;
    chessgui::app::UciEngine engine(entry);
    engine.setMultiPv(2);
    chessgui::app::AnalysisUpdate last;
    QObject::connect(&engine, &chessgui::app::AnalysisSource::updated,
                     [&](const chessgui::app::AnalysisUpdate& update) { last = update; });
    const core::Position start;
    engine.analyse(start);
    ASSERT_TRUE(QTest::qWaitFor(
        [&] { return last.lines.size() == 2 && last.lines.front().depth >= 10; }, 10000));
    EXPECT_EQ(last.position.fen(), start.fen());
    EXPECT_TRUE(engine.engineName().startsWith(QStringLiteral("Stockfish")));

    // A new position: the old search stops and the lines that come are for the new one.
    auto ruy =
        core::Position::fromFen("r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3")
            .value();
    engine.analyse(ruy);
    ASSERT_TRUE(QTest::qWaitFor(
        [&] {
            return last.position.fen() == ruy.fen() && !last.lines.empty() &&
                   last.lines.front().depth >= 10;
        },
        10000));
    EXPECT_FALSE(engine::pvMoves(ruy, last.lines.front().pv).empty());
    engine.stop();
}

} // namespace
