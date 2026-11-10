#include "PieceSet.hpp"

#include <QPainter>
#include <QSvgRenderer>

#include <cmath>

namespace chessgui::app {

QStringList PieceSet::available() {
    // Folders under app/resources/pieces/. See the README there for authors and licences.
    return {QStringLiteral("cburnett"), QStringLiteral("merida"), QStringLiteral("chessnut"),
            QStringLiteral("fantasy")};
}

PieceSet::PieceSet(const QString& name)
    : m_name(available().contains(name) ? name : available().front()) {
    static constexpr std::array<char, 6> kLetters{'P', 'N', 'B', 'R', 'Q', 'K'};
    for (int color = 0; color < 2; ++color) {
        for (std::size_t type = 0; type < kLetters.size(); ++type) {
            const QString path = QStringLiteral(":/pieces/%1/%2%3.svg")
                                     .arg(m_name)
                                     .arg(QLatin1Char(color == 0 ? 'w' : 'b'))
                                     .arg(QLatin1Char(kLetters[type]));
            const auto index = static_cast<std::size_t>(color * 6) + type;
            m_renderers[index] = std::make_unique<QSvgRenderer>(path);
        }
    }
}

PieceSet::~PieceSet() = default;
PieceSet::PieceSet(PieceSet&&) noexcept = default;
PieceSet& PieceSet::operator=(PieceSet&&) noexcept = default;

int PieceSet::indexOf(core::Piece piece) {
    // PieceType: None = 0, Pawn = 1 ... King = 6
    return ((piece.color == core::Color::White ? 0 : 1) * 6) + static_cast<int>(piece.type) - 1;
}

QPixmap PieceSet::pixmap(core::Piece piece, int size, qreal devicePixelRatio) {
    if (piece.isNone() || size <= 0) {
        return {};
    }
    if (size != m_cacheSize || devicePixelRatio != m_cacheRatio) {
        m_cache = {};
        m_cacheSize = size;
        m_cacheRatio = devicePixelRatio;
    }
    const auto index = static_cast<std::size_t>(indexOf(piece));
    QPixmap& cached = m_cache[index];
    if (cached.isNull()) {
        const int pixels = static_cast<int>(std::lround(size * devicePixelRatio));
        cached = QPixmap(pixels, pixels);
        cached.fill(Qt::transparent);
        QPainter painter(&cached);
        painter.setRenderHint(QPainter::Antialiasing);
        m_renderers[index]->render(&painter);
        cached.setDevicePixelRatio(devicePixelRatio);
    }
    return cached;
}

} // namespace chessgui::app
