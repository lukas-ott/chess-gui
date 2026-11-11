#include "AddDatabaseDialog.hpp"

#include <QButtonGroup>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#include <array>
#include <cstring>

namespace kiebitz::app {
namespace {

/// A database to get from the web. Only sources whose download page works are listed.
struct WebSource {
    const char* name;
    const char* about;
    const char* size;
    const char* licence;
    const char* page;
};

// Lumbra's Gigabase: free for non-commercial use (CC BY-NC 4.0), with credit; updated monthly.
const std::array<WebSource, 2> kSources{{
    {"Lumbra's Gigabase: Over the Board",
     QT_TRANSLATE_NOOP("AddDatabaseDialog", "Over 11 million over-the-board games, "
                                            "cleaned up and updated every month."),
     QT_TRANSLATE_NOOP("AddDatabaseDialog", "PGN in .7z, about 2.6 GB to download"),
     QT_TRANSLATE_NOOP("AddDatabaseDialog",
                       "CC BY-NC 4.0: free for non-commercial use, credit Lumbra's Gigabase."),
     "https://lumbrasgigabase.com/en/download-in-pgn-format-en/"},
    {"Lumbra's Gigabase: Online",
     QT_TRANSLATE_NOOP("AddDatabaseDialog",
                       "Over 6 million online games of strong players, updated every month."),
     QT_TRANSLATE_NOOP("AddDatabaseDialog", "PGN in .7z, about 1.4 GB to download"),
     QT_TRANSLATE_NOOP("AddDatabaseDialog",
                       "CC BY-NC 4.0: free for non-commercial use, credit Lumbra's Gigabase."),
     "https://lumbrasgigabase.com/en/download-in-pgn-format-en/"},
}};

const char* const kFileFilter =
    QT_TRANSLATE_NOOP("AddDatabaseDialog", "Chess databases (*.pgn *.7z *.zip *.gz *.bz2)");

QLabel* label(const QString& text, const char* role) {
    auto* result = new QLabel(text);
    result->setProperty("role", role);
    result->setWordWrap(true);
    return result;
}

} // namespace

AddDatabaseDialog::AddDatabaseDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Add Database"));
    setObjectName(QStringLiteral("addDatabaseDialog"));
    setMinimumWidth(620);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);

    // Web | Local
    auto* tabs = new QWidget;
    tabs->setProperty("role", "segmented");
    auto* tabRow = new QHBoxLayout(tabs);
    tabRow->setContentsMargins(2, 2, 2, 2);
    tabRow->setSpacing(2);
    auto* group = new QButtonGroup(this);
    for (const auto& [index, text, name] : {std::tuple{0, tr("Web"), "addDatabaseWeb"},
                                            std::tuple{1, tr("Local"), "addDatabaseLocal"}}) {
        auto* tab = new QPushButton(text);
        tab->setObjectName(QString::fromLatin1(name));
        tab->setProperty("role", "segment");
        tab->setCheckable(true);
        tab->setChecked(index == 0);
        tab->setFocusPolicy(Qt::NoFocus);
        tab->setFixedWidth(88);
        group->addButton(tab, index);
        tabRow->addWidget(tab);
    }
    layout->addWidget(tabs, 0, Qt::AlignLeft);
    m_pages = new QStackedWidget;
    m_pages->addWidget(buildWeb());
    m_pages->addWidget(buildLocal());
    layout->addWidget(m_pages, 1);
    connect(group, &QButtonGroup::idClicked, m_pages, &QStackedWidget::setCurrentIndex);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* cancel = new QPushButton(tr("Cancel"));
    cancel->setProperty("role", "secondary");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);
}

QWidget* AddDatabaseDialog::buildWeb() {
    auto* page = new QWidget;
    auto* box = new QVBoxLayout(page);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(10);
    for (const WebSource& source : kSources) {
        auto* card = new QFrame;
        card->setProperty("card", true);
        auto* cardBox = new QVBoxLayout(card);
        cardBox->setContentsMargins(16, 14, 16, 14);
        cardBox->setSpacing(4);
        cardBox->addWidget(label(QString::fromUtf8(source.name), "sectionTitle"));
        cardBox->addWidget(label(tr(source.about), "pvLine"));
        cardBox->addWidget(label(tr(source.size), "emptyHint"));
        cardBox->addWidget(label(tr(source.licence), "emptyHint"));
        auto* row = new QHBoxLayout;
        row->setSpacing(6);
        auto* open = new QPushButton(tr("Open Download Page"));
        open->setProperty("role", "secondary");
        connect(open, &QPushButton::clicked, this, [page = QString::fromLatin1(source.page)] {
            QDesktopServices::openUrl(QUrl(page));
        });
        auto* import = new QPushButton(tr("Import Downloaded File…"));
        import->setProperty("role", "primary");
        connect(import, &QPushButton::clicked, this, [this, &source] {
            const QString path = QFileDialog::getOpenFileName(
                this, tr("Import %1").arg(QString::fromUtf8(source.name)),
                QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                tr(kFileFilter));
            if (path.isEmpty()) {
                return;
            }
            m_path = path;
            m_source = QString::fromUtf8(source.name);
            m_name->setText(m_source);
            accept();
        });
        row->addWidget(open);
        row->addWidget(import);
        row->addStretch();
        cardBox->addSpacing(6);
        cardBox->addLayout(row);
        box->addWidget(card);
    }
    box->addWidget(label(tr("Have another database (Caissabase, MillionBase, your own)? "
                            "Import it under Local."),
                         "emptyHint"));
    box->addStretch();
    return page;
}

QWidget* AddDatabaseDialog::buildLocal() {
    auto* page = new QWidget;
    auto* box = new QVBoxLayout(page);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(8);
    box->addWidget(label(tr("A database you already have: a PGN file of any size, or PGN "
                            "packed in .7z, .zip, .gz or .bz2. Your file stays as it is."),
                         "pvLine"));
    auto* row = new QHBoxLayout;
    auto* choose = new QPushButton(tr("Choose File…"));
    choose->setObjectName(QStringLiteral("addDatabaseChoose"));
    choose->setProperty("role", "secondary");
    connect(choose, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Import a Database"),
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation), tr(kFileFilter));
        if (!path.isEmpty()) {
            chooseFile(path);
        }
    });
    m_file = label(tr("No file chosen"), "emptyHint");
    row->addWidget(choose);
    row->addWidget(m_file, 1);
    box->addLayout(row);
    box->addSpacing(6);
    auto* nameTitle = label(tr("NAME"), "cardTitle");
    box->addWidget(nameTitle);
    m_name = new QLineEdit;
    m_name->setObjectName(QStringLiteral("addDatabaseName"));
    m_name->setPlaceholderText(tr("Caissabase, Club games, …"));
    box->addWidget(m_name);
    auto* importRow = new QHBoxLayout;
    importRow->addStretch();
    m_import = new QPushButton(tr("Import"));
    m_import->setObjectName(QStringLiteral("addDatabaseImport"));
    m_import->setProperty("role", "primary");
    m_import->setEnabled(false);
    connect(m_import, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_name, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_import->setEnabled(!m_path.isEmpty() && !text.trimmed().isEmpty());
    });
    importRow->addWidget(m_import);
    box->addSpacing(6);
    box->addLayout(importRow);
    box->addStretch();
    return page;
}

void AddDatabaseDialog::chooseFile(const QString& path) {
    m_path = path;
    m_source.clear();
    const QFileInfo info(path);
    m_file->setText(QStringLiteral("%1  ·  %2")
                        .arg(info.fileName(), QLocale().formattedDataSize(
                                                  info.size(), 1, QLocale::DataSizeSIFormat)));
    QString name = info.fileName();
    for (const char* ending : {".7z", ".zip", ".gz", ".bz2", ".pgn"}) {
        if (name.endsWith(QLatin1String(ending), Qt::CaseInsensitive)) {
            name.chop(static_cast<int>(std::strlen(ending)));
        }
    }
    m_name->setText(name);
    m_import->setEnabled(!name.trimmed().isEmpty());
}

QString AddDatabaseDialog::name() const {
    return m_name->text().trimmed();
}

} // namespace kiebitz::app
