#pragma once

#include "Fide.hpp"

#include <QFrame>

#include <optional>
#include <vector>

namespace kiebitz::app {

class AppStyle;

/// The info card under a profile's "i": its FIDE player (name, title, federation, ratings,
/// year of birth, FIDE ID) with a link to the player's page (on Lichess), and "Not them?" to
/// pick another player who fits the name, or none.
class FideCard : public QFrame {
    Q_OBJECT

public:
    /// `status` is shown when there is no player (still looking, nothing found, ...).
    /// `retry`: offer "Try Again" (FIDE didn't answer, or found no one).
    /// `confirmed`: the player was said to be them (asked no more); `you`: the main profile.
    FideCard(const std::optional<FidePlayer>& player, const std::vector<FidePlayer>& candidates,
             const QString& status, bool retry, bool confirmed, bool you, const AppStyle& style,
             QWidget* parent = nullptr);

signals:
    /// A player picked from the candidates (0: none of them).
    void chosen(int fideId);
    void retryRequested();
};

} // namespace kiebitz::app
