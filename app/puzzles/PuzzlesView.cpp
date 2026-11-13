#include "PuzzlesView.hpp"

#include "kiebitz/pgn/pgn.hpp"
#include "kiebitz/puzzles/store.hpp"
#include "library/Library.hpp"
#include "puzzles/LichessPuzzles.hpp"
#include "repertoire/RepertoireStore.hpp"
#include "sound/Sounds.hpp"
#include "style/AppStyle.hpp"
#include "style/Icons.hpp"
#include "style/Layout.hpp"
#include "style/PageHeader.hpp"
#include "widgets/EmptyState.hpp"
#include "widgets/Message.hpp"
#include "widgets/Stepper.hpp"
#include "widgets/Toggle.hpp"

#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <random>

namespace kiebitz::app {
namespace {

constexpr int kMinRating = 400;
constexpr int kMaxRating = 3200;
constexpr int kProgressiveStep = 50;
constexpr int kReplyDelayMs = 350;
constexpr int kNextDelayMs = 900;
constexpr int kSolutionStepMs = 650;
constexpr int kIndexRole = Qt::UserRole;
/// Set once you chose a rating range (or "Harder after each solve" moved it): from then on it
/// no longer follows your profile's rating.
constexpr auto kOwnRange = "puzzles/ownRange";
constexpr int kRangeAroundRating = 150; ///< the default range: your rating, this much each way ///<
                                        ///< a list row: the file puzzle's index, or the id

QLabel* label(const QString& text, const char* role) {
    auto* result = new QLabel(text);
    result->setProperty("role", role);
    return result;
}

QFrame* card() {
    auto* frame = new QFrame;
    frame->setProperty("card", true);
    return frame;
}

QString duration(qint64 millis) {
    if (millis < 0) {
        return QStringLiteral("–"); // solved with the clock stopped
    }
    const qint64 seconds = millis / 1000;
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString sideName(core::Color color) {
    return color == core::Color::White ? QObject::tr("White") : QObject::tr("Black");
}

/// Lichess's themes worth filtering by come first in the list; the rest (length, phase) after.
QString themesText(const std::vector<std::string>& themes) {
    QStringList names;
    for (const std::string& theme : themes) {
        names << QString::fromStdString(puzzles::themeName(theme));
    }
    return names.join(QStringLiteral(", "));
}

/// A puzzle file's game: a name from its headers ("Aufgabe 1 · Standardmotiv am Damenflügel").
QString puzzleName(const core::Game& game, int index) {
    QStringList parts;
    for (const char* header : {"White", "Black", "Event"}) {
        const auto value = QString::fromUtf8(game.header(header).data(),
                                             static_cast<qsizetype>(game.header(header).size()));
        if (!value.isEmpty() && value != u"?" && !parts.contains(value) && parts.size() < 2) {
            parts << value;
        }
    }
    return parts.isEmpty() ? QObject::tr("Puzzle %1").arg(index + 1)
                           : parts.join(QStringLiteral("  ·  "));
}

} // namespace

PuzzlesView::PuzzlesView(LichessPuzzles& lichess, PuzzleStats& stats, Library& library,
                         Sounds& sounds, const AppStyle& style, QWidget* parent)
    : QWidget(parent), m_lichess(lichess), m_stats(stats), m_library(library), m_sounds(sounds),
      m_style(style), m_random(std::random_device{}()) {
    setObjectName(QStringLiteral("puzzlesView"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(layout::kPage);
    layout->setSpacing(layout::kPageSpacing);
    m_header = new PageHeader(tr("Puzzles"));
    m_sources = new QComboBox;
    m_sources->setObjectName(QStringLiteral("puzzleSource"));
    m_sources->setMinimumWidth(240);
    connect(m_sources, &QComboBox::activated, this,
            [this](int index) { setSource(m_sources->itemData(index).toString()); });
    m_header->addAction(m_sources);
    layout->addWidget(m_header);

    // The board, as big as the page allows.
    m_board = new BoardView;
    m_board->setObjectName(QStringLiteral("puzzleBoard"));
    m_board->setInteractive(false);
    connect(m_board, &BoardView::moveMade, this, &PuzzlesView::onMove);
    auto* boardCard = card();
    auto* boardBox = new QVBoxLayout(boardCard);
    boardBox->setContentsMargins(layout::kCard);
    boardBox->addWidget(m_board, 1);

    // ---- The Lichess download ----
    m_downloadText = new EmptyState(
        IconKind::Puzzle, tr("Lichess Puzzles"),
        tr("Six million puzzles from real games, free to use. A download of about 300 MB."),
        m_style);
    m_download = new QPushButton(tr("Download"));
    m_download->setObjectName(QStringLiteral("puzzleDownload"));
    m_download->setProperty("role", "primary");
    connect(m_download, &QPushButton::clicked, &m_lichess, &LichessPuzzles::download);
    m_progress = new QProgressBar;
    m_progress->setObjectName(QStringLiteral("puzzleProgress"));
    m_progress->setRange(0, 1000);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(4);
    m_progressText = label(QString(), "emptyHint");
    m_progressText->setObjectName(QStringLiteral("puzzleProgressText"));
    m_cancel = new QPushButton(tr("Cancel"));
    m_cancel->setProperty("role", "secondary");
    connect(m_cancel, &QPushButton::clicked, &m_lichess, &LichessPuzzles::cancel);
    auto* downloadPage = new QWidget;
    auto* downloadBox = new QVBoxLayout(downloadPage);
    downloadBox->setContentsMargins(0, 0, 0, 0);
    downloadBox->setSpacing(layout::kCardSpacing);
    downloadBox->addStretch();
    downloadBox->addWidget(m_downloadText);
    auto* downloadRow = new QHBoxLayout;
    downloadRow->addStretch();
    downloadRow->addWidget(m_download);
    downloadRow->addWidget(m_cancel);
    downloadRow->addStretch();
    downloadBox->addLayout(downloadRow);
    downloadBox->addWidget(m_progress);
    downloadBox->addWidget(m_progressText, 0, Qt::AlignHCenter);
    downloadBox->addStretch();

    // ---- The puzzle ----
    m_prompt = label(QString(), "practiceStatus");
    m_prompt->setObjectName(QStringLiteral("puzzlePrompt"));
    m_prompt->setWordWrap(true);
    m_details = label(QString(), "emptyHint");
    m_details->setObjectName(QStringLiteral("puzzleDetails"));
    m_details->setWordWrap(true);
    m_clock = label(QStringLiteral("0:00"), "statValue");
    m_clock->setObjectName(QStringLiteral("puzzleClock"));
    m_clock->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    fitClock();
    auto* promptRow = new QHBoxLayout;
    auto* promptText = new QVBoxLayout;
    promptText->setSpacing(4);
    promptText->addWidget(m_prompt);
    promptText->addWidget(m_details);
    // The clock can be stopped (and stays stopped for the next puzzles): those puzzles are
    // then left out of the average time.
    m_clockStopped = QSettings().value(QStringLiteral("puzzles/clockStopped"), false).toBool();
    m_pause = new QPushButton;
    m_pause->setObjectName(QStringLiteral("puzzlePause"));
    m_pause->setProperty("role", "nav");
    m_pause->setFocusPolicy(Qt::NoFocus);
    m_pause->setIconSize(QSize(14, 14));
    connect(m_pause, &QPushButton::clicked, this, &PuzzlesView::toggleClock);
    m_pause->setFixedSize(26, 26);
    auto* clockRow = new QHBoxLayout;
    clockRow->setSpacing(8);
    clockRow->addWidget(m_pause, 0, Qt::AlignVCenter);
    clockRow->addWidget(m_clock, 0, Qt::AlignVCenter);
    promptRow->addLayout(promptText, 1);
    promptRow->addLayout(clockRow);
    promptRow->setAlignment(clockRow, Qt::AlignTop);

    m_hint = new QPushButton(tr("Hint"));
    m_hint->setObjectName(QStringLiteral("puzzleHint"));
    m_hint->setProperty("role", "secondary");
    connect(m_hint, &QPushButton::clicked, this, &PuzzlesView::hint);
    m_solution = new QPushButton(tr("Solution"));
    m_solution->setObjectName(QStringLiteral("puzzleSolution"));
    m_solution->setProperty("role", "secondary");
    connect(m_solution, &QPushButton::clicked, this, &PuzzlesView::showSolution);
    m_analyse = new QPushButton(tr("Analyse"));
    m_analyse->setObjectName(QStringLiteral("puzzleAnalyse"));
    m_analyse->setProperty("role", "secondary");
    m_analyse->setToolTip(tr("Open the puzzle on the board"));
    connect(m_analyse, &QPushButton::clicked, this, [this] {
        if (m_solve) {
            emit analyseRequested(m_solve->game());
        }
    });
    m_next = new QPushButton(tr("Next Puzzle"));
    m_next->setObjectName(QStringLiteral("puzzleNext"));
    m_next->setProperty("role", "primary");
    connect(m_next, &QPushButton::clicked, this, &PuzzlesView::nextPuzzle);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addWidget(m_hint);
    buttons->addWidget(m_solution);
    buttons->addWidget(m_analyse);
    buttons->addStretch();
    buttons->addWidget(m_next);

    auto* tiles = new QHBoxLayout;
    tiles->setSpacing(8);
    const std::array<QString, 4> captions{tr("Solved"), tr("Clean"), tr("Streak"), tr("Average")};
    for (std::size_t i = 0; i < captions.size(); ++i) {
        auto* tile = new QFrame;
        tile->setProperty("statTile", true);
        auto* tileBox = new QVBoxLayout(tile);
        tileBox->setContentsMargins(10, 8, 10, 8);
        tileBox->setSpacing(2);
        tileBox->addWidget(label(captions[i], "statCaption"));
        m_tiles[i] = label(QStringLiteral("–"), "statValue");
        tileBox->addWidget(m_tiles[i]);
        tiles->addWidget(tile, 1);
    }
    m_tiles[0]->setObjectName(QStringLiteral("puzzleSolvedTile"));
    m_tiles[2]->setObjectName(QStringLiteral("puzzleStreakTile"));

    // Lichess: which puzzles.
    m_filters = new QWidget;
    auto* filters = new QGridLayout(m_filters);
    filters->setContentsMargins(0, 0, 0, 0);
    filters->setHorizontalSpacing(10);
    filters->setVerticalSpacing(8);
    const QSettings settings;
    m_low = new Stepper;
    m_low->setObjectName(QStringLiteral("puzzleLow"));
    m_high = new Stepper;
    m_high->setObjectName(QStringLiteral("puzzleHigh"));
    for (Stepper* stepper : {m_low, m_high}) {
        stepper->setRange(kMinRating, kMaxRating);
        stepper->setSingleStep(kProgressiveStep);
    }
    m_low->setValue(settings.value(QStringLiteral("puzzles/low"), 1200).toInt());
    m_high->setValue(settings.value(QStringLiteral("puzzles/high"), 1800).toInt());
    connect(m_low, &Stepper::valueChanged, this, [this](int value) {
        if (value > m_high->value()) {
            m_high->setValue(value);
        }
        QSettings().setValue(QLatin1String(kOwnRange), true);
        saveFilters();
        showLichessState();
    });
    connect(m_high, &Stepper::valueChanged, this, [this](int value) {
        if (value < m_low->value()) {
            m_low->setValue(value);
        }
        QSettings().setValue(QLatin1String(kOwnRange), true);
        saveFilters();
        showLichessState();
    });
    auto* ratingRow = new QHBoxLayout;
    ratingRow->setSpacing(6);
    ratingRow->addWidget(m_low);
    ratingRow->addWidget(label(QStringLiteral("–"), "emptyHint"));
    ratingRow->addWidget(m_high);
    ratingRow->addStretch();
    m_theme = new QComboBox;
    m_theme->setObjectName(QStringLiteral("puzzleTheme"));
    connect(m_theme, &QComboBox::activated, this, [this] { saveFilters(); });
    const auto toggleRow = [this](Toggle*& toggle, const QString& text, const char* key,
                                  bool fallback) {
        toggle = new Toggle(m_style);
        toggle->setChecked(QSettings().value(QLatin1String(key), fallback).toBool());
        connect(toggle, &Toggle::toggled, this, [this] { saveFilters(); });
        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        row->addWidget(toggle);
        row->addWidget(label(text, "infoLabel"), 1);
        return row;
    };
    filters->addWidget(label(tr("Rating"), "infoLabel"), 0, 0);
    filters->addLayout(ratingRow, 0, 1);
    filters->addWidget(label(tr("Theme"), "infoLabel"), 1, 0);
    filters->addWidget(m_theme, 1, 1);
    filters->addLayout(
        toggleRow(m_progressive, tr("Harder after each solve"), "puzzles/progressive", false), 2, 0,
        1, 2);
    filters->addLayout(
        toggleRow(m_autoNext, tr("Next puzzle when solved"), "puzzles/autoNext", true), 3, 0, 1, 2);
    filters->addLayout(
        toggleRow(m_hideRating, tr("Hide the rating until solved"), "puzzles/hideRating", false), 4,
        0, 1, 2);
    m_progressive->setObjectName(QStringLiteral("puzzleProgressive"));
    m_autoNext->setObjectName(QStringLiteral("puzzleAutoNext"));
    connect(m_hideRating, &Toggle::toggled, this, &PuzzlesView::showDetails);
    filters->setColumnStretch(1, 1);

    m_listTitle = label(QString(), "cardTitle");
    m_list = new QTreeWidget;
    m_list->setObjectName(QStringLiteral("puzzleList"));
    m_list->setColumnCount(3);
    m_list->setHeaderHidden(true);
    m_list->setRootIsDecorated(false);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setUniformRowHeights(true);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    connect(m_list, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        if (!isLichess()) {
            showFilePuzzle(item->data(0, kIndexRole).toInt());
            return;
        }
        // An earlier Lichess puzzle, again (found in the background: a search of the file).
        const QString id = item->data(0, kIndexRole).toString();
        if (!m_db || id.isEmpty()) {
            return;
        }
        const int request = ++m_pickRequest;
        using Found = std::optional<puzzles::Puzzle>;
        // Owned by the view and deleted when done (deleteLater); the analyzer can't see that.
        auto* watcher =
            new QFutureWatcher<Found>(this); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
        connect(watcher, &QFutureWatcher<Found>::finished, this, [this, watcher, request, id] {
            watcher->deleteLater();
            if (const Found found = watcher->result(); found && request == m_pickRequest) {
                show(*found, id.toStdString());
            }
        });
        watcher->setFuture(QtConcurrent::run([db = m_db, id]() -> Found {
            const auto index = db->find(id.toStdString());
            return index ? db->puzzle(*index) : std::nullopt;
        }));
    }); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

    auto* puzzlePage = new QWidget;
    auto* puzzleBox = new QVBoxLayout(puzzlePage);
    puzzleBox->setContentsMargins(0, 0, 0, 0);
    puzzleBox->setSpacing(layout::kCardSpacing);
    puzzleBox->addLayout(promptRow);
    puzzleBox->addLayout(buttons);
    puzzleBox->addSpacing(6);
    puzzleBox->addLayout(tiles);
    puzzleBox->addSpacing(6);
    puzzleBox->addWidget(m_filters);
    puzzleBox->addSpacing(6);
    puzzleBox->addWidget(m_listTitle);
    puzzleBox->addWidget(m_list, 1);

    m_side = new QStackedWidget;
    m_side->addWidget(downloadPage);
    m_side->addWidget(puzzlePage);
    auto* sideCard = card();
    sideCard->setMinimumWidth(360);
    sideCard->setMaximumWidth(440);
    auto* sideBox = new QVBoxLayout(sideCard);
    sideBox->setContentsMargins(layout::kCard);
    sideBox->addWidget(m_side);

    auto* row = new QHBoxLayout;
    row->setSpacing(layout::kPageSpacing);
    row->addWidget(boardCard, 1);
    row->addWidget(sideCard);
    layout->addLayout(row, 1);

    m_tick = new QTimer(this);
    m_tick->setInterval(250);
    connect(m_tick, &QTimer::timeout, this, &PuzzlesView::updateClock);

    connect(&m_lichess, &LichessPuzzles::stateChanged, this, [this] {
        if (m_lichess.state() == LichessPuzzles::State::Ready && isLichess() && m_started &&
            !m_db) {
            openLichess();
        } else {
            showLichessState();
        }
    });
    connect(&m_lichess, &LichessPuzzles::progressChanged, this, &PuzzlesView::showLichessState);
    connect(&m_lichess, &LichessPuzzles::failed, this,
            [this](const QString& message) { notice(this, tr("Lichess Puzzles"), message); });
    connect(&m_stats, &PuzzleStats::loaded, this,
            [this](const QString& source, const PuzzleStats::Summary& summary) {
                if (source == m_source) {
                    m_summary = summary;
                    refreshStats();
                    fillList();
                }
            });
    connect(&m_library, &Library::scanned, this, &PuzzlesView::refreshSources);
    connect(&m_style, &AppStyle::changed, this, &PuzzlesView::fillList);
    connect(&m_style, &AppStyle::changed, this, &PuzzlesView::fitClock);
    connect(&m_style, &AppStyle::changed, this, &PuzzlesView::updateClock);

    // The source of last time, opened when the page is first shown (activate()).
    m_source = QSettings()
                   .value(QStringLiteral("puzzles/source"), QString::fromLatin1(kLichess))
                   .toString();
    if (m_source != QLatin1String(kLichess) && !QFileInfo::exists(m_source)) {
        m_source = QString::fromLatin1(kLichess);
    }
    refreshSources();
    setPrompt(QString(), nullptr);
    updateClock();
    for (QPushButton* button : {m_hint, m_solution, m_analyse}) {
        button->setEnabled(false);
    }
}

PuzzlesView::~PuzzlesView() = default;

bool PuzzlesView::isLichess() const {
    return m_source == QLatin1String(kLichess);
}

void PuzzlesView::setBoardLook(const BoardTheme& theme, const QString& pieceSet) {
    m_board->setTheme(theme);
    m_board->setPieceSet(pieceSet);
}

void PuzzlesView::setBoardOptions(const BoardView::Options& options) {
    m_board->setOptions(options);
}

void PuzzlesView::setRatingSource(std::function<int()> highest) {
    m_highestRating = std::move(highest);
    followRating();
}

void PuzzlesView::followRating() {
    if (!m_highestRating || QSettings().value(QLatin1String(kOwnRange), false).toBool()) {
        return;
    }
    const int rating = m_highestRating();
    if (rating <= 0) {
        return;
    }
    // Centred on your rating, in the steppers' steps of 50.
    const int centre = ((rating + (kProgressiveStep / 2)) / kProgressiveStep) * kProgressiveStep;
    const int low = std::clamp(centre - kRangeAroundRating, kMinRating, kMaxRating);
    const int high = std::clamp(centre + kRangeAroundRating, kMinRating, kMaxRating);
    if (low == m_low->value() && high == m_high->value()) {
        return;
    }
    m_low->setValue(low);
    m_high->setValue(high);
    saveFilters();
    showLichessState();
}

void PuzzlesView::fitClock() {
    // As wide as the widest time it shows, and its digits all equally wide: the clock ticking
    // never moves what's beside it.
    QFont font = m_clock->font();
    font.setFeature(QFont::Tag("tnum"), 1);
    m_clock->setFont(font);
    m_clock->ensurePolished();
    m_clock->setFixedWidth(m_clock->fontMetrics().horizontalAdvance(QStringLiteral("00:00")) + 4);
    // The details always take two lines, however long they are now (hiding the rating makes
    // them shorter): what's below never jumps. Longer ones show whole in the tooltip.
    m_details->ensurePolished();
    m_details->setFixedHeight((2 * m_details->fontMetrics().lineSpacing()) + 2);
}

// ---- Sources -----------------------------------------------------------------------------------

void PuzzlesView::refreshSources() {
    const QSignalBlocker block(m_sources);
    m_sources->clear();
    m_sources->addItem(makeIcon(IconKind::Cloud, m_style.colors().muted, m_style.colors().dimmed),
                       tr("Lichess Puzzles"), QString::fromLatin1(kLichess));
    QStringList files;
    for (const LibraryFile& file : m_library.files()) {
        if (file.pgn && RepertoireStore::kindOf(*file.pgn) == library::FileKind::Puzzle) {
            files << file.path;
        }
    }
    files.sort(Qt::CaseInsensitive);
    for (const QString& path : files) {
        m_sources->addItem(
            makeIcon(IconKind::Puzzle, m_style.colors().muted, m_style.colors().dimmed),
            QFileInfo(path).completeBaseName(), path);
    }
    const int index = m_sources->findData(m_source);
    m_sources->setCurrentIndex(std::max(index, 0));
}

void PuzzlesView::setSource(const QString& source) {
    if (source == m_source) {
        return;
    }
    m_started = true;
    m_source = source;
    QSettings().setValue(QStringLiteral("puzzles/source"), source);
    if (m_sources->findData(source) < 0) {
        refreshSources();
    }
    if (m_sources->findData(source) < 0) { // not a puzzle file of the library (yet)
        m_sources->addItem(
            makeIcon(IconKind::Puzzle, m_style.colors().muted, m_style.colors().dimmed),
            QFileInfo(source).completeBaseName(), source);
    }
    {
        const QSignalBlocker block(m_sources);
        m_sources->setCurrentIndex(m_sources->findData(source));
    }
    ++m_run;
    m_solve.reset();
    m_puzzle.reset();
    m_tick->stop();
    m_board->setInteractive(false);
    m_board->setDrawings({}, {});
    m_filePuzzles.clear();
    m_summary = {};
    refreshStats();
    m_filters->setVisible(isLichess());
    if (isLichess()) {
        openLichess();
    } else {
        m_side->setCurrentIndex(1);
        loadFile(source);
    }
    m_stats.load(source);
}

void PuzzlesView::activate() {
    if (!m_started) {
        m_started = true;
        const QString source = m_source;
        m_source.clear();
        setSource(source);
    }
}

void PuzzlesView::openLichess() {
    if (!m_db) {
        m_db = m_lichess.db();
        if (m_db) {
            // The themes, by their names.
            const QSignalBlocker block(m_theme);
            m_theme->clear();
            m_theme->addItem(tr("All themes"), -1);
            std::vector<std::pair<QString, int>> themes;
            themes.reserve(m_db->themes().size());
            for (std::size_t i = 0; i < m_db->themes().size(); ++i) {
                themes.emplace_back(QString::fromStdString(puzzles::themeName(m_db->themes()[i])),
                                    static_cast<int>(i));
            }
            std::sort(themes.begin(), themes.end());
            for (const auto& [name, id] : themes) {
                m_theme->addItem(name, id);
            }
            const QString saved = QSettings().value(QStringLiteral("puzzles/theme")).toString();
            const int index = saved.isEmpty() ? 0 : m_theme->findText(saved);
            m_theme->setCurrentIndex(std::max(index, 0));
        }
    }
    showLichessState();
    if (m_db && !m_solve) {
        nextPuzzle();
    }
}

void PuzzlesView::showLichessState() {
    if (!isLichess()) {
        return;
    }
    const auto state = m_lichess.state();
    const bool busy =
        state == LichessPuzzles::State::Downloading || state == LichessPuzzles::State::Importing;
    if (state == LichessPuzzles::State::Ready && m_db) {
        m_side->setCurrentIndex(1);
        const QLocale locale;
        m_header->setSubtitle(tr("%1 Lichess puzzles  ·  %2 rated %3–%4")
                                  .arg(locale.toString(static_cast<qulonglong>(m_db->size())),
                                       locale.toString(static_cast<qulonglong>(
                                           m_db->count(m_low->value(), m_high->value()))))
                                  .arg(m_low->value())
                                  .arg(m_high->value()));
        return;
    }
    m_side->setCurrentIndex(0);
    m_header->setSubtitle(QString());
    m_download->setVisible(!busy);
    m_cancel->setVisible(busy);
    m_progress->setVisible(busy);
    m_progressText->setVisible(busy);
    m_progress->setValue(static_cast<int>(m_lichess.fraction() * 1000));
    m_progressText->setText(m_lichess.status());
}

void PuzzlesView::loadFile(const QString& path) {
    m_header->setSubtitle(tr("Reading…"));
    m_listTitle->setText(tr("PUZZLES IN THIS FILE"));
    m_list->clear();
    const int request = ++m_fileRequest;
    using Read = std::vector<FilePuzzle>;
    // Owned by the view and deleted when done (deleteLater); the analyzer can't see that.
    auto* watcher =
        new QFutureWatcher<Read>(this); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    connect(watcher, &QFutureWatcher<Read>::finished, this, [this, watcher, request, path] {
        watcher->deleteLater();
        if (request != m_fileRequest || path != m_source) {
            return;
        }
        m_filePuzzles = watcher->result();
        const auto solvable = std::count_if(m_filePuzzles.begin(), m_filePuzzles.end(),
                                            [](const FilePuzzle& p) { return p.puzzle; });
        if (m_filePuzzles.empty()) {
            m_header->setSubtitle(tr("No puzzles in this file"));
        } else if (solvable == 0) {
            m_header->setSubtitle(tr("No solutions in this file"));
        } else {
            m_header->setSubtitle(solvable == 1 ? tr("1 puzzle") : tr("%1 puzzles").arg(solvable));
        }
        fillList();
        if (solvable == 0) {
            setPrompt(m_filePuzzles.empty() ? tr("No puzzles here.")
                                            : tr("This file has no solutions to check."),
                      nullptr);
            m_details->setText(m_filePuzzles.empty()
                                   ? QString()
                                   : tr("Its positions have no moves. Pick the file with the "
                                        "solutions."));
            return;
        }
        m_fileIndex = -1;
        nextPuzzle();
    });
    watcher->setFuture(QtConcurrent::run([path] {
        Read result;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return result;
        }
        const QByteArray bytes = file.readAll();
        const auto games = pgn::parseAll(
            std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
        if (!games.ok()) {
            return result;
        }
        int index = 0;
        for (const core::Game& game : *games) {
            FilePuzzle entry;
            entry.name = puzzleName(game, index);
            entry.key = game.startPosition().fen() + "|" + entry.name.toStdString();
            entry.puzzle = puzzles::fromGame(game);
            result.push_back(std::move(entry));
            ++index;
        }
        return result;
    }));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

// ---- Puzzles -----------------------------------------------------------------------------------

void PuzzlesView::nextPuzzle() {
    if (m_solve && m_touched && !m_finished) {
        finish(false); // given up: a miss
    }
    if (!isLichess()) {
        if (m_filePuzzles.empty()) {
            return;
        }
        // The next one not yet solved cleanly, after this one; then from the start again.
        const int count = static_cast<int>(m_filePuzzles.size());
        for (int step = 1; step <= count; ++step) {
            const int index = (std::max(m_fileIndex, -1) + step + count) % count;
            const FilePuzzle& entry = m_filePuzzles[static_cast<std::size_t>(index)];
            const auto latest = m_summary.latest.find(entry.key);
            const bool clean = latest != m_summary.latest.end() && latest->second;
            if (entry.puzzle && (!clean || step == count)) {
                showFilePuzzle(index);
                return;
            }
        }
        return;
    }
    if (!m_db) {
        return;
    }
    const int request = ++m_pickRequest;
    const int low = m_low->value();
    const int high = m_high->value();
    const int theme = m_theme->currentData().toInt();
    using Picked = std::optional<puzzles::Puzzle>;
    // Owned by the view and deleted when done (deleteLater); the analyzer can't see that.
    auto* watcher =
        new QFutureWatcher<Picked>(this); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    connect(watcher, &QFutureWatcher<Picked>::finished, this, [this, watcher, request] {
        watcher->deleteLater();
        if (request != m_pickRequest) {
            return;
        }
        const Picked picked = watcher->result();
        if (!picked) {
            m_solve.reset();
            m_board->setInteractive(false);
            setPrompt(tr("No puzzles like that."), nullptr);
            m_details->setText(tr("Try a wider rating range or another theme."));
            return;
        }
        show(*picked, picked->id);
    });
    watcher->setFuture(
        QtConcurrent::run([db = m_db, low, high, theme, seed = m_random()]() -> Picked {
            std::mt19937_64 random(seed);
            // A damaged record is skipped (a few tries).
            for (int attempt = 0; attempt < 8; ++attempt) {
                const auto index = db->pick(low, high, theme, random);
                if (!index) {
                    return std::nullopt;
                }
                if (auto puzzle = db->puzzle(*index)) {
                    return puzzle;
                }
            }
            return std::nullopt;
        }));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

void PuzzlesView::showFilePuzzle(int index) {
    if (index < 0 || std::cmp_greater_equal(index, m_filePuzzles.size())) {
        return;
    }
    const FilePuzzle& entry = m_filePuzzles[static_cast<std::size_t>(index)];
    if (!entry.puzzle) {
        return;
    }
    if (m_solve && m_touched && !m_finished) {
        finish(false);
    }
    m_fileIndex = index;
    show(*entry.puzzle, entry.key);
    if (QTreeWidgetItem* item = m_list->topLevelItem(index)) {
        m_list->setCurrentItem(item);
        m_list->scrollToItem(item);
    }
}

void PuzzlesView::show(const puzzles::Puzzle& puzzle, const std::string& key) {
    const int run = ++m_run;
    m_puzzle = puzzle;
    const puzzles::Solve& solve = m_solve.emplace(puzzle);
    m_key = key;
    m_helped = false;
    m_touched = false;
    m_finished = false;
    m_hintShown.reset();
    m_review.clear();
    m_millis = 0;
    m_clockMillis = 0;
    m_elapsed.invalidate();
    m_tick->stop();
    m_clock->setText(QStringLiteral("0:00"));
    m_board->setFlipped(solve.solver() == core::Color::Black);
    m_board->setDrawings({}, {});
    m_board->setInteractive(false);
    for (QPushButton* button : {m_hint, m_solution, m_analyse}) {
        button->setEnabled(true);
    }
    showDetails();
    setPrompt(tr("%1 to move").arg(sideName(solve.solver())), nullptr);
    if (puzzle.opponentFirst) {
        // The position before the opponent's move, then the move: you see what happened.
        m_board->setPosition(puzzle.game.startPosition());
        QTimer::singleShot(450, this, [this, run] {
            if (run == m_run && m_solve) {
                showNode(m_solve->start(), true);
                startSolving();
            }
        });
    } else {
        showNode(solve.start(), false);
        startSolving();
    }
}

void PuzzlesView::startSolving() {
    m_board->setInteractive(true);
    m_board->setFocus();
    if (!m_clockStopped) {
        m_elapsed.start();
        m_tick->start();
    }
    emit puzzleShown();
}

void PuzzlesView::showNode(core::NodeId id, bool animate) {
    if (!m_solve) {
        return;
    }
    const core::Game& game = m_solve->game();
    const auto lastMove =
        id == core::Game::root() ? std::nullopt : std::optional(game.node(id).move);
    m_board->setPosition(game.positionAt(id), lastMove, animate);
    if (animate) {
        playSound(game, id);
    }
}

void PuzzlesView::playSound(const core::Game& game, core::NodeId id) {
    if (id == core::Game::root()) {
        return;
    }
    const core::Move move = game.node(id).move;
    const core::Position before = game.positionAt(game.parent(id));
    const core::Piece moved = before.pieceAt(move.from);
    const bool captures =
        !before.pieceAt(move.to).isNone() ||
        (moved.type == core::PieceType::Pawn && move.from.file() != move.to.file());
    Sounds::Kind kind = captures ? Sounds::Kind::Capture : Sounds::Kind::Move;
    if (game.positionAt(id).inCheck()) {
        kind = Sounds::Kind::Check;
    }
    m_sounds.play(kind);
}

void PuzzlesView::onMove(core::Move move) {
    if (!m_solve || m_finished || m_solve->awaitingReply()) {
        return;
    }
    puzzles::Solve& solve = *m_solve;
    m_touched = true;
    m_board->setDrawings({}, {});
    using Verdict = puzzles::Solve::Verdict;
    switch (solve.play(move)) {
    case Verdict::Wrong: {
        // Taken back: the move flashes red on its square, then you try again.
        showNode(solve.node(), false);
        m_board->setDrawings({}, {core::SquareMark{move.to, core::MarkColor::Red}});
        setPrompt(tr("Not that move. Try again."), "bad");
        const int run = m_run;
        QTimer::singleShot(700, this, [this, run] {
            if (run == m_run) {
                m_board->setDrawings({}, {});
            }
        });
        break;
    }
    case Verdict::Right: {
        showNode(solve.node(), false);
        playSound(solve.game(), solve.node());
        setPrompt(tr("Right! Keep going."), "good");
        m_board->setInteractive(false);
        const int run = m_run;
        QTimer::singleShot(kReplyDelayMs, this, [this, run] {
            if (run == m_run) {
                playReply();
            }
        });
        break;
    }
    case Verdict::Solved:
        showNode(solve.node(), false);
        playSound(solve.game(), solve.node());
        finish(true);
        break;
    }
}

void PuzzlesView::playReply() {
    if (!m_solve) {
        return;
    }
    puzzles::Solve& solve = *m_solve;
    const core::NodeId reply = solve.reply();
    if (reply != core::kNoNode) {
        showNode(reply, true);
    }
    m_hintShown.reset();
    if (solve.solved()) {
        finish(true);
        return;
    }
    m_board->setInteractive(true);
}

void PuzzlesView::hint() {
    if (!m_solve || m_finished) {
        return;
    }
    const auto expected = m_solve->expected();
    if (!expected) {
        return;
    }
    m_helped = true;
    m_touched = true;
    if (m_hintShown == expected) {
        // Second time: the move itself.
        m_board->setDrawings({core::Arrow{expected->from, expected->to, core::MarkColor::Green}},
                             {});
    } else {
        m_hintShown = expected;
        m_board->setDrawings({}, {core::SquareMark{expected->from, core::MarkColor::Green}});
    }
}

void PuzzlesView::showSolution() {
    if (!m_solve || m_finished) {
        return;
    }
    m_helped = true;
    m_touched = true;
    m_solve->markFailed();
    m_board->setInteractive(false);
    m_board->setDrawings({}, {});
    setPrompt(tr("The solution"), nullptr);
    // Played out, one move at a time.
    const int run = m_run;
    QTimer::singleShot(150, this, [this, run] { solutionStep(run); });
}

void PuzzlesView::solutionStep(int run) {
    if (run != m_run || !m_solve) {
        return;
    }
    puzzles::Solve& solve = *m_solve;
    if (solve.awaitingReply()) {
        showNode(solve.reply(), true);
    } else if (const auto move = solve.expected()) {
        solve.play(*move);
        showNode(solve.node(), true);
    }
    if (solve.solved()) {
        finish(true);
        return;
    }
    QTimer::singleShot(kSolutionStepMs, this, [this, run] { solutionStep(run); });
}

void PuzzlesView::finish(bool solved) {
    if (m_finished || !m_solve) {
        return;
    }
    const puzzles::Solve& solve = *m_solve;
    m_finished = true;
    m_tick->stop();
    m_millis = clockMillis();
    m_elapsed.invalidate();
    updateClock();
    m_board->setInteractive(false);
    const bool clean = solved && !solve.failed() && !m_helped;
    if (clean) {
        setPrompt(tr("Solved!"), "good");
    } else if (solved) {
        setPrompt(m_helped ? tr("Solved, with help.") : tr("Solved, after a miss."), nullptr);
    }
    m_hint->setEnabled(false);
    m_solution->setEnabled(false);
    showDetails(); // the rating, if it was hidden
    // The line, to step through with ← →.
    m_review = solve.game().path(solve.node());
    m_reviewAt = m_review.empty() ? 0 : m_review.size() - 1;

    puzzles::Attempt attempt;
    attempt.source = m_source.toStdString();
    attempt.puzzle = m_key;
    attempt.solved = solved;
    attempt.clean = clean;
    attempt.millis = m_clockStopped ? -1 : m_millis; // stopped: not timed
    attempt.rating = m_puzzle ? m_puzzle->rating : 0;
    attempt.when = QDateTime::currentSecsSinceEpoch();
    m_stats.record(attempt);
    m_stats.load(m_source);

    // Lichess: harder after a clean solve, easier after a miss.
    if (isLichess() && m_progressive->isChecked()) {
        const int shift = clean ? kProgressiveStep : -kProgressiveStep;
        const int width = m_high->value() - m_low->value();
        const int low = std::clamp(m_low->value() + shift, kMinRating, kMaxRating - width);
        m_low->setValue(low);
        m_high->setValue(low + width);
        QSettings().setValue(QLatin1String(kOwnRange), true);
        saveFilters();
        showLichessState();
    }
    if (solved && clean && m_autoNext->isChecked()) {
        const int run = m_run;
        QTimer::singleShot(kNextDelayMs, this, [this, run] {
            if (run == m_run) {
                nextPuzzle();
            }
        });
    }
}

void PuzzlesView::setPrompt(const QString& text, const char* mood) {
    m_prompt->setText(text);
    m_prompt->setProperty("mood", mood != nullptr ? QString::fromLatin1(mood) : QString());
    m_prompt->style()->unpolish(m_prompt);
    m_prompt->style()->polish(m_prompt);
}

void PuzzlesView::showDetails() {
    if (!m_puzzle) {
        m_details->clear();
        return;
    }
    if (!isLichess()) {
        const QString name = m_fileIndex >= 0 && std::cmp_less(m_fileIndex, m_filePuzzles.size())
                                 ? m_filePuzzles[static_cast<std::size_t>(m_fileIndex)].name
                                 : QString();
        // The file's own words before the moves ("Schwarz am Zug!").
        const std::string& comment = m_puzzle->game.node(core::Game::root()).comment;
        m_details->setText(
            comment.empty() ? name : name + QStringLiteral("\n") + QString::fromStdString(comment));
        m_details->setToolTip(m_details->text());
        return;
    }
    QStringList parts;
    if (m_hideRating->isChecked() && !m_finished) {
        parts << tr("Rating hidden");
    } else {
        parts << tr("Rating %1").arg(m_puzzle->rating);
    }
    if (m_finished || !m_hideRating->isChecked()) {
        const QString themes = themesText(m_puzzle->themes);
        if (!themes.isEmpty()) {
            parts << themes;
        }
    }
    m_details->setText(parts.join(QStringLiteral("  ·  ")));
    m_details->setToolTip(m_details->text());
}

qint64 PuzzlesView::clockMillis() const {
    return m_clockMillis + (m_elapsed.isValid() ? m_elapsed.elapsed() : 0);
}

void PuzzlesView::updateClock() {
    m_clock->setText(duration(m_finished ? m_millis : clockMillis()));
    m_clock->setEnabled(!m_clockStopped); // dimmed while stopped
    const auto& c = m_style.colors();
    m_pause->setIcon(
        makeIcon(m_clockStopped ? IconKind::Play : IconKind::Pause, c.text, c.dimmed, 14));
    m_pause->setToolTip(m_clockStopped ? tr("Start the clock") : tr("Stop the clock"));
}

void PuzzlesView::toggleClock() {
    m_clockStopped = !m_clockStopped;
    QSettings().setValue(QStringLiteral("puzzles/clockStopped"), m_clockStopped);
    const bool solving = m_solve && !m_finished;
    if (m_clockStopped) {
        m_clockMillis = clockMillis();
        m_elapsed.invalidate();
        m_tick->stop();
    } else if (solving) {
        m_elapsed.start();
        m_tick->start();
    }
    updateClock();
}

QString PuzzlesView::prompt() const {
    return m_prompt->text();
}

// ---- Stats and the list ------------------------------------------------------------------------

void PuzzlesView::refreshStats() {
    const puzzles::Totals& totals = m_summary.totals;
    if (totals.attempts == 0) {
        for (QLabel* tile : m_tiles) {
            tile->setText(QStringLiteral("–"));
        }
        return;
    }
    const QLocale locale;
    m_tiles[0]->setText(locale.toString(totals.solved));
    m_tiles[1]->setText(QStringLiteral("%1%").arg(
        static_cast<int>(std::lround(100.0 * totals.clean / totals.attempts))));
    m_tiles[2]->setText(totals.bestStreak > totals.streak
                            ? tr("%1 (best %2)").arg(totals.streak).arg(totals.bestStreak)
                            : QString::number(totals.streak));
    m_tiles[3]->setText(duration(totals.averageMillis));
}

void PuzzlesView::fillList() {
    m_list->clear();
    const auto& c = m_style.colors();
    const QIcon right = makeIcon(IconKind::Check, c.good, c.dimmed, 14);
    const QIcon wrong = makeIcon(IconKind::Close, c.bad, c.dimmed, 14);
    if (isLichess()) {
        m_listTitle->setText(tr("YOUR LATEST PUZZLES"));
        for (const puzzles::Attempt& attempt : m_summary.recent) {
            auto* item = new QTreeWidgetItem(m_list);
            item->setIcon(0, attempt.clean ? right : wrong);
            QString result = tr("Missed");
            if (attempt.clean) {
                result = tr("Solved");
            } else if (attempt.solved) {
                result = tr("With help");
            }
            item->setText(0, result);
            item->setData(0, kIndexRole, QString::fromStdString(attempt.puzzle));
            item->setText(1, attempt.rating > 0 ? QString::number(attempt.rating) : QString());
            item->setText(2, duration(attempt.millis));
            item->setForeground(1, c.muted);
            item->setForeground(2, c.muted);
        }
        return;
    }
    m_listTitle->setText(tr("PUZZLES IN THIS FILE"));
    for (std::size_t i = 0; i < m_filePuzzles.size(); ++i) {
        const FilePuzzle& entry = m_filePuzzles[i];
        auto* item = new QTreeWidgetItem(m_list);
        item->setText(0, entry.name);
        item->setToolTip(0, entry.name);
        item->setData(0, kIndexRole, static_cast<int>(i));
        const auto latest = m_summary.latest.find(entry.key);
        if (!entry.puzzle) {
            item->setText(1, tr("no solution"));
            item->setForeground(0, c.dimmed);
        } else if (latest != m_summary.latest.end()) {
            item->setIcon(0, latest->second ? right : wrong);
        }
        item->setForeground(1, c.muted);
    }
    if (m_fileIndex >= 0) {
        if (QTreeWidgetItem* item = m_list->topLevelItem(m_fileIndex)) {
            m_list->setCurrentItem(item);
        }
    }
}

QStringList PuzzlesView::listRows() const {
    QStringList rows;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* item = m_list->topLevelItem(i);
        QStringList parts{item->text(0)};
        for (int column = 1; column < 3; ++column) {
            if (!item->text(column).isEmpty()) {
                parts << item->text(column);
            }
        }
        rows << parts.join(QStringLiteral("  "));
    }
    return rows;
}

void PuzzlesView::saveFilters() const {
    QSettings settings;
    settings.setValue(QStringLiteral("puzzles/low"), m_low->value());
    settings.setValue(QStringLiteral("puzzles/high"), m_high->value());
    settings.setValue(QStringLiteral("puzzles/theme"),
                      m_theme->currentIndex() > 0 ? m_theme->currentText() : QString());
    settings.setValue(QStringLiteral("puzzles/progressive"), m_progressive->isChecked());
    settings.setValue(QStringLiteral("puzzles/autoNext"), m_autoNext->isChecked());
    settings.setValue(QStringLiteral("puzzles/hideRating"), m_hideRating->isChecked());
}

// ---- Keys --------------------------------------------------------------------------------------

void PuzzlesView::stepReview(int plies) {
    if (!m_finished || m_review.empty()) {
        return;
    }
    const auto last = static_cast<long long>(m_review.size()) - 1;
    const long long target = std::clamp(static_cast<long long>(m_reviewAt) + plies, 0LL, last);
    if (std::cmp_equal(target, m_reviewAt)) {
        return;
    }
    const bool forward = target == static_cast<long long>(m_reviewAt) + 1;
    m_reviewAt = static_cast<std::size_t>(target);
    showNode(m_review[m_reviewAt], forward);
}

void PuzzlesView::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Left:
        stepReview(-1);
        return;
    case Qt::Key_Right:
        stepReview(1);
        return;
    case Qt::Key_Up:
    case Qt::Key_Home:
        stepReview(-10000);
        return;
    case Qt::Key_Down:
    case Qt::Key_End:
        stepReview(10000);
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_finished) {
            nextPuzzle();
            return;
        }
        break;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

} // namespace kiebitz::app
