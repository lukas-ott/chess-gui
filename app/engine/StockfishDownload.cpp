#include "StockfishDownload.hpp"

#include "chessgui/engine/release.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>

namespace chessgui::app {
namespace {

constexpr auto kLatestRelease =
    "https://api.github.com/repos/official-stockfish/Stockfish/releases/latest";

QNetworkRequest makeRequest(const QUrl& url) {
    QNetworkRequest request(url);
    // A plain client name: no personal details go to GitHub.
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("ChessGui/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(30000); // without any data for this long, give up
    return request;
}

/// Mach-O executables start with one of these (64-bit, or a universal "fat" file).
bool isMachO(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray magic = file.read(4);
    return magic == QByteArray("\xCF\xFA\xED\xFE", 4) || magic == QByteArray("\xCA\xFE\xBA\xBE", 4);
}

} // namespace

StockfishDownload::StockfishDownload(QString folder, QObject* parent)
    : QObject(parent), m_folder(std::move(folder)), m_network(new QNetworkAccessManager(this)) {}

StockfishDownload::~StockfishDownload() {
    cancel();
}

void StockfishDownload::start() {
    if (m_running) {
        return;
    }
    m_running = true;
    fetchRelease();
}

void StockfishDownload::cancel() {
    if (!m_running) {
        return;
    }
    m_running = false;
    if (m_reply != nullptr) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_tar != nullptr) {
        m_tar->disconnect(this);
        m_tar->kill();
        m_tar->waitForFinished(1000);
        m_tar->deleteLater();
        m_tar = nullptr;
    }
    cleanUp();
}

void StockfishDownload::fetchRelease() {
    QNetworkRequest request = makeRequest(QUrl(QString::fromLatin1(kLatestRelease)));
    request.setRawHeader("Accept", "application/vnd.github+json");
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            fail(tr("Couldn't reach GitHub to find the latest Stockfish."));
            return;
        }
        const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
        std::vector<std::string> names;
        const QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
        for (const auto& asset : assets) {
            names.push_back(
                asset.toObject().value(QStringLiteral("name")).toString().toStdString());
        }
        const auto pick = engine::pickMacDownload(names, engine::thisCpu());
        if (!pick) {
            fail(tr("The latest Stockfish release has no download for this Mac."));
            return;
        }
        const QJsonObject asset = assets.at(static_cast<qsizetype>(*pick)).toObject();
        m_version = release.value(QStringLiteral("tag_name")).toString();
        m_name = release.value(QStringLiteral("name")).toString();
        if (m_name.isEmpty()) {
            m_name = QStringLiteral("Stockfish");
        }
        const auto sha = engine::sha256FromDigest(
            asset.value(QStringLiteral("digest")).toString().toStdString());
        const auto size = asset.value(QStringLiteral("size")).toInteger();
        emit releaseFound(m_name, size);
        download(asset.value(QStringLiteral("browser_download_url")).toString(), size,
                 sha ? QString::fromStdString(*sha) : QString());
    });
}

void StockfishDownload::download(const QString& url, qint64 size, const QString& sha256) {
    if (!QDir().mkpath(m_folder)) {
        fail(tr("Couldn't create the engines folder."));
        return;
    }
    const QString name = QUrl(url).fileName();
    m_file = std::make_unique<QFile>(QDir(m_folder).filePath(name + QStringLiteral(".part")));
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(tr("Couldn't save the download."));
        return;
    }
    m_hash.reset();
    m_reply = m_network->get(makeRequest(QUrl(url))); // GitHub redirects to its file host
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        const QByteArray chunk = m_reply->readAll();
        m_hash.addData(chunk);
        if (m_file->write(chunk) != chunk.size()) {
            fail(tr("Couldn't save the download (is the disk full?)."));
        }
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this,
            [this, size](qint64 received, qint64 total) {
                emit progress(received, total > 0 ? total : size);
            });
    connect(m_reply, &QNetworkReply::finished, this, [this, sha256] {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            fail(tr("The download didn't finish. Check the connection and try again."));
            return;
        }
        const QByteArray rest = reply->readAll();
        m_hash.addData(rest);
        m_file->write(rest);
        m_file->close();
        // GitHub publishes each file's SHA-256: a download that doesn't match is not used.
        if (!sha256.isEmpty() && QString::fromLatin1(m_hash.result().toHex()) != sha256) {
            fail(tr("The download was damaged (its checksum doesn't match). Try again."));
            return;
        }
        // tar decides the format by the name: drop ".part".
        QString archive = m_file->fileName();
        archive.chop(5);
        QFile::remove(archive);
        if (!QFile::rename(m_file->fileName(), archive)) {
            fail(tr("Couldn't save the download."));
            return;
        }
        m_file = nullptr;
        install(archive, m_version);
    });
}

void StockfishDownload::install(const QString& archive, const QString& version) {
    m_running = true;
    if (m_name.isEmpty()) {
        m_name = QStringLiteral("Stockfish");
    }
    const QString target =
        QDir(m_folder).filePath(version.isEmpty() ? QStringLiteral("stockfish") : version);
    QDir(target).removeRecursively(); // an older copy of the same release
    if (!QDir().mkpath(target)) {
        fail(tr("Couldn't create the engines folder."));
        return;
    }
    m_tar = new QProcess(this);
    connect(m_tar, &QProcess::finished, this,
            [this, archive, target](int code, QProcess::ExitStatus status) {
                m_tar->deleteLater();
                m_tar = nullptr;
                QFile::remove(archive);
                if (status != QProcess::NormalExit || code != 0) {
                    fail(tr("Couldn't unpack the download."));
                    return;
                }
                const QString engine = findEngine(target);
                if (engine.isEmpty()) {
                    fail(tr("The download has no engine for this Mac in it."));
                    return;
                }
                QFile::setPermissions(engine, QFile::permissions(engine) | QFile::ExeOwner |
                                                  QFile::ExeGroup | QFile::ExeOther);
                m_running = false;
                emit finished(m_name, engine);
            });
    connect(m_tar, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            fail(tr("Couldn't unpack the download."));
        }
    });
    // macOS's tar unpacks .tar, .tar.gz and .zip alike.
    m_tar->start(QStringLiteral("/usr/bin/tar"),
                 {QStringLiteral("-xf"), archive, QStringLiteral("-C"), target});
}

QString StockfishDownload::findEngine(const QString& folder) {
    QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
    QString fallback;
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        if (!info.fileName().startsWith(QStringLiteral("stockfish"), Qt::CaseInsensitive) ||
            !isMachO(info.absoluteFilePath())) {
            continue;
        }
        // Prefer the plain name over e.g. a "stockfish-…-debug" next to it.
        if (fallback.isEmpty() || info.fileName().size() < QFileInfo(fallback).fileName().size()) {
            fallback = info.absoluteFilePath();
        }
    }
    return fallback;
}

void StockfishDownload::fail(const QString& message) {
    if (!m_running) {
        return;
    }
    if (m_reply != nullptr) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_running = false;
    cleanUp();
    emit failed(message);
}

void StockfishDownload::cleanUp() {
    if (m_file) {
        m_file->close();
        m_file->remove(); // a half-finished download is useless
        m_file = nullptr;
    }
}

} // namespace chessgui::app
