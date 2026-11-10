#include "GameInfoPanel.hpp"

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QVBoxLayout>

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
    // Two columns of small titles over their fields, as elsewhere in the app (engine cards and
    // settings): names on the left, the shorter values beside them.
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(10, 10, 10, 10);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(12);
    grid->setColumnStretch(0, 3);
    grid->setColumnStretch(1, 2);

    const auto titled = [](const QString& title, QWidget* field) {
        auto* cell = new QWidget;
        auto* layout = new QVBoxLayout(cell);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);
        auto* label = new QLabel(title.toUpper());
        label->setProperty("role", "statTitle");
        layout->addWidget(label);
        layout->addWidget(field);
        return cell;
    };
    const auto field = [this](const QString& header, const QString& placeholder) {
        auto* edit = new QLineEdit;
        edit->setObjectName(QStringLiteral("header") + header);
        edit->setPlaceholderText(placeholder);
        connect(edit, &QLineEdit::textEdited, this, [this, header](const QString& text) {
            emit headerEdited(header, text.isEmpty() ? QStringLiteral("?") : text);
        });
        m_fields.push_back({header, edit});
        return edit;
    };
    m_result = new QComboBox;
    m_result->setObjectName(QStringLiteral("headerResult"));
    m_result->addItems({QStringLiteral("*"), QStringLiteral("1-0"), QStringLiteral("0-1"),
                        QStringLiteral("1/2-1/2")});
    connect(m_result, &QComboBox::activated, this,
            [this](int) { emit headerEdited(QStringLiteral("Result"), m_result->currentText()); });

    // Row by row: left (wide), right (short).
    grid->addWidget(titled(tr("White"), field(QStringLiteral("White"), tr("Player"))), 0, 0);
    grid->addWidget(titled(tr("Elo"), field(QStringLiteral("WhiteElo"), QString())), 0, 1);
    grid->addWidget(titled(tr("Black"), field(QStringLiteral("Black"), tr("Player"))), 1, 0);
    grid->addWidget(titled(tr("Elo"), field(QStringLiteral("BlackElo"), QString())), 1, 1);
    grid->addWidget(titled(tr("Event"), field(QStringLiteral("Event"), QString())), 2, 0);
    grid->addWidget(titled(tr("Result"), m_result), 2, 1);
    grid->addWidget(titled(tr("Site"), field(QStringLiteral("Site"), QString())), 3, 0);
    grid->addWidget(titled(tr("Date"), field(QStringLiteral("Date"), QStringLiteral("2026.11.10"))),
                    3, 1);
    grid->addWidget(
        titled(tr("Opening (ECO)"), field(QStringLiteral("ECO"), QStringLiteral("C65"))), 4, 0);
    grid->addWidget(titled(tr("Round"), field(QStringLiteral("Round"), QString())), 4, 1);
    grid->setRowStretch(5, 1);
    setGame(nullptr);
}

void GameInfoPanel::setGame(const core::Game* game) {
    setEnabled(game != nullptr);
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
