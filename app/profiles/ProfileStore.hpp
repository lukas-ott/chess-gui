#pragma once

#include "Fide.hpp"
#include "kiebitz/gamedb/database.hpp"

#include <QDateTime>
#include <QFuture>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class QNetworkAccessManager;

namespace kiebitz::app {

/// Where an account's games come from.
enum class AccountSite : std::uint8_t { Lichess, ChessCom };

/// An online account of a profile: its games are downloaded by username (public, no login).
struct Account {
    QString id; ///< "lichess-name", "chesscom-name": also its folder
    AccountSite site = AccountSite::Lichess;
    QString username;
    QDateTime synced;           ///< the last download (invalid: never)
    qint64 lichessSince = 0;    ///< Lichess: games after this time (ms) are new
    QStringList chessComMonths; ///< chess.com: months downloaded for good ("2026/10")
    QString title;              ///< on the site ("FM"), or empty
    /// The current ratings by time control ("bullet", "blitz", "rapid", "classical",
    /// "correspondence"; chess.com: "daily"), as the site has them.
    std::map<QString, int> ratings;
    std::map<QString, int> ratingChanges; ///< how each moved at the last sync that changed it
    int siteGames = 0;                    ///< games on the site (the downloads may be fewer)
};

/// A person whose games Kiebitz keeps: you (the main profile), or a friend or opponent. All of a
/// profile's games, from its accounts and the over-the-board games added to it, are one
/// database; each account (and the over-the-board games) is also a database of its own, so it
/// can be looked at, and explored, alone.
struct Profile {
    QString id;
    QString name;
    bool main = false;
    std::vector<Account> accounts;
    /// The FIDE player with this name: the closest match, or the one chosen. Looked up when the
    /// profile is made or renamed (names of two words or more).
    std::optional<FidePlayer> fide;
    std::vector<FidePlayer> fideCandidates; ///< everyone who fits the name, best first
    bool fideChosen = false;  ///< picked by hand (or "none of these"): not replaced by a lookup
    bool fideLooked = false;  ///< a lookup has been done for this name
    bool fideLooking = false; ///< a lookup is under way
    QString fideError;        ///< why the last lookup failed (FIDE not answering), or empty
    std::shared_ptr<const gamedb::Database> all; ///< null until built (or no games)
    /// By part: an account's id, or "otb".
    std::map<QString, std::shared_ptr<const gamedb::Database>> parts;
    /// The profile's results by part ("" = all its games), counted from its games.
    std::map<QString, gamedb::Record> records;
};

/// The profiles, kept in "Profiles" under the app's data folder:
///   profiles.json                 who they are and their accounts;
///   <profile>/<account>/*.pgn     the games as downloaded (or added: <profile>/otb/);
///   <profile>/db/<part>/          the databases built from them (part: an account, "otb",
///                                 or "all").
/// Downloads and building run in the background; changed() and progress() tell the page.
class ProfileStore : public QObject {
    Q_OBJECT

public:
    /// `root`: where profiles live (empty: the default).
    explicit ProfileStore(const QString& root = {}, QObject* parent = nullptr);
    ~ProfileStore() override;
    ProfileStore(const ProfileStore&) = delete;
    ProfileStore& operator=(const ProfileStore&) = delete;
    ProfileStore(ProfileStore&&) = delete;
    ProfileStore& operator=(ProfileStore&&) = delete;

    [[nodiscard]] static QString defaultRoot();
    [[nodiscard]] const std::vector<Profile>& profiles() const { return m_profiles; }
    [[nodiscard]] const Profile* profile(const QString& id) const;

    /// A new profile; returns its id.
    QString addProfile(const QString& name);
    void renameProfile(const QString& id, const QString& name);
    /// Removes a profile and its games (not the main one).
    void removeProfile(const QString& id);

    /// Adds an account and downloads its games. Returns its id, or empty if it is there already.
    QString addAccount(const QString& profileId, AccountSite site, const QString& username);
    void removeAccount(const QString& profileId, const QString& accountId);
    /// Downloads the new games of all of a profile's accounts, then rebuilds its databases.
    void sync(const QString& profileId);
    /// Adds over-the-board games (PGN files) to a profile.
    void addOtbGames(const QString& profileId, const QStringList& files);

    /// Who the profile is in a part's games ("" = all): usernames, and its names over the board.
    [[nodiscard]] static QStringList namesOf(const Profile& profile, const QString& part);

    /// Looks up the profile's FIDE player again (in the background; changed() follows).
    void lookUpFide(const QString& profileId);
    /// Picks one of the candidates as the profile's FIDE player (0: none of them).
    void chooseFide(const QString& profileId, int fideId);

    /// What is going on with a profile ("Downloading CatalanSimp's games…"), or empty.
    [[nodiscard]] QString status(const QString& profileId) const;
    [[nodiscard]] bool busy(const QString& profileId) const;

    /// The servers (tests point them at a fake one).
    void setServers(const QUrl& lichess, const QUrl& chessCom, const QUrl& fide = {});
    /// The Lichess login's token (empty: not signed in). Downloads with it go faster.
    void setLichessToken(std::function<QByteArray()> token) { m_lichessToken = std::move(token); }
    /// Waits for running builds (tests, quitting).
    void wait();

signals:
    void changed();
    void progress(const QString& profileId);
    void failed(const QString& profileId, const QString& message);

private:
    struct Job; ///< a sync in progress

    void load();
    void save() const;
    [[nodiscard]] Profile* find(const QString& id);
    [[nodiscard]] QString folderOf(const QString& profileId) const;
    void setStatus(const QString& profileId, const QString& status);
    void next(const std::shared_ptr<Job>& job);
    void downloadLichess(const std::shared_ptr<Job>& job, Account& account);
    void downloadChessCom(const std::shared_ptr<Job>& job, Account& account);
    void build(const QString& profileId);
    void fetchRatings(const std::shared_ptr<Job>& job, const QString& accountId);
    void computeRecords(const QString& profileId);
    void openDatabases(const QString& profileId);

    QString m_root;
    std::vector<Profile> m_profiles;
    std::map<QString, QString> m_status; ///< profile id → what it is doing
    QUrl m_lichess{QStringLiteral("https://lichess.org")};
    QUrl m_chessCom{QStringLiteral("https://api.chess.com")};
    std::function<QByteArray()> m_lichessToken;
    QUrl m_fide{QStringLiteral("https://ratings.fide.com")};
    QNetworkAccessManager* m_network = nullptr;
    std::vector<QFuture<void>> m_builds;
};

} // namespace kiebitz::app
