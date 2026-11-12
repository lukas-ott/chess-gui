#include "LibraryView.hpp"

#include "Library.hpp"
#include "NewFileDialog.hpp"
#include "board/BoardView.hpp"
#include "document/PgnIo.hpp"
#include "kiebitz/gamedb/database.hpp"
#include "kiebitz/repertoire/gaps.hpp"
#include "kiebitz/repertoire/summary.hpp"
#include "repertoire/RepertoireStore.hpp"
#include "style/AppStyle.hpp"
#include "style/Icons.hpp"
#include "style/Layout.hpp"
#include "style/PageHeader.hpp"
#include "widgets/DateRange.hpp"
#include "widgets/ElidedLabel.hpp"
#include "widgets/EmptyState.hpp"
#include "widgets/Message.hpp"
#include "widgets/SortCycle.hpp"

#include <QAbstractTableModel>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTableView>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <map>
#include <span>
#include <utility>

namespace kiebitz::app {

namespace {

using library::FileKind;

// Tree item data
constexpr int kKindRole = Qt::UserRole;         ///< kFolder or kFile
constexpr int kKeyRole = Qt::UserRole + 1;      ///< folder: relative path; file: absolute path
constexpr int kIndexRole = Qt::UserRole + 2;    ///< file: index into Library::files()
constexpr int kFileKindRole = Qt::UserRole + 3; ///< file: its library::FileKind
constexpr int kSideRole = Qt::UserRole + 4;     ///< file: the side a repertoire is for (0 = White)
constexpr int kNameRole = Qt::UserRole + 5;     ///< the name on disk (to notice a rename)
constexpr int kFolder = 1;
constexpr int kFile = 2;

// The selection card's pages
constexpr int kNothing = 0;
constexpr int kFolderPage = 1;
constexpr int kGamesPage = 2;
constexpr int kRepertoirePage = 3;

/// Each type of file has its own icon (Kiebitz's own line icons).
IconKind iconOf(FileKind kind) {
    switch (kind) {
    case FileKind::Repertoire:
        return IconKind::Branch;
    case FileKind::Tournament:
        return IconKind::Trophy;
    case FileKind::Puzzle:
        return IconKind::Puzzle;
    case FileKind::Other:
        return IconKind::File;
    case FileKind::Game:
        break;
    }
    return IconKind::Board;
}

QString typeName(FileKind kind, core::Color side) {
    switch (kind) {
    case FileKind::Repertoire:
        return side == core::Color::White ? QObject::tr("Repertoire for White")
                                          : QObject::tr("Repertoire for Black");
    case FileKind::Tournament:
        return QObject::tr("Tournament");
    case FileKind::Puzzle:
        return QObject::tr("Puzzles");
    case FileKind::Other:
        return QObject::tr("Other");
    case FileKind::Game:
        break;
    }
    return QObject::tr("Games");
}

constexpr auto kExpandedKey = "library/expandedFolders";
constexpr auto kFoldingSavedKey = "library/foldingSaved"; // an empty list can't say "saved"

QString toQString(std::string_view text) {
    const auto value = QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    return value == u"?" || value == u"????.??.??" ? QString() : value;
}

/// "Sicilian Defense: Najdorf Variation" from a game's headers, else its ECO code.
/// Whether any game of a file has `text` in its players, event, site, opening or annotator.
bool contains(const std::shared_ptr<PgnFile>& file, const QString& text) {
    if (!file) {
        return false;
    }
    const QByteArray needle = text.toUtf8();
    const std::string_view wanted(needle.constData(), static_cast<std::size_t>(needle.size()));
    const auto has = [&](std::string_view value) {
        // Case-insensitive for ASCII; other letters must match as typed.
        return std::search(value.begin(), value.end(), wanted.begin(), wanted.end(),
                           [](char a, char b) {
                               return std::tolower(static_cast<unsigned char>(a)) ==
                                      std::tolower(static_cast<unsigned char>(b));
                           }) != value.end();
    };
    static constexpr std::array<const char*, 8> kHeaders{
        "White", "Black", "Event", "Site", "Opening", "Variation", "ECO", "Annotator"};
    return std::any_of(file->games.begin(), file->games.end(), [&](const pgn::GameEntry& game) {
        return std::any_of(kHeaders.begin(), kHeaders.end(),
                           [&](const char* name) { return has(game.header(name)); });
    });
}

QString openingOf(const pgn::GameEntry& game) {
    QString opening = toQString(game.header("Opening"));
    if (const QString variation = toQString(game.header("Variation")); !variation.isEmpty()) {
        opening += (opening.isEmpty() ? QString() : QStringLiteral(": ")) + variation;
    }
    return opening.isEmpty() ? toQString(game.header("ECO")) : opening;
}

QString gamesText(int count) {
    return count == 1 ? QObject::tr("1 game") : QObject::tr("%1 games").arg(count);
}

QString number(std::size_t value) {
    return QLocale().toString(static_cast<qulonglong>(value));
}

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

} // namespace

namespace {

/// The file tree. Files and folders can be dragged onto a folder (or onto a file, meaning its
/// folder, or onto empty space, meaning the top); the move itself happens on disk, and the
/// library's rescan shows it, so the tree never shows a move that didn't work.
class FileTree : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;
    /// Called on a drop: the dragged item and the target folder item (null = the top).
    std::function<void(QTreeWidgetItem* dragged, QTreeWidgetItem* folder)> onDrop;

protected:
    /// The space before a row: only a fold arrow (the app's chevron), never part of the row's
    /// highlight, so a selected row is one rounded shape.
    void drawBranches(QPainter* painter, const QRect& rect,
                      const QModelIndex& index) const override {
        // The card's colour (the tree's own background is see-through).
        painter->fillRect(rect, QApplication::palette().color(QPalette::Base));
        const QTreeWidgetItem* item = itemFromIndex(index);
        if (item == nullptr || item->childCount() == 0) {
            return;
        }
        const QRectF box(rect.right() - indentation() + 1, rect.top(), indentation(),
                         rect.height());
        const QPointF c = box.center();
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(palette().color(QPalette::PlaceholderText), 1.4, Qt::SolidLine,
                             Qt::RoundCap, Qt::RoundJoin));
        QPainterPath chevron;
        if (item->isExpanded()) { // pointing down
            chevron.moveTo(c.x() - 3.5, c.y() - 1.5);
            chevron.lineTo(c.x(), c.y() + 2.0);
            chevron.lineTo(c.x() + 3.5, c.y() - 1.5);
        } else { // pointing right
            chevron.moveTo(c.x() - 1.5, c.y() - 3.5);
            chevron.lineTo(c.x() + 2.0, c.y());
            chevron.lineTo(c.x() - 1.5, c.y() + 3.5);
        }
        painter->drawPath(chevron);
        painter->restore();
    }

    void dropEvent(QDropEvent* event) override {
        QTreeWidgetItem* dragged = currentItem();
        QTreeWidgetItem* target = itemAt(event->position().toPoint());
        if (target != nullptr && target->data(0, kKindRole).toInt() != kFolder) {
            target = target->parent();
        }
        event->setDropAction(Qt::IgnoreAction); // the tree is rebuilt from the disk
        event->accept();
        if (dragged != nullptr && onDrop) {
            onDrop(dragged, target);
        }
    }
};

} // namespace

/// One file's games as a table whose columns fit the file's type:
///   games and other files: #, White, Black, Result, Date, Opening, Event;
///   a tournament: #, Round, White, Black, Result, Date, Opening;
///   puzzles: #, Puzzle, To move, Date.
/// Reads straight from the file's index, so thousands of games show at once.
constexpr int kDateRole = Qt::UserRole + 10; ///< a game row: its PGN date, any column

class GameListModel : public QAbstractTableModel {
public:
    enum class Column : std::uint8_t {
        Number,
        White,
        Black,
        Result,
        Date,
        Opening,
        Event,
        Round,
        Name,
        ToMove
    };

    using QAbstractTableModel::QAbstractTableModel;

    void setFile(std::shared_ptr<PgnFile> file, FileKind kind) {
        beginResetModel();
        m_file = std::move(file);
        switch (kind) {
        case FileKind::Tournament:
            m_columns = {Column::Number, Column::Round, Column::White,  Column::Black,
                         Column::Result, Column::Date,  Column::Opening};
            break;
        case FileKind::Puzzle:
            m_columns = {Column::Number, Column::Name, Column::ToMove, Column::Date};
            break;
        case FileKind::Game:
        case FileKind::Repertoire:
        case FileKind::Other:
            m_columns = {Column::Number, Column::White,   Column::Black, Column::Result,
                         Column::Date,   Column::Opening, Column::Event};
            break;
        }
        endResetModel();
    }
    [[nodiscard]] const std::shared_ptr<PgnFile>& file() const { return m_file; }
    [[nodiscard]] Column column(int section) const {
        return m_columns[static_cast<std::size_t>(section)];
    }
    /// Columns that take the spare width (names); the others fit their content.
    [[nodiscard]] bool stretches(int section) const {
        const Column c = column(section);
        return c == Column::White || c == Column::Black || c == Column::Opening ||
               c == Column::Event || c == Column::Name;
    }

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() || !m_file ? 0 : static_cast<int>(m_file->games.size());
    }
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : static_cast<int>(m_columns.size());
    }
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override {
        if (!m_file || index.row() < 0 || index.row() >= rowCount()) {
            return {};
        }
        const Column c = column(index.column());
        if (role == Qt::TextAlignmentRole &&
            (c == Column::Number || c == Column::Result || c == Column::Round)) {
            return QVariant::fromValue(Qt::AlignCenter);
        }
        if (role == kDateRole) { // for the date range, whichever the column
            return toQString(m_file->games[static_cast<std::size_t>(index.row())].header("Date"));
        }
        if (role != Qt::DisplayRole && role != Qt::ToolTipRole) {
            return {};
        }
        const auto& game = m_file->games[static_cast<std::size_t>(index.row())];
        switch (c) {
        case Column::Number:
            return index.row() + 1;
        case Column::White:
            // Opening files and studies often name no players: a row is never blank.
            if (toQString(game.header("White")).isEmpty() &&
                toQString(game.header("Black")).isEmpty()) {
                return fallbackName(index.row());
            }
            return toQString(game.header("White"));
        case Column::Black:
            return toQString(game.header("Black"));
        case Column::Result:
            return toQString(game.header("Result"));
        case Column::Date:
            return toQString(game.header("Date"));
        case Column::Opening:
            return openingOf(game);
        case Column::Event:
            return toQString(game.header("Event"));
        case Column::Round:
            return toQString(game.header("Round"));
        case Column::Name: {
            const QString white = toQString(game.header("White"));
            const QString black = toQString(game.header("Black"));
            if (white.isEmpty() && black.isEmpty()) {
                return fallbackName(index.row());
            }
            return white + QStringLiteral(" – ") + black;
        }
        case Column::ToMove: {
            // The side to move is the second field of the FEN; without one, White starts.
            const auto fields = toQString(game.header("FEN")).split(QLatin1Char(' '));
            return fields.size() > 1 && fields[1] == u"b" ? QObject::tr("Black")
                                                          : QObject::tr("White");
        }
        }
        return {};
    }
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role) const override {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
            return {};
        }
        switch (column(section)) {
        case Column::Number:
            return QStringLiteral("#");
        case Column::White:
            return QObject::tr("WHITE");
        case Column::Black:
            return QObject::tr("BLACK");
        case Column::Result:
            return QObject::tr("RESULT");
        case Column::Date:
            return QObject::tr("DATE");
        case Column::Opening:
            return QObject::tr("OPENING");
        case Column::Event:
            return QObject::tr("EVENT");
        case Column::Round:
            return QObject::tr("ROUND");
        case Column::Name:
            return QObject::tr("PUZZLE");
        case Column::ToMove:
            return QObject::tr("TO MOVE");
        }
        return {};
    }

    /// A game without players: its event, the file's name for a one-game file, or "Game 3".
    [[nodiscard]] QString fallbackName(int row) const {
        const auto& game = m_file->games[static_cast<std::size_t>(row)];
        if (const QString event = toQString(game.header("Event")); !event.isEmpty()) {
            return event;
        }
        return m_file->games.size() == 1 ? QFileInfo(m_file->path).completeBaseName()
                                         : QObject::tr("Game %1").arg(row + 1);
    }

private:
    std::shared_ptr<PgnFile> m_file;
    std::vector<Column> m_columns;
};

/// The games shown: those with the search text in any column, in the date range, in the order
/// of the column clicked.
class GameFilter : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setDates(std::string from, std::string to) {
        beginFilterChange();
        m_from = std::move(from);
        m_to = std::move(to);
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
    }

protected:
    [[nodiscard]] bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        if (!QSortFilterProxyModel::filterAcceptsRow(row, parent)) {
            return false;
        }
        if (m_from.empty() && m_to.empty()) {
            return true;
        }
        const QString date = sourceModel()->index(row, 0, parent).data(kDateRole).toString();
        return gamedb::dateInRange(date.toStdString(), m_from, m_to);
    }

private:
    std::string m_from;
    std::string m_to;
};

LibraryView::LibraryView(Library& library, RepertoireStore& store, const AppStyle& style,
                         QWidget* parent)
    : QWidget(parent), m_library(library), m_store(store), m_style(style),
      m_model(new GameListModel(this)), m_filter(new GameFilter(this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(layout::kPage);
    layout->setSpacing(layout::kPageSpacing);

    // ---- Header: title, counts, folder button --------------------------------------------------
    m_header = new PageHeader(tr("Library"));
    m_folderButton = new QPushButton;
    m_folderButton->setObjectName(QStringLiteral("libraryFolder"));
    m_folderButton->setProperty("role", "secondary");
    m_folderButton->setToolTip(
        tr("Your library folder. Click to look inside or choose another one."));
    connect(m_folderButton, &QPushButton::clicked, this, &LibraryView::changeFolderRequested);
    m_header->addAction(m_folderButton);
    layout->addWidget(m_header);

    // ---- Files: a folder tree, a search and a type filter --------------------------------------
    auto* tree = new FileTree;
    tree->onDrop = [this](QTreeWidgetItem* dragged, QTreeWidgetItem* folder) {
        const QString key = dragged->data(0, kKeyRole).toString();
        const QString path = dragged->data(0, kKindRole).toInt() == kFile
                                 ? key
                                 : QDir(m_library.folder()).filePath(key);
        const QString error =
            moveToFolder({path}, folder != nullptr ? folder->data(0, kKeyRole).toString() : "");
        if (!error.isEmpty()) {
            notice(this, tr("Couldn't move"), error);
        }
    };
    m_files = tree;
    m_files->setObjectName(QStringLiteral("libraryFiles"));
    m_files->setDragEnabled(true);
    m_files->setAcceptDrops(true);
    m_files->setDropIndicatorShown(true);
    m_files->setDragDropMode(QAbstractItemView::InternalMove);
    m_files->setEditTriggers(QAbstractItemView::EditKeyPressed); // F2, or Rename in the menu
    connect(m_files, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (m_rebuilding || column != 0) {
            return;
        }
        const QString name = item->text(0).trimmed();
        const QString before = item->data(0, kNameRole).toString();
        if (name == before) {
            return;
        }
        const QString path =
            item->data(0, kKindRole).toInt() == kFile
                ? item->data(0, kKeyRole).toString()
                : QDir(m_library.folder()).filePath(item->data(0, kKeyRole).toString());
        if (const QString error = rename(path, name); !error.isEmpty()) {
            const QSignalBlocker block(m_files);
            item->setText(0, before);
            notice(this, tr("Couldn't rename"), error);
        }
    });
    m_files->setColumnCount(1);
    m_files->setHeaderHidden(true);
    m_files->setFrameShape(QFrame::NoFrame);
    m_files->setIndentation(14);
    m_files->setUniformRowHeights(true);
    m_files->setAnimated(true);
    m_files->setTextElideMode(Qt::ElideRight);
    m_files->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_files->installEventFilter(this);
    connect(m_files, &QTreeWidget::currentItemChanged, this, [this] {
        if (!m_rebuilding) {
            showSelection();
        }
    });
    connect(m_files, &QTreeWidget::itemExpanded, this, &LibraryView::rememberExpanded);
    connect(m_files, &QTreeWidget::itemCollapsed, this, &LibraryView::rememberExpanded);
    // Double-click a file: open it (a folder folds, as usual).
    connect(m_files, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        if (const auto file = fileOf(item)) {
            emit openGameRequested(file, 0);
        }
    });
    // Right-click a folder or file to set its type.
    m_files->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_files, &QWidget::customContextMenuRequested, this, [this](QPoint at) {
        QTreeWidgetItem* item = m_files->itemAt(at);
        if (item == nullptr) {
            return;
        }
        // A file, or every file in a folder (one file per opening is the usual way).
        QStringList paths;
        const std::function<void(QTreeWidgetItem*)> collect = [&](QTreeWidgetItem* node) {
            if (node->data(0, kKindRole).toInt() == kFile) {
                paths << node->data(0, kKeyRole).toString();
            }
            for (int i = 0; i < node->childCount(); ++i) {
                collect(node->child(i));
            }
        };
        collect(item);
        if (paths.isEmpty()) {
            return;
        }
        QMenu menu(this);
        const bool isFile = item->data(0, kKindRole).toInt() == kFile;
        const QString itemPath =
            isFile ? item->data(0, kKeyRole).toString()
                   : QDir(m_library.folder()).filePath(item->data(0, kKeyRole).toString());
        if (const auto file = fileOf(item)) {
            menu.addAction(tr("Open"), this, [this, file] { emit openGameRequested(file, 0); });
            menu.addSeparator();
        }
        menu.addAction(tr("Rename"), this, [this, item] { m_files->editItem(item, 0); });
        menu.addAction(tr("Show in Finder"), this, [itemPath] {
            QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                                    {QStringLiteral("-R"), itemPath});
        });
        menu.addAction(tr("Move to Trash"), this, [this, itemPath] { askTrash(itemPath); });
        menu.addSeparator();
        QMenu* type = menu.addMenu(tr("File Type"));
        const int current =
            item->data(0, kKindRole).toInt() == kFile ? item->data(0, kFileKindRole).toInt() : -1;
        const int currentSide = item->data(0, kSideRole).toInt();
        const auto add = [&](const QString& text, FileKind kind, int side) {
            auto* action = type->addAction(text, this, [this, paths, kind, side] {
                emit fileKindRequested(paths, static_cast<int>(kind), side);
            });
            action->setCheckable(true);
            action->setChecked(current == static_cast<int>(kind) &&
                               (side < 0 || side == currentSide));
        };
        add(tr("Game"), FileKind::Game, -1);
        add(tr("Repertoire for White"), FileKind::Repertoire, 0);
        add(tr("Repertoire for Black"), FileKind::Repertoire, 1);
        add(tr("Tournament"), FileKind::Tournament, -1);
        add(tr("Puzzles"), FileKind::Puzzle, -1);
        add(tr("Other"), FileKind::Other, -1);
        menu.exec(m_files->viewport()->mapToGlobal(at));
    });

    m_fileSearch = new QLineEdit;
    m_fileSearch->setObjectName(QStringLiteral("libraryFileSearch"));
    m_fileSearch->setPlaceholderText(tr("Search files and games"));
    m_fileSearch->setClearButtonEnabled(true);
    // A short pause after typing before searching through every file's games.
    auto* searchDelay = new QTimer(this);
    searchDelay->setSingleShot(true);
    searchDelay->setInterval(150);
    connect(m_fileSearch, &QLineEdit::textChanged, searchDelay, qOverload<>(&QTimer::start));
    connect(searchDelay, &QTimer::timeout, this, &LibraryView::filtersChanged);
    m_typeFilter = new QComboBox;
    m_typeFilter->setObjectName(QStringLiteral("libraryTypeFilter"));
    m_typeFilter->setFocusPolicy(Qt::NoFocus);
    m_typeFilter->addItem(tr("All types"), -1);
    m_typeFilter->addItem(tr("Games"), static_cast<int>(FileKind::Game));
    m_typeFilter->addItem(tr("Repertoires"), static_cast<int>(FileKind::Repertoire));
    m_typeFilter->addItem(tr("Tournaments"), static_cast<int>(FileKind::Tournament));
    m_typeFilter->addItem(tr("Puzzles"), static_cast<int>(FileKind::Puzzle));
    m_typeFilter->addItem(tr("Other"), static_cast<int>(FileKind::Other));
    connect(m_typeFilter, &QComboBox::currentIndexChanged, this, &LibraryView::filtersChanged);

    auto* filesCard = card();
    m_cards[0] = filesCard;
    auto* filesBox = new QVBoxLayout(filesCard);
    filesBox->setContentsMargins(layout::kCard);
    filesBox->setSpacing(layout::kCardSpacing);
    // New File and New Folder go into the selected folder (or the selected file's).
    const std::array<std::pair<const char*, QString>, 2> newButtons{
        {{"libraryNewFile", tr("New File… (⌘N)")}, {"libraryNewFolder", tr("New Folder…")}}};
    for (std::size_t i = 0; i < newButtons.size(); ++i) {
        auto* button = new QPushButton;
        button->setObjectName(QString::fromLatin1(newButtons[i].first));
        button->setProperty("role", "secondary");
        button->setToolTip(newButtons[i].second);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedWidth(layout::kControlHeight);
        m_newButtons[i] = button;
    }
    connect(m_newButtons[0], &QPushButton::clicked, this,
            [this] { emit newFileRequested(QDir(m_library.folder()).filePath(targetFolder())); });
    connect(m_newButtons[1], &QPushButton::clicked, this, &LibraryView::askNewFolder);
    auto* filters = new QHBoxLayout;
    filters->setSpacing(6);
    filters->addWidget(m_fileSearch, 1);
    filters->addWidget(m_typeFilter);
    filters->addWidget(m_newButtons[0]);
    filters->addWidget(m_newButtons[1]);
    filesBox->addLayout(filters);
    filesBox->addWidget(m_files, 1);

    // ---- The selection -------------------------------------------------------------------------
    m_detail = new QStackedWidget;
    m_detail->setObjectName(QStringLiteral("libraryDetail"));
    m_detail->addWidget(new EmptyState(IconKind::File, tr("No file selected"), {}, m_style));
    m_folderState = new EmptyState(IconKind::Folder, {}, {}, m_style);
    m_detail->addWidget(m_folderState);

    // Games: a title row, the table, the preview under it.
    m_gamesIcon = new QLabel;
    m_gamesIcon->setFixedSize(22, 22);
    m_listTitle = new ElidedLabel;
    m_listTitle->setObjectName(QStringLiteral("libraryListTitle"));
    m_listTitle->setProperty("role", "sectionTitle");
    m_listTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_listCount = label(QString(), "emptyHint");
    m_listCount->setObjectName(QStringLiteral("libraryListCount"));
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("librarySearchGames"));
    m_search->setPlaceholderText(tr("Search players, events, openings"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(200);
    m_search->setMaximumWidth(280);
    m_filter->setSourceModel(m_model);
    m_filter->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_filter->setFilterKeyColumn(-1); // any column
    connect(m_search, &QLineEdit::textChanged, m_filter,
            &QSortFilterProxyModel::setFilterFixedString);
    m_dates = new DateRange(QStringLiteral("libraryDate"), m_style);
    connect(m_dates, &DateRange::changed, this,
            [this] { m_filter->setDates(m_dates->from(), m_dates->to()); });

    m_games = new QTableView;
    m_games->setObjectName(QStringLiteral("libraryGames"));
    m_games->setModel(m_filter);
    m_games->setFrameShape(QFrame::NoFrame);
    m_games->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_games->setSelectionMode(QAbstractItemView::SingleSelection);
    m_games->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_games->setShowGrid(false);
    m_games->setAlternatingRowColors(true);
    m_games->setWordWrap(false);
    m_games->verticalHeader()->hide();
    m_games->verticalHeader()->setDefaultSectionSize(32);
    m_games->horizontalHeader()->setHighlightSections(false);
    m_games->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // The columns share the width (no sideways scrolling).
    m_games->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_games->setTextElideMode(Qt::ElideRight);
    // Sortable by any column (as Finder's lists); in the file's order to begin with.
    m_games->horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder);
    m_games->horizontalHeader()->setSortIndicatorShown(true);
    m_games->setSortingEnabled(true);
    sortOnClicksThenReset(m_games->horizontalHeader(), 0); // a third click: the file's order
    m_games->installEventFilter(this);
    connect(m_games, &QTableView::doubleClicked, this, &LibraryView::openSelectedGame);
    connect(m_games->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            &LibraryView::showPreview);

    m_previewBoard = new BoardView;
    m_previewBoard->setInteractive(false);
    m_previewBoard->setObjectName(QStringLiteral("previewBoard"));
    m_previewBoard->setFocusPolicy(Qt::StrongFocus); // a click on it takes the arrow keys
    m_previewBoard->installEventFilter(this);
    m_previewBoard->setShowCoordinates(false);
    m_previewBoard->setFixedSize(212, 212);
    m_previewMove = label(QString(), "emptyHint");
    m_previewMove->setObjectName(QStringLiteral("previewMove"));
    auto* stepRow = new QHBoxLayout;
    stepRow->setSpacing(6);
    for (const auto& [tip, plies] :
         {std::pair{tr("Start (Home)"), -10000}, std::pair{tr("Back (←)"), -1},
          std::pair{tr("Forward (→)"), 1}, std::pair{tr("End (End)"), 10000}}) {
        auto* button = new QPushButton;
        button->setProperty("role", "nav");
        button->setToolTip(tip);
        button->setFocusPolicy(Qt::NoFocus);
        button->setIconSize(QSize(14, 14));
        connect(button, &QPushButton::clicked, this, [this, plies] {
            m_previewBoard->setFocus(); // from now on the arrow keys step through this game
            stepPreview(plies);
        });
        stepRow->addWidget(button);
        m_previewButtons.push_back(button);
    }
    stepRow->addStretch();
    m_previewButtons[1]->setObjectName(QStringLiteral("previewBack"));
    m_previewPlayers = label(QString(), "previewPlayers");
    m_previewPlayers->setObjectName(QStringLiteral("previewPlayers"));
    m_previewPlayers->setWordWrap(true);
    m_previewDetails = label(QString(), "emptyHint");
    m_previewDetails->setWordWrap(true);
    auto* open = new QPushButton(tr("Open Game"));
    open->setObjectName(QStringLiteral("libraryOpenGame"));
    open->setProperty("role", "primary");
    connect(open, &QPushButton::clicked, this, &LibraryView::openSelectedGame);
    auto* openRow = new QHBoxLayout;
    openRow->addWidget(open);
    openRow->addStretch();

    auto* previewInfo = new QVBoxLayout;
    previewInfo->setSpacing(6);
    previewInfo->addWidget(m_previewPlayers);
    previewInfo->addWidget(m_previewDetails);
    previewInfo->addSpacing(6);
    previewInfo->addWidget(m_previewMove);
    previewInfo->addLayout(stepRow);
    previewInfo->addStretch();
    previewInfo->addLayout(openRow);
    auto* previewGame = new QWidget;
    auto* previewRow = new QHBoxLayout(previewGame);
    previewRow->setContentsMargins(0, 0, 0, 0);
    previewRow->setSpacing(16);
    previewRow->addWidget(m_previewBoard, 0, Qt::AlignTop);
    previewRow->addLayout(previewInfo, 1);
    m_preview = new QStackedWidget;
    m_preview->addWidget(label(tr("Select a game to see it here."), "emptyHint"));
    m_preview->addWidget(previewGame);
    m_preview->setFixedHeight(212);

    auto* gamesPage = new QWidget;
    auto* gamesBox = new QVBoxLayout(gamesPage);
    gamesBox->setContentsMargins(0, 0, 0, 0);
    gamesBox->setSpacing(layout::kCardSpacing);
    auto* gamesHeader = new QHBoxLayout;
    gamesHeader->setSpacing(10);
    gamesHeader->addWidget(m_gamesIcon, 0, Qt::AlignTop);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(0);
    titles->addWidget(m_listTitle);
    titles->addWidget(m_listCount);
    gamesHeader->addLayout(titles, 1);
    gamesHeader->addWidget(m_search);
    gamesHeader->addWidget(m_dates);
    gamesBox->addLayout(gamesHeader);
    gamesBox->addWidget(m_games, 1);
    gamesBox->addSpacing(4);
    gamesBox->addWidget(m_preview);
    m_detail->addWidget(gamesPage);

    // Repertoire: what's in it, and buttons to open and practise it.
    m_repIcon = new QLabel;
    m_repIcon->setFixedSize(22, 22);
    m_repTitle = new ElidedLabel;
    m_repTitle->setObjectName(QStringLiteral("libraryRepertoireTitle"));
    m_repTitle->setProperty("role", "sectionTitle");
    m_repTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_repSubtitle = label(QString(), "emptyHint");
    m_repSubtitle->setObjectName(QStringLiteral("libraryRepertoireSubtitle"));
    auto* openFile = new QPushButton(tr("Open"));
    openFile->setObjectName(QStringLiteral("libraryOpenRepertoire"));
    openFile->setProperty("role", "secondary");
    connect(openFile, &QPushButton::clicked, this, [this] {
        if (m_file) {
            emit openGameRequested(m_file, 0);
        }
    });
    auto* practice = new QPushButton(tr("Practice"));
    practice->setObjectName(QStringLiteral("libraryPractice"));
    practice->setProperty("role", "primary");
    connect(practice, &QPushButton::clicked, this, [this] {
        if (m_file) {
            emit practiceRequested(m_file);
        }
    });
    auto* repHeader = new QHBoxLayout;
    repHeader->setSpacing(10);
    repHeader->addWidget(m_repIcon, 0, Qt::AlignTop);
    auto* repTitles = new QVBoxLayout;
    repTitles->setSpacing(0);
    repTitles->addWidget(m_repTitle);
    repTitles->addWidget(m_repSubtitle);
    repHeader->addLayout(repTitles, 1);
    repHeader->addWidget(openFile, 0, Qt::AlignTop);
    repHeader->addWidget(practice, 0, Qt::AlignTop);

    auto* stats = new QHBoxLayout;
    stats->setSpacing(10);
    const std::array<QString, 3> captions{tr("Positions to practise"), tr("Lines"),
                                          tr("Longest line")};
    for (std::size_t i = 0; i < captions.size(); ++i) {
        auto* tile = new QFrame;
        tile->setProperty("statTile", true);
        auto* tileBox = new QVBoxLayout(tile);
        tileBox->setContentsMargins(12, 10, 12, 10);
        tileBox->setSpacing(2);
        tileBox->addWidget(label(captions[i], "statCaption"));
        m_repStats[i] = label(QString(), "statValue");
        tileBox->addWidget(m_repStats[i]);
        stats->addWidget(tile, 1);
    }
    m_repStatus = label(QString(), "emptyHint");
    m_repOpenings = new QTreeWidget;
    m_repOpenings->setObjectName(QStringLiteral("repertoireOpenings"));
    m_repOpenings->setColumnCount(2);
    m_repOpenings->setHeaderHidden(true);
    m_repOpenings->setRootIsDecorated(false);
    m_repOpenings->setFrameShape(QFrame::NoFrame);
    m_repOpenings->setSelectionMode(QAbstractItemView::NoSelection);
    m_repOpenings->setFocusPolicy(Qt::NoFocus);
    m_repOpenings->setUniformRowHeights(true);
    m_repOpenings->setTextElideMode(Qt::ElideRight);
    m_repOpenings->header()->setStretchLastSection(false);
    m_repOpenings->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_repOpenings->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    auto* repertoirePage = new QWidget;
    auto* repBox = new QVBoxLayout(repertoirePage);
    repBox->setContentsMargins(0, 0, 0, 0);
    repBox->setSpacing(layout::kCardSpacing);
    repBox->addLayout(repHeader);
    repBox->addSpacing(4);
    repBox->addLayout(stats);
    repBox->addSpacing(8);
    repBox->addWidget(label(tr("OPENINGS IN THIS FILE"), "cardTitle"));
    repBox->addWidget(m_repStatus);
    repBox->addWidget(m_repOpenings, 1);

    // Gaps: common opponent moves the file doesn't answer yet, by the Explorer's games.
    m_checkGaps = new QPushButton(tr("Check"));
    m_checkGaps->setObjectName(QStringLiteral("libraryCheckGaps"));
    m_checkGaps->setProperty("role", "secondary");
    m_checkGaps->setProperty("compact", true);
    m_checkGaps->setToolTip(tr("Look for common opponent moves this file doesn't answer, in the "
                               "games of the Explorer's source"));
    connect(m_checkGaps, &QPushButton::clicked, this, &LibraryView::checkGaps);
    auto* gapsHeader = new QHBoxLayout;
    gapsHeader->addWidget(label(tr("GAPS"), "cardTitle"));
    gapsHeader->addStretch();
    gapsHeader->addWidget(m_checkGaps);
    m_gapsStatus = label(QString(), "emptyHint");
    m_gapsStatus->setObjectName(QStringLiteral("libraryGapsStatus"));
    m_gapsStatus->setWordWrap(true);
    m_gaps = new QTreeWidget;
    m_gaps->setObjectName(QStringLiteral("repertoireGaps"));
    m_gaps->setColumnCount(3);
    m_gaps->setHeaderHidden(true);
    m_gaps->setRootIsDecorated(false);
    m_gaps->setFrameShape(QFrame::NoFrame);
    m_gaps->setFocusPolicy(Qt::NoFocus);
    m_gaps->setUniformRowHeights(true);
    m_gaps->setCursor(Qt::PointingHandCursor);
    m_gaps->header()->setStretchLastSection(false);
    m_gaps->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_gaps->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_gaps->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    connect(m_gaps, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        const int index = m_gaps->indexOfTopLevelItem(item);
        if (m_file && index >= 0 && std::cmp_less(index, m_gapList.size())) {
            const repertoire::Gap& gap = m_gapList[static_cast<std::size_t>(index)];
            emit gapRequested(m_file->path, gap.line, gap.move);
        }
    });
    repBox->addSpacing(8);
    repBox->addLayout(gapsHeader);
    repBox->addWidget(m_gapsStatus);
    repBox->addWidget(m_gaps, 1);
    m_detail->addWidget(repertoirePage);
    connect(&m_store, &RepertoireStore::changed, this, [this] {
        if (m_detail->currentIndex() == kRepertoirePage) {
            refreshRepertoire();
        }
    });
    connect(&m_store, &RepertoireStore::openingsReady, this, [this] {
        if (m_detail->currentIndex() == kRepertoirePage) {
            refreshRepertoire();
        }
    });

    auto* detailCard = card();
    m_cards[1] = detailCard;
    auto* detailBox = new QVBoxLayout(detailCard);
    detailBox->setContentsMargins(layout::kCard);
    detailBox->addWidget(m_detail);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(filesCard);
    splitter->addWidget(detailCard);
    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    // Wide enough for normal file names; the width you set is kept.
    splitter->setObjectName(QStringLiteral("librarySplitter"));
    splitter->setSizes({400, 780});
    splitter->restoreState(QSettings().value(QStringLiteral("library/splitter")).toByteArray());
    connect(splitter, &QSplitter::splitterMoved, this, [splitter] {
        QSettings().setValue(QStringLiteral("library/splitter"), splitter->saveState());
    });

    // ---- Empty library -------------------------------------------------------------------------
    auto* empty = new QWidget;
    auto* emptyOuter = new QVBoxLayout(empty);
    auto* emptyCard = card();
    emptyCard->setFixedWidth(460); // a fixed width lets the wrapped text work out its height
    auto* emptyBox = new QVBoxLayout(emptyCard);
    emptyBox->setContentsMargins(28, 26, 28, 26);
    emptyBox->setSpacing(10);
    auto* emptyTitle = label(tr("Your library is empty"), "emptyTitle");
    auto* emptyHint = label(tr("Put PGN files into your library folder or drop them onto this "
                               "window. Already keep your games somewhere? Choose that folder "
                               "instead."),
                            "emptyHint");
    emptyHint->setWordWrap(true);
    emptyHint->setMinimumWidth(400);
    // Tall enough for the wrapped text (a wrapped label doesn't ask for its height by itself),
    // measured in the style sheet's font.
    emptyHint->ensurePolished();
    emptyHint->setMinimumHeight(emptyHint->heightForWidth(460 - 56) +
                                emptyHint->fontMetrics().descent());
    auto* choose = new QPushButton(tr("Choose Folder…"));
    choose->setProperty("role", "primary");
    connect(choose, &QPushButton::clicked, this, &LibraryView::changeFolderRequested);
    auto* emptyButtons = new QHBoxLayout;
    emptyButtons->addWidget(choose);
    emptyButtons->addStretch();
    emptyBox->addWidget(emptyTitle);
    emptyBox->addWidget(emptyHint);
    emptyBox->addSpacing(6);
    emptyBox->addLayout(emptyButtons);
    emptyOuter->addStretch(2);
    emptyOuter->addWidget(emptyCard, 0, Qt::AlignHCenter);
    emptyOuter->addStretch(3);

    m_pages = new QStackedWidget;
    m_pages->addWidget(splitter);
    m_pages->addWidget(empty);
    layout->addWidget(m_pages, 1);

    connect(&m_library, &Library::scanned, this, [this](int files, int games, qint64 ms) {
        m_header->setSubtitle(files == 0 ? QString()
                                         : (files == 1 ? tr("1 file") : tr("%1 files").arg(files)) +
                                               QStringLiteral("  ·  ") + gamesText(games));
        m_header->setSubtitleToolTip(tr("Loaded in %1 ms").arg(ms));
        refreshFiles();
    });
    // While files are still being read (iCloud may be downloading them), the list is already
    // there; the counts fill in as they arrive.
    connect(&m_library, &Library::progress, this, [this] {
        const auto count = static_cast<int>(m_library.files().size());
        m_header->setSubtitle((count == 1 ? tr("1 file") : tr("%1 files").arg(count)) +
                              QStringLiteral("  ·  ") + tr("reading…"));
        m_header->setSubtitleToolTip({});
        refreshFiles();
    });
    connect(&m_style, &AppStyle::changed, this, &LibraryView::updateIcons);
    // The card that has the keyboard gets an accent border, so it's clear where ↑ ↓ ← → go.
    m_focusOutline =
        connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
            const std::array<bool, 2> focused{now != nullptr && now == m_files,
                                              now != nullptr &&
                                                  (now == m_games || now == m_previewBoard)};
            for (std::size_t i = 0; i < m_cards.size(); ++i) {
                if (m_cards[i]->property("focused").toBool() != focused[i]) {
                    m_cards[i]->setProperty("focused", focused[i]);
                    m_cards[i]->style()->unpolish(m_cards[i]);
                    m_cards[i]->style()->polish(m_cards[i]);
                }
            }
        });
    updateIcons();
    refreshFiles();
}

LibraryView::~LibraryView() {
    // Qt deletes the child widgets after this destructor, and the focus moves as each one goes.
    // The outline handler must not run then: it reads members that are already gone.
    disconnect(m_focusOutline);
}

void LibraryView::filtersChanged() {
    if (m_fileSearch->text().trimmed().isEmpty() && m_typeFilter->currentData().toInt() < 0) {
        refreshFiles(); // nothing filtered: the folders as they were folded
    } else {
        applyFilters();
        showSelection(); // a file found by a player or opening shows those games
    }
}

void LibraryView::applyFilters() {
    // Files of other types, or whose names don't match the search, are hidden, and so are
    // folders left empty. While searching, the folders with matches open.
    // The search looks at the names and inside the files: players, events, openings.
    const int wanted = m_typeFilter->currentData().toInt();
    const QString search = m_fileSearch->text().trimmed();
    const bool filtering = wanted >= 0 || !search.isEmpty();
    const std::function<bool(QTreeWidgetItem*)> show = [&](QTreeWidgetItem* item) {
        bool visible = false;
        if (item->data(0, kKindRole).toInt() == kFile) {
            visible = (wanted < 0 || item->data(0, kFileKindRole).toInt() == wanted) &&
                      (search.isEmpty() || item->text(0).contains(search, Qt::CaseInsensitive) ||
                       contains(fileOf(item), search));
        } else {
            for (int i = 0; i < item->childCount(); ++i) {
                visible = show(item->child(i)) || visible;
            }
            if (visible && filtering) {
                item->setExpanded(true); // show what was found
            }
            visible = visible || !filtering; // an empty folder shows when nothing is filtered
        }
        item->setHidden(!visible);
        return visible;
    };
    const bool rebuilding = m_rebuilding;
    m_rebuilding = true; // opening folders for a search isn't remembered
    for (int i = 0; i < m_files->topLevelItemCount(); ++i) {
        show(m_files->topLevelItem(i));
    }
    m_rebuilding = rebuilding;
}

void LibraryView::setBoardLook(const BoardTheme& theme, const QString& pieceSet) {
    m_previewBoard->setTheme(theme);
    m_previewBoard->setPieceSet(pieceSet);
}

void LibraryView::updateIcons() {
    const auto& c = m_style.colors();
    m_folderButton->setIcon(makeIcon(IconKind::Folder, c.text, c.dimmed));
    m_newButtons[0]->setIcon(makeIcon(IconKind::NewFile, c.text, c.dimmed));
    m_newButtons[1]->setIcon(makeIcon(IconKind::NewFolder, c.text, c.dimmed));
    const std::array<IconKind, 4> kinds{IconKind::First, IconKind::Previous, IconKind::Next,
                                        IconKind::Last};
    for (std::size_t i = 0; i < m_previewButtons.size(); ++i) {
        m_previewButtons[i]->setIcon(makeIcon(kinds[i], c.text, c.dimmed, 14));
    }
    const QIcon folder = makeIcon(IconKind::Folder, c.muted, c.dimmed);
    for (QTreeWidgetItemIterator it(m_files); *it != nullptr; ++it) {
        const int kind = (*it)->data(0, kKindRole).toInt();
        if (kind == kFolder) {
            (*it)->setIcon(0, folder);
        } else if (kind == kFile) {
            const auto fileKind = static_cast<FileKind>((*it)->data(0, kFileKindRole).toInt());
            (*it)->setIcon(0, makeIcon(iconOf(fileKind), c.muted, c.dimmed));
        }
    }
    m_gamesIcon->setPixmap(makeIcon(iconOf(m_kind), c.text, c.dimmed, 22).pixmap(22, 22));
    m_repIcon->setPixmap(makeIcon(IconKind::Branch, c.text, c.dimmed, 22).pixmap(22, 22));
}

// ---- Files tree --------------------------------------------------------------------------------

std::shared_ptr<PgnFile> LibraryView::fileOf(const QTreeWidgetItem* item) const {
    if (item == nullptr || item->data(0, kKindRole).toInt() != kFile) {
        return nullptr;
    }
    const auto& files = m_library.files();
    const int index = item->data(0, kIndexRole).toInt();
    return index >= 0 && static_cast<std::size_t>(index) < files.size()
               ? files[static_cast<std::size_t>(index)].pgn
               : nullptr;
}

void LibraryView::refreshFiles() {
    // What was selected (by folder or file), to select it again after rebuilding.
    QString selectedKey;
    if (const QTreeWidgetItem* item = m_files->currentItem()) {
        selectedKey = item->data(0, kKeyRole).toString();
    }
    const int selectedGame = m_games->currentIndex().row();
    m_folderButton->setText(QFileInfo(m_library.folder()).fileName());

    const auto& files = m_library.files();
    if (files.empty() && m_library.folders().isEmpty()) {
        m_files->clear();
        // "Empty" only once the folder has really been looked at, never while it loads.
        m_pages->setCurrentIndex(m_library.isScanning() ? 0 : 1);
        m_file.reset();
        m_model->setFile(nullptr, FileKind::Game);
        m_detail->setCurrentIndex(kNothing);
        return;
    }
    m_pages->setCurrentIndex(0);

    m_rebuilding = true;
    m_files->clear();
    const QSettings settings;
    const bool firstTime = !settings.value(QString::fromLatin1(kFoldingSavedKey), false).toBool();
    const QStringList expanded = settings.value(QString::fromLatin1(kExpandedKey)).toStringList();
    const auto& c = m_style.colors();
    const QIcon folderIcon = makeIcon(IconKind::Folder, c.muted, c.dimmed);

    // Folders first (parents before children, alphabetical), then the files into them.
    std::map<QString, QTreeWidgetItem*> folders; // relative path → item; "" = top level
    const std::function<QTreeWidgetItem*(const QString&)> folderItem =
        [&](const QString& path) -> QTreeWidgetItem* {
        if (path.isEmpty() || path == u".") {
            return nullptr;
        }
        if (const auto it = folders.find(path); it != folders.end()) {
            return it->second;
        }
        const QFileInfo info(path);
        QTreeWidgetItem* parent = folderItem(info.path());
        auto* item = parent != nullptr ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_files);
        item->setText(0, info.fileName());
        item->setIcon(0, folderIcon);
        item->setData(0, kKindRole, kFolder);
        item->setData(0, kKeyRole, path);
        item->setData(0, kNameRole, info.fileName());
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        folders.emplace(path, item);
        return item;
    };
    QStringList folderPaths = m_library.folders(); // empty folders too
    for (const auto& file : files) {
        const QString path = QFileInfo(file.name).path();
        if (path != u"." && !folderPaths.contains(path)) {
            folderPaths << path;
        }
    }
    std::sort(folderPaths.begin(), folderPaths.end(), [](const QString& a, const QString& b) {
        return QString::localeAwareCompare(a, b) < 0;
    });
    for (const auto& path : folderPaths) {
        folderItem(path);
    }
    for (std::size_t i = 0; i < files.size(); ++i) {
        const QFileInfo info(files[i].name);
        QTreeWidgetItem* parent = folderItem(info.path());
        auto* item = parent != nullptr ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_files);
        item->setText(0, info.completeBaseName());
        item->setData(0, kNameRole, info.completeBaseName());
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setData(0, kKindRole, kFile);
        item->setData(0, kKeyRole, files[i].path);
        item->setData(0, kIndexRole, static_cast<int>(i));
        const FileKind kind =
            files[i].pgn ? RepertoireStore::kindOf(*files[i].pgn) : FileKind::Game;
        const core::Color side =
            files[i].pgn ? RepertoireStore::sideOf(*files[i].pgn) : core::Color::White;
        item->setData(0, kFileKindRole, static_cast<int>(kind));
        item->setData(0, kSideRole, side == core::Color::White ? 0 : 1);
        item->setIcon(0, makeIcon(iconOf(kind), c.muted, c.dimmed));
        item->setToolTip(0,
                         info.completeBaseName() + QStringLiteral("  ·  ") + typeName(kind, side));
    }
    for (const auto& [path, item] : folders) {
        // Remembered folding; the first time, only the top-level folders are open.
        item->setExpanded(firstTime ? item->parent() == nullptr : expanded.contains(path));
    }
    m_rebuilding = false;
    applyFilters();

    // A folder just made: select it, with the folders around it open.
    if (!m_selectAfterScan.isEmpty()) {
        for (QTreeWidgetItemIterator it(m_files); *it != nullptr; ++it) {
            if ((*it)->data(0, kKeyRole).toString() == m_selectAfterScan) {
                selectedKey = m_selectAfterScan;
                m_selectAfterScan.clear();
                for (QTreeWidgetItem* up = (*it)->parent(); up != nullptr; up = up->parent()) {
                    up->setExpanded(true);
                }
                break;
            }
        }
    }
    // Select what was selected before, or else the first file.
    QTreeWidgetItem* select = nullptr;
    for (QTreeWidgetItemIterator it(m_files); *it != nullptr; ++it) {
        if (!selectedKey.isEmpty() && (*it)->data(0, kKeyRole).toString() == selectedKey) {
            select = *it;
            break;
        }
    }
    if (select == nullptr) {
        // The first file that can be seen (not inside a closed folder), else the first item.
        for (QTreeWidgetItemIterator it(m_files); *it != nullptr; ++it) {
            const bool hidden = (*it)->parent() != nullptr && !(*it)->parent()->isExpanded();
            if ((*it)->data(0, kKindRole).toInt() == kFile && !hidden && !(*it)->isHidden()) {
                select = *it;
                break;
            }
        }
        if (select == nullptr) {
            select = m_files->topLevelItem(0);
        }
    }
    // Selecting something inside a closed folder would open it (Qt shows the current item), so
    // select the outermost closed folder around it instead: folders stay as the user left them.
    for (QTreeWidgetItem* up = select != nullptr ? select->parent() : nullptr; up != nullptr;
         up = up->parent()) {
        if (!up->isExpanded()) {
            select = up;
        }
    }
    m_rebuilding = true; // one showSelection() below, not one per change
    m_files->setCurrentItem(select);
    m_rebuilding = false;
    showSelection();
    // The same file as before: stay on the game that was selected.
    if (select != nullptr && select->data(0, kKeyRole).toString() == selectedKey &&
        m_detail->currentIndex() == kGamesPage && selectedGame > 0 &&
        selectedGame < m_filter->rowCount()) {
        m_games->setCurrentIndex(m_filter->index(selectedGame, 1));
    }
}

void LibraryView::rememberExpanded() {
    if (m_rebuilding || !m_fileSearch->text().isEmpty()) {
        return;
    }
    QStringList expanded;
    for (QTreeWidgetItemIterator it(m_files); *it != nullptr; ++it) {
        if ((*it)->data(0, kKindRole).toInt() == kFolder && (*it)->isExpanded()) {
            expanded << (*it)->data(0, kKeyRole).toString();
        }
    }
    QSettings settings;
    settings.setValue(QString::fromLatin1(kExpandedKey), expanded);
    settings.setValue(QString::fromLatin1(kFoldingSavedKey), true);
}

void LibraryView::selectFile(const QString& path) {
    for (QTreeWidgetItemIterator it(m_files); *it != nullptr; ++it) {
        if ((*it)->data(0, kKeyRole).toString() == path) {
            m_files->setCurrentItem(*it);
            return;
        }
    }
}

// ---- The selection -----------------------------------------------------------------------------

void LibraryView::showSelection() {
    const QTreeWidgetItem* item = m_files->currentItem();
    if (item == nullptr) {
        m_file.reset();
        m_detail->setCurrentIndex(kNothing);
        return;
    }
    if (item->data(0, kKindRole).toInt() == kFolder) {
        m_file.reset();
        showFolder(*item);
        return;
    }
    const auto file = fileOf(item);
    if (!file) {
        m_detail->setCurrentIndex(kNothing);
        return;
    }
    const auto kind = static_cast<FileKind>(item->data(0, kFileKindRole).toInt());
    if (kind == FileKind::Repertoire) {
        showRepertoire(file);
    } else {
        showGames(file, kind);
        // Found by what's inside (a player, an opening): show those games.
        const QString search = m_fileSearch->text().trimmed();
        m_search->setText(!search.isEmpty() && !item->text(0).contains(search, Qt::CaseInsensitive)
                              ? search
                              : QString());
    }
}

void LibraryView::showFolder(const QTreeWidgetItem& item) {
    int files = 0;
    int folders = 0;
    for (int i = 0; i < item.childCount(); ++i) {
        if (item.child(i)->isHidden()) {
            continue;
        }
        ++(item.child(i)->data(0, kKindRole).toInt() == kFile ? files : folders);
    }
    QStringList parts;
    if (folders > 0) {
        parts << (folders == 1 ? tr("1 folder") : tr("%1 folders").arg(folders));
    }
    parts << (files == 1 ? tr("1 file") : tr("%1 files").arg(files));
    m_folderState->setText(item.text(0), parts.join(QStringLiteral("  ·  ")));
    m_detail->setCurrentIndex(kFolderPage);
}

void LibraryView::showGames(const std::shared_ptr<PgnFile>& file, FileKind kind) {
    const bool sameFile = m_file == file && m_kind == kind;
    m_file = file;
    m_kind = kind;
    m_detail->setCurrentIndex(kGamesPage);
    const auto& c = m_style.colors();
    m_gamesIcon->setPixmap(makeIcon(iconOf(kind), c.text, c.dimmed, 22).pixmap(22, 22));
    m_listTitle->setFullText(QFileInfo(file->path).completeBaseName());
    const int games = static_cast<int>(file->games.size());
    m_listCount->setText(typeName(kind, core::Color::White) + QStringLiteral("  ·  ") +
                         gamesText(games));
    if (sameFile) {
        return; // the same table: keep the selected game and the search
    }
    m_model->setFile(file, kind);
    auto* header = m_games->horizontalHeader();
    for (int column = 0; column < m_model->columnCount(); ++column) {
        header->setSectionResizeMode(column, m_model->stretches(column)
                                                 ? QHeaderView::Stretch
                                                 : QHeaderView::ResizeToContents);
    }
    // Preview the first game straight away.
    if (m_filter->rowCount() > 0) {
        m_games->setCurrentIndex(m_filter->index(0, 1));
    } else {
        m_preview->setCurrentIndex(0);
    }
}

void LibraryView::showRepertoire(const std::shared_ptr<PgnFile>& file) {
    m_file = file;
    m_kind = FileKind::Repertoire;
    m_detail->setCurrentIndex(kRepertoirePage);
    m_repTitle->setFullText(QFileInfo(file->path).completeBaseName());
    const core::Color side = RepertoireStore::sideOf(*file);
    const int games = static_cast<int>(file->games.size());
    m_repSubtitle->setText(games > 1 ? typeName(FileKind::Repertoire, side) +
                                           QStringLiteral("  ·  ") + tr("%1 chapters").arg(games)
                                     : typeName(FileKind::Repertoire, side));
    m_store.loadOpenings();
    resetGaps();
    refreshRepertoire();
}

void LibraryView::resetGaps() {
    if (m_gapsCancel) {
        m_gapsCancel->store(true); // a check of another file stops
    }
    ++m_gapsRequest;
    m_gapList.clear();
    m_gaps->clear();
    m_gaps->hide();
    m_checkGaps->setEnabled(true);
    m_checkGaps->setText(tr("Check"));
    m_gapsStatus->setText(tr("Which common opponent moves this file doesn't answer yet, judged by "
                             "the games of the Explorer's source."));
}

void LibraryView::checkGaps() {
    if (!m_file) {
        return;
    }
    const RepertoireFile* built = m_store.builtFile(m_file->path);
    const GapSource source = m_gapSource ? m_gapSource() : GapSource{};
    if (built == nullptr || !source.explore) {
        m_gapsStatus->setText(built == nullptr ? tr("Reading the repertoire…")
                                               : source.unavailable);
        return;
    }
    resetGaps();
    const int request = m_gapsRequest;
    m_gapsCancel = std::make_shared<std::atomic<bool>>(false);
    m_checkGaps->setEnabled(false);
    m_checkGaps->setText(tr("Checking…"));
    m_gapsStatus->setText(tr("Checking against %1…").arg(source.name));
    // In the background, from copies: the file may change meanwhile.
    std::vector<core::Game> games = built->games;
    const core::Color side = RepertoireStore::sideOf(*m_file);
    // Owned by the view and deleted when done (deleteLater); the analyzer can't see that.
    auto* watcher = new QFutureWatcher<repertoire::GapReport>(
        this); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    connect(watcher, &QFutureWatcher<repertoire::GapReport>::finished, this,
            [this, watcher, request, name = source.name] {
                watcher->deleteLater();
                if (request != m_gapsRequest) {
                    return; // another file is shown now
                }
                const repertoire::GapReport report = watcher->result();
                m_checkGaps->setEnabled(true);
                m_checkGaps->setText(tr("Check Again"));
                m_gapList = report.gaps;
                m_gaps->clear();
                const QLocale locale;
                for (const repertoire::Gap& gap : m_gapList) {
                    // "8… h6": the move as the move list writes it, the line in the tooltip.
                    core::Position position;
                    QStringList line;
                    for (std::size_t k = 0; k < gap.line.size(); ++k) {
                        const QString number =
                            k % 2 == 0 ? QStringLiteral("%1. ").arg((k / 2) + 1) : QString();
                        line << number + QString::fromStdString(position.san(gap.line[k]));
                        position.play(gap.line[k]);
                    }
                    const std::size_t ply = gap.line.size();
                    const QString move =
                        QStringLiteral("%1%2 %3")
                            .arg((ply / 2) + 1)
                            .arg(ply % 2 == 0 ? QStringLiteral(".") : QStringLiteral("…"))
                            .arg(QString::fromStdString(gap.san));
                    auto* item = new QTreeWidgetItem(m_gaps);
                    item->setText(0, move);
                    item->setToolTip(0, (line.join(u' ') + QStringLiteral("  ") + move).trimmed());
                    item->setText(1, QStringLiteral("%1%").arg(qRound(gap.share * 100)));
                    item->setToolTip(1,
                                     tr("Played in %1% of the games here (%2)")
                                         .arg(qRound(gap.share * 100))
                                         .arg(locale.toString(static_cast<qulonglong>(gap.games))));
                    item->setForeground(1, m_style.colors().muted);
                    const double percent = gap.likelihood * 100;
                    item->setText(2, percent >= 1 ? tr("%1% of games").arg(qRound(percent))
                                                  : tr("<1% of games"));
                    item->setToolTip(2, tr("How often you'd meet it from the start, playing "
                                           "this file"));
                    item->setForeground(2, m_style.colors().muted);
                }
                m_gaps->setVisible(!m_gapList.empty());
                QString status;
                if (m_gapList.empty()) {
                    status = tr("No gaps against %1: every common reply is answered.").arg(name);
                } else if (m_gapList.size() == 1) {
                    status = tr("1 gap against %1. Click it to add your answer.").arg(name);
                } else {
                    status = tr("%1 gaps against %2. Click one to add your answer.")
                                 .arg(m_gapList.size())
                                 .arg(name);
                }
                if (report.stopped) {
                    status += QLatin1Char(' ') +
                              tr("(Checked the %1 most likely positions.)").arg(report.asked);
                }
                m_gapsStatus->setText(status);
                emit gapsChecked();
            });
    watcher->setFuture(QtConcurrent::run(
        [games = std::move(games), side, explore = source.explore, cancel = m_gapsCancel] {
            repertoire::Tree tree(side);
            for (std::size_t k = 0; k < games.size(); ++k) {
                tree.add(games[k], 0, static_cast<int>(k));
            }
            return repertoire::findGaps(tree, explore, {}, [cancel] { return cancel->load(); });
        }));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

QStringList LibraryView::repertoireGaps() const {
    QStringList texts;
    for (int i = 0; i < m_gaps->topLevelItemCount(); ++i) {
        const auto* item = m_gaps->topLevelItem(i);
        texts << QStringLiteral("%1  %2  %3").arg(item->text(0), item->text(1), item->text(2));
    }
    return texts;
}

QString LibraryView::gapsStatus() const {
    return m_gapsStatus->text();
}

void LibraryView::refreshRepertoire() {
    if (!m_file) {
        return;
    }
    const int request = ++m_summaryRequest;
    const RepertoireFile* built = m_store.builtFile(m_file->path);
    if (built == nullptr) {
        // Not read yet (or its type just changed): the store says when it's there.
        for (QLabel* stat : m_repStats) {
            stat->setText(QStringLiteral("…"));
        }
        m_repOpenings->clear();
        m_repStatus->setText(tr("Reading the repertoire…"));
        m_repStatus->show();
        return;
    }
    // Summed up in the background, from copies (the store may rebuild meanwhile).
    std::vector<core::Game> games = built->games;
    const core::Color side = RepertoireStore::sideOf(*m_file);
    const auto names = m_store.sharedOpenings();
    // Owned by the view and deleted when done (deleteLater); the analyzer can't see that.
    auto* watcher = new QFutureWatcher<repertoire::Summary>(
        this); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    connect(watcher, &QFutureWatcher<repertoire::Summary>::finished, this,
            [this, watcher, request, named = names != nullptr] {
                watcher->deleteLater();
                if (request != m_summaryRequest) {
                    return;
                }
                const repertoire::Summary summary = watcher->result();
                m_repStats[0]->setText(number(summary.positions));
                m_repStats[1]->setText(number(summary.lines));
                m_repStats[2]->setText(summary.deepestMove > 0
                                           ? tr("move %1").arg(summary.deepestMove)
                                           : QStringLiteral("–"));
                m_repOpenings->clear();
                for (const auto& opening : summary.openings) {
                    auto* item = new QTreeWidgetItem(m_repOpenings);
                    item->setText(0, QString::fromStdString(opening.name));
                    item->setToolTip(0, item->text(0));
                    item->setText(1, opening.positions == 1
                                         ? tr("1 position")
                                         : tr("%1 positions").arg(number(opening.positions)));
                    item->setForeground(1, m_style.colors().muted);
                }
                QString status;
                if (!named) {
                    status = tr("Looking up the opening names…");
                } else if (summary.openings.empty()) {
                    status = tr("No named openings in this file yet.");
                }
                m_repStatus->setText(status);
                m_repStatus->setVisible(!m_repStatus->text().isEmpty());
            });
    watcher->setFuture(QtConcurrent::run([games = std::move(games), side, names] {
        return repertoire::summarize(games, side, names.get());
    }));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

// ---- New folders, moving -----------------------------------------------------------------------

QString LibraryView::targetFolder() const {
    const QTreeWidgetItem* item = m_files->currentItem();
    if (item == nullptr) {
        return {};
    }
    if (item->data(0, kKindRole).toInt() == kFolder) {
        return item->data(0, kKeyRole).toString();
    }
    const QString folder =
        QFileInfo(QDir(m_library.folder()).relativeFilePath(item->data(0, kKeyRole).toString()))
            .path();
    return folder == u"." ? QString() : folder;
}

void LibraryView::askNewFolder() {
    NewFolderDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted || dialog.name().isEmpty()) {
        return;
    }
    if (const QString error = createFolder(targetFolder(), dialog.name()); !error.isEmpty()) {
        notice(this, tr("New Folder"), error);
    }
}

QString LibraryView::createFolder(const QString& parent, const QString& name) {
    if (name.contains(QLatin1Char('/')) || name.startsWith(QLatin1Char('.'))) {
        return tr("A folder name can't contain “/” or start with a dot.");
    }
    const QString relative = parent.isEmpty() ? name : parent + QLatin1Char('/') + name;
    const QDir root(m_library.folder());
    if (QFileInfo::exists(root.filePath(relative))) {
        return tr("There already is something called “%1” there.").arg(name);
    }
    if (!root.mkpath(relative)) {
        return tr("The folder “%1” couldn't be made.").arg(name);
    }
    m_selectAfterScan = relative;
    m_library.rescan();
    return {};
}

QString LibraryView::moveToFolder(const QStringList& paths, const QString& folder) {
    const QDir root(m_library.folder());
    const QString target = QDir::cleanPath(root.filePath(folder));
    for (const QString& path : paths) {
        const QString from = QDir::cleanPath(path);
        const QFileInfo info(from);
        const QString to = QDir(target).filePath(info.fileName());
        if (QDir::cleanPath(info.path()) == target) {
            continue; // already there
        }
        if (info.isDir() && (target == from || target.startsWith(from + QLatin1Char('/')))) {
            return tr("A folder can't go into itself.");
        }
        if (QFileInfo::exists(to)) {
            return tr("There already is a “%1” in that folder.").arg(info.fileName());
        }
        if (!QDir().rename(from, to)) {
            return tr("“%1” couldn't be moved.").arg(info.fileName());
        }
        emit pathMoved(from, to);
        if (info.isDir()) {
            m_selectAfterScan = root.relativeFilePath(to); // keep the folder in view
        }
    }
    m_library.rescan();
    return {};
}

QString LibraryView::rename(const QString& path, const QString& name) {
    if (name.isEmpty() || name.contains(QLatin1Char('/')) || name.startsWith(QLatin1Char('.'))) {
        return tr("A name can't be empty, contain “/” or start with a dot.");
    }
    const QFileInfo info(path);
    // A file keeps its ending (.pgn); the name shown is without it.
    const QString newName = info.isDir() ? name : name + QLatin1Char('.') + info.suffix();
    const QString to = QDir(info.path()).filePath(newName);
    if (QFileInfo::exists(to) && to.compare(path, Qt::CaseInsensitive) != 0) {
        return tr("There already is a “%1” there.").arg(newName);
    }
    if (!QDir().rename(path, to)) {
        return tr("“%1” couldn't be renamed.").arg(info.fileName());
    }
    emit pathMoved(path, to);
    m_selectAfterScan = info.isDir() ? QDir(m_library.folder()).relativeFilePath(to) : to;
    m_library.rescan();
    return {};
}

void LibraryView::askTrash(const QString& path) {
    if (!confirm(this, tr("Move to Trash"),
                 tr("Move “%1” to the Trash?").arg(QFileInfo(path).fileName()), tr("Move to Trash"),
                 MessageDialog::Action::Destructive,
                 QFileInfo(path).isDir()
                     ? tr("Everything in it goes too. Open games from it are closed.")
                     : tr("Open games from it are closed."))) {
        return;
    }
    if (const QString error = moveToTrash(path); !error.isEmpty()) {
        notice(this, tr("Couldn't move to the Trash"), error);
    }
}

QString LibraryView::moveToTrash(const QString& path) {
    emit pathRemoved(path); // tabs of it close first, so nothing saves it back
    const bool moved = m_trash ? m_trash(path) : QFile::moveToTrash(path);
    if (!moved) {
        return tr("“%1” couldn't be moved to the Trash.").arg(QFileInfo(path).fileName());
    }
    m_library.rescan();
    return {};
}

QStringList LibraryView::repertoireStats() const {
    QStringList result;
    for (const QLabel* stat : m_repStats) {
        result << stat->text();
    }
    return result;
}

QStringList LibraryView::repertoireOpenings() const {
    QStringList result;
    for (int i = 0; i < m_repOpenings->topLevelItemCount(); ++i) {
        const auto* item = m_repOpenings->topLevelItem(i);
        result << item->text(0) + QStringLiteral("  ") + item->text(1);
    }
    return result;
}

// ---- Keyboard ----------------------------------------------------------------------------------

bool LibraryView::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() != QEvent::KeyPress || watched == m_files) {
        return QWidget::eventFilter(watched, event); // the tree's keys are its own
    }
    const auto* key = dynamic_cast<QKeyEvent*>(event);
    if (key == nullptr) {
        return QWidget::eventFilter(watched, event);
    }
    // The keys go to whatever was clicked last; they never move the focus themselves.
    const bool onPreview = watched == m_previewBoard;
    switch (key->key()) {
    case Qt::Key_Left: // in the game list too: ↑ ↓ pick a game, ← → play through it
        stepPreview(-1);
        return true;
    case Qt::Key_Right:
        stepPreview(1);
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        openSelectedGame();
        return true;
    case Qt::Key_Up: // on the preview: start and end, as on a game tab
    case Qt::Key_Home:
        if (onPreview) {
            stepPreview(-10000);
        }
        return onPreview;
    case Qt::Key_Down:
    case Qt::Key_End:
        if (onPreview) {
            stepPreview(10000);
        }
        return onPreview;
    default:
        return false;
    }
}

// ---- Preview -----------------------------------------------------------------------------------

int LibraryView::selectedRow() const {
    const QModelIndex index = m_filter->mapToSource(m_games->currentIndex());
    return index.isValid() ? index.row() : -1;
}

void LibraryView::showPreview() {
    const int index = selectedRow();
    const std::shared_ptr<PgnFile> file = m_model->file();
    m_previewGame.reset();
    m_previewLine.clear();
    if (!file || index < 0) {
        m_preview->setCurrentIndex(0);
        return;
    }
    // Headers come from the index; the moves need the game parsed (in the background).
    const auto& entry = file->games[static_cast<std::size_t>(index)];
    const auto player = [&](const char* name, const char* elo) {
        QString text = toQString(entry.header(name));
        if (text.isEmpty()) {
            text = tr("Unknown");
        }
        if (const QString rating = toQString(entry.header(elo)); !rating.isEmpty()) {
            text += QStringLiteral(" (%1)").arg(rating);
        }
        return text;
    };
    const bool noPlayers =
        toQString(entry.header("White")).isEmpty() && toQString(entry.header("Black")).isEmpty();
    const QString title = m_model->fallbackName(index);
    m_previewPlayers->setText(noPlayers ? title
                                        : player("White", "WhiteElo") + QStringLiteral("  –  ") +
                                              player("Black", "BlackElo"));
    QStringList details;
    for (const char* name : {"Event", "Date", "Result"}) {
        if (const QString value = toQString(entry.header(name));
            !value.isEmpty() && value != u"*") {
            details << value;
        }
    }
    m_previewDetails->setText(details.join(QStringLiteral("  ·  ")));
    m_previewMove->clear();
    m_preview->setCurrentIndex(1);

    const int request = ++m_previewRequest;
    // Owned by the view and deleted when done (deleteLater); the analyzer can't see that.
    auto* watcher =
        new QFutureWatcher<pgnio::Parsed>(this); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    connect(watcher, &QFutureWatcher<pgnio::Parsed>::finished, this, [this, watcher, request] {
        watcher->deleteLater();
        if (request != m_previewRequest) {
            return; // the user has already moved on to another game
        }
        pgnio::Parsed parsed = watcher->result();
        if (!parsed.game) {
            m_previewBoard->setPosition(core::Position());
            m_previewMove->setText(tr("This game couldn't be read."));
            return;
        }
        m_previewGame = std::move(parsed.game);
        m_previewLine = m_previewGame->mainLine();
        // A game starts at its final position; a puzzle at the position to solve.
        m_previewPly = m_kind == FileKind::Puzzle ? 0 : m_previewLine.size() - 1;
        const int moves = (m_previewGame->ply(m_previewLine.back()) + 1) / 2;
        if (moves > 0) {
            const QString count = moves == 1 ? tr("1 move") : tr("%1 moves").arg(moves);
            m_previewDetails->setText(m_previewDetails->text().isEmpty()
                                          ? count
                                          : m_previewDetails->text() + QStringLiteral("  ·  ") +
                                                count);
        }
        showPreviewPly(false);
    });
    watcher->setFuture(pgnio::parse(file, index));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

void LibraryView::stepPreview(int plies) {
    if (!m_previewGame || m_previewLine.empty()) {
        return;
    }
    const auto current = static_cast<long long>(m_previewPly);
    const auto last = static_cast<long long>(m_previewLine.size()) - 1;
    const long long target = std::clamp(current + plies, 0LL, last);
    if (target == current) {
        return;
    }
    m_previewPly = static_cast<std::size_t>(target);
    showPreviewPly(target == current + 1); // slide only for a single step forward
}

void LibraryView::showPreviewPly(bool animate) {
    if (!m_previewGame) {
        return;
    }
    const core::Game& game = *m_previewGame;
    const core::NodeId node = m_previewLine[m_previewPly];
    const auto lastMove =
        node == core::Game::root() ? std::nullopt : std::optional(game.node(node).move);
    m_previewBoard->setPosition(game.positionAt(node), lastMove, animate);
    if (node == core::Game::root()) {
        m_previewMove->setText(tr("Start position"));
    } else {
        const bool white = game.mover(node) == core::Color::White;
        const QString move = QStringLiteral("%1%2 %3")
                                 .arg(game.moveNumber(node))
                                 .arg(white ? QStringLiteral(".") : QStringLiteral("…"))
                                 .arg(QString::fromStdString(game.node(node).san));
        m_previewMove->setText(m_previewPly + 1 == m_previewLine.size()
                                   ? tr("Final position, after %1").arg(move)
                                   : tr("After %1").arg(move));
    }
    const bool atStart = m_previewPly == 0;
    const bool atEnd = m_previewPly + 1 == m_previewLine.size();
    m_previewButtons[0]->setEnabled(!atStart);
    m_previewButtons[1]->setEnabled(!atStart);
    m_previewButtons[2]->setEnabled(!atEnd);
    m_previewButtons[3]->setEnabled(!atEnd);
}

void LibraryView::openSelectedGame() {
    if (const int index = selectedRow(); index >= 0 && m_model->file()) {
        emit openGameRequested(m_model->file(), index);
    }
}

} // namespace kiebitz::app
