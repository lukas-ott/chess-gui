#include "LichessPuzzles.hpp"

#include "kiebitz/puzzles/store.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <cstdio>

namespace kiebitz::app {
namespace {

QString megabytes(qint64 bytes) {
    return QLocale().toString(static_cast<double>(bytes) / 1e6, 'f', 0) + QStringLiteral(" MB");
}

} // namespace

LichessPuzzles::LichessPuzzles(QString folder, QObject* parent)
    : QObject(parent), m_folder(std::move(folder)) {
    if (m_folder.isEmpty()) {
        QString base = qEnvironmentVariable("KIEBITZ_SETTINGS_DIR"); // tests: never the real one
        if (base.isEmpty()) {
            base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        }
        m_folder = QDir(base).filePath(QStringLiteral("Puzzles"));
    }
    if (QFileInfo::exists(file())) {
        m_state = State::Ready;
    }
    m_poll = new QTimer(this);
    m_poll->setInterval(200);
    connect(m_poll, &QTimer::timeout, this, [this] {
        if (m_read && m_importBytes > 0) {
            m_fraction = std::min(1.0, static_cast<double>(m_read->load()) /
                                           static_cast<double>(m_importBytes));
            m_status = tr("Preparing the puzzles… %1%").arg(static_cast<int>(m_fraction * 100));
            emit progressChanged();
        }
    });
}

LichessPuzzles::~LichessPuzzles() {
    if (m_cancel) {
        m_cancel->store(true);
    }
    if (m_reply != nullptr) {
        m_reply->abort();
    }
}

QString LichessPuzzles::file() const {
    return QDir(m_folder).filePath(QStringLiteral("lichess.puzzles"));
}

std::shared_ptr<const puzzles::PuzzleDb> LichessPuzzles::db() {
    if (!m_db && m_state == State::Ready) {
        std::string error;
        m_db = puzzles::PuzzleDb::open(file().toStdString(), &error);
        if (!m_db) {
            setState(State::Missing); // damaged: download it again
        }
    }
    return m_db;
}

void LichessPuzzles::setState(State state) {
    m_state = state;
    emit stateChanged();
}

void LichessPuzzles::fail(const QString& message) {
    m_poll->stop();
    m_fraction = 0;
    m_status.clear();
    setState(QFileInfo::exists(file()) ? State::Ready : State::Missing);
    emit failed(message);
}

void LichessPuzzles::download() {
    if (m_state == State::Downloading || m_state == State::Importing) {
        return;
    }
    if (!QDir().mkpath(m_folder)) {
        fail(tr("Couldn't create the puzzles folder."));
        return;
    }
    const QString target = QDir(m_folder).filePath(QStringLiteral("lichess_db_puzzle.csv.zst"));
    m_download = std::make_unique<QFile>(target + QStringLiteral(".part"));
    if (!m_download->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(tr("Couldn't save the download."));
        return;
    }
    if (m_network == nullptr) {
        m_network = new QNetworkAccessManager(this);
    }
    m_fraction = 0;
    m_status = tr("Downloading…");
    setState(State::Downloading);
    QNetworkRequest request{QUrl(QString::fromLatin1(kUrl))};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Kiebitz"));
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        const QByteArray chunk = m_reply->readAll();
        if (m_download->write(chunk) != chunk.size()) {
            m_reply->abort();
            fail(tr("Couldn't save the download (is the disk full?)."));
        }
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        const qint64 size = total > 0 ? total : kDownloadBytes;
        m_fraction = std::min(1.0, static_cast<double>(received) / static_cast<double>(size));
        m_status = tr("Downloading… %1 of %2").arg(megabytes(received), megabytes(size));
        emit progressChanged();
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, target] {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();
        if (m_state != State::Downloading) {
            return; // cancelled, or failed meanwhile
        }
        if (reply->error() != QNetworkReply::NoError) {
            m_download->close();
            m_download->remove();
            fail(tr("The download didn't finish. Check the connection and try again."));
            return;
        }
        m_download->write(reply->readAll());
        m_download->close();
        QFile::remove(target);
        if (!m_download->rename(target)) {
            m_download->remove();
            fail(tr("Couldn't save the download."));
            return;
        }
        m_download = nullptr;
        importFile(target, true);
    });
}

void LichessPuzzles::importFile(const QString& path, bool removeAfter) {
    if (!QDir().mkpath(m_folder)) {
        fail(tr("Couldn't create the puzzles folder."));
        return;
    }
    m_db.reset(); // the file is replaced: never keep a mapping of the old one
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    m_read = std::make_shared<std::atomic<std::uint64_t>>(0);
    m_importBytes = std::max<qint64>(1, QFileInfo(path).size());
    m_fraction = 0;
    m_status = tr("Preparing the puzzles…");
    setState(State::Importing);
    m_poll->start();
    using Result = puzzles::PuzzleImportResult;
    // Owned by this object and deleted when done (deleteLater); the analyzer can't see that.
    auto* watcher =
        new QFutureWatcher<Result>(this); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, path, removeAfter] {
        watcher->deleteLater();
        m_poll->stop();
        const Result result = watcher->result();
        if (removeAfter) {
            QFile::remove(path);
        }
        if (result.cancelled) {
            m_fraction = 0;
            m_status.clear();
            setState(QFileInfo::exists(file()) ? State::Ready : State::Missing);
            return;
        }
        if (!result.error.empty()) {
            fail(QString::fromStdString(result.error));
            return;
        }
        m_fraction = 1;
        m_status.clear();
        setState(State::Ready);
    });
    watcher->setFuture(QtConcurrent::run([path, target = file(), cancel = m_cancel, read = m_read] {
        std::FILE* in = std::fopen(QFile::encodeName(path).constData(), "rb");
        if (in == nullptr) {
            Result result;
            result.error = "Couldn't read the download.";
            return result;
        }
        puzzles::PuzzleImportOptions options;
        options.cancel = cancel.get();
        Result result = puzzles::importLichess(
            [&](char* buffer, std::size_t size) {
                const std::size_t n = std::fread(buffer, 1, size, in);
                read->fetch_add(n);
                return n;
            },
            QFile::encodeName(target).toStdString(), options);
        std::fclose(in);
        return result;
    }));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

void LichessPuzzles::cancel() {
    if (m_state == State::Downloading) {
        setState(QFileInfo::exists(file()) ? State::Ready : State::Missing);
        if (m_reply != nullptr) {
            m_reply->abort();
        }
        if (m_download) {
            m_download->close();
            m_download->remove();
            m_download = nullptr;
        }
        m_fraction = 0;
        m_status.clear();
        emit progressChanged();
    } else if (m_state == State::Importing && m_cancel) {
        m_cancel->store(true); // the import stops and says so
    }
}

void LichessPuzzles::remove() {
    if (m_state != State::Ready) {
        return;
    }
    m_db.reset();
    QFile::remove(file());
    setState(State::Missing);
}

} // namespace kiebitz::app
