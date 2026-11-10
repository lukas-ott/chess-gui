#pragma once

#include "style/Icons.hpp"

#include <QWidget>

class QLabel;

namespace kiebitz::app {

class AppStyle;

/// What an empty area shows, the same everywhere: an icon in a soft circle, a short title and,
/// if needed, one line under it ("No engine selected", "No game selected").
class EmptyState : public QWidget {
    Q_OBJECT

public:
    EmptyState(IconKind icon, const QString& title, const QString& hint, const AppStyle& style,
               QWidget* parent = nullptr);

private:
    void updateIcon();

    IconKind m_kind;
    const AppStyle& m_style;
    QLabel* m_icon = nullptr;
};

} // namespace kiebitz::app
