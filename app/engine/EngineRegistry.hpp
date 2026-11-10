#pragma once

#include "EngineSettings.hpp"

#include <QObject>
#include <QString>

#include <vector>

namespace chessgui::app {

class StockfishDownload;

/// The engines the user has (kept in the settings), and the ways to get more: download
/// Stockfish, use one already on the Mac, or add any UCI engine by its file. The Engines page
/// edits it; the analysis panel reads it.
class EngineRegistry : public QObject {
    Q_OBJECT

public:
    explicit EngineRegistry(QObject* parent = nullptr);

    [[nodiscard]] const std::vector<EngineEntry>& engines() const noexcept { return m_engines; }
    [[nodiscard]] const EngineEntry* find(const QString& name) const;

    /// Adds an engine, or replaces the one with the same name.
    void add(const EngineEntry& entry);
    void remove(const QString& name);

    /// Asks the program at `path` for its name in the background ("uci" → "id name …"), which
    /// also shows whether it is a UCI engine at all, then adds it (added() or failed()).
    void addFromFile(const QString& path);
    /// Downloads the latest Stockfish (progress through download()'s signals), then adds it.
    void downloadStockfish();
    [[nodiscard]] StockfishDownload* download();
    [[nodiscard]] bool isDownloading() const;

signals:
    /// The list changed (added, removed, settings).
    void changed();
    /// An engine was added (by the user, a download, or a file).
    void added(const QString& name);
    /// Adding or downloading an engine failed.
    void failed(const QString& message);

private:
    std::vector<EngineEntry> m_engines;
    StockfishDownload* m_download = nullptr;
};

} // namespace chessgui::app
