#pragma once

#include <QString>

#include <vector>

namespace kiebitz::app {

/// A UCI engine the user added, and how to run it.
struct EngineEntry {
    QString name; ///< shown in the UI ("Stockfish 19"); unique
    QString path; ///< the executable; never shown in the UI
    int threads = defaultThreads();
    int hashMb = 256;
    int lines = defaultLines(); ///< how many lines (MultiPV) it shows

    /// Lines for a new engine: what the user last chose for the Engine tab, else 3.
    [[nodiscard]] static int defaultLines();

    /// Half the CPU cores (at least 1): fast, and leaves the Mac responsive.
    [[nodiscard]] static int defaultThreads();
};

/// The engine list, stored in the settings.
namespace engines {

constexpr int kMaxLines = 5;
/// Lines for the Lichess cloud, which isn't in the list.
[[nodiscard]] int cloudLines();
void setCloudLines(int lines);

[[nodiscard]] std::vector<EngineEntry> load();
void save(const std::vector<EngineEntry>& list);
/// A Stockfish already on this Mac (Homebrew), if there is one: offered as a shortcut.
[[nodiscard]] QString findInstalledStockfish();
/// Where downloaded engines go (in Application Support).
[[nodiscard]] QString downloadFolder();

} // namespace engines
} // namespace kiebitz::app
