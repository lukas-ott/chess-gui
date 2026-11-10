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

    /// Half the CPU cores (at least 1): fast, and leaves the Mac responsive.
    [[nodiscard]] static int defaultThreads();
};

/// The engine list, stored in the settings.
namespace engines {

[[nodiscard]] std::vector<EngineEntry> load();
void save(const std::vector<EngineEntry>& list);
/// A Stockfish already on this Mac (Homebrew), if there is one: offered as a shortcut.
[[nodiscard]] QString findInstalledStockfish();
/// Where downloaded engines go (in Application Support).
[[nodiscard]] QString downloadFolder();

} // namespace engines
} // namespace kiebitz::app
