#include "GameInfoPanel.hpp"

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>

#include <algorithm>

namespace kiebitz::app {
namespace {

QString value(const core::Game& game, const QString& name) {
    const auto text = game.header(name.toStdString());
    const auto result = QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    // PGN writes "?" for unknown values; an empty field says that better.
    return result == u"?" || result == u"????.??.??" ? QString() : result;
}

} // namespace

GameInfoPanel::GameInfoPanel(QWidget* parent) : QWidget(parent) {
    // A list of label and value, one per row, like a file's info in Finder. The values read as
    // text; they show a field when hovered and are edited in place. A player's Elo sits beside
    // the name.
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(14, 12, 14, 12);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(2);
    grid->setColumnStretch(1, 1);

    const auto field = [this](const QString& header, const QString& placeholder) {
        auto* edit = new QLineEdit;
        edit->setObjectName(QStringLiteral("header") + header);
        edit->setProperty("role", "inlineField");
        edit->setPlaceholderText(placeholder);
        connect(edit, &QLineEdit::textEdited, this, [this, header](const QString& text) {
            emit headerEdited(header, text.isEmpty() ? QStringLiteral("?") : text);
        });
        m_fields.push_back({header, edit});
        return edit;
    };
    m_result = new QComboBox;
    m_result->setObjectName(QStringLiteral("headerResult"));
    m_result->setProperty("role", "inlineField");
    m_result->addItems({QStringLiteral("*"), QStringLiteral("1-0"), QStringLiteral("0-1"),
                        QStringLiteral("1/2-1/2")});
    connect(m_result, &QComboBox::activated, this,
            [this](int) { emit headerEdited(QStringLiteral("Result"), m_result->currentText()); });

    m_side = new QComboBox;
    m_side->setObjectName(QStringLiteral("headerOrientation"));
    m_side->setProperty("role", "inlineField");
    m_side->addItem(tr("White"), QStringLiteral("white"));
    m_side->addItem(tr("Black"), QStringLiteral("black"));
    connect(m_side, &QComboBox::activated, this, [this](int) {
        emit headerEdited(QStringLiteral("Orientation"), m_side->currentData().toString());
    });

    int row = 0;
    // `games` / `repertoires`: which kind of file the row is for.
    const auto add = [&](const QString& title, QWidget* value, QWidget* extra, bool games,
                         bool repertoires) {
        auto* label = new QLabel(title);
        label->setProperty("role", "infoLabel");
        grid->addWidget(label, row, 0);
        if (extra != nullptr) {
            grid->addWidget(value, row, 1);
            grid->addWidget(extra, row, 2);
        } else {
            grid->addWidget(value, row, 1, 1, 2);
        }
        m_rows.push_back({{label, value, extra}, games, repertoires});
        ++row;
        return label;
    };
    const auto elo = [&](const QString& header) {
        QLineEdit* edit = field(header, tr("Elo"));
        edit->setFixedWidth(64);
        edit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return edit;
    };
    add(tr("White"), field(QStringLiteral("White"), tr("Player")), elo(QStringLiteral("WhiteElo")),
        true, false);
    add(tr("Black"), field(QStringLiteral("Black"), tr("Player")), elo(QStringLiteral("BlackElo")),
        true, false);
    add(tr("Result"), m_result, nullptr, true, false);
    m_eventLabel =
        add(tr("Event"), field(QStringLiteral("Event"), tr("Event")), nullptr, true, true);
    add(tr("For"), m_side, nullptr, false, true);
    add(tr("Opening"), field(QStringLiteral("ECO"), tr("ECO code, e.g. C65")), nullptr, true, true);
    add(tr("Author"), field(QStringLiteral("Annotator"), tr("Who wrote it")), nullptr, false, true);
    add(tr("Site"), field(QStringLiteral("Site"), tr("Place")), nullptr, true, false);
    add(tr("Date"), field(QStringLiteral("Date"), QStringLiteral("2026.11.10")), nullptr, true,
        true);
    add(tr("Round"), field(QStringLiteral("Round"), QStringLiteral("1")), nullptr, true, false);
    grid->setRowStretch(row, 1);
    setGame(nullptr);
}

void GameInfoPanel::setGame(const core::Game* game, bool repertoire) {
    setEnabled(game != nullptr);
    for (const Row& row : m_rows) {
        for (QWidget* widget : row.widgets) {
            if (widget != nullptr) {
                widget->setVisible(repertoire ? row.repertoires : row.games);
            }
        }
    }
    m_eventLabel->setText(repertoire ? tr("Name") : tr("Event"));
    {
        const QSignalBlocker block(m_side);
        const bool black =
            game != nullptr && value(*game, QStringLiteral("Orientation")).toLower() == u"black";
        m_side->setCurrentIndex(black ? 1 : 0);
    }
    for (const auto& field : m_fields) {
        const QSignalBlocker block(field.edit);
        field.edit->setText(game != nullptr ? value(*game, field.header) : QString());
    }
    const QSignalBlocker block(m_result);
    const int index =
        game != nullptr ? m_result->findText(value(*game, QStringLiteral("Result"))) : 0;
    m_result->setCurrentIndex(std::max(index, 0));
}

} // namespace kiebitz::app
