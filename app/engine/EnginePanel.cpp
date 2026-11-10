#include "EnginePanel.hpp"

#include "EngineRegistry.hpp"
#include "EngineSection.hpp"
#include "style/AppStyle.hpp"
#include "style/Icons.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace chessgui::app {
namespace {

constexpr auto kLinesKey = "engine/lines";
constexpr auto kRunningKey = "engine/lastRunning";

} // namespace

QString EnginePanel::cloudName() {
    return tr("Lichess Cloud");
}

EnginePanel::EnginePanel(EngineRegistry& registry, const AppStyle& style, QWidget* parent)
    : QWidget(parent), m_registry(registry), m_style(style) {
    const QSettings settings;
    m_multiPv = std::clamp(settings.value(QString::fromLatin1(kLinesKey), 3).toInt(), 1,
                           EngineSection::kMaxLines);
    m_lastRunning = settings.value(QString::fromLatin1(kRunningKey)).toStringList();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    // Top: how many lines each engine shows, and the way to the Engines page.
    auto* header = new QHBoxLayout;
    header->setSpacing(6);
    m_noEngine = new QWidget;
    m_noEngine->setObjectName(QStringLiteral("noEngine"));
    auto* noEngineLayout = new QHBoxLayout(m_noEngine);
    noEngineLayout->setContentsMargins(0, 0, 0, 0);
    auto* noEngineText = new QLabel(tr("No engine on this Mac yet"));
    noEngineText->setProperty("role", "engineStatus");
    noEngineLayout->addWidget(noEngineText);
    auto* getOne = new QPushButton(tr("Get Stockfish"));
    getOne->setObjectName(QStringLiteral("getStockfish"));
    getOne->setProperty("role", "link");
    getOne->setFocusPolicy(Qt::NoFocus);
    getOne->setCursor(Qt::PointingHandCursor);
    connect(getOne, &QPushButton::clicked, this, &EnginePanel::manageEnginesRequested);
    noEngineLayout->addWidget(getOne);
    m_lineCount = new QSpinBox;
    m_lineCount->setObjectName(QStringLiteral("engineLines"));
    m_lineCount->setRange(1, EngineSection::kMaxLines);
    m_lineCount->setValue(m_multiPv);
    m_lineCount->setSuffix(tr(" lines"));
    m_lineCount->setFocusPolicy(Qt::NoFocus);
    connect(m_lineCount, &QSpinBox::valueChanged, this, &EnginePanel::setLineCount);
    m_manage = new QToolButton;
    m_manage->setObjectName(QStringLiteral("manageEngines"));
    m_manage->setToolTip(tr("Engines"));
    m_manage->setFocusPolicy(Qt::NoFocus);
    m_manage->setIconSize(QSize(18, 18));
    connect(m_manage, &QToolButton::clicked, this, &EnginePanel::manageEnginesRequested);
    header->addWidget(m_noEngine);
    header->addStretch();
    header->addWidget(m_lineCount);
    header->addWidget(m_manage);
    layout->addLayout(header);

    // The engines, one section each; they scroll when several run with many lines.
    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("engineSections"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget;
    content->setObjectName(QStringLiteral("engineSectionsContent"));
    m_sectionLayout = new QVBoxLayout(content);
    m_sectionLayout->setContentsMargins(0, 0, 0, 0);
    m_sectionLayout->setSpacing(4);
    m_sectionLayout->addStretch();
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);

    connect(&m_registry, &EngineRegistry::changed, this, &EnginePanel::rebuildSections);
    // A newly added engine is what Space starts next, if nothing else ran before.
    connect(&m_registry, &EngineRegistry::added, this, [this](const QString& name) {
        if (!isRunning()) {
            m_lastRunning = {name};
            rememberRunning();
        }
    });
    connect(&m_style, &AppStyle::changed, this, &EnginePanel::updateIcons);
    updateIcons();
    rebuildSections();
}

void EnginePanel::rebuildSections() {
    // Wanted: the user's engines in order, then the cloud. Sections that stay keep running.
    std::vector<std::pair<QString, std::optional<EngineEntry>>> wanted;
    for (const auto& entry : m_registry.engines()) {
        wanted.emplace_back(entry.name, entry);
    }
    wanted.emplace_back(cloudName(), std::nullopt);

    std::vector<EngineSection*> sections;
    for (const auto& [name, entry] : wanted) {
        EngineSection* existing = section(name);
        if (existing != nullptr) {
            if (entry) {
                const EngineEntry* now = m_registry.find(name);
                existing->setEntry(*now); // restarts it if its settings changed while running
            }
            sections.push_back(existing);
            continue;
        }
        auto* created = new EngineSection(name, entry, m_style);
        created->setLineCount(m_multiPv);
        created->setPosition(m_position);
        connect(created, &EngineSection::changed, this, &EnginePanel::publish);
        connect(created, &EngineSection::playRequested, this, &EnginePanel::playRequested);
        sections.push_back(created);
    }
    // Removed engines stop and go.
    for (auto* old : m_sections) {
        if (std::find(sections.begin(), sections.end(), old) == sections.end()) {
            old->disconnect(this);
            old->setRunning(false);
            old->hide();
            old->deleteLater();
        }
    }
    m_sections = std::move(sections);
    for (std::size_t i = 0; i < m_sections.size(); ++i) {
        m_sectionLayout->insertWidget(static_cast<int>(i), m_sections[i]);
    }
    m_noEngine->setVisible(m_registry.engines().empty());
    publish();
}

void EnginePanel::setPosition(std::optional<core::Position> position) {
    m_position = std::move(position);
    for (auto* s : m_sections) {
        s->setPosition(m_position);
    }
}

void EnginePanel::setRunning(const QString& name, bool running) {
    if (EngineSection* s = section(name)) {
        s->setRunning(running);
        if (running) {
            rememberRunning();
        }
    }
}

bool EnginePanel::isRunning(const QString& name) const {
    const EngineSection* s = section(name);
    return s != nullptr && s->isRunning();
}

bool EnginePanel::isRunning() const {
    return std::any_of(m_sections.begin(), m_sections.end(),
                       [](const EngineSection* s) { return s->isRunning(); });
}

void EnginePanel::toggle() {
    if (isRunning()) {
        rememberRunning();
        for (auto* s : m_sections) {
            s->setRunning(false);
        }
        return;
    }
    bool started = false;
    for (const QString& name : m_lastRunning) {
        if (EngineSection* s = section(name)) {
            s->setRunning(true);
            started = true;
        }
    }
    if (!started && !m_sections.empty()) {
        m_sections.front()->setRunning(true); // the first engine, or the cloud if there is none
        rememberRunning();
    }
}

EngineSection* EnginePanel::section(const QString& name) const {
    const auto it = std::find_if(m_sections.begin(), m_sections.end(),
                                 [&](const EngineSection* s) { return s->name() == name; });
    return it == m_sections.end() ? nullptr : *it;
}

EngineSection* EnginePanel::primary() const {
    const auto it = std::find_if(m_sections.begin(), m_sections.end(),
                                 [](const EngineSection* s) { return s->isRunning(); });
    return it == m_sections.end() ? nullptr : *it;
}

const std::vector<engine::Info>& EnginePanel::lines() const {
    static const std::vector<engine::Info> kNone;
    const EngineSection* s = primary();
    return s != nullptr ? s->lines() : kNone;
}

void EnginePanel::setLineCount(int lines) {
    lines = std::clamp(lines, 1, EngineSection::kMaxLines);
    m_multiPv = lines;
    {
        const QSignalBlocker block(m_lineCount);
        m_lineCount->setValue(lines);
    }
    QSettings().setValue(QString::fromLatin1(kLinesKey), lines);
    for (auto* s : m_sections) {
        s->setLineCount(lines);
    }
}

void EnginePanel::publish() {
    const EngineSection* s = primary();
    if (s == nullptr) {
        emit analysisChanged({}, std::nullopt, false);
        return;
    }
    emit analysisChanged(s->bestMoves(), s->score(), true);
}

void EnginePanel::rememberRunning() {
    QStringList running;
    for (const auto* s : m_sections) {
        if (s->isRunning()) {
            running << s->name();
        }
    }
    if (!running.isEmpty()) {
        m_lastRunning = running;
    }
    QSettings().setValue(QString::fromLatin1(kRunningKey), m_lastRunning);
}

void EnginePanel::updateIcons() {
    const auto& c = m_style.colors();
    m_manage->setIcon(makeIcon(IconKind::Engine, c.muted, c.dimmed, 18));
}

} // namespace chessgui::app
