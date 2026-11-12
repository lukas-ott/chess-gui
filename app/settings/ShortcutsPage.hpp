#pragma once

#include <QWidget>

#include <vector>

class QKeySequenceEdit;
class QLabel;
class QPushButton;

namespace kiebitz::app {

class Shortcuts;

/// Settings › Keyboard: every command with its key, grouped by menu. Click a key and press the
/// new one (⌫: none, Esc: keep it); Reset brings back the default.
class ShortcutsPage : public QWidget {
    Q_OBJECT

public:
    explicit ShortcutsPage(Shortcuts& shortcuts, QWidget* parent = nullptr);

    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Row {
        QString id;
        QKeySequenceEdit* edit = nullptr;
        QPushButton* reset = nullptr;
    };
    void refresh();
    void finish(const Row& row, const QKeySequence& key);

    Shortcuts& m_shortcuts;
    QLabel* m_notice = nullptr; ///< why a key wasn't taken
    std::vector<Row> m_rows;
};

} // namespace kiebitz::app
