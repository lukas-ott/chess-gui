#include "board/PieceSet.hpp"

#include <QImage>
#include <gtest/gtest.h>

namespace {

using chessgui::app::PieceSet;
using namespace chessgui::core;

/// Every bundled set has all 12 pieces, and they actually draw something.
TEST(PieceSet, AllSetsRenderAllPieces) {
    for (const auto& name : PieceSet::available()) {
        PieceSet set(name);
        EXPECT_EQ(set.name(), name);
        for (const Color color : {Color::White, Color::Black}) {
            for (int type = 1; type <= 6; ++type) {
                const Piece piece{static_cast<PieceType>(type), color};
                const QImage image = set.pixmap(piece, 64, 2.0).toImage();
                ASSERT_FALSE(image.isNull()) << name.toStdString() << " " << piece.fenChar();
                EXPECT_EQ(image.width(), 128); // 64 logical px at 2x
                // Some pixel in the middle row is not transparent.
                bool drawn = false;
                for (int x = 0; x < image.width() && !drawn; ++x) {
                    drawn = qAlpha(image.pixel(x, image.height() / 2)) > 0;
                }
                EXPECT_TRUE(drawn) << name.toStdString() << " " << piece.fenChar();
            }
        }
    }
}

TEST(PieceSet, UnknownNameFallsBackToDefault) {
    const PieceSet set(QStringLiteral("does-not-exist"));
    EXPECT_EQ(set.name(), PieceSet::available().front());
}

TEST(PieceSet, NoPieceIsEmpty) {
    PieceSet set;
    EXPECT_TRUE(set.pixmap(Piece{}, 64, 1.0).isNull());
}

} // namespace
