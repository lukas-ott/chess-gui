#include "FideCard.hpp"

#include "style/AppStyle.hpp"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace kiebitz::app {
namespace {

QLabel* label(const QString& text, const char* role) {
    auto* result = new QLabel(text);
    result->setProperty("role", role);
    result->setWordWrap(true);
    return result;
}

QString rating(int value) {
    return value > 0 ? QString::number(value) : QStringLiteral("–");
}

QString describe(const FidePlayer& player) {
    QStringList parts;
    if (!player.title.isEmpty()) {
        parts << player.title;
    }
    if (!player.federation.isEmpty()) {
        parts << player.federation;
    }
    if (player.born > 0) {
        parts << FideCard::tr("born %1").arg(player.born);
    }
    parts << FideCard::tr("FIDE ID %1").arg(player.id);
    return parts.join(QStringLiteral("  ·  "));
}

} // namespace

FideCard::FideCard(const std::optional<FidePlayer>& player,
                   const std::vector<FidePlayer>& candidates, const QString& status, bool retry,
                   bool confirmed, bool you, const AppStyle& /*style*/, QWidget* parent)
    : QFrame(parent, Qt::Popup) {
    setObjectName(QStringLiteral("fideCard"));
    setAttribute(Qt::WA_TranslucentBackground); // its own rounded card shows
    setAttribute(Qt::WA_DeleteOnClose);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* body = new QFrame;
    body->setProperty("card", true);
    outer->addWidget(body);
    auto* box = new QVBoxLayout(body);
    box->setContentsMargins(16, 14, 16, 14);
    box->setSpacing(6);
    setFixedWidth(340);
    box->addWidget(label(tr("FIDE"), "cardTitle"));

    // The player found: name, ratings, the page on Lichess.
    auto* found = new QWidget;
    found->setObjectName(QStringLiteral("fidePlayer"));
    auto* foundBox = new QVBoxLayout(found);
    foundBox->setContentsMargins(0, 0, 0, 0);
    foundBox->setSpacing(6);
    if (player) {
        foundBox->addWidget(label(player->name, "sectionTitle"));
        foundBox->addWidget(label(describe(*player), "emptyHint"));
        auto* tiles = new QHBoxLayout;
        tiles->setSpacing(8);
        for (const auto& [caption, value] :
             {std::pair{tr("Standard"), player->standard}, std::pair{tr("Rapid"), player->rapid},
              std::pair{tr("Blitz"), player->blitz}}) {
            auto* tile = new QFrame;
            tile->setProperty("statTile", true);
            auto* tileBox = new QVBoxLayout(tile);
            tileBox->setContentsMargins(10, 8, 10, 8);
            tileBox->setSpacing(1);
            tileBox->addWidget(label(caption, "statCaption"));
            tileBox->addWidget(label(rating(value), "statValue"));
            tiles->addWidget(tile, 1);
        }
        foundBox->addSpacing(4);
        foundBox->addLayout(tiles);
        foundBox->addSpacing(4);
        auto* buttons = new QHBoxLayout;
        buttons->setSpacing(6);
        auto* open = new QPushButton(tr("Open FIDE Profile"));
        open->setObjectName(QStringLiteral("fideOpen"));
        open->setProperty("role", confirmed ? "primary" : "secondary");
        connect(open, &QPushButton::clicked, this,
                [url = player->profileUrl()] { QDesktopServices::openUrl(QUrl(url)); });
        buttons->addWidget(open);
        buttons->addStretch();
        foundBox->addLayout(buttons);
    } else {
        foundBox->addWidget(label(status, "emptyHint"));
        if (retry) {
            auto* again = new QPushButton(tr("Try Again"));
            again->setObjectName(QStringLiteral("fideRetry"));
            again->setProperty("role", "secondary");
            connect(again, &QPushButton::clicked, this, [this] {
                emit retryRequested();
                close();
            });
            auto* row = new QHBoxLayout;
            row->addWidget(again);
            row->addStretch();
            foundBox->addLayout(row);
        }
    }
    box->addWidget(found);

    // Who it is, among everyone with this name (and "none of these").
    auto* others = new QWidget;
    others->setObjectName(QStringLiteral("fideOthers"));
    auto* othersBox = new QVBoxLayout(others);
    othersBox->setContentsMargins(0, 6, 0, 0);
    othersBox->setSpacing(6);
    QString title = tr("PLAYERS WITH THIS NAME");
    if (player) {
        title = you ? tr("WHICH ONE IS YOU?") : tr("WHICH ONE IS IT?");
    }
    othersBox->addWidget(label(title, "cardTitle"));
    auto* list = new QListWidget;
    list->setObjectName(QStringLiteral("fideCandidates"));
    list->setFrameShape(QFrame::NoFrame);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setTextElideMode(Qt::ElideRight);
    for (const FidePlayer& candidate : candidates) {
        if (player && candidate.id == player->id) {
            continue; // not them, as just said
        }
        // Short: the name, federation and best rating; the rest in the tooltip.
        QStringList brief{candidate.name};
        if (!candidate.federation.isEmpty()) {
            brief << candidate.federation;
        }
        if (const int best = std::max({candidate.standard, candidate.rapid, candidate.blitz});
            best > 0) {
            brief << QString::number(best);
        }
        auto* item = new QListWidgetItem(brief.join(QStringLiteral("  ·  ")), list);
        item->setToolTip(describe(candidate));
        item->setData(Qt::UserRole, candidate.id);
    }
    auto* none = new QListWidgetItem(tr("None of these"), list);
    none->setData(Qt::UserRole, 0);
    list->setFixedHeight((std::min(list->count(), 6) * 28) + 4);
    connect(list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        emit chosen(item->data(Qt::UserRole).toInt());
        close();
    });
    othersBox->addWidget(list);
    box->addWidget(others);
    // Without a player found, the list is the choice; with one, only after a "No".
    others->setVisible(!player && !candidates.empty());

    // Found but not confirmed: asked once. Yes keeps them; No takes them away for the list.
    if (player && !confirmed) {
        auto* question = new QWidget;
        question->setObjectName(QStringLiteral("fideQuestion"));
        auto* row = new QHBoxLayout(question);
        row->setContentsMargins(0, 6, 0, 0);
        row->setSpacing(6);
        row->addWidget(label(you ? tr("Is this you?") : tr("Is this them?"), "checkHeadline"), 1);
        auto* yes = new QPushButton(tr("Yes"));
        yes->setObjectName(QStringLiteral("fideYes"));
        yes->setProperty("role", "primary");
        connect(yes, &QPushButton::clicked, this, [this, id = player->id] {
            emit chosen(id);
            close();
        });
        auto* no = new QPushButton(tr("No"));
        no->setObjectName(QStringLiteral("fideNo"));
        no->setProperty("role", "secondary");
        connect(no, &QPushButton::clicked, this, [this, found, others, question] {
            found->hide();
            question->hide();
            others->show();
            adjustSize();
        });
        row->addWidget(yes);
        row->addWidget(no);
        box->addWidget(question);
    }
}

} // namespace kiebitz::app
