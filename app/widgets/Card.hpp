#pragma once

#include <QFrame>
#include <QLabel>
#include <QString>

namespace kiebitz::app::ui {

/// A rounded panel (styled through the "card" property in AppStyle).
inline QFrame* card() {
    auto* frame = new QFrame;
    frame->setProperty("card", true);
    return frame;
}

/// A label styled by its role in AppStyle ("cardTitle", "hint", ...).
inline QLabel* label(const QString& text, const char* role) {
    auto* result = new QLabel(text);
    result->setProperty("role", role);
    return result;
}

} // namespace kiebitz::app::ui
