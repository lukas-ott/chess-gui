#include "MainWindow.hpp"

#include "BoardView.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

namespace chessgui::app {
namespace {

QWidget* placeholder(const QString& text) {
    auto* label = new QLabel(text);
    label->setAlignment(Qt::AlignCenter);
    label->setEnabled(false); // greyed out until the feature exists
    return label;
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("ChessGui"));
    buildMenus();
    buildLayout();
    restoreState();
}

void MainWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* openAction = fileMenu->addAction(tr("&Open…"));
    openAction->setShortcut(QKeySequence::Open);
    openAction->setEnabled(false); // M3: PGN viewer/editor
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction(tr("&Quit"), qApp, &QApplication::quit);
    quitAction->setShortcut(QKeySequence::Quit);
    quitAction->setMenuRole(QAction::QuitRole);

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    auto* aboutAction = helpMenu->addAction(tr("&About ChessGui"), this, &MainWindow::showAbout);
    aboutAction->setMenuRole(QAction::AboutRole);
}

void MainWindow::buildLayout() {
    auto* documentTabs = new QTabBar;
    documentTabs->setDocumentMode(true);
    documentTabs->setTabsClosable(true);
    documentTabs->setMovable(true);
    documentTabs->addTab(tr("Game 1"));

    auto* notation = new QPlainTextEdit;
    notation->setReadOnly(true);
    notation->setPlaceholderText(tr("Notation (M3)"));

    auto* panels = new QTabWidget;
    panels->setDocumentMode(true);
    panels->addTab(placeholder(tr("Engine analysis (M4)")), tr("Engine"));
    panels->addTab(placeholder(tr("Opening explorer (v2)")), tr("Explorer"));
    panels->addTab(placeholder(tr("Repertoire and gap check (v2)")), tr("Repertoire"));
    panels->addTab(placeholder(tr("Game info (M3)")), tr("Game info"));

    m_sideSplitter = new QSplitter(Qt::Vertical);
    m_sideSplitter->addWidget(notation);
    m_sideSplitter->addWidget(panels);
    m_sideSplitter->setChildrenCollapsible(false);

    m_mainSplitter = new QSplitter(Qt::Horizontal);
    m_mainSplitter->addWidget(new BoardView);
    m_mainSplitter->addWidget(m_sideSplitter);
    m_mainSplitter->setChildrenCollapsible(false);
    m_mainSplitter->setStretchFactor(0, 3);
    m_mainSplitter->setStretchFactor(1, 2);

    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(documentTabs);
    layout->addWidget(m_mainSplitter, 1);
    setCentralWidget(central);
}

void MainWindow::restoreState() {
    const QSettings settings;
    if (!restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray())) {
        resize(1200, 780);
    }
    m_mainSplitter->restoreState(
        settings.value(QStringLiteral("window/mainSplitter")).toByteArray());
    m_sideSplitter->restoreState(
        settings.value(QStringLiteral("window/sideSplitter")).toByteArray());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/mainSplitter"), m_mainSplitter->saveState());
    settings.setValue(QStringLiteral("window/sideSplitter"), m_sideSplitter->saveState());
    event->accept();
}

void MainWindow::showAbout() {
    QMessageBox::about(this, tr("About ChessGui"),
                       tr("<b>ChessGui %1</b><p>A fast native chess GUI for opening preparation."
                          "<p>Licensed under the GNU GPL v3.")
                           .arg(QApplication::applicationVersion()));
}

} // namespace chessgui::app
