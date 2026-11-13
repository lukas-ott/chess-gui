#pragma once

#include <QString>

class QTranslator;

/// The app's language: German, Norwegian (Bokmål) or English (the texts in the code).
namespace kiebitz::app::language {

/// The language to show for a Settings choice ("system", "en", "de", "nb"): for "system", the
/// first of macOS's preferred languages Kiebitz has (Nynorsk and "no" get Bokmål); else
/// English.
[[nodiscard]] QString resolve(const QString& chosen);
/// Loads the translation for the language chosen in Settings into `translator` and installs
/// it (before any window exists). False for English, or if it couldn't be loaded.
bool install(QTranslator& translator);

} // namespace kiebitz::app::language
