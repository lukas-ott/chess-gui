#include "LichessLogin.hpp"

#include "Keychain.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

namespace kiebitz::app {
namespace {

constexpr auto kClientId = "kiebitz";
constexpr auto kService = "Kiebitz Lichess";
constexpr auto kUsernameKey = "lichess/username";
constexpr int kWaitMs = 5 * 60 * 1000; ///< for the browser's answer

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

/// The page the browser shows once Lichess has sent it back here.
QByteArray page(bool ok) {
    const QByteArray text =
        ok ? "Kiebitz is signed in to Lichess. You can close this tab."
           : "Kiebitz couldn't sign in to Lichess. You can close this tab and try again.";
    const QByteArray body = "<!doctype html><meta charset=utf-8><title>Kiebitz</title>"
                            "<body style=\"font:16px -apple-system,sans-serif;margin:60px\">" +
                            text + "</body>";
    return "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
           QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}

} // namespace

LichessLogin::Storage LichessLogin::keychain() {
    const QString service = QString::fromLatin1(kService);
    return {
        [service] { return keychain::load(service, QStringLiteral("token")); },
        [service](const QByteArray& token) {
            keychain::save(service, QStringLiteral("token"), token);
        },
        [service] { keychain::remove(service, QStringLiteral("token")); },
        [] { return QSettings().value(QString::fromLatin1(kUsernameKey)).toString(); },
        [](const QString& name) { QSettings().setValue(QString::fromLatin1(kUsernameKey), name); },
    };
}

LichessLogin::LichessLogin(Storage storage, QObject* parent)
    : QObject(parent), m_storage(std::move(storage)),
      m_open([](const QUrl& url) { QDesktopServices::openUrl(url); }),
      m_network(new QNetworkAccessManager(this)) {
    if (m_storage.loadUsername) {
        m_username = m_storage.loadUsername();
    }
}

void LichessLogin::setServer(const QUrl& lichess) {
    m_lichess = lichess;
}

void LichessLogin::setOpenUrl(std::function<void(const QUrl&)> open) {
    m_open = std::move(open);
}

QByteArray LichessLogin::token() {
    if (!signedIn()) {
        return {};
    }
    if (!m_token) {
        m_token = m_storage.loadToken ? m_storage.loadToken().value_or(QByteArray{}) : QByteArray{};
    }
    return *m_token;
}

void LichessLogin::signIn() {
    cancel();
    m_listener = new QTcpServer(this);
    if (!m_listener->listen(QHostAddress::LocalHost)) {
        stopListening();
        emit failed(tr("Couldn't wait for Lichess's answer on this Mac."));
        return;
    }
    m_verifier = randomText(48);
    m_state = QString::fromLatin1(randomText(16));
    m_redirect = QStringLiteral("http://127.0.0.1:%1/").arg(m_listener->serverPort());
    connect(m_listener, &QTcpServer::newConnection, this, [this] {
        QTcpSocket* socket = m_listener->nextPendingConnection();
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            // "GET /?code=...&state=... HTTP/1.1": the first line is all that's needed.
            const QByteArray request = socket->readAll();
            const QUrl target(QString::fromLatin1(request.split(' ').value(1)));
            const QUrlQuery query(target);
            const QString code = query.queryItemValue(QStringLiteral("code"));
            const bool ok = !code.isEmpty();
            socket->write(page(ok));
            socket->disconnectFromHost();
            if (target.path() == u"/") { // not the browser asking for a favicon
                answered(code, query.queryItemValue(QStringLiteral("state")),
                         query.queryItemValue(QStringLiteral("error")));
            }
        });
    });
    m_giveUp = new QTimer(this);
    m_giveUp->setSingleShot(true);
    connect(m_giveUp, &QTimer::timeout, this, [this] {
        stopListening();
        emit changed();
    });
    m_giveUp->start(kWaitMs);

    const QByteArray challenge =
        QCryptographicHash::hash(m_verifier, QCryptographicHash::Sha256)
            .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    QUrl url = m_lichess.resolved(QUrl(QStringLiteral("/oauth")));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("client_id"), QString::fromLatin1(kClientId));
    query.addQueryItem(QStringLiteral("redirect_uri"), m_redirect);
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("code_challenge"), QString::fromLatin1(challenge));
    query.addQueryItem(QStringLiteral("state"), m_state);
    url.setQuery(query);
    emit changed(); // busy
    m_open(url);
}

void LichessLogin::cancel() {
    if (busy()) {
        stopListening();
        emit changed();
    }
}

void LichessLogin::stopListening() {
    if (m_listener != nullptr) {
        m_listener->close();
        m_listener->deleteLater();
        m_listener = nullptr;
    }
    if (m_giveUp != nullptr) {
        m_giveUp->deleteLater();
        m_giveUp = nullptr;
    }
}

void LichessLogin::answered(const QString& code, const QString& state, const QString& error) {
    if (!busy()) {
        return;
    }
    stopListening();
    if (code.isEmpty() || state != m_state) {
        emit changed();
        if (error != u"access_denied") { // you said no: nothing to report
            emit failed(tr("Lichess didn't sign Kiebitz in. Try again?"));
        }
        return;
    }
    // The code and the verifier for a token.
    QNetworkRequest request(m_lichess.resolved(QUrl(QStringLiteral("/api/token"))));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    form.addQueryItem(QStringLiteral("code"), code);
    form.addQueryItem(QStringLiteral("code_verifier"), QString::fromLatin1(m_verifier));
    form.addQueryItem(QStringLiteral("redirect_uri"), m_redirect);
    form.addQueryItem(QStringLiteral("client_id"), QString::fromLatin1(kClientId));
    QNetworkReply* reply = m_network->post(request, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QByteArray token = QJsonDocument::fromJson(reply->readAll())
                                     .object()
                                     .value(QStringLiteral("access_token"))
                                     .toString()
                                     .toUtf8();
        if (reply->error() != QNetworkReply::NoError || token.isEmpty()) {
            emit changed();
            emit failed(tr("Lichess didn't sign Kiebitz in. Try again?"));
            return;
        }
        finish(token);
    });
}

void LichessLogin::finish(const QByteArray& token) {
    // Who it is: the account the token belongs to.
    QNetworkRequest request(m_lichess.resolved(QUrl(QStringLiteral("/api/account"))));
    request.setRawHeader("Authorization", "Bearer " + token);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, token] {
        reply->deleteLater();
        const QString name = QJsonDocument::fromJson(reply->readAll())
                                 .object()
                                 .value(QStringLiteral("username"))
                                 .toString();
        if (reply->error() != QNetworkReply::NoError || name.isEmpty()) {
            emit changed();
            emit failed(tr("Signed in, but Lichess didn't say who you are. Try again?"));
            return;
        }
        m_token = token;
        m_username = name;
        if (m_storage.saveToken) {
            m_storage.saveToken(token);
            m_storage.saveUsername(name);
        }
        emit changed();
    });
}

void LichessLogin::signOut() {
    const QByteArray old = token();
    if (!old.isEmpty()) { // Lichess forgets the token too
        QNetworkRequest request(m_lichess.resolved(QUrl(QStringLiteral("/api/token"))));
        request.setRawHeader("Authorization", "Bearer " + old);
        request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
        const QNetworkReply* reply = m_network->deleteResource(request);
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    }
    m_token = QByteArray{};
    m_username.clear();
    if (m_storage.removeToken) {
        m_storage.removeToken();
        m_storage.saveUsername({});
    }
    emit changed();
}

void LichessLogin::tokenRejected() {
    if (!signedIn()) {
        return;
    }
    m_token = QByteArray{};
    m_username.clear();
    if (m_storage.removeToken) {
        m_storage.removeToken();
        m_storage.saveUsername({});
    }
    emit changed();
    emit failed(tr("Lichess signed Kiebitz out. Sign in again to use its explorer."));
}

} // namespace kiebitz::app
