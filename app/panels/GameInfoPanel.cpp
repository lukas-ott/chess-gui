#include "GameInfoPanel.hpp"

#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSignalBlocker>

#include <algorithm>
#include <array>

namespace chessgui::app {
namespace {

QString value(const core::Game& game, const QString& name) {
    const auto text = game.header(name.toStdString());
    const auto result = QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    // PGN writes "?" for unknown values; an empty field says that better.
    return result == u"?" || result == u"????.??.??" ? QString() : result;
}

} // namespace

GameInfoPanel::GameInfoPanel(QWidget* parent) : QWidget(parent) {
    auto* form = new QFormLayout(this);
    form->setContentsMargins(12, 10, 12, 10);
    form->setVerticalSpacing(6);
    form->setLabelAlignment(Qt::AlignRight);

    // header name, label, placeholder
    const std::array<std::array<QString, 3>, 9> fields{{
        {QStringLiteral("White"), tr("White"), tr("Player")},
        {QStringLiteral("WhiteElo"), tr("White Elo"), QString()},
        {QStringLiteral("Black"), tr("Black"), tr("Player")},
        {QStringLiteral("BlackElo"), tr("Black Elo"), QString()},
        {QStringLiteral("Event"), tr("Event"), QString()},
        {QStringLiteral("Site"), tr("Site"), QString()},
        {QStringLiteral("Date"), tr("Date"), QStringLiteral("2026.11.10")},
        {QStringLiteral("Round"), tr("Round"), QString()},
        {QStringLiteral("ECO"), tr("Opening (ECO)"), QStringLiteral("C65")},
    }};
    for (const auto& [header, label, placeholder] : fields) {
        auto* edit = new QLineEdit;
        edit->setPlaceholderText(placeholder);
        connect(edit, &QLineEdit::textEdited, this, [this, header](const QString& text) {
            emit headerEdited(header, text.isEmpty() ? QStringLiteral("?") : text);
        });
        form->addRow(label, edit);
        m_fields.push_back({header, edit});
        if (header == u"BlackElo") {
            m_result = new QComboBox;
            m_result->addItems({QStringLiteral("*"), QStringLiteral("1-0"), QStringLiteral("0-1"),
                                QStringLiteral("1/2-1/2")});
            connect(m_result, &QComboBox::activated, this, [this](int) {
                emit headerEdited(QStringLiteral("Result"), m_result->currentText());
            });
            form->addRow(tr("Result"), m_result);
        }
    }
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

} // namespace chessgui::app
