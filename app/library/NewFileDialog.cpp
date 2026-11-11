#include "NewFileDialog.hpp"

#include "kiebitz/core/game.hpp"
#include "kiebitz/pgn/pgn.hpp"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace kiebitz::app {
namespace {

QLabel* title(const QString& text) {
    auto* result = new QLabel(text);
    result->setProperty("role", "cardTitle");
    return result;
}

/// A row of segments, one of which is chosen (the Engine tab's Lines control looks the same).
QWidget* segments(QButtonGroup* group, const QStringList& names, int chosen) {
    auto* row = new QWidget;
    row->setProperty("role", "segmented");
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);
    group->setExclusive(true);
    for (int i = 0; i < names.size(); ++i) {
        auto* segment = new QPushButton(names[i]);
        segment->setProperty("role", "segment");
        segment->setCheckable(true);
        segment->setChecked(i == chosen);
        segment->setFocusPolicy(Qt::NoFocus);
        segment->setMinimumWidth(84);
        group->addButton(segment, i);
        layout->addWidget(segment);
    }
    return row;
}

} // namespace

NewFileDialog::NewFileDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("New File"));
    setObjectName(QStringLiteral("newFileDialog"));
    setMinimumWidth(520);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(8);

    layout->addWidget(title(tr("NAME")));
    m_name = new QLineEdit;
    m_name->setObjectName(QStringLiteral("newFileName"));
    m_name->setPlaceholderText(tr("Najdorf, Club championship 2026, …"));
    layout->addWidget(m_name);

    layout->addSpacing(8);
    layout->addWidget(title(tr("TYPE")));
    m_kinds = new QButtonGroup(this);
    layout->addWidget(
        segments(m_kinds,
                 {tr("Game"), tr("Repertoire"), tr("Tournament"), tr("Puzzle"), tr("Other")}, 0),
        0, Qt::AlignLeft);

    // A repertoire is for one side.
    m_sideRow = new QWidget;
    auto* sideLayout = new QVBoxLayout(m_sideRow);
    sideLayout->setContentsMargins(0, 8, 0, 0);
    sideLayout->setSpacing(8);
    sideLayout->addWidget(title(tr("FOR")));
    m_sides = new QButtonGroup(this);
    sideLayout->addWidget(segments(m_sides, {tr("White"), tr("Black")}, 0), 0, Qt::AlignLeft);
    m_sideRow->hide();
    layout->addWidget(m_sideRow);
    connect(m_kinds, &QButtonGroup::idToggled, this,
            [this](int, bool) { m_sideRow->setVisible(kind() == library::FileKind::Repertoire); });

    layout->addSpacing(8);
    layout->addWidget(title(tr("PGN")));
    m_pgn = new QPlainTextEdit;
    m_pgn->setObjectName(QStringLiteral("newFilePgn"));
    m_pgn->setPlaceholderText(tr("Leave empty to start from scratch"));
    m_pgn->setMinimumHeight(160);
    layout->addWidget(m_pgn, 1);

    layout->addSpacing(8);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* cancel = new QPushButton(tr("Cancel"));
    cancel->setProperty("role", "secondary");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    m_create = new QPushButton(tr("Create"));
    m_create->setObjectName(QStringLiteral("newFileCreate"));
    m_create->setProperty("role", "primary");
    m_create->setDefault(true);
    m_create->setEnabled(false);
    connect(m_create, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_name, &QLineEdit::textChanged, this,
            [this](const QString& text) { m_create->setEnabled(!text.trimmed().isEmpty()); });
    buttons->addWidget(cancel);
    buttons->addWidget(m_create);
    layout->addLayout(buttons);
}

NewFolderDialog::NewFolderDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("New Folder"));
    setObjectName(QStringLiteral("newFolderDialog"));
    setMinimumWidth(380);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(8);
    layout->addWidget(title(tr("NAME")));
    m_name = new QLineEdit;
    m_name->setObjectName(QStringLiteral("newFolderName"));
    m_name->setPlaceholderText(tr("Openings, Club games, …"));
    layout->addWidget(m_name);
    layout->addSpacing(12);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* cancel = new QPushButton(tr("Cancel"));
    cancel->setProperty("role", "secondary");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    auto* create = new QPushButton(tr("Create"));
    create->setProperty("role", "primary");
    create->setDefault(true);
    create->setEnabled(false);
    connect(create, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_name, &QLineEdit::textChanged, this,
            [create](const QString& text) { create->setEnabled(!text.trimmed().isEmpty()); });
    buttons->addWidget(cancel);
    buttons->addWidget(create);
    layout->addLayout(buttons);
}

QString NewFolderDialog::name() const {
    return m_name->text().trimmed();
}

QString NewFileDialog::name() const {
    return m_name->text().trimmed();
}

library::FileKind NewFileDialog::kind() const {
    return static_cast<library::FileKind>(std::max(0, m_kinds->checkedId()));
}

core::Color NewFileDialog::side() const {
    return m_sides->checkedId() == 1 ? core::Color::Black : core::Color::White;
}

QString NewFileDialog::pgn() const {
    return m_pgn->toPlainText();
}

std::string NewFileDialog::fileText(const QString& name, library::FileKind kind, core::Color side,
                                    const QString& pgn) {
    std::string text = pgn.trimmed().toStdString();
    if (pgn::indexGames(text).empty()) { // nothing usable given: one empty game
        core::Game game;
        game.setHeader("Event", name.toStdString());
        text = pgn::writeGame(game);
    } else {
        text += '\n';
    }
    text = pgn::setHeaderInAllGames(text, library::kFileTypeHeader, library::nameOf(kind));
    if (kind == library::FileKind::Repertoire) {
        text = pgn::setHeaderInAllGames(text, "Orientation",
                                        side == core::Color::White ? "white" : "black");
    }
    return text;
}

} // namespace kiebitz::app
