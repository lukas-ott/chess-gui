#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <map>
#include <optional>

class QNetworkAccessManager;
class QTcpServer;
class QTcpSocket;
class QTimer;

namespace kiebitz::app {

/// Signing in to Lichess, as desktop apps do it (OAuth with PKCE): your browser opens Lichess's
/// sign-in page, you allow Kiebitz there, and Lichess sends the browser back to a one-off address
/// on this Mac (127.0.0.1) that this class listens on for the answer. Several accounts can be
/// signed in, one after the other; their tokens are kept in a file only you can read. Signed in,
/// Kiebitz can use Lichess's opening explorer (which asks for a login) and download that account's
/// games faster.
class LichessLogin : public QObject {
    Q_OBJECT

public:
    /// Where the tokens and usernames are kept. Empty functions: in memory only (tests).
    struct Storage {
        std::function<std::optional<QByteArray>(const QString& username)> loadToken;
        std::function<void(const QString& username, const QByteArray& token)> saveToken;
        std::function<void(const QString& username)> removeToken;
        std::function<QStringList()> loadUsernames;
        std::function<void(const QStringList& usernames)> saveUsernames;
    };
    /// In a file only you can read (permissions 600). Not the Keychain: macOS would ask for
    /// your password after every new build of an app without a fixed developer certificate.
    [[nodiscard]] static Storage privateFile(const QString& path = defaultFile());
    /// Kiebitz's Application Support folder, lichess-logins.json.
    [[nodiscard]] static QString defaultFile();

    explicit LichessLogin(Storage storage, QObject* parent = nullptr);

    /// For tests: the Lichess server, and what opens the sign-in page (a browser by default).
    void setServer(const QUrl& lichess);
    void setOpenUrl(std::function<void(const QUrl&)> open);

    /// Any account signed in; this one (any case).
    [[nodiscard]] bool signedIn() const { return !m_usernames.isEmpty(); }
    [[nodiscard]] bool signedIn(const QString& username) const;
    /// The accounts signed in, as Lichess writes their names.
    [[nodiscard]] QStringList usernames() const { return m_usernames; }
    /// An account's token (read from its file the first time), or empty; without a name,
    /// the first account's (for the explorer: any login will do).
    [[nodiscard]] QByteArray token(const QString& username = {});
    /// Waiting for the browser.
    [[nodiscard]] bool busy() const { return !m_pending.empty(); }

    /// Signs in one more account. `username`: the one wanted (Lichess then asks for that
    /// account's login if the browser is signed in as someone else); empty: whoever is there.
    void signIn(const QString& username = {});
    void cancel();
    void signOut(const QString& username);
    /// Lichess no longer takes an account's token (it said 401): signed out, with a message.
    /// Without a name: the account token() gave.
    void tokenRejected(const QString& username = {});

signals:
    void changed();
    void failed(const QString& message);
    /// A sign-in just finished, for this account (the browser had the focus); `wanted`: the
    /// account it was for, or empty.
    void signedInNow(const QString& username, const QString& wanted);

private:
    /// A sign-in waiting for the browser to come back (by its OAuth state).
    struct Pending {
        QByteArray verifier; ///< PKCE: the secret whose hash went with the sign-in page
        QString wanted;      ///< the account asked for, or empty
        QDateTime started;
    };
    [[nodiscard]] bool listen();
    void request(QTcpSocket* socket);
    void exchange(QTcpSocket* socket, const QString& code, const Pending& pending);
    void finish(QTcpSocket* socket, const QByteArray& token, const QString& wanted);
    /// The browser's answer: on to `url` (on Lichess), or a short page when there is none.
    static void reply(QTcpSocket* socket, const QUrl& url, const QString& problem = {});
    void forget(const QString& username);
    [[nodiscard]] QString known(const QString& username) const; ///< as stored, or empty
    [[nodiscard]] QString redirectUri() const;

    Storage m_storage;
    QUrl m_lichess{QStringLiteral("https://lichess.org")};
    std::function<void(const QUrl&)> m_open;
    QNetworkAccessManager* m_network = nullptr;
    QStringList m_usernames;
    std::map<QString, QByteArray> m_tokens; ///< by username in lower case; read lazily
    /// Where the browser comes back: one address for the whole session, so an older Lichess
    /// tab or a reload still gets an answer. Closed once nothing has waited for a while.
    QTcpServer* m_listener = nullptr;
    QTimer* m_idle = nullptr;
    std::map<QString, Pending> m_pending;
};

} // namespace kiebitz::app
