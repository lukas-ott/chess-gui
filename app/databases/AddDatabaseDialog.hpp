#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;

namespace kiebitz::app {

/// "Add Database", in two parts, as En Croissant has it:
///   Web: free databases to get from the web. Their sites want you to accept their licence on
///        their own page, so the dialog opens that page; the file you download there is then
///        imported from here (no need to unpack it).
///   Local: a database you already have somewhere (PGN, or PGN in .7z, .zip, .gz or .bz2).
class AddDatabaseDialog : public QDialog {
    Q_OBJECT

public:
    explicit AddDatabaseDialog(QWidget* parent = nullptr);

    /// After Accept: the file to import, the database's name and where it came from.
    [[nodiscard]] QString path() const { return m_path; }
    [[nodiscard]] QString name() const;
    [[nodiscard]] QString source() const { return m_source; }

    /// For tests: choose a file as if picked in the Local part.
    void chooseFile(const QString& path);

private:
    QWidget* buildWeb();
    QWidget* buildLocal();

    QStackedWidget* m_pages = nullptr;
    QLabel* m_file = nullptr;
    QLineEdit* m_name = nullptr;
    QPushButton* m_import = nullptr;
    QString m_path;
    QString m_source;
};

} // namespace kiebitz::app
