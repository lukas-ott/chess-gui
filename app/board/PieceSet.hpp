#pragma once

#include "kiebitz/core/types.hpp"

#include <QPixmap>
#include <QString>
#include <QStringList>

#include <array>
#include <memory>

class QSvgRenderer;

namespace kiebitz::app {

/// One set of 12 SVG pieces, rendered to pixmaps on demand.
///
/// SVG rendering is slow compared with drawing a pixmap, so each piece is rendered once per
/// square size and device pixel ratio and then reused for every repaint.
class PieceSet {
public:
    /// Names of the sets bundled with the app; the first is the default.
    [[nodiscard]] static QStringList available();

    /// Loads a bundled set. Falls back to the default set for an unknown name.
    explicit PieceSet(const QString& name = available().front());
    ~PieceSet();
    PieceSet(const PieceSet&) = delete;
    PieceSet& operator=(const PieceSet&) = delete;
    PieceSet(PieceSet&&) noexcept;
    PieceSet& operator=(PieceSet&&) noexcept;

    [[nodiscard]] const QString& name() const noexcept { return m_name; }

    /// The piece drawn into a square of `size` logical pixels. Empty for PieceType::None.
    [[nodiscard]] QPixmap pixmap(core::Piece piece, int size, qreal devicePixelRatio);

private:
    static constexpr int kPieceCount = 12;
    [[nodiscard]] static int indexOf(core::Piece piece);

    QString m_name;
    std::array<std::unique_ptr<QSvgRenderer>, kPieceCount> m_renderers;
    std::array<QPixmap, kPieceCount> m_cache;
    int m_cacheSize = 0;
    qreal m_cacheRatio = 0;
};

} // namespace kiebitz::app
