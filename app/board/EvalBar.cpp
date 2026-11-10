#include "EvalBar.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>

namespace chessgui::app {
namespace {

// Like the board's squares, the bar's colours are part of the board's look, not the app theme:
// White's share is white, Black's is black.
const QColor kWhiteSide(0xF0, 0xF0, 0xF0);
const QColor kBlackSide(0x40, 0x3D, 0x39);
const QColor kMiddle(0x80, 0x80, 0x80, 140);
const QColor kTextOnWhite(0x40, 0x3D, 0x39);
const QColor kTextOnBlack(0xF0, 0xF0, 0xF0);

} // namespace

EvalBar::EvalBar(QWidget* parent) : QWidget(parent), m_animation(new QVariantAnimation(this)) {
    setObjectName(QStringLiteral("evalBar"));
    setFixedWidth(kWidth);
    m_animation->setDuration(250);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_shown = value.toDouble();
        update();
    });
}

void EvalBar::setScore(std::optional<engine::Score> score) {
    m_score = score;
    const double target = score ? (engine::winningChances(*score) + 1.0) / 2.0 : 0.5;
    setToolTip(score ? QString::fromStdString(engine::formatScore(*score)) : QString());
    if (target == m_target) {
        update(); // the text may still have changed
        return;
    }
    m_target = target;
    m_animation->stop();
    if (!isVisible()) {
        m_shown = target;
        update();
        return;
    }
    m_animation->setStartValue(m_shown);
    m_animation->setEndValue(target);
    m_animation->start();
}

void EvalBar::setFlipped(bool flipped) {
    m_flipped = flipped;
    update();
}

void EvalBar::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF bar = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath clip;
    clip.addRoundedRect(bar, 4, 4);
    painter.setClipPath(clip);

    painter.fillRect(bar, kBlackSide);
    const qreal white = bar.height() * m_shown;
    // White's share grows from White's side of the board.
    const QRectF whiteRect = m_flipped
                                 ? QRectF(bar.left(), bar.top(), bar.width(), white)
                                 : QRectF(bar.left(), bar.bottom() - white, bar.width(), white);
    painter.fillRect(whiteRect, kWhiteSide);
    painter.fillRect(QRectF(bar.left(), bar.center().y() - 0.5, bar.width(), 1), kMiddle);

    if (!m_score) {
        return;
    }
    // The score, small, at the end of whoever is better (as chess.com shows it): one decimal,
    // without a sign, since the side it's written on already says who is better.
    QString text = QString::fromStdString(engine::formatScore(*m_score));
    if (text.startsWith(QLatin1Char('+')) || text.startsWith(QLatin1Char('-'))) {
        text.remove(0, 1);
    }
    if (m_score->kind == engine::Score::Kind::Centipawns) {
        text = QString::number(std::abs(m_score->value) / 100.0, 'f', 1);
    }
    const bool whiteBetter = m_score->value >= 0;
    QFont font = painter.font();
    font.setPixelSize(9);
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.setPen(whiteBetter ? kTextOnWhite : kTextOnBlack);
    const bool atBottom = whiteBetter != m_flipped;
    const QRectF textRect = atBottom ? QRectF(bar.left(), bar.bottom() - 16, bar.width(), 14)
                                     : QRectF(bar.left(), bar.top() + 2, bar.width(), 14);
    painter.drawText(textRect, Qt::AlignCenter, text);
}

} // namespace chessgui::app
