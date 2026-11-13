#pragma once

#include <QString>

#include <functional>

/// Notifications in macOS's Notification Centre (a banner, also while Kiebitz is in front):
/// e.g. that a training part's time is up. They need your permission, which macOS asks for
/// once. Outside an app bundle (tests) nothing is shown.
namespace kiebitz::app::notifications {

/// Asks macOS for permission to show notifications; it asks you only the first time. `denied`
/// is called (on the UI thread) if notifications are off or macOS refused, with why.
void requestPermission(std::function<void(const QString& why)> denied = {});
/// Shows a notification, if allowed.
void post(const QString& title, const QString& body);
/// Tests: notifications go here instead (empty: to macOS again).
void setSinkForTests(std::function<void(const QString& title, const QString& body)> sink);

} // namespace kiebitz::app::notifications
