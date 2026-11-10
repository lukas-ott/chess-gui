#include "LibraryView.hpp"

#include "Library.hpp"
#include "board/BoardView.hpp"
#include "document/PgnIo.hpp"
#include "style/AppStyle.hpp"
#include "style/Icons.hpp"

#include <QAbstractTableModel>
#include <QApplication>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTableView>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <functional>
#include <map>

namespace chessgui::app {
/// A one-line label that cuts its text off with "…" when there isn't room, instead of forcing
/// its column (and with it the window) to be as wide as the text. The full text is the tooltip.
class ElidedLabel : public QLabel {
public:
    using QLabel::QLabel;

    void setFullText(const QString& text) {
        m_full = text;
        setToolTip(text);
        updateText();
    }
    [[nodiscard]] QSize minimumSizeHint() const override {
        return {fontMetrics().horizontalAdvance(QStringLiteral("…")), QLabel::sizeHint().height()};
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QLabel::resizeEvent(event);
        updateText();
    }

private:
    void updateText() { setText(fontMetrics().elidedText(m_full, Qt::ElideRight, width())); }

    QString m_full;
};

namespace {

// Tree item data
constexpr int kKindRole = Qt::UserRole;      ///< kFolder or kFile
constexpr int kKeyRole = Qt::UserRole + 1;   ///< folder: relative path; file: absolute path
constexpr int kIndexRole = Qt::UserRole + 2; ///< file: index into Library::files()
constexpr int kFolder = 1;
constexpr int kFile = 2;

constexpr auto kExpandedKey = "library/expandedFolders";
constexpr auto kFoldingSavedKey = "library/foldingSaved"; // an empty list can't say "saved"

QString toQString(std::string_view text) {
    const auto value = QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    return value == u"?" || value == u"????.??.??" ? QString() : value;
}

QString gamesText(int count) {
    return count == 1 ? QObject::tr("1 game") : QObject::tr("%1 games").arg(count);
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

/// Games as a table: #, White, Black, Result, Date, Event. A row is one game of one file, so a
/// folder's games from many files fit in the same table. Reads straight from each file's index,
/// so thousands of games show at once.
class GameListModel : public QAbstractTableModel {
public:
    struct Row {
        std::shared_ptr<PgnFile> file;
        int index = 0;
    };

    using QAbstractTableModel::QAbstractTableModel;

    void setRows(std::vector<Row> rows) {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
    }
    [[nodiscard]] const Row* row(int r) const {
        return r >= 0 && static_cast<std::size_t>(r) < m_rows.size()
                   ? &m_rows[static_cast<std::size_t>(r)]
                   : nullptr;
    }

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
    }
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : static_cast<int>(kColumns.size());
    }
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override {
        const Row* r = row(index.row());
        if (r == nullptr) {
            return {};
        }
        if (role == Qt::TextAlignmentRole && (index.column() == 0 || index.column() == 4)) {
            return QVariant::fromValue(Qt::AlignCenter);
        }
        if (role != Qt::DisplayRole && role != Qt::ToolTipRole) {
            return {};
        }
        if (index.column() == 0) {
            return index.row() + 1;
        }
        const auto& game = r->file->games[static_cast<std::size_t>(r->index)];
        // The file's name comes first: opening files and studies often name no players, and
        // a row should never be blank.
        if (index.column() == 1) {
            return QFileInfo(r->file->path).completeBaseName();
        }
        return toQString(game.header(kColumns[static_cast<std::size_t>(index.column())].header));
    }
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role) const override {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
            return {};
        }
        return QObject::tr(kColumns[static_cast<std::size_t>(section)].title);
    }

private:
    struct Column {
        const char* title;
        const char* header;
    };
    static constexpr std::array<Column, 7> kColumns{{{"#", ""},
                                                     {"FILE", ""},
                                                     {"WHITE", "White"},
                                                     {"BLACK", "Black"},
                                                     {"RESULT", "Result"},
                                                     {"DATE", "Date"},
                                                     {"EVENT", "Event"}}};
    std::vector<Row> m_rows;
};

LibraryView::LibraryView(Library& library, const AppStyle& style, QWidget* parent)
    : QWidget(parent), m_library(library), m_style(style), m_model(new GameListModel(this)),
      m_filter(new QSortFilterProxyModel(this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 20);
    layout->setSpacing(14);

    // ---- Header: title, counts, folder button --------------------------------------------------
    auto* header = new QHBoxLayout;
    header->setSpacing(10);
    header->addWidget(label(tr("Library"), "pageTitle"));
    m_status = label(QString(), "emptyHint");
    header->addWidget(m_status, 0, Qt::AlignBottom);
    header->addStretch();
    m_folderButton = new QPushButton;
    m_folderButton->setObjectName(QStringLiteral("libraryFolder"));
    m_folderButton->setProperty("role", "secondary");
    m_folderButton->setToolTip(
        tr("Your library folder. Click to look inside or choose another one."));
    connect(m_folderButton, &QPushButton::clicked, this, &LibraryView::changeFolderRequested);
    header->addWidget(m_folderButton);
    layout->addLayout(header);

    // ---- Files: a folder tree ------------------------------------------------------------------
    m_files = new QTreeWidget;
    m_files->setObjectName(QStringLiteral("libraryFiles"));
    m_files->setColumnCount(2);
    m_files->setHeaderHidden(true);
    m_files->setFrameShape(QFrame::NoFrame);
    m_files->setIndentation(14);
    m_files->setUniformRowHeights(true);
    m_files->setAnimated(true);
    m_files->setTextElideMode(Qt::ElideRight);
    m_files->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_files->header()->setStretchLastSection(false);
    m_files->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_files->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_files->installEventFilter(this);
    connect(m_files, &QTreeWidget::currentItemChanged, this, [this] {
        if (!m_rebuilding) {
            showSelection();
        }
    });
    connect(m_files, &QTreeWidget::itemExpanded, this, &LibraryView::rememberExpanded);
    connect(m_files, &QTreeWidget::itemCollapsed, this, &LibraryView::rememberExpanded);
    auto* filesCard = card();
    m_cards[0] = filesCard;
    auto* filesBox = new QVBoxLayout(filesCard);
    filesBox->setContentsMargins(10, 12, 10, 10);
    filesBox->setSpacing(8);
    filesBox->addWidget(label(tr("FILES"), "cardTitle"));
    filesBox->addWidget(m_files, 1);

    // ---- Games ---------------------------------------------------------------------------------
    m_listTitle = new ElidedLabel;
    m_listTitle->setObjectName(QStringLiteral("libraryListTitle"));
    m_listTitle->setProperty("role", "sectionTitle");
    m_listTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_listCount = label(QString(), "emptyHint");
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search files, players, events"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(240);
    m_search->setMaximumWidth(300);
    m_filter->setSourceModel(m_model);
    m_filter->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_filter->setFilterKeyColumn(-1); // any column
    connect(m_search, &QLineEdit::textChanged, m_filter,
            &QSortFilterProxyModel::setFilterFixedString);

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
    m_games->verticalHeader()->setDefaultSectionSize(34);
    m_games->horizontalHeader()->setHighlightSections(false);
    // The columns share the width (no sideways scrolling): names and event stretch, the short
    // columns fit their content.
    m_games->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_games->setTextElideMode(Qt::ElideRight);
    for (const int column : {1, 2, 3, 6}) {
        m_games->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Stretch);
    }
    for (const int column : {0, 4, 5}) {
        m_games->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    m_games->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_games->installEventFilter(this);
    connect(m_games, &QTableView::doubleClicked, this, &LibraryView::openSelectedGame);
    connect(m_games->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            &LibraryView::showPreview);

    auto* gamesCard = card();
    m_cards[1] = gamesCard;
    auto* gamesBox = new QVBoxLayout(gamesCard);
    gamesBox->setContentsMargins(14, 12, 14, 10);
    gamesBox->setSpacing(10);
    auto* gamesHeader = new QHBoxLayout;
    auto* titles = new QVBoxLayout;
    titles->setSpacing(0);
    titles->addWidget(m_listTitle);
    titles->addWidget(m_listCount);
    gamesHeader->addLayout(titles, 1);
    gamesHeader->addWidget(m_search);
    gamesBox->addLayout(gamesHeader);
    gamesBox->addWidget(m_games, 1);

    // ---- Preview -------------------------------------------------------------------------------
    m_previewBoard = new BoardView;
    m_previewBoard->setInteractive(false);
    m_previewBoard->setObjectName(QStringLiteral("previewBoard"));
    m_previewBoard->setFocusPolicy(Qt::StrongFocus); // a click on it takes the arrow keys
    m_previewBoard->installEventFilter(this);
    m_previewBoard->setShowCoordinates(false);
    m_previewBoard->setMinimumSize(200, 200);
    // As tall as it is wide, at the top of the card: details follow right underneath.
    QSizePolicy square(QSizePolicy::Preferred, QSizePolicy::Preferred);
    square.setHeightForWidth(true);
    m_previewBoard->setSizePolicy(square);
    m_previewMove = label(QString(), "emptyHint");
    m_previewMove->setObjectName(QStringLiteral("previewMove"));
    m_previewMove->setAlignment(Qt::AlignCenter);
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
    m_previewButtons[1]->setObjectName(QStringLiteral("previewBack"));
    m_previewPlayers = label(QString(), "previewPlayers");
    m_previewPlayers->setObjectName(QStringLiteral("previewPlayers"));
    m_previewPlayers->setWordWrap(true);
    m_previewDetails = label(QString(), "emptyHint");
    m_previewDetails->setWordWrap(true);
    auto* open = new QPushButton(tr("Open Game"));
    open->setProperty("role", "primary");
    connect(open, &QPushButton::clicked, this, &LibraryView::openSelectedGame);

    auto* details = new QWidget;
    auto* detailsBox = new QVBoxLayout(details);
    detailsBox->setContentsMargins(0, 0, 0, 0);
    detailsBox->setSpacing(8);
    detailsBox->addWidget(m_previewBoard);
    detailsBox->addWidget(m_previewMove);
    detailsBox->addLayout(stepRow);
    detailsBox->addSpacing(4);
    detailsBox->addWidget(m_previewPlayers);
    detailsBox->addWidget(m_previewDetails);
    detailsBox->addSpacing(4);
    detailsBox->addWidget(open);
    detailsBox->addStretch(1);
    auto* pickHint = label(tr("Select a game to preview it."), "emptyHint");
    pickHint->setAlignment(Qt::AlignCenter);
    m_preview = new QStackedWidget;
    m_preview->addWidget(pickHint);
    m_preview->addWidget(details);

    auto* previewCard = card();
    m_cards[2] = previewCard;
    auto* previewBox = new QVBoxLayout(previewCard);
    previewBox->setContentsMargins(14, 12, 14, 14);
    previewBox->setSpacing(10);
    previewBox->addWidget(label(tr("PREVIEW"), "cardTitle"));
    previewBox->addWidget(m_preview, 1);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(filesCard);
    splitter->addWidget(gamesCard);
    splitter->addWidget(previewCard);
    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({270, 580, 320});

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
        m_status->setText(files == 0 ? QString()
                                     : (files == 1 ? tr("1 file") : tr("%1 files").arg(files)) +
                                           QStringLiteral("  ·  ") + gamesText(games));
        m_status->setToolTip(tr("Loaded in %1 ms").arg(ms));
        refreshFiles();
    });
    connect(&m_style, &AppStyle::changed, this, &LibraryView::updateIcons);
    // The column that has the keyboard gets an accent border, so it's clear where ↑ ↓ ← → go.
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
        const std::array<QWidget*, 3> owners{m_files, m_games, m_previewBoard};
        for (std::size_t i = 0; i < m_cards.size(); ++i) {
            const bool focused = now != nullptr && now == owners[i];
            if (m_cards[i]->property("focused").toBool() != focused) {
                m_cards[i]->setProperty("focused", focused);
                m_cards[i]->style()->unpolish(m_cards[i]);
                m_cards[i]->style()->polish(m_cards[i]);
            }
        }
    });
    updateIcons();
    refreshFiles();
}

void LibraryView::setBoardLook(const BoardTheme& theme, const QString& pieceSet) {
    m_previewBoard->setTheme(theme);
    m_previewBoard->setPieceSet(pieceSet);
}

void LibraryView::updateIcons() {
    const auto& c = m_style.colors();
    m_folderButton->setIcon(makeIcon(IconKind::Folder, c.text, c.dimmed));
    const std::array<IconKind, 4> kinds{IconKind::First, IconKind::Previous, IconKind::Next,
                                        IconKind::Last};
    for (std::size_t i = 0; i < m_previewButtons.size(); ++i) {
        m_previewButtons[i]->setIcon(makeIcon(kinds[i], c.text, c.dimmed, 14));
    }
    // Folder icons in the tree follow the colours too.
    const QIcon folder = makeIcon(IconKind::Folder, c.muted, c.dimmed);
    for (QTreeWidgetItemIterator it(m_files); *it != nullptr; ++it) {
        if ((*it)->data(0, kKindRole).toInt() == kFolder) {
            (*it)->setIcon(0, folder);
        }
    }
}

// ---- Files tree --------------------------------------------------------------------------------

void LibraryView::refreshFiles() {
    // What was selected (by folder or file), to select it again after rebuilding.
    QString selectedKey;
    if (const QTreeWidgetItem* item = m_files->currentItem()) {
        selectedKey = item->data(0, kKeyRole).toString();
    }
    const int selectedGame = m_games->currentIndex().row();
    m_folderButton->setText(QFileInfo(m_library.folder()).fileName());

    const auto& files = m_library.files();
    if (files.empty()) {
        m_files->clear();
        m_pages->setCurrentIndex(1);
        m_model->setRows({});
        return;
    }
    m_pages->setCurrentIndex(0);

    m_rebuilding = true;
    m_files->clear();
    const QSettings settings;
    const bool firstTime = !settings.value(QString::fromLatin1(kFoldingSavedKey), false).toBool();
    const QStringList expanded = settings.value(QString::fromLatin1(kExpandedKey)).toStringList();
    const QIcon folderIcon =
        makeIcon(IconKind::Folder, m_style.colors().muted, m_style.colors().dimmed);
    const QColor countColor = m_style.colors().muted;

    // Folders first (parents before children, alphabetical), then the files into them.
    std::map<QString, QTreeWidgetItem*> folders; // relative path → item; "" = top level
    std::map<QString, int> folderGames;
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
        QFont font = item->font(0);
        font.setWeight(QFont::DemiBold);
        item->setFont(0, font);
        folders.emplace(path, item);
        return item;
    };
    QStringList folderPaths;
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
        const int games = static_cast<int>(files[i].pgn ? files[i].pgn->games.size() : 0);
        item->setText(0, info.completeBaseName());
        item->setToolTip(0, info.completeBaseName());
        item->setText(1, QString::number(games));
        item->setForeground(1, countColor);
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setData(0, kKindRole, kFile);
        item->setData(0, kKeyRole, files[i].path);
        item->setData(0, kIndexRole, static_cast<int>(i));
        for (QString path = info.path(); path != u"." && !path.isEmpty();
             path = QFileInfo(path).path()) {
            folderGames[path] += games;
        }
    }
    for (const auto& [path, item] : folders) {
        item->setText(1, QString::number(folderGames[path]));
        item->setForeground(1, countColor);
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        // Remembered folding; the first time, only the top-level folders are open.
        item->setExpanded(firstTime ? item->parent() == nullptr : expanded.contains(path));
    }
    m_rebuilding = false;

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
            if ((*it)->data(0, kKindRole).toInt() == kFile && !hidden) {
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
    m_files->setCurrentItem(select);
    showSelection();
    // The same file or folder as before: stay on the game that was selected.
    if (select != nullptr && select->data(0, kKeyRole).toString() == selectedKey &&
        selectedGame > 0 && selectedGame < m_filter->rowCount()) {
        m_games->setCurrentIndex(m_filter->index(selectedGame, 1));
    }
}

void LibraryView::rememberExpanded() {
    if (m_rebuilding) {
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

void LibraryView::showSelection() {
    // A file lists its games; a folder lists every game in the files below it.
    std::vector<GameListModel::Row> rows;
    const QTreeWidgetItem* item = m_files->currentItem();
    int fileCount = 0;
    if (item != nullptr) {
        const auto& files = m_library.files();
        const auto addFile = [&](const QTreeWidgetItem* fileItem) {
            const int index = fileItem->data(0, kIndexRole).toInt();
            if (index < 0 || static_cast<std::size_t>(index) >= files.size()) {
                return;
            }
            const auto& pgn = files[static_cast<std::size_t>(index)].pgn;
            for (std::size_t g = 0; pgn && g < pgn->games.size(); ++g) {
                rows.push_back({pgn, static_cast<int>(g)});
            }
            ++fileCount;
        };
        const std::function<void(const QTreeWidgetItem*)> addAll =
            [&](const QTreeWidgetItem* node) {
                if (node->data(0, kKindRole).toInt() == kFile) {
                    addFile(node);
                }
                for (int i = 0; i < node->childCount(); ++i) {
                    addAll(node->child(i));
                }
            };
        addAll(item);
        m_listTitle->setFullText(item->text(0));
    }
    const bool folder = item != nullptr && item->data(0, kKindRole).toInt() == kFolder;
    const int games = static_cast<int>(rows.size());
    m_model->setRows(std::move(rows));
    m_listCount->setText(folder
                             ? gamesText(games) + QStringLiteral("  ·  ") +
                                   (fileCount == 1 ? tr("1 file") : tr("%1 files").arg(fileCount))
                             : gamesText(games));
    m_games->resizeColumnToContents(0);
    // Preview the first game straight away.
    if (m_filter->rowCount() > 0) {
        m_games->setCurrentIndex(m_filter->index(0, 1));
    } else {
        m_preview->setCurrentIndex(0);
    }
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
    const GameListModel::Row* row = m_model->row(selectedRow());
    m_previewGame.reset();
    m_previewLine.clear();
    if (row == nullptr) {
        m_preview->setCurrentIndex(0);
        return;
    }
    // Headers come from the index; the moves need the game parsed (in the background).
    const auto& entry = row->file->games[static_cast<std::size_t>(row->index)];
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
    m_previewPlayers->setText(noPlayers ? QFileInfo(row->file->path).completeBaseName()
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
        m_previewPly = m_previewLine.size() - 1; // start at the final position
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
    watcher->setFuture(pgnio::parse(row->file, row->index));
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
    if (const GameListModel::Row* row = m_model->row(selectedRow())) {
        emit openGameRequested(row->file, row->index);
    }
}

} // namespace chessgui::app
