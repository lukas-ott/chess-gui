#pragma once

#include <QString>
#include <QWidget>

class QGridLayout;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QToolButton;
class QWidget;

namespace kiebitz::app {

class AppStyle;
class EngineRegistry;
class Stepper;

/// The Engines page (a sidebar section, like En Croissant's): the engines as cards on the left,
/// the selected one's details and settings on the right. New engines come from the + menu:
/// download Stockfish, use the one already on this Mac, or add any UCI engine by its file.
class EnginesView : public QWidget {
    Q_OBJECT

public:
    EnginesView(EngineRegistry& registry, const AppStyle& style, QWidget* parent = nullptr);

    /// Shows this engine's details (an engine name, or the Lichess cloud's).
    void select(const QString& name);
    [[nodiscard]] const QString& selected() const noexcept { return m_selected; }

private:
    void rebuildCards();
    void showDetails();
    void updateIcons();
    void startDownload();
    void chooseFile();
    void showError(const QString& message);

    EngineRegistry& m_registry;
    const AppStyle& m_style;
    QString m_selected;

    QPushButton* m_folderButton = nullptr;
    QToolButton* m_addButton = nullptr;
    QLineEdit* m_search = nullptr;
    QGridLayout* m_grid = nullptr;
    QWidget* m_downloadBox = nullptr;
    QLabel* m_downloadLabel = nullptr;
    QProgressBar* m_downloadBar = nullptr;
    QLabel* m_error = nullptr;

    QStackedWidget* m_details = nullptr; ///< nothing selected, or the details
    QLabel* m_detailIcon = nullptr;
    QLabel* m_detailName = nullptr;
    QLabel* m_detailSubtitle = nullptr;
    QLabel* m_detailAbout = nullptr;
    QWidget* m_settings = nullptr;
    Stepper* m_threads = nullptr;
    Stepper* m_hash = nullptr;
    Stepper* m_lines = nullptr;
    QWidget* m_processSettings = nullptr; ///< threads and hash (not for the cloud)
    QPushButton* m_remove = nullptr;
};

} // namespace kiebitz::app
