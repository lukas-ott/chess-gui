#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace kiebitz::puzzles {
class PuzzleDb;
}

namespace kiebitz::app {

/// The Lichess puzzle database on this Mac: downloaded once (lichess_db_puzzle.csv.zst, CC0,
/// ~300 MB), imported in the background into one compact file, then opened (mapped) whenever
/// the Puzzles page needs it. The download is removed after the import.
class LichessPuzzles : public QObject {
    Q_OBJECT

public:
    enum class State : std::uint8_t { Missing, Downloading, Importing, Ready };

    /// `folder`: where the puzzle file goes (empty: "Puzzles" in the app's data folder; tests
    /// set KIEBITZ_SETTINGS_DIR).
    explicit LichessPuzzles(QString folder = {}, QObject* parent = nullptr);
    ~LichessPuzzles() override;
    LichessPuzzles(const LichessPuzzles&) = delete;
    LichessPuzzles& operator=(const LichessPuzzles&) = delete;
    LichessPuzzles(LichessPuzzles&&) = delete;
    LichessPuzzles& operator=(LichessPuzzles&&) = delete;

    static constexpr auto kUrl = "https://database.lichess.org/lichess_db_puzzle.csv.zst";
    static constexpr qint64 kDownloadBytes = 310'000'000; ///< about, until the server says

    [[nodiscard]] State state() const noexcept { return m_state; }
    /// The puzzles, or null until they're there. Opened on first use (mapping is instant).
    [[nodiscard]] std::shared_ptr<const puzzles::PuzzleDb> db();
    /// Progress of the download or import, 0 to 1; and what it says ("Downloading… 120 MB").
    [[nodiscard]] double fraction() const noexcept { return m_fraction; }
    [[nodiscard]] QString status() const { return m_status; }

    void download();
    /// The import on its own, from a file already on disk (download() ends with it; tests).
    void importFile(const QString& path, bool removeAfter);
    void cancel();
    /// Deletes the puzzle file (to free the space).
    void remove();

    [[nodiscard]] QString file() const;

signals:
    void stateChanged();
    void progressChanged();
    void failed(const QString& message);

private:
    void setState(State state);
    void fail(const QString& message);

    QString m_folder;
    State m_state = State::Missing;
    std::shared_ptr<const puzzles::PuzzleDb> m_db;
    QNetworkAccessManager* m_network = nullptr;
    QNetworkReply* m_reply = nullptr;
    std::unique_ptr<QFile> m_download;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    std::shared_ptr<std::atomic<std::uint64_t>> m_read; ///< bytes the import has read
    QTimer* m_poll = nullptr;                           ///< the import's progress
    qint64 m_importBytes = 0;
    double m_fraction = 0;
    QString m_status;
};

} // namespace kiebitz::app
