#include "MainWindow.hpp"
#include "board/BoardView.hpp"
#include "notation/NotationView.hpp"
#include "settings/Settings.hpp"
#include "settings/SettingsView.hpp"
#include "settings/Shortcuts.hpp"
#include "style/AppStyle.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDir>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QTest>
#include <QWheelEvent>
#include <gtest/gtest.h>

namespace {

using kiebitz::app::BoardView;
using kiebitz::app::MainWindow;
using kiebitz::app::NotationView;
using kiebitz::app::Settings;
using kiebitz::app::SettingsView;
using kiebitz::app::Shortcuts;
using namespace kiebitz::core;

const QKeyCombination kCommandBracket(Qt::CTRL | Qt::Key_BracketLeft); // ⌘[

/// The window's shortcut for `key`, if it has one.
QShortcut* windowKey(const QWidget& window, const QKeySequence& key) {
    for (auto* shortcut : window.findChildren<QShortcut*>()) {
        if (shortcut->key() == key) {
            return shortcut;
        }
    }
    return nullptr;
}

TEST(Shortcuts, TellsWhichKeysTheWindowMustHandle) {
    EXPECT_TRUE(Shortcuts::needsWindowKey(QKeySequence(Qt::Key_Left)));
    EXPECT_TRUE(Shortcuts::needsWindowKey(QKeySequence(Qt::Key_Space)));
    EXPECT_TRUE(Shortcuts::needsWindowKey(QKeySequence(Qt::SHIFT | Qt::Key_Home)));
    EXPECT_FALSE(Shortcuts::needsWindowKey(QKeySequence(Qt::Key_F)));
    EXPECT_FALSE(Shortcuts::needsWindowKey(QKeySequence(Qt::CTRL | Qt::Key_Left)));
    EXPECT_FALSE(Shortcuts::needsWindowKey(QKeySequence()));

    EXPECT_TRUE(Shortcuts::isReserved(QKeySequence::keyBindings(QKeySequence::Copy).front()));
    EXPECT_TRUE(Shortcuts::isReserved(QKeySequence(Qt::Key_Tab)));
    EXPECT_FALSE(Shortcuts::isReserved(QKeySequence(Qt::CTRL | Qt::Key_S)));
    EXPECT_FALSE(Shortcuts::isReserved(QKeySequence()));

    EXPECT_EQ(Shortcuts::plainText(QStringLiteral("Start / Stop &Engine…")),
              QStringLiteral("Start - Stop Engine"));
}

TEST(Shortcuts, SetsRefusesClashesKeepsAndResets) {
    QSettings().remove(QStringLiteral("shortcuts"));
    const auto build = [](QWidget& window, QMenuBar& bar) {
        QMenu* menu = bar.addMenu(QStringLiteral("&Game"));
        QAction* back = menu->addAction(QStringLiteral("&Back"));
        back->setShortcut(Qt::Key_Left);
        menu->addAction(QStringLiteral("&Save"))->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_S));
        menu->addAction(QStringLiteral("Flip…"));
        menu->addSeparator();
        menu->addMenu(QStringLiteral("Colours"))->addAction(QStringLiteral("Brown"));
        auto* group = new QActionGroup(&window);
        group->addAction(menu->addAction(QStringLiteral("Dark")));
        return back;
    };
    QWidget window;
    QMenuBar bar(&window);
    QAction* back = build(window, bar);
    auto* shortcuts = new Shortcuts(&window);
    shortcuts->addMenus(&bar, {back});
    shortcuts->load();

    // Not the separator, the submenu or a choice from a list.
    ASSERT_EQ(shortcuts->commands().size(), 3U);
    EXPECT_EQ(shortcuts->commands()[0].id, QStringLiteral("Game.Back"));
    EXPECT_EQ(shortcuts->commands()[2].id, QStringLiteral("Game.Flip"));

    // ← isn't handled by the macOS menu bar: the window has it, the menu item only shows it.
    QShortcut* left = windowKey(window, QKeySequence(Qt::Key_Left));
    ASSERT_NE(left, nullptr);
    EXPECT_TRUE(left->isEnabled());
    EXPECT_EQ(back->shortcutContext(), Qt::WidgetShortcut);
    shortcuts->setGameKeysEnabled(false); // in the Library the arrows belong to the lists
    EXPECT_FALSE(left->isEnabled());
    shortcuts->setGameKeysEnabled(true);
    EXPECT_TRUE(left->isEnabled());

    // A key another command has, or one that text fields need: refused, naming why.
    EXPECT_TRUE(shortcuts->set(QStringLiteral("Game.Flip"), QKeySequence(Qt::CTRL | Qt::Key_S))
                    .contains(QStringLiteral("Save")));
    EXPECT_FALSE(shortcuts
                     ->set(QStringLiteral("Game.Flip"),
                           QKeySequence::keyBindings(QKeySequence::Copy).front())
                     .isEmpty());
    EXPECT_TRUE(shortcuts->key(QStringLiteral("Game.Flip")).isEmpty());

    EXPECT_TRUE(shortcuts->set(QStringLiteral("Game.Flip"), QKeySequence(Qt::Key_F)).isEmpty());
    EXPECT_FALSE(shortcuts->isDefault(QStringLiteral("Game.Flip")));
    EXPECT_TRUE(shortcuts->set(QStringLiteral("Game.Back"), kCommandBracket).isEmpty());
    EXPECT_EQ(back->shortcut(), QKeySequence(kCommandBracket));
    EXPECT_EQ(back->shortcutContext(), Qt::WindowShortcut); // the menu bar handles ⌘ keys
    EXPECT_FALSE(left->isEnabled());

    // While a new key is pressed in Settings no command has a key, so none runs.
    shortcuts->setRecording(true);
    EXPECT_TRUE(back->shortcut().isEmpty());
    EXPECT_EQ(shortcuts->key(QStringLiteral("Game.Back")), QKeySequence(kCommandBracket));
    shortcuts->setRecording(false);
    EXPECT_EQ(back->shortcut(), QKeySequence(kCommandBracket));

    // Kept for the next launch.
    {
        QWidget again;
        QMenuBar againBar(&again);
        QAction* againBack = build(again, againBar);
        Shortcuts loaded(&again);
        loaded.addMenus(&againBar, {againBack});
        loaded.load();
        EXPECT_EQ(loaded.key(QStringLiteral("Game.Flip")), QKeySequence(Qt::Key_F));
        EXPECT_EQ(againBack->shortcut(), QKeySequence(kCommandBracket));
        EXPECT_TRUE(loaded.isDefault(QStringLiteral("Game.Save")));
    }

    shortcuts->reset(QStringLiteral("Game.Flip"));
    EXPECT_TRUE(shortcuts->key(QStringLiteral("Game.Flip")).isEmpty());
    EXPECT_TRUE(shortcuts->isDefault(QStringLiteral("Game.Flip")));
    shortcuts->resetAll();
    EXPECT_EQ(back->shortcut(), QKeySequence(Qt::Key_Left));
    EXPECT_TRUE(left->isEnabled());
    EXPECT_FALSE(QSettings().contains(QStringLiteral("shortcuts/Game.Back")));
}

class SettingsPageTest : public ::testing::Test {
protected:
    void SetUp() override {
        window.show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
        board = window.findChild<BoardView*>(QStringLiteral("gameBoard"));
        notation = window.findChild<NotationView*>();
        page = window.findChild<SettingsView*>();
        ASSERT_NE(board, nullptr);
        ASSERT_NE(notation, nullptr);
        ASSERT_NE(page, nullptr);
        window.showSection(MainWindow::Section::Board);
    }
    void TearDown() override {
        // Settings are kept for the whole test process: leave the defaults for the next test.
        for (const char* key : {Settings::kCoordinates, Settings::kFigurines, Settings::kTextSize,
                                Settings::kWheel, Settings::kAnimation}) {
            Settings::instance().reset(key);
        }
        QSettings().remove(QStringLiteral("shortcuts"));
    }
    void play(std::string_view san) {
        const Move move = board->position().parseSan(san).value();
        emit board->moveMade(move);
    }
    template <typename T> T* find(const QString& name) {
        auto* widget = window.findChild<T*>(name);
        EXPECT_NE(widget, nullptr) << name.toStdString();
        return widget;
    }
    void openSettings(const QString& section) {
        window.showSection(MainWindow::Section::Settings);
        page->showSection(section);
        QApplication::processEvents();
    }

    kiebitz::app::AppStyle style{*qApp};
    MainWindow window{style};
    BoardView* board = nullptr;
    NotationView* notation = nullptr;
    SettingsView* page = nullptr;
};

TEST_F(SettingsPageTest, ChangesShowOnTheBoardTheMovesAndTheStyle) {
    openSettings(QStringLiteral("Board"));
    QTest::mouseClick(find<QPushButton>(QStringLiteral("settingsCoordinates:all")), Qt::LeftButton);
    EXPECT_EQ(board->options().coordinates, BoardView::Options::Coordinates::All);
    QTest::mouseClick(find<QPushButton>(QStringLiteral("settingsCoordinates:none")),
                      Qt::LeftButton);
    EXPECT_FALSE(board->showCoordinates());

    // The View menu and the page show the same setting.
    QAction* coordinates = nullptr;
    for (auto* candidate : window.findChildren<QAction*>()) {
        if (candidate->text().remove(QLatin1Char('&')) == QStringLiteral("Show Coordinates")) {
            coordinates = candidate;
        }
    }
    ASSERT_NE(coordinates, nullptr);
    EXPECT_FALSE(coordinates->isChecked());
    coordinates->trigger();
    EXPECT_TRUE(board->showCoordinates());
    EXPECT_TRUE(find<QPushButton>(QStringLiteral("settingsCoordinates:edge"))->isChecked());

    window.showSection(MainWindow::Section::Board);
    play("Nf3");
    openSettings(QStringLiteral("Moves"));
    QTest::mouseClick(find<QPushButton>(QStringLiteral("settingsFigurines:true")), Qt::LeftButton);
    window.showSection(MainWindow::Section::Board);
    EXPECT_TRUE(notation->plainText().contains(QStringLiteral("♘f3")))
        << notation->plainText().toStdString();
    openSettings(QStringLiteral("Moves"));
    QTest::mouseClick(find<QPushButton>(QStringLiteral("settingsFigurines:false")), Qt::LeftButton);
    window.showSection(MainWindow::Section::Board);
    EXPECT_TRUE(notation->plainText().contains(QStringLiteral("Nf3")))
        << notation->plainText().toStdString();

    openSettings(QStringLiteral("Appearance"));
    QTest::mouseClick(find<QPushButton>(QStringLiteral("settingsTextSize:120")), Qt::LeftButton);
    EXPECT_EQ(style.textScale(), 120);
    QTest::mouseClick(find<QPushButton>(QStringLiteral("settingsTextSize:100")), Qt::LeftButton);
    EXPECT_EQ(style.textScale(), 100);
}

TEST_F(SettingsPageTest, TheWheelStepsThroughTheMovesWhenOn) {
    Settings::instance().setValue(Settings::kAnimation, QStringLiteral("off"));
    play("e4");
    play("e5");
    const std::string afterE5 = board->position().fen();
    const QPointF centre = QRectF(board->rect()).center();
    const auto wheel = [&](int y) {
        QWheelEvent event(centre, board->mapToGlobal(centre), {}, QPoint(0, y), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(board, &event);
    };
    Settings::instance().setValue(Settings::kWheel, false);
    wheel(120);
    EXPECT_EQ(board->position().fen(), afterE5);

    Settings::instance().setValue(Settings::kWheel, true);
    wheel(120); // a notch up: one move back
    EXPECT_EQ(board->position().fen(),
              "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
    wheel(-60); // a trackpad's half step: nothing yet
    EXPECT_NE(board->position().fen(), afterE5);
    wheel(-60);
    EXPECT_EQ(board->position().fen(), afterE5);
}

TEST_F(SettingsPageTest, AKeyIsChangedByPressingIt) {
    openSettings(QStringLiteral("Keyboard"));
    auto* backField = find<QKeySequenceEdit>(QStringLiteral("shortcut:Game.Back"));
    auto* forwardField = find<QKeySequenceEdit>(QStringLiteral("shortcut:Game.Forward"));
    ASSERT_NE(backField, nullptr);
    ASSERT_NE(forwardField, nullptr);
    EXPECT_EQ(backField->keySequence(), QKeySequence(Qt::Key_Left));

    // Click the field and press ⌘[: Back has it now (and nothing ran meanwhile).
    QTest::mouseClick(backField, Qt::LeftButton);
    ASSERT_TRUE(QTest::qWaitFor([&] { return backField->hasFocus(); }, 5000));
    QTest::keyClick(backField, Qt::Key_BracketLeft, Qt::ControlModifier);
    ASSERT_TRUE(QTest::qWaitFor([&] { return !backField->hasFocus(); }, 5000));
    EXPECT_EQ(backField->keySequence(), QKeySequence(kCommandBracket));
    EXPECT_TRUE(find<QPushButton>(QStringLiteral("shortcutReset:Game.Back"))->isVisibleTo(page));

    // The same key for Forward: refused, with why, and Forward keeps →.
    QTest::mouseClick(forwardField, Qt::LeftButton);
    ASSERT_TRUE(QTest::qWaitFor([&] { return forwardField->hasFocus(); }, 5000));
    QTest::keyClick(forwardField, Qt::Key_BracketLeft, Qt::ControlModifier);
    ASSERT_TRUE(QTest::qWaitFor([&] { return !forwardField->hasFocus(); }, 5000));
    EXPECT_EQ(forwardField->keySequence(), QKeySequence(Qt::Key_Right))
        << forwardField->keySequence().toString().toStdString();
    EXPECT_TRUE(
        find<QLabel>(QStringLiteral("shortcutsNotice"))->text().contains(QStringLiteral("Back")));

    // Esc keeps the key; ⌫ takes it away.
    QTest::mouseClick(forwardField, Qt::LeftButton);
    ASSERT_TRUE(QTest::qWaitFor([&] { return forwardField->hasFocus(); }, 5000));
    QTest::keyClick(forwardField, Qt::Key_Escape);
    ASSERT_TRUE(QTest::qWaitFor([&] { return !forwardField->hasFocus(); }, 5000));
    EXPECT_EQ(forwardField->keySequence(), QKeySequence(Qt::Key_Right));
    QTest::mouseClick(forwardField, Qt::LeftButton);
    ASSERT_TRUE(QTest::qWaitFor([&] { return forwardField->hasFocus(); }, 5000));
    QTest::keyClick(forwardField, Qt::Key_Backspace);
    ASSERT_TRUE(QTest::qWaitFor([&] { return !forwardField->hasFocus(); }, 5000));
    EXPECT_TRUE(forwardField->keySequence().isEmpty());

    // ⌘[ goes back in a game; ← no longer does.
    window.showSection(MainWindow::Section::Board);
    play("e4");
    play("e5");
    const std::string afterE5 = board->position().fen();
    QTest::keyClick(&window, Qt::Key_Left);
    EXPECT_EQ(board->position().fen(), afterE5);
    QTest::keyClick(&window, Qt::Key_BracketLeft, Qt::ControlModifier);
    EXPECT_EQ(board->position().fen(),
              "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");

    // Reset All: ← again.
    openSettings(QStringLiteral("Keyboard"));
    QTest::mouseClick(find<QPushButton>(QStringLiteral("shortcutsResetAll")), Qt::LeftButton);
    EXPECT_EQ(backField->keySequence(), QKeySequence(Qt::Key_Left));
    window.showSection(MainWindow::Section::Board);
    QTest::keyClick(&window, Qt::Key_Left);
    EXPECT_EQ(board->position().fen(), std::string(kStartFen));
}

/// The Settings page's sections, saved when KIEBITZ_SNAPSHOT_DIR is set.
TEST_F(SettingsPageTest, Snapshots) {
    const QString dir = qEnvironmentVariable("KIEBITZ_SNAPSHOT_DIR");
    if (dir.isEmpty()) {
        GTEST_SKIP() << "KIEBITZ_SNAPSHOT_DIR not set";
    }
    window.resize(1200, 800);
    for (const QString& section :
         {QStringLiteral("Board"), QStringLiteral("Moves"), QStringLiteral("Appearance"),
          QStringLiteral("Engines"), QStringLiteral("Repertoire"), QStringLiteral("Sounds"),
          QStringLiteral("Keyboard"), QStringLiteral("Folders")}) {
        openSettings(section);
        QTest::qWait(50);
        window.grab().save(
            QDir(dir).filePath(QStringLiteral("settings-%1.png").arg(section.toLower())));
    }
}

} // namespace
