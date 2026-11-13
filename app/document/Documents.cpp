#include "Documents.hpp"

#include "document/PgnIo.hpp"
#include "library/Library.hpp"
#include "style/AppStyle.hpp"
#include "style/Icons.hpp"
#include "util/WhenReady.hpp"
#include "widgets/Message.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QTabBar>
#include <QToolButton>

#include <algorithm>
#include <optional>
#include <utility>

namespace kiebitz::app {
namespace {

using core::Game;
using core::NodeId;

constexpr int kStatusMs = 4000;

QString toQString(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

} // namespace

Documents::Documents(Library& library, AppStyle& style, QWidget* window)
    : QObject(window), m_library(library), m_style(style), m_window(window) {
    m_tabs = new QTabBar;
    m_tabs->setObjectName(QStringLiteral("documentTabs"));
    m_tabs->setDocumentMode(true);
    m_tabs->setDrawBase(false);
    m_tabs->setExpanding(false);
    m_tabs->setTabsClosable(true);
    m_tabs->setFocusPolicy(Qt::NoFocus);
    m_tabs->setElideMode(Qt::ElideRight);
    connect(m_tabs, &QTabBar::tabCloseRequested, this, &Documents::close);
    m_autosave.setSingleShot(true);
    m_autosave.setInterval(1000);
    connect(&m_autosave, &QTimer::timeout, this, &Documents::autosave);
}

Documents::~Documents() = default;

GameDocument* Documents::at(int tab) const {
    if (tab < 0) {
        return nullptr;
    }
    // Each tab remembers its document's id (tabs can be closed in any order).
    const int id = m_tabs->tabData(tab).toInt();
    for (const auto& document : m_documents) {
        if (document->id == id) {
            return document.get();
        }
    }
    return nullptr;
}

GameDocument* Documents::current() const {
    return at(m_tabs->currentIndex());
}

void Documents::add(std::unique_ptr<GameDocument> document) {
    // An untouched empty game is replaced rather than left lying around in its own tab.
    for (int tab = 0; tab < m_tabs->count(); ++tab) {
        if (GameDocument* existing = at(tab); existing->isPristine()) {
            const int id = existing->id; // the tab knows the document by this id: keep it
            *existing = std::move(*document);
            existing->id = id;
            const QSignalBlocker block(m_tabs); // shown() below shows it either way
            m_tabs->setCurrentIndex(tab);
            emit shown();
            return;
        }
    }
    const GameDocument* raw = document.get();
    m_documents.push_back(std::move(document));
    const int tab = m_tabs->addTab(raw->title());
    m_tabs->setTabData(tab, raw->id);
    // Our own close button (drawn icon) instead of the platform's, on the right.
    auto* close = new QToolButton;
    close->setObjectName(QStringLiteral("tabClose"));
    close->setIconSize(QSize(12, 12));
    close->setFixedSize(26, 26); // 20 x 20 for the ✕, plus its top and right margins
    close->setAutoRaise(true);
    close->setFocusPolicy(Qt::NoFocus);
    close->setToolTip(tr("Close tab (⌘W)"));
    const auto& c = m_style.colors();
    close->setIcon(makeIcon(IconKind::Close, c.muted, c.dimmed, 12));
    connect(close, &QToolButton::clicked, this, [this, close] {
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (m_tabs->tabButton(i, QTabBar::RightSide) == close) {
                this->close(i);
                return;
            }
        }
    });
    m_tabs->setTabButton(tab, QTabBar::LeftSide, nullptr);
    m_tabs->setTabButton(tab, QTabBar::RightSide, close);
    {
        const QSignalBlocker block(m_tabs);
        m_tabs->setCurrentIndex(tab);
    }
    emit shown();
}

void Documents::close(int tab) {
    GameDocument* d = at(tab);
    if (d == nullptr) {
        return;
    }
    // No question when a tab closes: a game from a file is saved (as autosave would), any other
    // game (a new one, one opened from a database) just goes. Quitting does the same.
    if (inFile(*d) && !saveNow(*d)) {
        return; // couldn't be saved: keep it open (saveNow said why)
    }
    m_tabs->removeTab(tab); // switches tabs first, so nothing shows the game being removed
    std::erase_if(m_documents, [d](const auto& document) { return document.get() == d; });
    if (m_tabs->count() == 0) {
        add(std::make_unique<GameDocument>()); // the board always has a game
    }
    updateTitles();
}

GameDocument* Documents::select(const std::function<bool(const GameDocument&)>& matches) {
    for (int tab = 0; tab < m_tabs->count(); ++tab) {
        if (GameDocument* d = at(tab); d != nullptr && matches(*d)) {
            m_tabs->setCurrentIndex(tab);
            return d;
        }
    }
    return nullptr;
}

void Documents::restyle() {
    const auto& c = m_style.colors();
    for (int tab = 0; tab < m_tabs->count(); ++tab) {
        if (auto* close = qobject_cast<QToolButton*>(m_tabs->tabButton(tab, QTabBar::RightSide))) {
            close->setIcon(makeIcon(IconKind::Close, c.muted, c.dimmed, 12));
        }
    }
}

void Documents::updateTitles() {
    for (int tab = 0; tab < m_tabs->count(); ++tab) {
        if (const GameDocument* d = at(tab)) {
            // A long title (opening files have them) is cut off; hovering shows all of it.
            const QString title = d->title();
            const QString shown = m_tabs->fontMetrics().elidedText(title, Qt::ElideRight, 240);
            m_tabs->setTabText(tab, shown);
            m_tabs->setTabToolTip(tab, shown == title ? QString() : title);
        }
    }
    emit titlesChanged();
}

// ---- Saving ------------------------------------------------------------------------------------

bool Documents::inFile(const GameDocument& document) {
    return document.file && !document.file->path.isEmpty() && document.gameIndex >= 0 &&
           document.file->hasText;
}

void Documents::setModified(GameDocument& document) {
    if (!document.modified) {
        document.modified = true;
        updateTitles();
    }
    if (inFile(document)) {
        m_autosave.start(); // (again): saves a second after the last change
    }
}

void Documents::autosave() {
    m_autosave.stop();
    std::vector<std::shared_ptr<PgnFile>> files;
    for (const auto& d : m_documents) {
        if (!d->modified || !inFile(*d)) {
            continue;
        }
        if (m_writing.contains(d->file->path)) {
            m_autosave.start(); // its file is still being written: try again in a moment
            continue;
        }
        std::optional<std::string> text =
            pgnio::replaceGame(*d->file, d->gameIndex, pgn::writeGame(d->game));
        if (!text) {
            continue;
        }
        // Into the shared text now, so another tab of the same file builds on this change.
        d->file->text = std::move(*text);
        d->file->games = pgn::indexGames(d->file->text);
        d->modified = false;
        if (std::find(files.begin(), files.end(), d->file) == files.end()) {
            files.push_back(d->file);
        }
    }
    for (const auto& file : files) {
        const QString path = file->path;
        m_writing << path;
        whenReady(this, pgnio::write(path, file->text), [this, path](const QString& error) {
            m_writing.removeOne(path);
            if (!error.isEmpty()) {
                notice(m_window, tr("Couldn't save"), error);
                emit fileError(error);
                return;
            }
            emit fileSaved(path);
        });
    }
    updateTitles();
}

bool Documents::saveNow(GameDocument& document) {
    if (!document.modified) {
        return true;
    }
    if (inFile(document)) { // saved anyway a moment later: do it now, and wait for it
        // An autosave of this file still being written must not land after this one.
        QElapsedTimer waited;
        waited.start();
        while (m_writing.contains(document.file->path) && waited.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
        }
        std::optional<std::string> text =
            pgnio::replaceGame(*document.file, document.gameIndex, pgn::writeGame(document.game));
        if (text) {
            const QString error = pgnio::write(document.file->path, *text).result();
            if (error.isEmpty()) {
                document.file->text = std::move(*text);
                document.file->games = pgn::indexGames(document.file->text);
                document.modified = false;
                return true;
            }
            notice(m_window, tr("Couldn't save"), error);
        }
        return false;
    }
    return true; // not in a file: nothing to save it to
}

bool Documents::saveAll() {
    for (const auto& document : m_documents) {
        if (inFile(*document) && !saveNow(*document)) {
            return false; // saveNow said why
        }
    }
    return true;
}

void Documents::save(GameDocument& document) {
    if (!inFile(document)) {
        saveAs(document);
        return;
    }
    document.modified = true; // ⌘S always writes, even right after an autosave
    autosave();
}

void Documents::saveAs(GameDocument& document) {
    const QString title = document.title();
    const QString name = title.startsWith(u"• ") ? title.mid(2) : title;
    const QString path = QFileDialog::getSaveFileName(
        m_window, tr("Save Game"), QDir(m_library.folder()).filePath(name + u".pgn"),
        tr("PGN files (*.pgn)"));
    if (!path.isEmpty()) {
        saveAs(document, path);
    }
}

void Documents::saveAs(GameDocument& document, const QString& path) {
    const auto file = std::make_shared<PgnFile>();
    file->path = path;
    file->text = pgn::writeGame(document.game);
    file->games = pgn::indexGames(file->text);
    std::string text = file->text;
    write(document, path, std::move(text), file);
}

void Documents::write(GameDocument& document, const QString& path, std::string text,
                      const std::shared_ptr<PgnFile>& newFile) {
    GameDocument* target = &document;
    const auto shared = std::make_shared<std::string>(text);
    whenReady(this, pgnio::write(path, std::move(text)),
              [this, target, path, shared, newFile](const QString& error) {
                  // The tab may have been closed while the file was being written.
                  const bool alive =
                      std::any_of(m_documents.begin(), m_documents.end(),
                                  [target](const auto& d) { return d.get() == target; });
                  if (!error.isEmpty()) {
                      notice(m_window, tr("Couldn't save"), error);
                      emit fileError(error);
                      return;
                  }
                  if (alive) {
                      if (newFile) {
                          target->file = newFile;
                          target->gameIndex = 0;
                      } else {
                          target->file->text = *shared;
                          target->file->games = pgn::indexGames(target->file->text);
                      }
                      target->modified = false;
                      updateTitles();
                  }
                  emit message(tr("Saved %1").arg(QFileInfo(path).fileName()), kStatusMs);
                  emit fileSaved(path);
              });
}

// ---- Opening -----------------------------------------------------------------------------------

void Documents::openFile(const QString& path) {
    emit message(tr("Opening %1…").arg(QFileInfo(path).fileName()), 0);
    whenReady(this, pgnio::load(path), [this, path](const pgnio::Loaded& loaded) {
        emit message({}, 0);
        if (!loaded.file) {
            notice(m_window, tr("Couldn't open"), loaded.error);
            emit fileError(loaded.error);
            return;
        }
        if (loaded.file->games.empty()) {
            const QString message = tr("%1 contains no games.").arg(QFileInfo(path).fileName());
            notice(m_window, tr("No games"), message);
            emit fileError(message);
            return;
        }
        int index = 0;
        if (loaded.file->games.size() > 1) {
            // A file with several games: let the user pick one.
            QStringList items;
            for (std::size_t i = 0; i < loaded.file->games.size(); ++i) {
                const auto& game = loaded.file->games[i];
                items << QStringLiteral("%1.  %2 – %3   %4")
                             .arg(i + 1)
                             .arg(toQString(game.header("White")), toQString(game.header("Black")),
                                  toQString(game.header("Result")));
            }
            bool ok = false;
            const QString choice = QInputDialog::getItem(
                m_window, QFileInfo(path).fileName(),
                tr("This file has %n game(s). Which one?", nullptr, static_cast<int>(items.size())),
                items, 0, false, &ok);
            if (!ok) {
                return;
            }
            index = static_cast<int>(items.indexOf(choice));
        }
        if (index == 0 && !loaded.first) {
            notice(m_window, tr("Couldn't read the game"), loaded.error);
            emit fileError(loaded.error);
            return;
        }
        if (index == 0) {
            auto document = std::make_unique<GameDocument>();
            document->game = *loaded.first;
            document->file = loaded.file;
            document->gameIndex = 0;
            add(std::move(document));
            emit fileOpened(path);
        } else {
            openGame(loaded.file, index);
        }
    });
}

void Documents::openGame(const std::shared_ptr<PgnFile>& file, int index) {
    // Already open? Go there.
    for (int tab = 0; tab < m_tabs->count(); ++tab) {
        const GameDocument* d = at(tab);
        if (d != nullptr && d->file && !file->path.isEmpty() && d->file->path == file->path &&
            d->gameIndex == index) {
            m_tabs->setCurrentIndex(tab);
            emit shown();
            return;
        }
    }
    if (!file->hasText) {
        // From the Library, which keeps only the index: a game's tab saves into the file's text,
        // so it needs all of it. The game is found again by where it starts in the file.
        const std::size_t offset =
            index >= 0 && static_cast<std::size_t>(index) < file->games.size()
                ? file->games[static_cast<std::size_t>(index)].offset
                : 0;
        const auto indexIn = [index, offset](const PgnFile& full) {
            const auto& games = full.games;
            const auto match = std::find_if(games.begin(), games.end(),
                                            [offset](const auto& g) { return g.offset == offset; });
            return match != games.end() ? static_cast<int>(match - games.begin())
                                        : std::min(index, static_cast<int>(games.size()) - 1);
        };
        // Another tab already has this file: share its text, so the tabs' saves add up. Checked
        // again once the file is read, in case another tab opened it meanwhile.
        const auto openInTab = [this, indexIn, path = file->path] {
            if (const auto shared = sharedFile(path)) {
                openGame(shared, indexIn(*shared));
                return true;
            }
            return false;
        };
        if (openInTab()) {
            return;
        }
        whenReady(this, pgnio::load(file->path),
                  [this, indexIn, openInTab](const pgnio::Loaded& loaded) {
                      if (openInTab()) {
                          return;
                      }
                      if (!loaded.file) {
                          notice(m_window, tr("Couldn't read the game"), loaded.error);
                          emit fileError(loaded.error);
                          return;
                      }
                      openGame(loaded.file, indexIn(*loaded.file));
                  });
        return;
    }
    whenReady(this, pgnio::parse(file, index), [this, file, index](const pgnio::Parsed& parsed) {
        if (!parsed.game) {
            notice(m_window, tr("Couldn't read the game"), parsed.error);
            emit fileError(parsed.error);
            return;
        }
        auto document = std::make_unique<GameDocument>();
        document->game = *parsed.game;
        document->file = file;
        document->gameIndex = index;
        add(std::move(document));
        emit fileOpened(file->path);
    });
}

void Documents::openText(const QString& text) {
    const std::string trimmed = text.trimmed().toStdString();
    if (trimmed.empty()) {
        return;
    }
    // A FEN: a new game from that position.
    if (auto position = core::Position::fromFen(trimmed)) {
        auto document = std::make_unique<GameDocument>();
        document->game = Game(std::move(*position));
        add(std::move(document));
        return;
    }
    // Otherwise PGN: open its first game.
    whenReady(this, pgnio::loadText(trimmed), [this](const pgnio::Loaded& loaded) {
        if (!loaded.first) {
            const QString message = loaded.error.isEmpty()
                                        ? tr("The clipboard holds neither a FEN nor a PGN game.")
                                        : loaded.error;
            emit this->message(message, kStatusMs);
            emit fileError(message);
            return;
        }
        auto document = std::make_unique<GameDocument>();
        document->game = *loaded.first;
        document->modified = true; // pasted: not saved anywhere yet
        add(std::move(document));
    });
}

QString Documents::createFile(const QString& name, const std::string& text, const QString& folder) {
    // A name that is safe as a file name, and not taken yet.
    QString base = name;
    base.replace(QLatin1Char('/'), QLatin1Char('-')).replace(QLatin1Char(':'), QLatin1Char('-'));
    const QDir into(folder.isEmpty() ? m_library.folder() : folder);
    QString path = into.filePath(base + u".pgn");
    for (int n = 2; QFileInfo::exists(path); ++n) {
        path = into.filePath(QStringLiteral("%1 %2.pgn").arg(base).arg(n));
    }
    QString error = pgnio::write(path, text).result(); // small: written right away
    if (!error.isEmpty()) {
        return error;
    }
    m_library.rescan();
    openFile(path);
    return {};
}

// ---- Files changing underneath -----------------------------------------------------------------

std::shared_ptr<PgnFile> Documents::sharedFile(const QString& path) const {
    for (const auto& d : m_documents) {
        if (d->file && d->file->hasText && d->file->path == path) {
            return d->file;
        }
    }
    return nullptr;
}

void Documents::reload(const QString& path, const std::shared_ptr<PgnFile>& file) {
    for (const auto& d : m_documents) {
        if (!d->file || d->file->path != path) {
            continue;
        }
        d->file = file; // one shared text from now on, so edits from both sides add up
        if (d->gameIndex < 0 || std::cmp_greater_equal(d->gameIndex, file->games.size())) {
            continue;
        }
        const auto& entry = file->games[static_cast<std::size_t>(d->gameIndex)];
        auto game = pgn::parseGame(std::string_view(file->text).substr(entry.offset, entry.length));
        if (!game) {
            continue;
        }
        // Stay on the same move: follow the moves that led there in the reloaded game.
        const std::vector<core::Move> line = lineTo(d->game, d->current);
        d->game = std::move(*game);
        d->current = Game::root();
        for (const core::Move& move : line) {
            const auto children = d->game.children(d->current);
            const auto next = std::find_if(children.begin(), children.end(), [&](NodeId child) {
                return d->game.node(child).move == move;
            });
            if (next == children.end()) {
                break;
            }
            d->current = *next;
        }
        d->modified = false;
        emit reloaded(d->id);
    }
    updateTitles();
}

void Documents::movePath(const QString& from, const QString& to) {
    for (const auto& d : m_documents) {
        if (!d->file) {
            continue;
        }
        QString& path = d->file->path;
        if (path == from) {
            path = to;
        } else if (path.startsWith(from + QLatin1Char('/'))) {
            path = to + path.mid(from.size());
        }
    }
    updateTitles();
}

void Documents::closeUnder(const QString& path) {
    for (int tab = m_tabs->count() - 1; tab >= 0; --tab) {
        GameDocument* d = at(tab);
        if (d != nullptr && d->file &&
            (d->file->path == path || d->file->path.startsWith(path + QLatin1Char('/')))) {
            d->modified = false; // it's going: nothing to save
            close(tab);
        }
    }
}

} // namespace kiebitz::app
