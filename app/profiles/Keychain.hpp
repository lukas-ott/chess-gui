#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

namespace kiebitz::app::keychain {

/// A secret in the macOS Keychain (a generic password), by service and account name. Only this
/// app can read what it saved without the user being asked.
[[nodiscard]] std::optional<QByteArray> load(const QString& service, const QString& account);
bool save(const QString& service, const QString& account, const QByteArray& secret);
void remove(const QString& service, const QString& account);

} // namespace kiebitz::app::keychain
