#include "LichessLogin.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

#include <memory>

namespace kiebitz::app {
namespace {

constexpr auto kClientId = "kiebitz";
constexpr auto kUsernameKey = "lichess/username";   ///< settings of before: one account
constexpr auto kUsernamesKey = "lichess/usernames"; ///< settings of before: several
constexpr int kWaitMs = 15 * 60 * 1000; ///< for the browser's answer (a login may take a while)

/// Random bytes as base64url without padding (what OAuth wants for the verifier and state).
QByteArray randomText(int bytes) {
    QByteArray data(bytes, Qt::Uninitialized);
    for (char& byte : data) {
        byte = static_cast<char>(QRandomGenerator::system()->bounded(256));
    }
    return data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QByteArray userAgent() {
    // A plain client name: no personal details go to Lichess.
    return QByteArray("Kiebitz/") + QCoreApplication::applicationVersion().toUtf8();
}

} // namespace

QString LichessLogin::defaultFile() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("lichess-logins.json"));
}

LichessLogin::Storage LichessLogin::privateFile(const QString& path) {
    // {"accounts": [{"username": "...", "token": "..."}, ...]}, readable by you only. Kiebitz
    // asks Lichess for no permissions, so a token can read public data and the explorer only.
    const auto read = [path] {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll())
                                                    .object()[QStringLiteral("accounts")]
                                                    .toArray()
                                              : QJsonArray{};
    };
    const auto write = [path](const QJsonArray& accounts) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return;
        }
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        file.write(QJsonDocument(QJsonObject{{QStringLiteral("accounts"), accounts}}).toJson());
        if (file.commit()) {
            QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        }
    };
    const auto same = [](const QJsonValue& account, const QString& username) {
        return account.toObject()[QStringLiteral("username")].toString().compare(
                   username, Qt::CaseInsensitive) == 0;
    };
    return {
        [read, same](const QString& username) -> std::optional<QByteArray> {
            for (const auto& account : read()) {
                if (same(account, username)) {
                    return account.toObject()[QStringLiteral("token")].toString().toUtf8();
                }
            }
            return std::nullopt;
        },
        [read, write, same](const QString& username, const QByteArray& token) {
            QJsonArray accounts;
            for (const auto& account : read()) {
                if (!same(account, username)) {
                    accounts.append(account);
                }
            }
            accounts.append(QJsonObject{{QStringLiteral("username"), username},
                                        {QStringLiteral("token"), QString::fromUtf8(token)}});
            write(accounts);
        },
        [read, write, same](const QString& username) {
            QJsonArray accounts;
            for (const auto& account : read()) {
                if (!same(account, username)) {
                    accounts.append(account);
                }
            }
            write(accounts);
        },
        [read] {
            QStringList names;
            for (const auto& account : read()) {
                names << account.toObject()[QStringLiteral("username")].toString();
            }
            return names;
        },
        [](const QStringList& /*names*/) {
            // The file's order is the accounts' order: nothing more to keep. The settings of
            // before (one account, its token in the Keychain) go: sign in once more.
            QSettings settings;
            settings.remove(QString::fromLatin1(kUsernameKey));
            settings.remove(QString::fromLatin1(kUsernamesKey));
        },
    };
}

LichessLogin::LichessLogin(Storage storage, QObject* parent)
    : QObject(parent), m_storage(std::move(storage)),
      m_open([](const QUrl& url) { QDesktopServices::openUrl(url); }),
      m_network(new QNetworkAccessManager(this)) {
    if (m_storage.loadUsernames) {
        m_usernames = m_storage.loadUsernames();
    }
}

void LichessLogin::setServer(const QUrl& lichess) {
    m_lichess = lichess;
}

void LichessLogin::setOpenUrl(std::function<void(const QUrl&)> open) {
    m_open = std::move(open);
}

QString LichessLogin::known(const QString& username) const {
    for (const QString& name : m_usernames) {
        if (name.compare(username, Qt::CaseInsensitive) == 0) {
            return name;
        }
    }
    return {};
}

bool LichessLogin::signedIn(const QString& username) const {
    return !known(username).isEmpty();
}

QByteArray LichessLogin::token(const QString& username) {
    const QString name = username.isEmpty() ? m_usernames.value(0) : known(username);
    if (name.isEmpty()) {
        return {};
    }
    const QString key = name.toLower();
    if (const auto found = m_tokens.find(key); found != m_tokens.end()) {
        return found->second;
    }
    const QByteArray token =
        m_storage.loadToken ? m_storage.loadToken(name).value_or(QByteArray{}) : QByteArray{};
    m_tokens[key] = token;
    return token;
}

QString LichessLogin::redirectUri() const {
    return QStringLiteral("http://127.0.0.1:%1/").arg(m_listener->serverPort());
}

bool LichessLogin::listen() {
    if (m_listener == nullptr) {
        m_listener = new QTcpServer(this);
        if (!m_listener->listen(QHostAddress::LocalHost)) {
            delete m_listener;
            m_listener = nullptr;
            return false;
        }
        connect(m_listener, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = m_listener->nextPendingConnection()) {
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                const auto buffer = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
                    // The request line and headers; the rest of it doesn't matter.
                    buffer->append(socket->readAll());
                    if (buffer->contains("\r\n\r\n") && !socket->property("answered").toBool()) {
                        socket->setProperty("answered", true);
                        socket->setProperty("request", *buffer);
                        request(socket);
                    }
                });
            }
        });
        // Closed when nothing has waited for a while (and opened again for the next sign-in).
        m_idle = new QTimer(this);
        m_idle->setSingleShot(true);
        m_idle->setInterval(kWaitMs);
        connect(m_idle, &QTimer::timeout, this, [this] {
            m_pending.clear();
            m_listener->close();
            m_listener->deleteLater();
            m_listener = nullptr;
            m_idle->deleteLater();
            m_idle = nullptr;
            emit changed();
        });
    }
    m_idle->start();
    return true;
}

void LichessLogin::signIn(const QString& username) {
    if (!listen()) {
        emit failed(tr("Couldn't wait for Lichess's answer on this Mac."));
        return;
    }
    const QString state = QString::fromLatin1(randomText(16));
    Pending pending{randomText(48), username, QDateTime::currentDateTime()};
    const QByteArray challenge =
        QCryptographicHash::hash(pending.verifier, QCryptographicHash::Sha256)
            .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    m_pending[state] = std::move(pending);
    QUrl url = m_lichess.resolved(QUrl(QStringLiteral("/oauth")));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("client_id"), QString::fromLatin1(kClientId));
    query.addQueryItem(QStringLiteral("redirect_uri"), redirectUri());
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("code_challenge"), QString::fromLatin1(challenge));
    query.addQueryItem(QStringLiteral("state"), state);
    if (!username.isEmpty()) {
        // That account: signed in as someone else, Lichess offers "Log in as <username>".
        query.addQueryItem(QStringLiteral("username"), username);
    }
    url.setQuery(query);
    emit changed(); // busy
    m_open(url);
}

void LichessLogin::cancel() {
    if (busy()) {
        m_pending.clear(); // a late answer gets sent on to Lichess
        emit changed();
    }
}

void LichessLogin::request(QTcpSocket* socket) {
    // "GET /?code=...&state=... HTTP/1.1".
    const QByteArray text = socket->property("request").toByteArray();
    const QUrl target(QString::fromLatin1(text.split(' ').value(1)));
    if (target.path() != u"/") { // the browser asking for a favicon
        socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        socket->disconnectFromHost();
        return;
    }
    const QUrlQuery query(target);
    const QString state = query.queryItemValue(QStringLiteral("state"));
    const QString code = query.queryItemValue(QStringLiteral("code"));
    const auto found = m_pending.find(state);
    if (found == m_pending.end()) {
        // Over already (done, cancelled, a reload): on to Lichess, not an error page.
        reply(socket, m_lichess);
        return;
    }
    const Pending pending = found->second;
    m_pending.erase(found);
    if (code.isEmpty()) {
        emit changed();
        if (query.queryItemValue(QStringLiteral("error")) != u"access_denied") {
            emit failed(tr("Lichess didn't sign Kiebitz in. Try again?"));
        }
        reply(socket, m_lichess); // you said no: back to Lichess
        return;
    }
    exchange(socket, code, pending);
}

void LichessLogin::exchange(QTcpSocket* socket, const QString& code, const Pending& pending) {
    // The code and the verifier for a token; the browser waits meanwhile.
    QNetworkRequest request(m_lichess.resolved(QUrl(QStringLiteral("/api/token"))));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    form.addQueryItem(QStringLiteral("code"), code);
    form.addQueryItem(QStringLiteral("code_verifier"), QString::fromLatin1(pending.verifier));
    form.addQueryItem(QStringLiteral("redirect_uri"), redirectUri());
    form.addQueryItem(QStringLiteral("client_id"), QString::fromLatin1(kClientId));
    QNetworkReply* reply = m_network->post(request, form.toString(QUrl::FullyEncoded).toUtf8());
    const QPointer<QTcpSocket> browser(socket);
    connect(reply, &QNetworkReply::finished, this, [this, reply, browser, wanted = pending.wanted] {
        reply->deleteLater();
        const QByteArray token = QJsonDocument::fromJson(reply->readAll())
                                     .object()
                                     .value(QStringLiteral("access_token"))
                                     .toString()
                                     .toUtf8();
        if (reply->error() != QNetworkReply::NoError || token.isEmpty()) {
            emit changed();
            emit failed(tr("Lichess didn't sign Kiebitz in. Try again?"));
            LichessLogin::reply(browser, {},
                                tr("Lichess didn't sign Kiebitz in. Close this tab and "
                                   "try again from Kiebitz."));
            return;
        }
        finish(browser, token, wanted);
    });
}

void LichessLogin::finish(QTcpSocket* socket, const QByteArray& token, const QString& wanted) {
    // Who it is: the account the token belongs to.
    QNetworkRequest request(m_lichess.resolved(QUrl(QStringLiteral("/api/account"))));
    request.setRawHeader("Authorization", "Bearer " + token);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    QNetworkReply* reply = m_network->get(request);
    const QPointer<QTcpSocket> browser(socket);
    connect(reply, &QNetworkReply::finished, this, [this, reply, token, browser, wanted] {
        reply->deleteLater();
        const QString name = QJsonDocument::fromJson(reply->readAll())
                                 .object()
                                 .value(QStringLiteral("username"))
                                 .toString();
        if (reply->error() != QNetworkReply::NoError || name.isEmpty()) {
            emit changed();
            emit failed(tr("Signed in, but Lichess didn't say who you are. Try again?"));
            LichessLogin::reply(browser, {},
                                tr("Lichess didn't say who you are. Close this tab and try "
                                   "again from Kiebitz."));
            return;
        }
        // A new account, or a new token for one signed in before.
        if (const QString before = known(name); !before.isEmpty()) {
            m_usernames.removeAll(before);
        }
        m_usernames << name;
        m_tokens[name.toLower()] = token;
        if (m_storage.saveToken) {
            m_storage.saveToken(name, token);
            m_storage.saveUsernames(m_usernames);
        }
        // The browser: on to the account's page on Lichess (no page from this Mac to see).
        LichessLogin::reply(browser, m_lichess.resolved(QUrl(QStringLiteral("/@/") + name)));
        emit changed();
        emit signedInNow(name, wanted);
    });
}

void LichessLogin::reply(QTcpSocket* socket, const QUrl& url, const QString& problem) {
    if (socket == nullptr) {
        return; // the browser gave up waiting
    }
    QByteArray answer;
    if (url.isValid() && problem.isEmpty()) {
        answer = "HTTP/1.1 303 See Other\r\nLocation: " + url.toEncoded() +
                 "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    } else {
        // Rare: something went wrong. A short page in the system's look, light or dark.
        const QByteArray body =
            "<!doctype html><html><head><meta charset=utf-8><title>Kiebitz</title><style>"
            ":root{color-scheme:light dark}body{font:15px -apple-system,sans-serif;"
            "display:flex;min-height:90vh;align-items:center;justify-content:center}"
            "</style></head><body><p>" +
            problem.toHtmlEscaped().toUtf8() + "</p></body></html>";
        answer = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                 QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    }
    socket->write(answer);
    socket->disconnectFromHost();
}

void LichessLogin::forget(const QString& username) {
    const QString name = known(username);
    if (name.isEmpty()) {
        return;
    }
    m_usernames.removeAll(name);
    m_tokens.erase(name.toLower());
    if (m_storage.removeToken) {
        m_storage.removeToken(name);
        m_storage.saveUsernames(m_usernames);
    }
}

void LichessLogin::signOut(const QString& username) {
    const QByteArray old = token(username);
    if (!old.isEmpty()) { // Lichess forgets the token too
        QNetworkRequest request(m_lichess.resolved(QUrl(QStringLiteral("/api/token"))));
        request.setRawHeader("Authorization", "Bearer " + old);
        request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
        const QNetworkReply* reply = m_network->deleteResource(request);
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    }
    forget(username);
    emit changed();
}

void LichessLogin::tokenRejected(const QString& username) {
    const QString name = username.isEmpty() ? m_usernames.value(0) : known(username);
    if (name.isEmpty()) {
        return;
    }
    forget(name);
    emit changed();
    emit failed(tr("Lichess signed %1 out of Kiebitz. Sign in again to use it.").arg(name));
}

} // namespace kiebitz::app
