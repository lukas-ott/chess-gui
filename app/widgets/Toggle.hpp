#pragma once

#include <QAbstractButton>

namespace kiebitz::app {

class AppStyle;

/// An on/off switch, as macOS draws them: a pill with a knob, in the accent colour when on.
class Toggle : public QAbstractButton {
    Q_OBJECT

public:
    explicit Toggle(const AppStyle& style, QWidget* parent = nullptr);
    [[nodiscard]] QSize sizeHint() const override { return {36, 20}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    const AppStyle& m_style;
};

} // namespace kiebitz::app
