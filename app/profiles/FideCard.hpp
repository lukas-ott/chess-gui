#pragma once

#include "Fide.hpp"

#include <QFrame>

#include <optional>
#include <vector>

namespace kiebitz::app {

class AppStyle;

/// The info card under a profile's "i": its FIDE player (name, title, federation, ratings,
/// year of birth, FIDE ID) with a link to the profile on ratings.fide.com, and "Not them?" to
/// pick another player who fits the name, or none.
class FideCard : public QFrame {
    Q_OBJECT

public:
    /// `status` is shown when there is no player (still looking, nothing found, ...).
    /// `retry`: offer "Try Again" (FIDE didn't answer, or found no one).
    FideCard(const std::optional<FidePlayer>& player, const std::vector<FidePlayer>& candidates,
             const QString& status, bool retry, const AppStyle& style, QWidget* parent = nullptr);

signals:
    /// A player picked from the candidates (0: none of them).
    void chosen(int fideId);
    void retryRequested();
};

} // namespace kiebitz::app
