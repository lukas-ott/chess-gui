#pragma once

#include <QMainWindow>

class QSplitter;

namespace chessgui::app {

/// Fixed layout from the spec: document tabs on top, board on the left,
/// notation + tabbed panels (Engine, Explorer, Repertoire, Game info) on the right.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildMenus();
    void buildLayout();
    void restoreState();
    void showAbout();

    QSplitter* m_mainSplitter = nullptr;
    QSplitter* m_sideSplitter = nullptr;
};

} // namespace chessgui::app
