#include "EnginesView.hpp"

#include "EnginePanel.hpp"
#include "EngineRegistry.hpp"
#include "StockfishDownload.hpp"
#include "style/AppStyle.hpp"
#include "style/Icons.hpp"
#include "style/Layout.hpp"
#include "style/PageHeader.hpp"
#include "widgets/EmptyState.hpp"
#include "widgets/Stepper.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QThread>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace kiebitz::app {
namespace {

QLabel* label(const QString& text, const char* role) {
    auto* result = new QLabel(text);
    result->setProperty("role", role);
    return result;
}

QFrame* card() {
    auto* frame = new QFrame;
    frame->setProperty("card", true);
    return frame;
}

/// "Stockfish 19" → "19"; a name without a version number gives "—".
QString versionOf(const QString& name) {
    const QString last = name.section(QLatin1Char(' '), -1);
    return !last.isEmpty() && last.front().isDigit() ? last : QStringLiteral("—");
}

/// A label over a value, as on En Croissant's engine cards ("VERSION / 19").
QWidget* stat(const QString& title, const QString& value) {
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(label(title, "statTitle"));
    layout->addWidget(label(value, "statValue"));
    return widget;
}

/// A small title over a control, like the cards' stats.
QWidget* setting(const QString& title, Stepper* stepper) {
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(label(title, "statTitle"));
    layout->addWidget(stepper, 0, Qt::AlignLeft);
    return widget;
}

/// One engine as a clickable card.
class EngineCard : public QFrame {
public:
    EngineCard(const QString& name, const QIcon& icon, const QString& subtitle,
               const std::vector<std::pair<QString, QString>>& stats, std::function<void()> onClick)
        : m_onClick(std::move(onClick)) {
        setProperty("card", true);
        setProperty("engineCard", true);
        setCursor(Qt::PointingHandCursor);
        setObjectName(QStringLiteral("engineCard"));
        setAccessibleName(name);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(16, 14, 16, 14);
        layout->setSpacing(12);
        auto* top = new QHBoxLayout;
        top->setSpacing(12);
        auto* iconLabel = new QLabel;
        iconLabel->setPixmap(icon.pixmap(28, 28));
        iconLabel->setFixedSize(28, 28);
        auto* names = new QVBoxLayout;
        names->setSpacing(1);
        names->addWidget(label(name, "cardName"));
        auto* sub = label(subtitle, "emptyHint");
        sub->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        names->addWidget(sub);
        top->addWidget(iconLabel, 0, Qt::AlignTop);
        top->addLayout(names, 1);
        layout->addLayout(top);
        auto* row = new QHBoxLayout;
        row->setSpacing(24);
        for (const auto& [title, value] : stats) {
            row->addWidget(stat(title, value));
        }
        row->addStretch();
        layout->addLayout(row);
    }

    void setSelected(bool selected) {
        setProperty("selected", selected);
        style()->unpolish(this);
        style()->polish(this);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override { event->accept(); }
    void mouseReleaseEvent(QMouseEvent* event) override {
        event->accept();
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
            // Run once Qt is done delivering this click: what it does may delete this card
            // (deleting a widget inside its own mouse event crashed the app).
            QMetaObject::invokeMethod(parentWidget(), m_onClick, Qt::QueuedConnection);
        }
    }

private:
    std::function<void()> m_onClick;
};

} // namespace

EnginesView::EnginesView(EngineRegistry& registry, const AppStyle& style, QWidget* parent)
    : QWidget(parent), m_registry(registry), m_style(style) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(layout::kPage);
    layout->setSpacing(layout::kPageSpacing);

    // ---- Header: title and the engines folder (the same button as the Library's) ---------------
    auto* header = new PageHeader(tr("Engines"));
    m_folderButton = new QPushButton(tr("Engine Folder"));
    m_folderButton->setObjectName(QStringLiteral("enginesFolder"));
    m_folderButton->setProperty("role", "secondary");
    m_folderButton->setToolTip(tr("Show the downloaded engines in Finder"));
    connect(m_folderButton, &QPushButton::clicked, this, [] {
        QDir().mkpath(engines::downloadFolder());
        QDesktopServices::openUrl(QUrl::fromLocalFile(engines::downloadFolder()));
    });
    header->addAction(m_folderButton);
    layout->addWidget(header);

    // ---- Left: search, +, cards
    // -------------------------------------------------------------------
    auto* list = card();
    auto* listLayout = new QVBoxLayout(list);
    listLayout->setContentsMargins(layout::kCard);
    listLayout->setSpacing(layout::kCardSpacing);
    auto* tools = new QHBoxLayout;
    tools->setSpacing(8);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, &EnginesView::rebuildCards);
    m_addButton = new QToolButton;
    m_addButton->setObjectName(QStringLiteral("addEngine"));
    m_addButton->setProperty("role", "icon");
    m_addButton->setProperty("outlined", true);
    m_addButton->setToolTip(tr("Add an engine"));
    m_addButton->setFixedSize(layout::kControlHeight, layout::kControlHeight);
    m_addButton->setIconSize(QSize(16, 16));
    m_addButton->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(m_addButton);
    menu->addAction(tr("Download Stockfish"), this, &EnginesView::startDownload);
    auto* installed = menu->addAction(tr("Use the Stockfish on This Mac"), this, [this] {
        m_registry.addFromFile(engines::findInstalledStockfish());
    });
    menu->addAction(tr("Add Engine from File…"), this, &EnginesView::chooseFile);
    connect(menu, &QMenu::aboutToShow, this,
            [installed] { installed->setVisible(!engines::findInstalledStockfish().isEmpty()); });
    m_addButton->setMenu(menu);
    tools->addWidget(m_search, 1);
    tools->addWidget(m_addButton);
    listLayout->addLayout(tools);

    m_downloadBox = new QWidget;
    auto* downloadLayout = new QHBoxLayout(m_downloadBox);
    downloadLayout->setContentsMargins(0, 0, 0, 0);
    m_downloadLabel = label(QString(), "emptyHint");
    m_downloadBar = new QProgressBar;
    m_downloadBar->setObjectName(QStringLiteral("downloadBar"));
    m_downloadBar->setTextVisible(false);
    m_downloadBar->setFixedHeight(6);
    auto* cancel = new QPushButton(tr("Cancel"));
    cancel->setProperty("role", "secondary");
    connect(cancel, &QPushButton::clicked, this, [this] {
        m_registry.download()->cancel();
        m_downloadBox->hide();
    });
    auto* downloadText = new QVBoxLayout;
    downloadText->addWidget(m_downloadLabel);
    downloadText->addWidget(m_downloadBar);
    downloadLayout->addLayout(downloadText, 1);
    downloadLayout->addWidget(cancel);
    m_downloadBox->hide();
    listLayout->addWidget(m_downloadBox);
    m_error = label(QString(), "error");
    m_error->setWordWrap(true);
    m_error->hide();
    listLayout->addWidget(m_error);

    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("engineCards"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* cards = new QWidget;
    cards->setObjectName(QStringLiteral("engineCardsContent"));
    auto* cardsLayout = new QVBoxLayout(cards);
    cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_grid = new QGridLayout;
    m_grid->setSpacing(12);
    m_grid->setColumnStretch(0, 1);
    m_grid->setColumnStretch(1, 1);
    cardsLayout->addLayout(m_grid);
    cardsLayout->addStretch();
    scroll->setWidget(cards);
    listLayout->addWidget(scroll, 1);

    // ---- Right: details -------------------------------------------------------------------------
    auto* details = card();
    auto* detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(layout::kCard);
    m_details = new QStackedWidget;
    detailsLayout->addWidget(m_details);

    auto* empty = new EmptyState(IconKind::Engine, tr("No engine selected"), {}, m_style);
    m_details->addWidget(empty);

    auto* page = new QWidget;
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(14);
    auto* title = new QHBoxLayout;
    title->setSpacing(12);
    m_detailIcon = new QLabel;
    m_detailIcon->setFixedSize(36, 36);
    auto* names = new QVBoxLayout;
    names->setSpacing(1);
    m_detailName = label(QString(), "pageTitle");
    m_detailName->setObjectName(QStringLiteral("engineDetailName"));
    m_detailSubtitle = label(QString(), "emptyHint");
    names->addWidget(m_detailName);
    names->addWidget(m_detailSubtitle);
    title->addWidget(m_detailIcon, 0, Qt::AlignTop);
    title->addLayout(names, 1);
    pageLayout->addLayout(title);
    m_detailAbout = label(QString(), "emptyHint");
    m_detailAbout->setWordWrap(true);
    pageLayout->addWidget(m_detailAbout);

    // Settings laid out like the card's stats: a small title over each value.
    m_settings = new QWidget;
    auto* settingsRow = new QHBoxLayout(m_settings);
    settingsRow->setContentsMargins(0, 0, 0, 0);
    settingsRow->setSpacing(24);
    m_threads = new Stepper;
    m_threads->setObjectName(QStringLiteral("engineThreads"));
    m_threads->setRange(1, std::max(1, QThread::idealThreadCount()));
    m_hash = new Stepper;
    m_hash->setObjectName(QStringLiteral("engineHash"));
    m_hash->setRange(16, 16384);
    m_hash->setSingleStep(64);
    m_hash->setSuffix(tr(" MB"));
    settingsRow->addWidget(setting(tr("THREADS"), m_threads));
    settingsRow->addWidget(setting(tr("HASH"), m_hash));
    settingsRow->addStretch();
    const auto save = [this] {
        if (const EngineEntry* current = m_registry.find(m_selected)) {
            EngineEntry entry = *current;
            entry.threads = m_threads->value();
            entry.hashMb = m_hash->value();
            if (entry.threads != current->threads || entry.hashMb != current->hashMb) {
                m_registry.add(entry);
            }
        }
    };
    // Saved (and a running engine restarted with it) once a value is chosen, not per keystroke.
    connect(m_threads, &Stepper::valueChanged, this, save);
    connect(m_hash, &Stepper::valueChanged, this, save);
    pageLayout->addWidget(m_settings);

    auto* buttons = new QHBoxLayout;
    m_remove = new QPushButton(tr("Remove"));
    m_remove->setObjectName(QStringLiteral("removeEngine"));
    m_remove->setProperty("role", "secondary");
    connect(m_remove, &QPushButton::clicked, this, [this] {
        const QString name = m_selected;
        m_selected.clear();
        m_registry.remove(name);
    });
    buttons->addWidget(m_remove);
    buttons->addStretch();
    pageLayout->addLayout(buttons);
    pageLayout->addStretch();
    m_details->addWidget(page);

    auto* columns = new QHBoxLayout;
    columns->setSpacing(layout::kPageSpacing);
    columns->addWidget(list, 1);
    columns->addWidget(details, 1);
    layout->addLayout(columns, 1);

    // ---- Wiring
    // -----------------------------------------------------------------------------------
    connect(&m_registry, &EngineRegistry::changed, this, [this] {
        rebuildCards();
        showDetails();
    });
    connect(&m_registry, &EngineRegistry::added, this, &EnginesView::select);
    connect(&m_registry, &EngineRegistry::failed, this, &EnginesView::showError);
    const StockfishDownload* download = m_registry.download();
    connect(download, &StockfishDownload::releaseFound, this,
            [this](const QString& name, qint64 bytes) {
                m_downloadLabel->setText(tr("Downloading %1 · %2 MB")
                                             .arg(name)
                                             .arg(static_cast<double>(bytes) / 1e6, 0, 'f', 0));
            });
    connect(download, &StockfishDownload::progress, this, [this](qint64 received, qint64 total) {
        m_downloadBar->setRange(0, 1000);
        m_downloadBar->setValue(total > 0 ? static_cast<int>(received * 1000 / total) : 0);
        if (total > 0 && received >= total) {
            m_downloadLabel->setText(tr("Installing…"));
            m_downloadBar->setRange(0, 0); // busy
        }
    });
    connect(download, &StockfishDownload::finished, this, [this] { m_downloadBox->hide(); });
    connect(download, &StockfishDownload::failed, this, [this] { m_downloadBox->hide(); });
    connect(&m_style, &AppStyle::changed, this, [this] {
        updateIcons();
        rebuildCards();
        showDetails();
    });
    updateIcons();
    rebuildCards();
    showDetails();
}

void EnginesView::select(const QString& name) {
    m_selected = name;
    m_error->hide();
    // Only the highlight changes: the cards stay (one of them may be the one just clicked).
    for (auto* frame : findChildren<QFrame*>(QStringLiteral("engineCard"))) {
        if (auto* card = dynamic_cast<EngineCard*>(frame)) {
            card->setSelected(card->accessibleName() == name);
        }
    }
    showDetails();
}

void EnginesView::rebuildCards() {
    while (const QLayoutItem* item = m_grid->takeAt(0)) {
        if (QWidget* old = item->widget()) {
            old->hide();
            old->setObjectName(QString()); // no longer one of the cards
            old->deleteLater();            // not now: a click on it may still be on its way
        }
        delete item;
    }
    const auto& c = m_style.colors();
    const QString filter = m_search->text().trimmed();
    std::vector<QWidget*> cards;
    const auto matches = [&](const QString& name) {
        return filter.isEmpty() || name.contains(filter, Qt::CaseInsensitive);
    };
    bool hasStockfish = false;
    for (const auto& entry : m_registry.engines()) {
        hasStockfish = hasStockfish || entry.name.startsWith(QStringLiteral("Stockfish"));
        if (!matches(entry.name)) {
            continue;
        }
        auto* engineCard =
            new EngineCard(entry.name, makeIcon(IconKind::Engine, c.text, c.dimmed, 28),
                           QFileInfo(entry.path).fileName(),
                           {{tr("VERSION"), versionOf(entry.name)},
                            {tr("THREADS"), QString::number(entry.threads)},
                            {tr("HASH"), tr("%1 MB").arg(entry.hashMb)}},
                           [this, name = entry.name] { select(name); });
        engineCard->setSelected(entry.name == m_selected);
        cards.push_back(engineCard);
    }
    if (matches(EnginePanel::cloudName())) {
        auto* cloud = new EngineCard(EnginePanel::cloudName(),
                                     makeIcon(IconKind::Cloud, c.text, c.dimmed, 28),
                                     QStringLiteral("lichess.org"), {{tr("TYPE"), tr("Cloud")}},
                                     [this] { select(EnginePanel::cloudName()); });
        cloud->setSelected(m_selected == EnginePanel::cloudName());
        cards.push_back(cloud);
    }
    if (!hasStockfish && filter.isEmpty()) {
        // Not set up yet: offer the obvious first engine as a card of its own.
        auto* get =
            new EngineCard(tr("Get Stockfish"), makeIcon(IconKind::Plus, c.accent, c.dimmed, 28),
                           tr("Free, and the strongest"), {{tr("SOURCE"), tr("Official release")}},
                           [this] { startDownload(); });
        get->setObjectName(QStringLiteral("getStockfishCard"));
        cards.push_back(get);
    }
    for (std::size_t i = 0; i < cards.size(); ++i) {
        m_grid->addWidget(cards[i], static_cast<int>(i / 2), static_cast<int>(i % 2));
    }
}

void EnginesView::showDetails() {
    const auto& c = m_style.colors();
    const bool cloud = m_selected == EnginePanel::cloudName();
    const EngineEntry* entry = m_registry.find(m_selected);
    if (!cloud && entry == nullptr) {
        m_details->setCurrentIndex(0);
        return;
    }
    m_details->setCurrentIndex(1);
    m_detailName->setText(m_selected);
    m_detailIcon->setPixmap(
        makeIcon(cloud ? IconKind::Cloud : IconKind::Engine, c.text, c.dimmed, 36).pixmap(36, 36));
    m_settings->setVisible(!cloud);
    m_remove->setVisible(!cloud);
    if (cloud) {
        m_detailSubtitle->setText(QStringLiteral("lichess.org"));
        m_detailAbout->setVisible(true);
        m_detailAbout->setText(tr("Deep evaluations Lichess has already computed, for positions "
                                  "its users have analysed. Needs an internet connection."));
        return;
    }
    m_detailSubtitle->setText(
        tr("Version %1  ·  %2").arg(versionOf(entry->name), QFileInfo(entry->path).fileName()));
    m_detailAbout->clear();
    m_detailAbout->setVisible(false);
    m_threads->setValue(entry->threads);
    m_hash->setValue(entry->hashMb);
}

void EnginesView::updateIcons() {
    const auto& c = m_style.colors();
    m_folderButton->setIcon(makeIcon(IconKind::Folder, c.text, c.dimmed));
    m_addButton->setIcon(makeIcon(IconKind::Plus, c.text, c.dimmed, 16));
}

void EnginesView::startDownload() {
    if (m_registry.isDownloading()) {
        return;
    }
    m_error->hide();
    m_downloadLabel->setText(tr("Looking for the latest Stockfish…"));
    m_downloadBar->setRange(0, 0);
    m_downloadBox->show();
    m_registry.downloadStockfish();
}

void EnginesView::chooseFile() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Add a UCI Engine"));
    if (!path.isEmpty()) {
        m_registry.addFromFile(path);
    }
}

void EnginesView::showError(const QString& message) {
    m_error->setText(message);
    m_error->show();
}

} // namespace kiebitz::app
