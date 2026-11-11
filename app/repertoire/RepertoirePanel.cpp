#include "RepertoirePanel.hpp"

#include "RepertoireStore.hpp"
#include "kiebitz/repertoire/drill.hpp"
#include "style/AppStyle.hpp"
#include "widgets/Stepper.hpp"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace kiebitz::app {
namespace {

constexpr auto kDepthKey = "practice/depth";

QLabel* label(const QString& text, const char* role) {
    auto* result = new QLabel(text);
    result->setProperty("role", role);
    result->setWordWrap(true);
    return result;
}

/// "7. f3" or "6… Qb6": the move at `ply` of a line, as the move list writes it.
QString moveAt(const std::vector<core::Move>& line, std::size_t ply) {
    core::Position position;
    for (std::size_t k = 0; k < ply; ++k) {
        position.play(line[k]);
    }
    const bool white = position.sideToMove() == core::Color::White;
    return QStringLiteral("%1%2 %3")
        .arg((ply / 2) + 1)
        .arg(white ? QStringLiteral(".") : QStringLiteral("…"))
        .arg(QString::fromStdString(position.san(line[ply])));
}

/// The moves as "Nf3 or d4": the main ones when there are any (alternatives aside).
QString sans(const std::vector<repertoire::Continuation>& moves) {
    const bool anyMain =
        std::any_of(moves.begin(), moves.end(), [](const auto& move) { return move.main; });
    QStringList result;
    for (const auto& move : moves) {
        if (move.main || !anyMain) {
            result << QString::fromStdString(move.san);
        }
    }
    return result.join(QObject::tr(" or "));
}

/// "Philidor Defense" or "Philidor Defense and 2 more": the files, by name.
QString fileNames(const QStringList& paths) {
    if (paths.isEmpty()) {
        return {};
    }
    const QString first = QFileInfo(paths.front()).completeBaseName();
    return paths.size() == 1 ? first
                             : QObject::tr("%1 and %2 more").arg(first).arg(paths.size() - 1);
}

std::vector<core::Move> lineTo(const core::Game& game, core::NodeId id) {
    std::vector<core::Move> line;
    for (const core::NodeId node : game.path(id)) {
        if (node != core::Game::root()) {
            line.push_back(game.node(node).move);
        }
    }
    return line;
}

} // namespace

RepertoirePanel::RepertoirePanel(RepertoireStore& store, const AppStyle& style, QWidget* parent)
    : QWidget(parent), m_store(store), m_style(style) {
    setObjectName(QStringLiteral("repertoirePanel"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 10, 12, 10);
    m_pages = new QStackedWidget;
    outer->addWidget(m_pages);

    // 0: no repertoire to check against yet.
    auto* none = new QWidget;
    auto* noneLayout = new QVBoxLayout(none);
    noneLayout->setContentsMargins(0, 0, 0, 0);
    noneLayout->addWidget(label(tr("No repertoire yet"), "emptyTitle"));
    noneLayout->addWidget(label(tr("Set a file's type to Repertoire (right-click it in the "
                                   "Library), and games are checked against it here."),
                                "emptyHint"));
    noneLayout->addStretch();
    m_pages->addWidget(none);

    // 1: the check, one row per side.
    auto* check = new QWidget;
    auto* checkLayout = new QVBoxLayout(check);
    checkLayout->setContentsMargins(0, 0, 0, 0);
    checkLayout->setSpacing(14);
    for (const core::Color side : {core::Color::White, core::Color::Black}) {
        Row& row = m_rows[side == core::Color::White ? 0 : 1];
        row.widget = new QWidget;
        auto* box = new QVBoxLayout(row.widget);
        box->setContentsMargins(0, 0, 0, 0);
        box->setSpacing(6);
        row.title = label(QString(), "cardTitle");
        box->addWidget(row.title);
        row.status = label(QString(), "pvLine");
        box->addWidget(row.status);
        auto* buttons = new QHBoxLayout;
        buttons->setSpacing(6);
        row.jump = new QPushButton(tr("Jump to Line"));
        row.jump->setObjectName(side == core::Color::White ? QStringLiteral("jumpWhite")
                                                           : QStringLiteral("jumpBlack"));
        row.jump->setProperty("role", "secondary");
        row.jump->setFocusPolicy(Qt::NoFocus);
        row.jump->setToolTip(tr("Open the repertoire file at this position"));
        connect(row.jump, &QPushButton::clicked, this, [this, side] {
            const Row& r = m_rows[side == core::Color::White ? 0 : 1];
            if (!r.file.isEmpty()) {
                emit jumpRequested(r.file, r.jumpTo);
            }
        });
        buttons->addWidget(row.jump);
        row.add = new QPushButton(tr("Add Line"));
        row.add->setProperty("role", "secondary");
        row.add->setFocusPolicy(Qt::NoFocus);
        row.add->setToolTip(tr("Add this game's moves up to here to the repertoire"));
        connect(row.add, &QPushButton::clicked, this,
                [this, side] { emit addLineRequested(side); });
        buttons->addWidget(row.add);
        buttons->addStretch();
        box->addLayout(buttons);
        checkLayout->addWidget(row.widget);
    }
    checkLayout->addStretch();
    m_pages->addWidget(check);

    // 2: practice, for a repertoire file.
    auto* practice = new QWidget;
    auto* practiceLayout = new QVBoxLayout(practice);
    practiceLayout->setContentsMargins(0, 0, 0, 0);
    practiceLayout->setSpacing(10);
    practiceLayout->addWidget(label(tr("PRACTICE"), "cardTitle"));
    m_practiceCount = label(QString(), "pvLine");
    m_practiceCount->setObjectName(QStringLiteral("practiceCount"));
    practiceLayout->addWidget(m_practiceCount);
    auto* depthRow = new QHBoxLayout;
    depthRow->setSpacing(10);
    depthRow->addWidget(label(tr("Up to move"), "engineStatus"));
    m_depth = new Stepper;
    m_depth->setObjectName(QStringLiteral("practiceDepth"));
    m_depth->setRange(1, 99);
    m_depth->setValue(QSettings().value(QString::fromLatin1(kDepthKey), 15).toInt());
    connect(m_depth, &Stepper::valueChanged, this, [this](int value) {
        QSettings().setValue(QString::fromLatin1(kDepthKey), value);
        refreshPractice();
    });
    depthRow->addWidget(m_depth);
    depthRow->addStretch();
    practiceLayout->addLayout(depthRow);
    auto* practiceButtons = new QHBoxLayout;
    practiceButtons->setSpacing(6);
    m_practiceAll = new QPushButton;
    m_practiceAll->setObjectName(QStringLiteral("practiceAll"));
    m_practiceAll->setProperty("role", "primary");
    m_practiceAll->setFocusPolicy(Qt::NoFocus);
    connect(m_practiceAll, &QPushButton::clicked, this,
            [this] { emit practiceRequested(false, practiceDepth()); });
    m_practiceHere = new QPushButton;
    m_practiceHere->setObjectName(QStringLiteral("practiceHere"));
    m_practiceHere->setProperty("role", "secondary");
    m_practiceHere->setFocusPolicy(Qt::NoFocus);
    connect(m_practiceHere, &QPushButton::clicked, this,
            [this] { emit practiceRequested(true, practiceDepth()); });
    practiceButtons->addWidget(m_practiceAll);
    practiceButtons->addWidget(m_practiceHere);
    practiceButtons->addStretch();
    practiceLayout->addLayout(practiceButtons);
    practiceLayout->addStretch();
    m_pages->addWidget(practice);

    connect(&m_store, &RepertoireStore::changed, this, &RepertoirePanel::refresh);
    refresh();
}

void RepertoirePanel::setGame(const core::Game* game, core::NodeId current, bool repertoireFile,
                              core::Color side) {
    m_game = game;
    m_current = current;
    m_repertoireFile = repertoireFile;
    m_side = side;
    refresh();
}

void RepertoirePanel::setViewSide(core::Color side) {
    if (side != m_viewSide) {
        m_viewSide = side;
        refresh();
    }
}

int RepertoirePanel::practiceDepth() const {
    return m_depth->value();
}

QString RepertoirePanel::statusText(core::Color side) const {
    const Row& row = m_rows[side == core::Color::White ? 0 : 1];
    return row.widget->isVisibleTo(this) ? row.status->text() : QString();
}

QString RepertoirePanel::practiceText() const {
    return m_practiceCount->text();
}

void RepertoirePanel::refresh() {
    if (m_repertoireFile && m_game != nullptr) {
        m_pages->setCurrentIndex(2);
        refreshPractice();
        return;
    }
    const bool any = !m_store.files(core::Color::White).isEmpty() ||
                     !m_store.files(core::Color::Black).isEmpty();
    m_pages->setCurrentIndex(any ? 1 : 0);
    refreshCheck();
}

void RepertoirePanel::refreshPractice() {
    if (m_game == nullptr) {
        return;
    }
    const repertoire::Drill all(*m_game, m_side, core::Game::root(), practiceDepth(), 0);
    const repertoire::Drill here(*m_game, m_side, m_current, practiceDepth(), 0);
    const QLocale locale;
    m_practiceCount->setText(
        all.trainable() == 1 ? tr("1 position to practice")
                             : tr("%1 positions to practice")
                                   .arg(locale.toString(static_cast<qulonglong>(all.trainable()))));
    m_practiceAll->setText(tr("Practice Whole File"));
    m_practiceAll->setEnabled(all.trainable() > 0);
    m_practiceHere->setText(tr("Practice From Here (%1)")
                                .arg(locale.toString(static_cast<qulonglong>(here.trainable()))));
    m_practiceHere->setEnabled(here.trainable() > 0 && m_current != core::Game::root());
}

void RepertoirePanel::refreshCheck() {
    const bool fromStart =
        m_game != nullptr && m_game->startPosition().hash() == core::Position().hash();
    const std::vector<core::Move> line =
        m_game != nullptr ? lineTo(*m_game, m_current) : std::vector<core::Move>{};
    // The whole line of the game through this move (on along its main line), to find the file
    // that follows it the longest: that one stays named while you go through the game.
    std::vector<core::Move> whole = line;
    if (m_game != nullptr) {
        for (core::NodeId node = m_game->mainChild(m_current); node != core::kNoNode;
             node = m_game->mainChild(node)) {
            whole.push_back(m_game->node(node).move);
        }
    }
    for (const core::Color side : {core::Color::White, core::Color::Black}) {
        Row& row = m_rows[side == core::Color::White ? 0 : 1];
        const QStringList& files = m_store.files(side);
        // Only the repertoire of the side the board is viewed from.
        const bool shown = side == m_viewSide && !files.isEmpty();
        row.widget->setVisible(shown);
        if (!shown) {
            continue;
        }
        row.title->setText(
            (side == core::Color::White ? tr("WHITE REPERTOIRE") : tr("BLACK REPERTOIRE")) +
            QStringLiteral("  ·  ") +
            (files.size() == 1 ? tr("1 file") : tr("%1 files").arg(files.size())));
        row.add->setEnabled(false);
        row.jump->setEnabled(false);
        const BuiltRepertoire* built = m_store.get(side);
        if (built == nullptr) {
            row.status->setText(tr("Reading the repertoire…"));
            continue;
        }
        if (m_game == nullptr || !fromStart) {
            row.status->setText(m_game == nullptr ? QString()
                                                  : tr("This game starts from a set-up position."));
            continue;
        }
        // The positions along the whole line, as far as the repertoire has them.
        const repertoire::Book wholeBook = built->tree.follow(core::Position(), whole);
        std::vector<std::uint64_t> hashes{core::Position().hash()};
        {
            core::Position position;
            for (std::size_t k = 0; std::cmp_less(k, wholeBook.plies); ++k) {
                position.play(whole[k]);
                hashes.push_back(position.hash());
            }
        }
        // The file with the deepest of them; the one named before if it is as deep.
        QString file;
        for (std::size_t k = hashes.size(); k-- > 0 && file.isEmpty();) {
            const QStringList with = m_store.filesWith(side, hashes[k]);
            if (!with.isEmpty()) {
                file = with.contains(row.file) ? row.file : with.front();
            }
        }
        row.file = file;

        const repertoire::Book book = built->tree.follow(core::Position(), line);
        const auto at = static_cast<std::size_t>(book.plies);
        row.add->setEnabled(at < line.size());
        // Jump to the deepest position up to this move that the file has.
        row.jumpTo = 0;
        for (std::size_t k = std::min(at, hashes.size() - 1) + 1; k-- > 0;) {
            if (m_store.filesWith(side, hashes[k]).contains(file)) {
                row.jumpTo = hashes[k];
                break;
            }
        }
        row.jump->setEnabled(!file.isEmpty() && row.jumpTo != 0);
        const QString where = fileNames(file.isEmpty() ? QStringList{} : QStringList{file});
        core::Position position;
        for (std::size_t k = 0; k < at; ++k) {
            position.play(line[k]);
        }
        if (!book.left) {
            const repertoire::Entry* entry = built->tree.find(position.hash());
            QString text = tr("In book · %1").arg(where);
            if (entry != nullptr && built->tree.isOurMove(*entry) && !entry->moves.empty()) {
                text += tr(" · you play %1").arg(sans(entry->moves));
            }
            row.status->setText(text);
        } else if (book.byUs && !book.expected.empty()) {
            row.status->setText(tr("Left the book with %1 · %2 plays %3")
                                    .arg(moveAt(line, at), where, sans(book.expected)));
        } else if (!book.expected.empty()) {
            row.status->setText(tr("%1 isn't covered yet · %2").arg(moveAt(line, at), where));
        } else if (at > 0) {
            row.status->setText(
                tr("The repertoire ends after %1 · %2").arg(moveAt(line, at - 1), where));
        } else {
            row.status->setText(tr("Not in your repertoire"));
        }
    }
}

} // namespace kiebitz::app
