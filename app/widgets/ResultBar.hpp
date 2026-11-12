#pragma once

#include <QWidget>

#include <array>
#include <cstdint>

namespace kiebitz::app {

class AppStyle;

/// A bar in three parts with their shares written in when there is room: White won, drawn,
/// Black won (as Lichess and En Croissant show results), or a player's won, drawn, lost.
class ResultBar : public QWidget {
public:
    enum class Kind : std::uint8_t { Results, Record };
    explicit ResultBar(const AppStyle& style, int height = 18, Kind kind = Kind::Results,
                       QWidget* parent = nullptr);
    void setResults(std::uint64_t white, std::uint64_t draws, std::uint64_t black);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    const AppStyle& m_style;
    Kind m_kind;
    std::array<std::uint64_t, 3> m_counts{};
};

} // namespace kiebitz::app
