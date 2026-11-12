#pragma once

#include <QDialog>

class QLineEdit;

namespace kiebitz::app {

/// Asks for a name, in the app's own look (New Profile, Rename).
class NameDialog : public QDialog {
    Q_OBJECT

public:
    NameDialog(const QString& title, const QString& action, const QString& placeholder,
               const QString& text = {}, QWidget* parent = nullptr);
    [[nodiscard]] QString name() const;

private:
    QLineEdit* m_name = nullptr;
};

} // namespace kiebitz::app
