#pragma once

#include "ProfileStore.hpp"

#include <QDialog>

class QButtonGroup;
class QLineEdit;

namespace kiebitz::app {

/// "Add Account": Lichess or chess.com, and the username. Public games need no login, so any
/// player's account can be added (yours, a friend's, an opponent's).
class AddAccountDialog : public QDialog {
    Q_OBJECT

public:
    explicit AddAccountDialog(QWidget* parent = nullptr);
    [[nodiscard]] AccountSite site() const;
    [[nodiscard]] QString username() const;

private:
    QButtonGroup* m_sites = nullptr;
    QLineEdit* m_username = nullptr;
};

} // namespace kiebitz::app
