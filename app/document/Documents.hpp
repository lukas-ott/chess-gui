#pragma once

#include "document/GameDocument.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class QTabBar;
class QWidget;

namespace kiebitz::app {

class AppStyle;
class Library;

/// The open games and the tab bar that shows them, one tab per game: opening files and games,
/// closing tabs, and saving. Games from a file are saved on their own a second after a change
/// (and when their tab closes or the app quits); other games just go when closed.
///
/// The window asks for the board (shown()) and shows the game of the current tab; it follows
/// the tab bar's currentChanged.
class Documents : public QObject {
    Q_OBJECT

public:
    /// `window`: the parent of dialogs and messages.
    Documents(Library& library, AppStyle& style, QWidget* window);
    ~Documents() override;
    Documents(const Documents&) = delete;
    Documents& operator=(const Documents&) = delete;
    Documents(Documents&&) = delete;
    Documents& operator=(Documents&&) = delete;

    /// The tab bar (the window puts it in its layout).
    [[nodiscard]] QTabBar* tabs() const { return m_tabs; }
    [[nodiscard]] GameDocument* at(int tab) const;
    /// The game of the current tab (whether or not the board is the page shown).
    [[nodiscard]] GameDocument* current() const;
    [[nodiscard]] const std::vector<std::unique_ptr<GameDocument>>& all() const {
        return m_documents;
    }

    /// Shows a game in a tab: a new one, or the tab of an untouched empty game.
    void add(std::unique_ptr<GameDocument> document);
    /// Closes a tab; a game from a file is saved first (if it can't be, the tab stays).
    void close(int tab);
    /// Shows the first tab whose game matches, and returns that game (null: none).
    GameDocument* select(const std::function<bool(const GameDocument&)>& matches);

    /// Opens a PGN file (in the background) and shows its first game, or the one picked.
    void openFile(const QString& path);
    /// Opens one game of a file in a tab (or switches to it if it's already open).
    void openGame(const std::shared_ptr<PgnFile>& file, int index);
    /// A FEN (a new game from there) or PGN (its first game), e.g. from the clipboard.
    void openText(const QString& text);
    /// Writes a new file into `folder` (empty = the Library folder; a free name) and opens it.
    /// Empty on success, else the error.
    QString createFile(const QString& name, const std::string& text, const QString& folder = {});

    /// A game changed: marked, and saved a second after the last change if it's in a file.
    void setModified(GameDocument& document);
    /// ⌘S: written now (games not in a file: Save As).
    void save(GameDocument& document);
    /// Asks where, then saveAs(document, path).
    void saveAs(GameDocument& document);
    /// A new file with just this game.
    void saveAs(GameDocument& document, const QString& path);
    /// Writes every changed game that lives in a file. Games of the same file go into its text
    /// one after another, then the file is written once.
    void autosave();
    /// Before quitting: every game from a file saved, and waited for. False if one couldn't be.
    bool saveAll();
    [[nodiscard]] static bool inFile(const GameDocument& document);

    /// Tab titles (and tooltips for long ones) from the games; then titlesChanged().
    void updateTitles();
    /// The icons drawn in the tabs, after the colours changed.
    void restyle();

    /// The text of a file a tab has open (so edits from elsewhere go into the same text), or null.
    [[nodiscard]] std::shared_ptr<PgnFile> sharedFile(const QString& path) const;
    /// A file changed under open games: they reload from it, staying on the same move.
    void reload(const QString& path, const std::shared_ptr<PgnFile>& file);
    /// A file or folder moved: its tabs save to where it is now.
    void movePath(const QString& from, const QString& to);
    /// A file or folder went: its tabs close, with nothing to save.
    void closeUnder(const QString& path);

signals:
    /// A tab was added or chosen: the board page should show it.
    void shown();
    void titlesChanged();
    /// A game was read again from its file (reload()).
    void reloaded(int documentId);
    void fileOpened(const QString& path);
    void fileSaved(const QString& path);
    void fileError(const QString& message);
    /// For the status bar; `ms` 0: until the next message.
    void message(const QString& text, int ms);

private:
    /// Saved right away (it autosaves anyway), waiting for it. False if it couldn't be written
    /// (it says why). Games not in a file: nothing to do.
    bool saveNow(GameDocument& document);
    void write(GameDocument& document, const QString& path, std::string text,
               const std::shared_ptr<PgnFile>& newFile);

    Library& m_library;
    AppStyle& m_style;
    QWidget* m_window;
    std::vector<std::unique_ptr<GameDocument>> m_documents;
    QTabBar* m_tabs = nullptr;
    QTimer m_autosave;     ///< a second after the last change, changed games are saved
    QStringList m_writing; ///< files being written now (the next autosave waits for them)
};

} // namespace kiebitz::app
