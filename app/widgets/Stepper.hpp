#pragma once

#include <QWidget>

class QLineEdit;

namespace kiebitz::app {

/// A number between − and + buttons, for settings like threads and hash size: easier to hit
/// and cleaner than a spin box's tiny arrows. The number can also be typed.
class Stepper : public QWidget {
    Q_OBJECT

public:
    explicit Stepper(QWidget* parent = nullptr);

    void setRange(int minimum, int maximum);
    void setSingleStep(int step) { m_step = step; }
    /// Shown after the number ("256 MB"); typing it is optional.
    void setSuffix(const QString& suffix);
    void setValue(int value);
    [[nodiscard]] int value() const noexcept { return m_value; }

signals:
    /// A new value was chosen (a click on − or +, or a typed number confirmed with Return or by
    /// leaving the field). Not emitted by setValue().
    void valueChanged(int value);

private:
    void commit(int value);
    void showValue();

    QLineEdit* m_edit = nullptr;
    int m_value = 0;
    int m_minimum = 0;
    int m_maximum = 99;
    int m_step = 1;
    QString m_suffix;
};

} // namespace kiebitz::app
