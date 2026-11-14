#pragma once

#include <QWidget>

#include <functional>
#include <map>
#include <utility>
#include <vector>

class QButtonGroup;
class QLabel;
class QListWidget;
class QStackedWidget;
class QVBoxLayout;

namespace kiebitz::app {

class AppStyle;
class PageHeader;

/// The Settings page: sections on the left (Board, Moves, Appearance, ...), their settings on
/// the right, one row each with what it does and its control. Everything is kept by Settings
/// (and changes at once); what is set elsewhere too (the View menu) shows here as it changes.
class SettingsView : public QWidget {
    Q_OBJECT

public:
    explicit SettingsView(const AppStyle& style, QWidget* parent = nullptr);

    /// The folders in the Folders section: name and path, in order. The first one (the
    /// library) can be changed.
    void setFolders(const std::vector<std::pair<QString, QString>>& folders);
    /// A section made elsewhere (shortcuts): its title and content, added at the end or before
    /// the section titled `before`.
    void addSection(const QString& title, QWidget* content, const QString& before = {});
    /// For tests and links: shows a section by title.
    void showSection(const QString& title);

    /// A setting's row, for sections made elsewhere: a title, a line about it, and its control.
    [[nodiscard]] static QWidget* row(const QString& title, const QString& about, QWidget* control);
    /// A box of rows with a title over it.
    [[nodiscard]] static QVBoxLayout* group(QVBoxLayout* page, const QString& title);

signals:
    void customColorsRequested();
    void changeLibraryFolderRequested();
    void showEnginesRequested();
    /// "Try" in the Sounds section: play a move's sound.
    void soundTestRequested();

private:
    QWidget* boardSection();
    QWidget* movesSection();
    QWidget* appearanceSection();
    /// The dark (or light) themes as previews, their buttons in `group` (one chosen of all).
    QWidget* themePicker(QButtonGroup* group, bool dark);
    QWidget* enginesSection();
    QWidget* foldersSection();
    QWidget* repertoireSection();
    QWidget* soundsSection();
    /// Choices side by side for a setting (one of them on).
    QWidget* segments(const char* key, const std::vector<std::pair<QVariant, QString>>& choices,
                      const QString& name);
    QWidget* toggle(const char* key, const QString& name);
    /// Called when a setting changes, so its control shows the new value.
    void follow(const char* key, std::function<void()> refresh);

    const AppStyle& m_style;
    QListWidget* m_sections = nullptr;
    QStackedWidget* m_pages = nullptr;
    QVBoxLayout* m_folders = nullptr;
    std::multimap<QString, std::function<void()>> m_refresh;
};

} // namespace kiebitz::app
