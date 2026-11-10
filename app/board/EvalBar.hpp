#pragma once

#include "chessgui/engine/score.hpp"

#include <QWidget>

#include <optional>

class QVariantAnimation;

namespace chessgui::app {

/// The bar beside the board that shows who is better: White's share grows from White's side
/// (the bottom, unless the board is flipped), following the same winning-chances curve as
/// Lichess, with the score written at the leading end.
class EvalBar : public QWidget {
    Q_OBJECT

public:
    explicit EvalBar(QWidget* parent = nullptr);

    /// The score from White's view; nullopt shows an even bar with no number.
    void setScore(std::optional<engine::Score> score);
    [[nodiscard]] const std::optional<engine::Score>& score() const noexcept { return m_score; }
    void setFlipped(bool flipped);
    /// White's share of the bar, 0–1, as currently drawn (after the animation).
    [[nodiscard]] double whiteShare() const noexcept { return m_target; }

    [[nodiscard]] QSize sizeHint() const override { return {kWidth, 200}; }
    [[nodiscard]] QSize minimumSizeHint() const override { return {kWidth, 60}; }

    static constexpr int kWidth = 22;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::optional<engine::Score> m_score;
    bool m_flipped = false;
    double m_shown = 0.5; ///< White's share being drawn (animates towards m_target)
    double m_target = 0.5;
    QVariantAnimation* m_animation = nullptr;
};

} // namespace chessgui::app
