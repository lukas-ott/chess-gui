#pragma once

#include "document/GameDocument.hpp"
#include "kiebitz/library/file_kind.hpp"
#include "kiebitz/repertoire/openings.hpp"
#include "kiebitz/repertoire/tree.hpp"

#include <QObject>
#include <QString>
#include <QStringList>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace kiebitz::app {

class Library;

/// One file of a repertoire, with its text and games, as the tree was built from.
struct RepertoireFile {
    std::shared_ptr<PgnFile> file; ///< with its text; shared with tabs that show its games
    std::vector<core::Game> games; ///< parsed; index = game index in the file
    std::size_t textHash = 0;      ///< of the text the games were parsed from
};

/// All repertoire files of one side, merged into one tree of positions.
struct BuiltRepertoire {
    core::Color side = core::Color::White;
    std::vector<RepertoireFile> files;
    repertoire::Tree tree;
};

/// The user's repertoires, by side.
///
/// A file is a repertoire when its type says so ([FileType "Repertoire"], chosen in the
/// Library); which side it is for comes from its [Orientation] header. All repertoire files of
/// a side are merged into one tree, so a game can be checked against every one of them at once,
/// and the tree knows which file each position comes from ("in book in Philidor Defense").
///
/// The store watches the Library, rebuilds a side's tree in the background when its files
/// change, adds lines to the right file (and saves it), changes file types, and loads the
/// opening names on first use.
class RepertoireStore : public QObject {
    Q_OBJECT

public:
    explicit RepertoireStore(Library& library, QObject* parent = nullptr);

    [[nodiscard]] static library::FileKind kindOf(const PgnFile& file);
    /// The side its [Orientation] headers say, if they say one.
    [[nodiscard]] static std::optional<core::Color> orientationOf(const PgnFile& file);
    /// A repertoire file's side: its [Orientation], else White.
    [[nodiscard]] static core::Color sideOf(const PgnFile& file);

    /// The built repertoire of a side, or null (no repertoire files, or still being read).
    [[nodiscard]] const BuiltRepertoire* get(core::Color side) const;
    /// The Library's repertoire files of a side, known before they are built.
    [[nodiscard]] const QStringList& files(core::Color side) const;
    [[nodiscard]] bool isBuilding(core::Color side) const;

    /// Opening names; null until loaded (loadOpenings() starts it, openingsReady() follows).
    [[nodiscard]] const repertoire::OpeningNames* openings() const { return m_openings.get(); }
    /// The same, shared, for work in the background.
    [[nodiscard]] std::shared_ptr<const repertoire::OpeningNames> sharedOpenings() const {
        return m_openings;
    }
    /// A repertoire file's parsed games as the built tree has them, or null (not a repertoire
    /// file, or not built yet).
    [[nodiscard]] const RepertoireFile* builtFile(const QString& path) const;
    void loadOpenings();

    /// Sets the type of files ([FileType] in every game). A repertoire also gets its side as
    /// [Orientation].
    void setKind(const QStringList& paths, library::FileKind kind,
                 std::optional<core::Color> side = std::nullopt);

    /// Adding a line (moves from the starting position) to a side's repertoire.
    enum class Outcome : std::uint8_t {
        Added,
        AlreadyThere,
        Conflict, ///< the repertoire answers differently where it is our move
        NoFile,   ///< not built (yet)
    };
    struct Added {
        Outcome outcome = Outcome::NoFile;
        int ply = 0;                                    ///< where the line left the repertoire
        std::vector<repertoire::Continuation> expected; ///< for a conflict: what it plays there
        QString path;                                   ///< the file it went into
    };
    /// Adds the moves of `line` the repertoire doesn't have yet, into the file where the line
    /// leaves it, and saves that file. A second answer where it's our move is only added with
    /// `allowSecondAnswer`.
    Added addLine(core::Color side, std::span<const core::Move> line, bool allowSecondAnswer);

    /// The files (paths) of a side's repertoire that have this position.
    [[nodiscard]] QStringList filesWith(core::Color side, std::uint64_t position) const;

    /// The text of a repertoire file as the store has it (shared, so edits add up), or null.
    [[nodiscard]] std::shared_ptr<PgnFile> sharedFile(const QString& path) const;

    /// Called before an edit: the window writes pending changes of open games into their files'
    /// texts first, so the store builds on them.
    void setBeforeEdit(std::function<void()> hook) { m_beforeEdit = std::move(hook); }
    /// The text of a file as an open tab has it (or null): edits start from there.
    void setOpenText(std::function<std::shared_ptr<PgnFile>(const QString& path)> hook) {
        m_openText = std::move(hook);
    }

signals:
    /// A side's repertoire was (re)built, or its files changed.
    void changed(kiebitz::core::Color side);
    void openingsReady();
    /// The store changed a file's text (a line added, a header set): open games of it reload
    /// from `file`, which holds the new text.
    void fileEdited(const QString& path, std::shared_ptr<kiebitz::app::PgnFile> file);
    void failed(const QString& message);

private:
    struct Side {
        QStringList files;
        QStringList builtFrom; ///< "path|size|mtime" the current build used
        std::shared_ptr<BuiltRepertoire> built;
        int generation = 0; ///< a newer build or edit makes older results stale
        bool building = false;
    };

    void libraryChanged();
    void rebuild(core::Color side);
    /// Re-reads games whose text changed since the build (a tab saved into the file).
    static void catchUp(BuiltRepertoire& built);
    void write(const std::shared_ptr<PgnFile>& file);
    /// Changes the headers of many files in the background, then saves them.
    void editHeaders(const QStringList& paths,
                     const std::function<std::string(const std::string&)>& change);
    [[nodiscard]] Side& sideData(core::Color side) {
        return m_sides[side == core::Color::White ? 0 : 1];
    }
    [[nodiscard]] const Side& sideData(core::Color side) const {
        return m_sides[side == core::Color::White ? 0 : 1];
    }

    Library& m_library;
    std::array<Side, 2> m_sides;
    int m_pendingWrites = 0;
    std::shared_ptr<const repertoire::OpeningNames> m_openings;
    bool m_openingsLoading = false;
    std::function<void()> m_beforeEdit;
    std::function<std::shared_ptr<PgnFile>(const QString& path)> m_openText;
};

} // namespace kiebitz::app
