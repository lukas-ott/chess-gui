#pragma once

#include <QString>

class QSettings;

/// The app used to be called ChessGui. macOS keeps an app's settings and its Application Support
/// folder under its name, so after the rename they would look empty. On the first launch as
/// Kiebitz this carries them over, once.
namespace kiebitz::app::migration {

/// Copies every setting from `from` that `to` doesn't have yet (never overwriting one).
/// Returns how many it copied. Both should have fallbacks off (see fromChessGui()).
int copySettings(const QSettings& from, QSettings& to);

/// Moves the downloaded engines from `oldFolder` to `newFolder`, if the old one is there and
/// the new one isn't yet. Returns whether it moved them.
bool moveEngines(const QString& oldFolder, const QString& newFolder);

/// Points engine paths in `settings` that are inside `oldFolder` at the same place inside
/// `newFolder`. Returns how many it changed.
int rewriteEnginePaths(const QString& oldFolder, const QString& newFolder, QSettings& settings);

/// All of the above, from ChessGui's settings and folders to this app's. Runs once (it leaves a
/// marker in the settings) and costs next to nothing after that.
void fromChessGui();

} // namespace kiebitz::app::migration
