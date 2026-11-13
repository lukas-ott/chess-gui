#include "Stepper.hpp"

#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
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
    // The number (typed digits only) and its unit beside it, centred together between − and +.
    m_edit = new QLineEdit;
    m_edit->setProperty("role", "stepperValue");
    m_edit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_edit->setValidator(new QIntValidator(0, 999'999'999, m_edit));
    connect(m_edit, &QLineEdit::textEdited, this, [this] { fitEdit(); });
    connect(m_edit, &QLineEdit::editingFinished, this, [this] {
        bool ok = false;
        const int typed = m_edit->text().toInt(&ok);
        commit(ok ? typed : m_value);
    });
    m_suffixLabel = new QLabel;
    m_suffixLabel->setProperty("role", "stepperSuffix");
    m_suffixLabel->hide();
    auto* value = new QWidget;
    value->setFixedWidth(72);
    auto* valueRow = new QHBoxLayout(value);
    valueRow->setContentsMargins(0, 0, 0, 0);
    valueRow->setSpacing(0);
    valueRow->addStretch();
    valueRow->addWidget(m_edit);
    valueRow->addWidget(m_suffixLabel);
    valueRow->addStretch();
    row->addWidget(step(QStringLiteral("−"), QStringLiteral("stepDown"), -1));
    row->addWidget(value);
    row->addWidget(step(QStringLiteral("+"), QStringLiteral("stepUp"), 1));
    showValue();
}

void Stepper::setRange(int minimum, int maximum) {
    m_minimum = minimum;
    m_maximum = std::max(minimum, maximum);
    m_value = std::clamp(m_value, m_minimum, m_maximum);
    fitEdit();
    showValue();
}

void Stepper::setSuffix(const QString& suffix) {
    m_suffix = suffix;
    m_suffixLabel->setText(suffix);
    m_suffixLabel->setVisible(!suffix.isEmpty());
    fitEdit();
    showValue();
}

void Stepper::fitEdit() {
    // Without a unit: the whole width. With one: as wide as the digits, so number and unit
    // stay centred together (the stepper itself never changes width).
    if (m_suffix.isEmpty()) {
        m_edit->setFixedWidth(72);
        m_edit->setAlignment(Qt::AlignCenter);
        return;
    }
    const QString digits = m_edit->text().isEmpty() ? QStringLiteral("0") : m_edit->text();
    const int room = 72 - m_suffixLabel->sizeHint().width();
    m_edit->setFixedWidth(std::min(room, m_edit->fontMetrics().horizontalAdvance(digits) + 4));
    m_edit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
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
    m_edit->setText(QString::number(m_value));
    fitEdit();
}

} // namespace kiebitz::app
