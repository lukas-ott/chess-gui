#pragma once

#include "kiebitz/core/types.hpp"
#include "kiebitz/library/file_kind.hpp"

#include <QDialog>

class QButtonGroup;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QWidget;

namespace kiebitz::app {

/// "New File": a name, what kind of file it is (game, repertoire with its side, tournament,
/// puzzles, other) and, optionally, PGN to start from.
class NewFileDialog : public QDialog {
    Q_OBJECT

public:
    explicit NewFileDialog(QWidget* parent = nullptr);

    [[nodiscard]] QString name() const;
    [[nodiscard]] library::FileKind kind() const;
    [[nodiscard]] core::Color side() const; ///< for a repertoire
    [[nodiscard]] QString pgn() const;

    /// The file's text: the PGN given (or an empty game), with its type (and side) in the
    /// headers.
    [[nodiscard]] static std::string fileText(const QString& name, library::FileKind kind,
                                              core::Color side, const QString& pgn);

private:
    QLineEdit* m_name = nullptr;
    QButtonGroup* m_kinds = nullptr;
    QWidget* m_sideRow = nullptr;
    QButtonGroup* m_sides = nullptr;
    QPlainTextEdit* m_pgn = nullptr;
    QPushButton* m_create = nullptr;
};

/// "New Folder": just a name.
class NewFolderDialog : public QDialog {
    Q_OBJECT

public:
    explicit NewFolderDialog(QWidget* parent = nullptr);
    [[nodiscard]] QString name() const;

private:
    QLineEdit* m_name = nullptr;
};

} // namespace kiebitz::app
