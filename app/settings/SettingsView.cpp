#include "SettingsView.hpp"

#include "Settings.hpp"
#include "board/BoardTheme.hpp"
#include "board/PieceSet.hpp"
#include "engine/EngineSettings.hpp"
#include "style/AppStyle.hpp"
#include "style/Icons.hpp"
#include "style/Layout.hpp"
#include "style/PageHeader.hpp"
#include "widgets/Stepper.hpp"
#include "widgets/Toggle.hpp"

#include <QButtonGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace kiebitz::app {
namespace {

QLabel* label(const QString& text, const char* role) {
    auto* result = new QLabel(text);
    result->setProperty("role", role);
    return result;
}

/// A section's page: its rows in a column that scrolls.
QWidget* scrolling(QWidget* content) {
    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("settingsScroll"));
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return scroll;
}

/// A board's colours as a small 2×2 board.
QIcon themeIcon(const BoardTheme& theme) {
    QPixmap pixmap(40, 40);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    for (int i = 0; i < 4; ++i) {
        const bool light = (i == 0 || i == 3);
        painter.fillRect(QRect((i % 2) * 20, (i / 2) * 20, 20, 20),
                         light ? theme.light : theme.dark);
    }
    return {pixmap};
}

/// The accent colours offered (the first one: the theme's own blue).
const std::vector<std::pair<QString, QString>>& accents() {
    static const std::vector<std::pair<QString, QString>> colours{
        {QString(), QObject::tr("Blue")},
        {QStringLiteral("#8B6CF0"), QObject::tr("Purple")},
        {QStringLiteral("#E96CB5"), QObject::tr("Pink")},
        {QStringLiteral("#F0645E"), QObject::tr("Red")},
        {QStringLiteral("#F29B3F"), QObject::tr("Orange")},
        {QStringLiteral("#E3C341"), QObject::tr("Yellow")},
        {QStringLiteral("#4CC38A"), QObject::tr("Green")},
        {QStringLiteral("#3BB8C4"), QObject::tr("Teal")},
    };
    return colours;
}

} // namespace

SettingsView::SettingsView(const AppStyle& style, QWidget* parent)
    : QWidget(parent), m_style(style) {
    setObjectName(QStringLiteral("settingsView"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(layout::kPage);
    layout->setSpacing(layout::kPageSpacing);
    layout->addWidget(new PageHeader(tr("Settings")));

    m_sections = new QListWidget;
    m_sections->setObjectName(QStringLiteral("settingsSections"));
    m_sections->setFrameShape(QFrame::NoFrame);
    m_sections->setFixedWidth(200);
    m_sections->setSpacing(2);
    m_pages = new QStackedWidget;
    connect(m_sections, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    auto* listCard = new QFrame;
    listCard->setProperty("card", true);
    auto* listBox = new QVBoxLayout(listCard);
    listBox->setContentsMargins(layout::kCard);
    listBox->addWidget(m_sections);
    auto* pageCard = new QFrame;
    pageCard->setProperty("card", true);
    auto* pageBox = new QVBoxLayout(pageCard);
    pageBox->setContentsMargins(0, 0, 0, 0);
    pageBox->addWidget(m_pages);
    auto* body = new QHBoxLayout;
    body->setSpacing(layout::kPageSpacing);
    body->addWidget(listCard);
    body->addWidget(pageCard, 1);
    layout->addLayout(body, 1);

    addSection(tr("Board"), boardSection());
    addSection(tr("Moves"), movesSection());
    addSection(tr("Appearance"), appearanceSection());
    addSection(tr("Engines"), enginesSection());
    addSection(tr("Repertoire"), repertoireSection());
    addSection(tr("Sounds"), soundsSection());
    addSection(tr("Folders"), foldersSection());
    m_sections->setCurrentRow(0);

    connect(&Settings::instance(), &Settings::changed, this, [this](const QString& key) {
        const auto [first, last] = m_refresh.equal_range(key);
        for (auto it = first; it != last; ++it) {
            it->second();
        }
    });
}

void SettingsView::addSection(const QString& title, QWidget* content, const QString& before) {
    int at = m_sections->count();
    for (int i = 0; i < m_sections->count(); ++i) {
        if (m_sections->item(i)->text() == before) {
            at = i;
        }
    }
    auto* item = new QListWidgetItem(title);
    item->setSizeHint(QSize(0, 34));
    m_sections->insertItem(at, item);
    m_pages->insertWidget(at, scrolling(content));
}

void SettingsView::showSection(const QString& title) {
    for (int i = 0; i < m_sections->count(); ++i) {
        if (m_sections->item(i)->text() == title) {
            m_sections->setCurrentRow(i);
        }
    }
}

void SettingsView::follow(const char* key, std::function<void()> refresh) {
    refresh();
    m_refresh.emplace(QString::fromLatin1(key), std::move(refresh));
}

QWidget* SettingsView::row(const QString& title, const QString& about, QWidget* control) {
    auto* frame = new QWidget;
    frame->setObjectName(QStringLiteral("settingsRow"));
    auto* box = new QHBoxLayout(frame);
    box->setContentsMargins(0, 10, 0, 10);
    box->setSpacing(16);
    auto* texts = new QVBoxLayout;
    texts->setSpacing(2);
    texts->addWidget(label(title, "databaseName"));
    if (!about.isEmpty()) {
        auto* line = label(about, "emptyHint");
        line->setWordWrap(true);
        texts->addWidget(line);
    }
    box->addLayout(texts, 1);
    if (control != nullptr) {
        box->addWidget(control, 0, Qt::AlignVCenter);
    }
    return frame;
}

QVBoxLayout* SettingsView::group(QVBoxLayout* page, const QString& title) {
    page->addWidget(label(title.toUpper(), "cardTitle"));
    auto* box = new QFrame;
    box->setProperty("statTile", true);
    auto* rows = new QVBoxLayout(box);
    rows->setContentsMargins(16, 4, 16, 4);
    rows->setSpacing(0);
    page->addWidget(box);
    page->addSpacing(14);
    return rows;
}

QWidget* SettingsView::segments(const char* key,
                                const std::vector<std::pair<QVariant, QString>>& choices,
                                const QString& name) {
    auto* track = new QWidget;
    track->setProperty("role", "segmented");
    auto* box = new QHBoxLayout(track);
    box->setContentsMargins(2, 2, 2, 2);
    box->setSpacing(2);
    auto* buttons = new QButtonGroup(track);
    for (std::size_t i = 0; i < choices.size(); ++i) {
        auto* button = new QPushButton(choices[i].second);
        button->setObjectName(QStringLiteral("%1:%2").arg(name, choices[i].first.toString()));
        button->setProperty("role", "segment");
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        buttons->addButton(button, static_cast<int>(i));
        box->addWidget(button);
        connect(button, &QPushButton::clicked, this,
                [key, value = choices[i].first] { Settings::instance().setValue(key, value); });
    }
    buttons->setExclusive(true);
    follow(key, [key, buttons, name] {
        // The button named "<name>:<the value now>".
        const QString chosen = name + QLatin1Char(':') + Settings::instance().string(key);
        for (QAbstractButton* button : buttons->buttons()) {
            if (button->objectName() == chosen) {
                button->setChecked(true);
            }
        }
    });
    return track;
}

QWidget* SettingsView::toggle(const char* key, const QString& name) {
    auto* toggle = new Toggle(m_style);
    toggle->setObjectName(name);
    connect(toggle, &Toggle::clicked, this,
            [key](bool on) { Settings::instance().setValue(key, on); });
    follow(key, [key, toggle] { toggle->setChecked(Settings::instance().flag(key)); });
    return toggle;
}

QWidget* SettingsView::boardSection() {
    auto* page = new QWidget;
    auto* box = new QVBoxLayout(page);
    box->setContentsMargins(24, 20, 24, 20);
    box->setSpacing(6);

    // How it looks: its colours and pieces, as small previews.
    QVBoxLayout* look = group(box, tr("Look"));
    auto* themes = new QWidget;
    auto* themeRow = new QHBoxLayout(themes);
    themeRow->setContentsMargins(0, 0, 0, 0);
    themeRow->setSpacing(6);
    auto* themeButtons = new QButtonGroup(themes);
    for (const BoardTheme& theme : BoardTheme::presets()) {
        auto* button = new QPushButton;
        button->setObjectName(QStringLiteral("settingsTheme:") + theme.name);
        button->setProperty("role", "swatch");
        button->setToolTip(theme.name);
        button->setIcon(themeIcon(theme));
        button->setIconSize(QSize(32, 32));
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        themeButtons->addButton(button);
        themeRow->addWidget(button);
        connect(button, &QPushButton::clicked, this,
                [name = theme.name] { Settings::instance().setValue(Settings::kTheme, name); });
    }
    auto* custom = new QPushButton(tr("Custom…"));
    custom->setObjectName(QStringLiteral("settingsCustomColours"));
    custom->setProperty("role", "secondary");
    custom->setProperty("compact", true);
    custom->setCheckable(true);
    themeButtons->addButton(custom);
    connect(custom, &QPushButton::clicked, this, &SettingsView::customColorsRequested);
    themeRow->addWidget(custom);
    themeButtons->setExclusive(true);
    follow(Settings::kTheme, [themeButtons, custom] {
        const QString name = Settings::instance().string(Settings::kTheme);
        bool found = false;
        for (QAbstractButton* button : themeButtons->buttons()) {
            if (button->toolTip() == name && button != custom) {
                button->setChecked(true);
                found = true;
            }
        }
        if (!found) {
            custom->setChecked(name == u"Custom");
            if (name.isEmpty() && !themeButtons->buttons().isEmpty()) {
                themeButtons->buttons().front()->setChecked(true); // the default
            }
        }
    });
    look->addWidget(row(tr("Board colours"), {}, themes));

    auto* pieces = new QWidget;
    auto* pieceRow = new QHBoxLayout(pieces);
    pieceRow->setContentsMargins(0, 0, 0, 0);
    pieceRow->setSpacing(6);
    auto* pieceButtons = new QButtonGroup(pieces);
    for (const QString& name : PieceSet::available()) {
        PieceSet set(name);
        auto* button = new QPushButton;
        button->setObjectName(QStringLiteral("settingsPieces:") + name);
        button->setProperty("role", "swatch");
        button->setToolTip(name);
        button->setIcon(QIcon(
            set.pixmap({core::PieceType::Knight, core::Color::White}, 32, devicePixelRatioF())));
        button->setIconSize(QSize(32, 32));
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        pieceButtons->addButton(button);
        pieceRow->addWidget(button);
        connect(button, &QPushButton::clicked, this,
                [name] { Settings::instance().setValue(Settings::kPieces, name); });
    }
    pieceButtons->setExclusive(true);
    follow(Settings::kPieces, [pieceButtons] {
        QString name = Settings::instance().string(Settings::kPieces);
        if (name.isEmpty() || !PieceSet::available().contains(name)) {
            name = PieceSet::available().front();
        }
        for (QAbstractButton* button : pieceButtons->buttons()) {
            button->setChecked(button->toolTip() == name);
        }
    });
    look->addWidget(row(tr("Pieces"), {}, pieces));
    look->addWidget(row(tr("Coordinates"),
                        tr("The files and ranks, along the edge or on every square."),
                        segments(Settings::kCoordinates,
                                 {{QStringLiteral("none"), tr("None")},
                                  {QStringLiteral("edge"), tr("Edge")},
                                  {QStringLiteral("all"), tr("Every square")}},
                                 QStringLiteral("settingsCoordinates"))));
    look->addWidget(row(tr("Last move"), tr("Its two squares highlighted."),
                        toggle(Settings::kLastMove, QStringLiteral("settingsLastMove"))));
    look->addWidget(row(tr("Where a piece can go"),
                        tr("Dots on the squares a picked-up piece can move to."),
                        toggle(Settings::kLegalDots, QStringLiteral("settingsLegalDots"))));
    look->addWidget(row(tr("Moving pieces"), tr("How fast a move slides into place."),
                        segments(Settings::kAnimation,
                                 {{QStringLiteral("off"), tr("Off")},
                                  {QStringLiteral("fast"), tr("Fast")},
                                  {QStringLiteral("normal"), tr("Normal")},
                                  {QStringLiteral("slow"), tr("Slow")}},
                                 QStringLiteral("settingsAnimation"))));

    // How it plays.
    QVBoxLayout* play = group(box, tr("Playing moves"));
    play->addWidget(row(tr("Move pieces by"), tr("Dragging them, clicking from and to, or either."),
                        segments(Settings::kMoveMethod,
                                 {{QStringLiteral("both"), tr("Either")},
                                  {QStringLiteral("drag"), tr("Dragging")},
                                  {QStringLiteral("click"), tr("Clicking")}},
                                 QStringLiteral("settingsMoveMethod"))));
    play->addWidget(row(tr("Promote to a queen"), tr("Without asking which piece."),
                        toggle(Settings::kAutoQueen, QStringLiteral("settingsAutoQueen"))));
    play->addWidget(row(tr("Scroll through moves"),
                        tr("The mouse wheel or trackpad over the board steps back and forward."),
                        toggle(Settings::kWheel, QStringLiteral("settingsWheel"))));
    box->addStretch();
    return page;
}

QWidget* SettingsView::movesSection() {
    auto* page = new QWidget;
    auto* box = new QVBoxLayout(page);
    box->setContentsMargins(24, 20, 24, 20);
    box->setSpacing(6);
    QVBoxLayout* list = group(box, tr("Move list"));
    list->addWidget(row(tr("Layout"), tr("Flowing text, or rows with White's and Black's moves."),
                        segments(Settings::kNotationRows, {{false, tr("Text")}, {true, tr("Rows")}},
                                 QStringLiteral("settingsRows"))));
    list->addWidget(
        row(tr("Pieces"), tr("As letters (Nf3) or as symbols (♘f3)."),
            segments(Settings::kFigurines, {{false, tr("Letters")}, {true, tr("Symbols")}},
                     QStringLiteral("settingsFigurines"))));
    box->addStretch();
    return page;
}

QWidget* SettingsView::appearanceSection() {
    auto* page = new QWidget;
    auto* box = new QVBoxLayout(page);
    box->setContentsMargins(24, 20, 24, 20);
    box->setSpacing(6);
    QVBoxLayout* look = group(box, tr("App"));
    look->addWidget(row(tr("Appearance"), tr("Light or dark, or as macOS is set."),
                        segments(Settings::kAppearance,
                                 {{QStringLiteral("system"), tr("System")},
                                  {QStringLiteral("light"), tr("Light")},
                                  {QStringLiteral("dark"), tr("Dark")}},
                                 QStringLiteral("settingsAppearance"))));
    // The app's language (each language names itself); it changes on the next start.
    look->addWidget(row(tr("Language"), tr("As macOS is set, or one of these. After a restart."),
                        segments(Settings::kLanguage,
                                 {{QStringLiteral("system"), tr("System")},
                                  {QStringLiteral("en"), QStringLiteral("English")},
                                  {QStringLiteral("de"), QStringLiteral("Deutsch")},
                                  {QStringLiteral("nb"), QStringLiteral("Norsk")}},
                                 QStringLiteral("settingsLanguage"))));

    // The accent colour: round swatches, and one of your own.
    auto* colours = new QWidget;
    auto* colourRow = new QHBoxLayout(colours);
    colourRow->setContentsMargins(0, 0, 0, 0);
    colourRow->setSpacing(6);
    auto* colourButtons = new QButtonGroup(colours);
    const auto swatch = [](const QColor& colour) {
        QPixmap pixmap(22, 22);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colour);
        painter.drawEllipse(QRectF(1, 1, 20, 20));
        return QIcon(pixmap);
    };
    for (const auto& [value, name] : accents()) {
        auto* button = new QPushButton;
        button->setObjectName(QStringLiteral("settingsAccent:") + name);
        button->setProperty("role", "accentSwatch");
        button->setToolTip(name);
        button->setIcon(swatch(value.isEmpty() ? AppStyle::darkColors().accent : QColor(value)));
        button->setIconSize(QSize(22, 22));
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        colourButtons->addButton(button);
        colourRow->addWidget(button);
        connect(button, &QPushButton::clicked, this,
                [value] { Settings::instance().setValue(Settings::kAccent, value); });
    }
    auto* own = new QPushButton(tr("Other…"));
    own->setObjectName(QStringLiteral("settingsAccentOther"));
    own->setProperty("role", "secondary");
    own->setProperty("compact", true);
    own->setCheckable(true);
    colourButtons->addButton(own);
    connect(own, &QPushButton::clicked, this, [this] {
        const QString now = Settings::instance().string(Settings::kAccent);
        const QColor chosen = QColorDialog::getColor(
            now.isEmpty() ? m_style.colors().accent : QColor(now), this, tr("Accent Colour"));
        if (chosen.isValid()) {
            Settings::instance().setValue(Settings::kAccent, chosen.name());
        } else {
            Settings::instance().changed(QString::fromLatin1(Settings::kAccent)); // back to ticks
        }
    });
    colourRow->addWidget(own);
    colourButtons->setExclusive(true);
    follow(Settings::kAccent, [colourButtons, own] {
        const QString now = Settings::instance().string(Settings::kAccent).toLower();
        bool found = false;
        int i = 0;
        for (const auto& [value, name] : accents()) {
            if (value.toLower() == now) {
                colourButtons->buttons().at(i)->setChecked(true);
                found = true;
            }
            ++i;
        }
        own->setChecked(!found);
    });
    look->addWidget(row(tr("Accent colour"), tr("Buttons, selections and highlights."), colours));
    look->addWidget(row(
        tr("Text size"), {},
        segments(
            Settings::kTextSize,
            {{90, tr("Smaller")}, {100, tr("Default")}, {110, tr("Larger")}, {120, tr("Largest")}},
            QStringLiteral("settingsTextSize"))));
    box->addStretch();
    return page;
}

QWidget* SettingsView::enginesSection() {
    auto* page = new QWidget;
    auto* box = new QVBoxLayout(page);
    box->setContentsMargins(24, 20, 24, 20);
    box->setSpacing(6);
    QVBoxLayout* rows = group(box, tr("Analysis"));
    auto* cloud = new Stepper;
    cloud->setObjectName(QStringLiteral("settingsCloudLines"));
    cloud->setRange(1, engines::kMaxLines);
    cloud->setValue(engines::cloudLines());
    connect(cloud, &Stepper::valueChanged, this, [](int lines) { engines::setCloudLines(lines); });
    rows->addWidget(row(tr("Lichess cloud lines"),
                        tr("How many of the best moves the Lichess cloud shows."), cloud));
    auto* open = new QPushButton(tr("Open Engines"));
    open->setProperty("role", "secondary");
    open->setProperty("compact", true);
    connect(open, &QPushButton::clicked, this, &SettingsView::showEnginesRequested);
    rows->addWidget(row(tr("Each engine"),
                        tr("Lines, threads and memory are set per engine, on the Engines page."),
                        open));
    box->addStretch();
    return page;
}

QWidget* SettingsView::repertoireSection() {
    auto* page = new QWidget;
    auto* box = new QVBoxLayout(page);
    box->setContentsMargins(24, 20, 24, 20);
    box->setSpacing(6);
    const auto stepper = [this](const char* key, int low, int high, const QString& suffix,
                                const QString& name) {
        auto* field = new Stepper;
        field->setObjectName(name);
        field->setRange(low, high);
        field->setSuffix(suffix);
        connect(field, &Stepper::valueChanged, this,
                [key](int value) { Settings::instance().setValue(key, value); });
        follow(key, [key, field] { field->setValue(Settings::instance().number(key)); });
        return field;
    };
    QVBoxLayout* practice = group(box, tr("Practice"));
    practice->addWidget(
        row(tr("Up to move"), tr("How deep practice goes."),
            stepper(Settings::kPracticeDepth, 1, 60, {}, QStringLiteral("settingsPracticeDepth"))));
    QVBoxLayout* gaps = group(box, tr("Gap check"));
    gaps->addWidget(row(tr("Played at least"),
                        tr("How often an opponent's move must be played in a position to count "
                           "as a gap."),
                        stepper(Settings::kGapShare, 1, 50, QStringLiteral("%"),
                                QStringLiteral("settingsGapShare"))));
    gaps->addWidget(
        row(tr("Games at least"), tr("Positions with fewer games aren't judged."),
            stepper(Settings::kGapGames, 1, 1000, {}, QStringLiteral("settingsGapGames"))));
    box->addStretch();
    return page;
}

QWidget* SettingsView::soundsSection() {
    auto* page = new QWidget;
    auto* box = new QVBoxLayout(page);
    box->setContentsMargins(24, 20, 24, 20);
    box->setSpacing(6);
    QVBoxLayout* rows = group(box, tr("Board"));
    rows->addWidget(row(tr("Move sounds"), tr("A sound for each move, capture and check."),
                        toggle(Settings::kSounds, QStringLiteral("settingsSounds"))));
    auto* sets = new QComboBox;
    sets->setObjectName(QStringLiteral("settingsSoundSet"));
    sets->setFocusPolicy(Qt::NoFocus);
    for (const auto& [value, name] :
         {std::pair{"standard", tr("Standard")}, std::pair{"woodland", tr("Woodland")},
          std::pair{"sfx", tr("Effects")}, std::pair{"piano", tr("Piano")},
          std::pair{"nes", tr("Retro")}, std::pair{"futuristic", tr("Futuristic")}}) {
        sets->addItem(name, QString::fromLatin1(value));
    }
    connect(sets, &QComboBox::activated, this,
            [sets] { Settings::instance().setValue(Settings::kSoundSet, sets->currentData()); });
    follow(Settings::kSoundSet, [sets] {
        const QSignalBlocker block(sets);
        sets->setCurrentIndex(
            std::max(0, sets->findData(Settings::instance().string(Settings::kSoundSet))));
    });
    rows->addWidget(row(tr("Sound set"), {}, sets));
    auto* volume = new QWidget;
    auto* volumeRow = new QHBoxLayout(volume);
    volumeRow->setContentsMargins(0, 0, 0, 0);
    volumeRow->setSpacing(10);
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setObjectName(QStringLiteral("settingsVolume"));
    slider->setRange(0, 100);
    slider->setFixedWidth(180);
    slider->setFocusPolicy(Qt::NoFocus);
    // Kept when let go (not on every step of a drag).
    connect(slider, &QSlider::sliderReleased, this,
            [slider] { Settings::instance().setValue(Settings::kVolume, slider->value()); });
    connect(slider, &QSlider::valueChanged, this, [slider](int value) {
        if (!slider->isSliderDown()) {
            Settings::instance().setValue(Settings::kVolume, value); // a click on the track
        }
    });
    auto* test = new QPushButton(tr("Try"));
    test->setObjectName(QStringLiteral("settingsSoundTest"));
    test->setProperty("role", "secondary");
    test->setProperty("compact", true);
    connect(test, &QPushButton::clicked, this, &SettingsView::soundTestRequested);
    volumeRow->addWidget(slider);
    volumeRow->addWidget(test);
    follow(Settings::kVolume, [slider] {
        const QSignalBlocker block(slider);
        slider->setValue(Settings::instance().number(Settings::kVolume));
    });
    follow(Settings::kSounds, [slider, test] {
        const bool on = Settings::instance().flag(Settings::kSounds);
        slider->setEnabled(on);
        test->setEnabled(on);
    });
    rows->addWidget(row(tr("Volume"), {}, volume));
    box->addStretch();
    return page;
}

QWidget* SettingsView::foldersSection() {
    auto* page = new QWidget;
    auto* box = new QVBoxLayout(page);
    box->setContentsMargins(24, 20, 24, 20);
    box->setSpacing(6);
    m_folders = group(box, tr("Where things are"));
    box->addStretch();
    return page;
}

void SettingsView::setFolders(const std::vector<std::pair<QString, QString>>& folders) {
    while (const QLayoutItem* item = m_folders->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (std::size_t i = 0; i < folders.size(); ++i) {
        const auto& [name, path] = folders[i];
        auto* buttons = new QWidget;
        auto* buttonRow = new QHBoxLayout(buttons);
        buttonRow->setContentsMargins(0, 0, 0, 0);
        buttonRow->setSpacing(6);
        if (i == 0) {
            auto* change = new QPushButton(tr("Change…"));
            change->setObjectName(QStringLiteral("settingsChangeLibrary"));
            change->setProperty("role", "secondary");
            change->setProperty("compact", true);
            connect(change, &QPushButton::clicked, this,
                    &SettingsView::changeLibraryFolderRequested);
            buttonRow->addWidget(change);
        }
        auto* show = new QPushButton(tr("Show in Finder"));
        show->setProperty("role", "secondary");
        show->setProperty("compact", true);
        connect(show, &QPushButton::clicked, this, [path] {
            QDir().mkpath(path);
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });
        buttonRow->addWidget(show);
        m_folders->addWidget(row(name, QDir::toNativeSeparators(path), buttons));
    }
}

} // namespace kiebitz::app
