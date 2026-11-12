#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>
#include <optional>

class QNetworkAccessManager;
class QTcpServer;
class QTimer;

namespace kiebitz::app {

/// Signing in to Lichess, as desktop apps do it (OAuth with PKCE): your browser opens Lichess's
/// sign-in page, you allow Kiebitz there, and Lichess sends the browser back to a one-off address
/// on this Mac (127.0.0.1) that this class listens on for the answer. The token it gets is kept
/// in the Keychain; the username in the settings. Signed in, Kiebitz can use Lichess's opening
/// explorer (which asks for a login) and download games faster.
class LichessLogin : public QObject {
    Q_OBJECT

public:
    /// Where the token and username are kept. Empty functions: in memory only (tests).
    struct Storage {
        std::function<std::optional<QByteArray>()> loadToken;
        std::function<void(const QByteArray&)> saveToken;
        std::function<void()> removeToken;
        std::function<QString()> loadUsername;
        std::function<void(const QString&)> saveUsername;
    };
    /// In the Keychain and the settings.
    [[nodiscard]] static Storage keychain();

    explicit LichessLogin(Storage storage, QObject* parent = nullptr);

    /// For tests: the Lichess server, and what opens the sign-in page (a browser by default).
    void setServer(const QUrl& lichess);
    void setOpenUrl(std::function<void(const QUrl&)> open);

    [[nodiscard]] bool signedIn() const { return !m_username.isEmpty(); }
    [[nodiscard]] QString username() const { return m_username; }
    /// The token (read from the Keychain the first time), or empty.
    [[nodiscard]] QByteArray token();
    /// Waiting for the browser.
    [[nodiscard]] bool busy() const { return m_listener != nullptr; }

    void signIn();
    void cancel();
    void signOut();
    /// Lichess no longer takes the token (it said 401): signed out, with a message.
    void tokenRejected();

signals:
    void changed();
    void failed(const QString& message);

private:
    void answered(const QString& code, const QString& state, const QString& error);
    void finish(const QByteArray& token);
    void stopListening();

    Storage m_storage;
    QUrl m_lichess{QStringLiteral("https://lichess.org")};
    std::function<void(const QUrl&)> m_open;
    QNetworkAccessManager* m_network = nullptr;
    QString m_username;
    std::optional<QByteArray> m_token; ///< read lazily: nothing slow at startup
    QTcpServer* m_listener = nullptr;
    QTimer* m_giveUp = nullptr;
    QByteArray m_verifier; ///< PKCE: the secret whose hash went with the sign-in page
    QString m_state;       ///< matches the answer to this sign-in
    QString m_redirect;
};

} // namespace kiebitz::app
