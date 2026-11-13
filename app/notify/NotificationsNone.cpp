// Not on macOS: no Notification Centre to show them in. Training still says so in the status
// bar and with a sound.

#include "Notifications.hpp"

#include <utility>

namespace kiebitz::app::notifications {
namespace {

std::function<void(const QString&, const QString&)>& sink() {
    static std::function<void(const QString&, const QString&)> function;
    return function;
}

} // namespace

void requestPermission(std::function<void(const QString& why)> /*denied*/) {}

void post(const QString& title, const QString& body) {
    if (sink()) {
        sink()(title, body);
    }
}

void setSinkForTests(std::function<void(const QString& title, const QString& body)> function) {
    sink() = std::move(function);
}

} // namespace kiebitz::app::notifications
