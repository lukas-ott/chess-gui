#include "Stepper.hpp"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>

#include <algorithm>

namespace kiebitz::app {

Stepper::Stepper(QWidget* parent) : QWidget(parent) {
    setProperty("role", "stepper");
    setAttribute(Qt::WA_StyledBackground); // draws the track from the style sheet
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(2, 2, 2, 2);
    row->setSpacing(0);
    const auto step = [this](const QString& text, const QString& name, int direction) {
        auto* button = new QPushButton(text);
        button->setObjectName(name);
        button->setProperty("role", "step");
        button->setFocusPolicy(Qt::NoFocus);
        button->setAutoRepeat(true); // hold to keep stepping
        connect(button, &QPushButton::clicked, this,
                [this, direction] { commit(m_value + (direction * m_step)); });
        return button;
    };
    m_edit = new QLineEdit;
    m_edit->setProperty("role", "stepperValue");
    m_edit->setAlignment(Qt::AlignCenter);
    m_edit->setFixedWidth(72);
    connect(m_edit, &QLineEdit::editingFinished, this, [this] {
        // Only the digits count: "256 MB", "256" and "256MB" all mean 256.
        QString digits;
        for (const QChar c : m_edit->text()) {
            if (c.isDigit()) {
                digits += c;
            }
        }
        bool ok = false;
        const int typed = digits.toInt(&ok);
        commit(ok ? typed : m_value);
    });
    row->addWidget(step(QStringLiteral("−"), QStringLiteral("stepDown"), -1));
    row->addWidget(m_edit);
    row->addWidget(step(QStringLiteral("+"), QStringLiteral("stepUp"), 1));
    showValue();
}

void Stepper::setRange(int minimum, int maximum) {
    m_minimum = minimum;
    m_maximum = std::max(minimum, maximum);
    m_value = std::clamp(m_value, m_minimum, m_maximum);
    showValue();
}

void Stepper::setSuffix(const QString& suffix) {
    m_suffix = suffix;
    showValue();
}

void Stepper::setValue(int value) {
    m_value = std::clamp(value, m_minimum, m_maximum);
    showValue();
}

void Stepper::commit(int value) {
    value = std::clamp(value, m_minimum, m_maximum);
    const bool changed = value != m_value;
    m_value = value;
    showValue();
    if (changed) {
        emit valueChanged(value);
    }
}

void Stepper::showValue() {
    m_edit->setText(QString::number(m_value) + m_suffix);
}

} // namespace kiebitz::app
