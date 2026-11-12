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
                   const AppStyle& /*style*/, QWidget* parent)
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
    if (player) {
        box->addWidget(label(player->name, "sectionTitle"));
        box->addWidget(label(describe(*player), "emptyHint"));
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
        box->addSpacing(4);
        box->addLayout(tiles);
        box->addSpacing(4);
        auto* buttons = new QHBoxLayout;
        auto* open = new QPushButton(tr("Open FIDE Profile"));
        open->setObjectName(QStringLiteral("fideOpen"));
        open->setProperty("role", "primary");
        connect(open, &QPushButton::clicked, this,
                [url = player->profileUrl()] { QDesktopServices::openUrl(QUrl(url)); });
        buttons->addWidget(open);
        buttons->addStretch();
        box->addLayout(buttons);
    } else {
        box->addWidget(label(status, "emptyHint"));
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
            box->addLayout(row);
        }
    }

    // Not them? Everyone else who fits the name, and "none of these".
    if (!candidates.empty()) {
        box->addSpacing(6);
        box->addWidget(label(player ? tr("NOT THEM?") : tr("PLAYERS WITH THIS NAME"), "cardTitle"));
        auto* list = new QListWidget;
        list->setObjectName(QStringLiteral("fideCandidates"));
        list->setFrameShape(QFrame::NoFrame);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setTextElideMode(Qt::ElideRight);
        for (const FidePlayer& candidate : candidates) {
            if (player && candidate.id == player->id) {
                continue;
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
        list->setFixedHeight((std::min(list->count(), 5) * 28) + 4);
        connect(list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            emit chosen(item->data(Qt::UserRole).toInt());
            close();
        });
        box->addWidget(list);
    }
}

} // namespace kiebitz::app
