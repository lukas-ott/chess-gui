#pragma once

#include <QCryptographicHash>
#include <QObject>
#include <QString>

#include <memory>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

namespace kiebitz::app {

/// Downloads the latest official Stockfish for macOS from its GitHub releases, the way En
/// Croissant offers it: one click, no browser.
///
/// Steps, all in the background: ask GitHub for the latest release, pick the macOS file
/// (engine::pickMacDownload), download it while computing its SHA-256, check that against the
/// checksum GitHub publishes, unpack it (macOS's own tar), and find the engine inside.
class StockfishDownload : public QObject {
    Q_OBJECT

public:
    /// `folder` is where engines are kept (engines::downloadFolder() in the app).
    explicit StockfishDownload(QString folder, QObject* parent = nullptr);
    ~StockfishDownload() override;
    StockfishDownload(const StockfishDownload&) = delete;
    StockfishDownload& operator=(const StockfishDownload&) = delete;
    StockfishDownload(StockfishDownload&&) = delete;
    StockfishDownload& operator=(StockfishDownload&&) = delete;

    void start();
    void cancel();
    [[nodiscard]] bool isRunning() const noexcept { return m_running; }

    /// The last steps on their own: unpacks `archive` into `<folder>/<version>` and finds the
    /// engine. start() ends with this; tests call it directly.
    void install(const QString& archive, const QString& version);

    /// The engine executable in an unpacked download: a Mach-O file named "stockfish…".
    [[nodiscard]] static QString findEngine(const QString& folder);

signals:
    /// Which release is being fetched ("Stockfish 19"), once known.
    void releaseFound(const QString& name, qint64 bytes);
    void progress(qint64 received, qint64 total);
    /// Done: the engine is at `path`.
    void finished(const QString& name, const QString& path);
    void failed(const QString& message);

private:
    void fetchRelease();
    void download(const QString& url, qint64 size, const QString& sha256);
    void fail(const QString& message);
    void cleanUp();

    QString m_folder;
    QNetworkAccessManager* m_network = nullptr;
    QNetworkReply* m_reply = nullptr;
    QProcess* m_tar = nullptr;
    std::unique_ptr<QFile> m_file;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    QString m_name;    ///< "Stockfish 19"
    QString m_version; ///< "sf_19": the folder it goes into
    bool m_running = false;
};

} // namespace kiebitz::app
