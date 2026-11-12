#include "ProfileStore.hpp"

#include "kiebitz/core/version.hpp"
#include "kiebitz/gamedb/import.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrlQuery>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <deque>
#include <functional>
#include <tuple>

namespace kiebitz::app {

/// A sync in progress: the accounts still to download, then the build.
struct ProfileStore::Job {
    QString profileId;
    std::deque<QString> accounts;
};

namespace {

constexpr auto kOtb = "otb";
constexpr auto kAll = "all";

QString siteName(AccountSite site) {
    return site == AccountSite::Lichess ? QStringLiteral("lichess") : QStringLiteral("chesscom");
}

QByteArray userAgent() {
    // Lichess and chess.com ask API clients to say who they are.
    const std::string_view version = core::version();
    return QByteArray("Kiebitz/") +
           QByteArray(version.data(), static_cast<qsizetype>(version.size())) +
           " (+https://github.com/lukas-ott/kiebitz)";
}

QJsonObject ratingsJson(const std::map<QString, int>& ratings) {
    QJsonObject object;
    for (const auto& [speed, rating] : ratings) {
        object[speed] = rating;
    }
    return object;
}

QJsonObject toJson(const FidePlayer& player) {
    return {{QStringLiteral("id"), player.id},
            {QStringLiteral("name"), player.name},
            {QStringLiteral("title"), player.title},
            {QStringLiteral("federation"), player.federation},
            {QStringLiteral("standard"), player.standard},
            {QStringLiteral("rapid"), player.rapid},
            {QStringLiteral("blitz"), player.blitz},
            {QStringLiteral("born"), player.born}};
}

FidePlayer fideFromJson(const QJsonObject& object) {
    FidePlayer player;
    player.id = object[QStringLiteral("id")].toInt();
    player.name = object[QStringLiteral("name")].toString();
    player.title = object[QStringLiteral("title")].toString();
    player.federation = object[QStringLiteral("federation")].toString();
    player.standard = object[QStringLiteral("standard")].toInt();
    player.rapid = object[QStringLiteral("rapid")].toInt();
    player.blitz = object[QStringLiteral("blitz")].toInt();
    player.born = object[QStringLiteral("born")].toInt();
    return player;
}

/// FIDE is searched for names of two words or more ("Me" or "Magnus" alone would find anyone).
bool worthLookingUp(const QString& name) {
    return name.split(u' ', Qt::SkipEmptyParts).size() >= 2;
}

/// Why a download failed, in words.
QString downloadError(const QString& site, const QString& name, int status, const QString& error) {
    if (status == 404) {
        return ProfileStore::tr("There is no %1 account called %2.").arg(site, name);
    }
    if (status == 429) {
        return ProfileStore::tr("%1 asks to wait a minute before the next download.").arg(site);
    }
    return ProfileStore::tr("%1's %2 games couldn't be downloaded: %3").arg(name, site, error);
}

/// The PGN files of a folder, oldest name first.
QStringList pgnFiles(const QString& folder) {
    QStringList files;
    const auto entries =
        QDir(folder).entryInfoList({QStringLiteral("*.pgn")}, QDir::Files, QDir::Name);
    for (const QFileInfo& entry : entries) {
        files << entry.absoluteFilePath();
    }
    return files;
}

/// Builds a database from PGN files into `folder` (replacing what is there). Empty on
/// success, else the error.
QString buildDatabase(const QStringList& files, const QString& folder, const QString& name) {
    const QString part = folder + QStringLiteral(".part");
    QDir(part).removeRecursively();
    QDir().mkpath(part);
    gamedb::DatabaseInfo info;
    info.name = name.toStdString();
    info.source = "profile";
    auto writer = gamedb::DatabaseWriter::create(part.toStdString(), info);
    if (!writer.ok()) {
        return QString::fromStdString(writer.error().message);
    }
    // The files one after another, as one stream.
    int index = 0;
    QFile file;
    const auto read = [&](char* buffer, std::size_t size) -> std::size_t {
        while (true) {
            if (!file.isOpen()) {
                if (index >= files.size()) {
                    return 0;
                }
                file.setFileName(files[index++]);
                if (!file.open(QIODevice::ReadOnly)) {
                    continue;
                }
                buffer[0] = '\n'; // a game never runs from one file into the next
                return 1;
            }
            const qint64 got = file.read(buffer, static_cast<qint64>(size));
            if (got > 0) {
                return static_cast<std::size_t>(got);
            }
            file.close();
        }
    };
    gamedb::importPgn(**writer, read);
    if (const std::string error = (*writer)->finish(); !error.empty()) {
        return QString::fromStdString(error);
    }
    writer->reset();
    // Swap: the old one goes only once the new one is in place.
    const QString old = folder + QStringLiteral(".old");
    QDir(old).removeRecursively();
    if (QFileInfo::exists(folder)) {
        QDir().rename(folder, old); // NOLINT(readability-suspicious-call-argument): moved aside
    }
    if (!QDir().rename(part, folder)) {
        return QStringLiteral("The database couldn't be put in place.");
    }
    QDir(old).removeRecursively();
    return {};
}

} // namespace

ProfileStore::ProfileStore(const QString& root, QObject* parent)
    : QObject(parent), m_root(root.isEmpty() ? defaultRoot() : root),
      m_network(new QNetworkAccessManager(this)) {
    load();
}

ProfileStore::~ProfileStore() {
    wait();
}

QString ProfileStore::defaultRoot() {
    QString folder = qEnvironmentVariable("KIEBITZ_SETTINGS_DIR"); // tests: never the real one
    if (folder.isEmpty()) {
        folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    return QDir(folder).filePath(QStringLiteral("Profiles"));
}

// ---- Profiles and accounts ---------------------------------------------------------------------

void ProfileStore::load() {
    QFile file(QDir(m_root).filePath(QStringLiteral("profiles.json")));
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonArray profiles = QJsonDocument::fromJson(file.readAll()).array();
        for (const auto& value : profiles) {
            const QJsonObject object = value.toObject();
            Profile profile;
            profile.id = object[QStringLiteral("id")].toString();
            profile.name = object[QStringLiteral("name")].toString();
            profile.main = object[QStringLiteral("main")].toBool();
            for (const auto& accountValue : object[QStringLiteral("accounts")].toArray()) {
                const QJsonObject a = accountValue.toObject();
                Account account;
                account.id = a[QStringLiteral("id")].toString();
                account.site = a[QStringLiteral("site")].toString() == u"chesscom"
                                   ? AccountSite::ChessCom
                                   : AccountSite::Lichess;
                account.username = a[QStringLiteral("username")].toString();
                account.synced =
                    QDateTime::fromString(a[QStringLiteral("synced")].toString(), Qt::ISODate);
                account.lichessSince =
                    static_cast<qint64>(a[QStringLiteral("lichessSince")].toDouble());
                for (const auto& month : a[QStringLiteral("chessComMonths")].toArray()) {
                    account.chessComMonths << month.toString();
                }
                account.title = a[QStringLiteral("title")].toString();
                account.siteGames = a[QStringLiteral("siteGames")].toInt();
                const QJsonObject changes = a[QStringLiteral("ratingChanges")].toObject();
                for (auto it = changes.begin(); it != changes.end(); ++it) {
                    account.ratingChanges[it.key()] = it.value().toInt();
                }
                const QJsonObject ratings = a[QStringLiteral("ratings")].toObject();
                for (auto it = ratings.begin(); it != ratings.end(); ++it) {
                    account.ratings[it.key()] = it.value().toInt();
                }
                profile.accounts.push_back(account);
            }
            if (object.contains(QStringLiteral("fide"))) {
                profile.fide = fideFromJson(object[QStringLiteral("fide")].toObject());
            }
            for (const auto& candidate : object[QStringLiteral("fideCandidates")].toArray()) {
                profile.fideCandidates.push_back(fideFromJson(candidate.toObject()));
            }
            profile.fideChosen = object[QStringLiteral("fideChosen")].toBool();
            profile.fideLooked = object[QStringLiteral("fideLooked")].toBool();
            if (!profile.id.isEmpty()) {
                m_profiles.push_back(std::move(profile));
            }
        }
    }
    // There is always a main profile: you.
    if (std::none_of(m_profiles.begin(), m_profiles.end(),
                     [](const Profile& p) { return p.main; })) {
        Profile you;
        you.id = QStringLiteral("me");
        you.name = tr("Me");
        you.main = true;
        m_profiles.insert(m_profiles.begin(), std::move(you));
        save();
    }
    for (const Profile& profile : m_profiles) {
        openDatabases(profile.id);
    }
    // Names not looked up yet (FIDE couldn't be reached last time): try now.
    QStringList pending;
    for (const Profile& profile : m_profiles) {
        if (!profile.fideLooked && !profile.fideChosen) {
            pending << profile.id;
        }
    }
    QMetaObject::invokeMethod(
        this,
        [this, pending] {
            for (const QString& id : pending) {
                lookUpFide(id);
            }
        },
        Qt::QueuedConnection);
}

void ProfileStore::save() const {
    QJsonArray profiles;
    for (const Profile& profile : m_profiles) {
        QJsonArray accounts;
        for (const Account& account : profile.accounts) {
            accounts.append(QJsonObject{
                {QStringLiteral("id"), account.id},
                {QStringLiteral("site"), siteName(account.site)},
                {QStringLiteral("username"), account.username},
                {QStringLiteral("synced"), account.synced.toString(Qt::ISODate)},
                {QStringLiteral("lichessSince"), static_cast<double>(account.lichessSince)},
                {QStringLiteral("chessComMonths"),
                 QJsonArray::fromStringList(account.chessComMonths)},
                {QStringLiteral("title"), account.title},
                {QStringLiteral("ratings"), ratingsJson(account.ratings)},
                {QStringLiteral("ratingChanges"), ratingsJson(account.ratingChanges)},
                {QStringLiteral("siteGames"), account.siteGames},
            });
        }
        QJsonObject object{{QStringLiteral("id"), profile.id},
                           {QStringLiteral("name"), profile.name},
                           {QStringLiteral("main"), profile.main},
                           {QStringLiteral("accounts"), accounts},
                           {QStringLiteral("fideChosen"), profile.fideChosen},
                           {QStringLiteral("fideLooked"), profile.fideLooked}};
        if (profile.fide.has_value()) {
            object[QStringLiteral("fide")] = toJson(profile.fide.value_or(FidePlayer{}));
        }
        QJsonArray candidates;
        for (const FidePlayer& candidate : profile.fideCandidates) {
            candidates.append(toJson(candidate));
        }
        object[QStringLiteral("fideCandidates")] = candidates;
        profiles.append(object);
    }
    QDir().mkpath(m_root);
    // Written next to it and renamed, so a crash never leaves half a file.
    const QString path = QDir(m_root).filePath(QStringLiteral("profiles.json"));
    QFile file(path + QStringLiteral(".part"));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(profiles).toJson());
        file.close();
        QFile::remove(path);
        QFile::rename(path + QStringLiteral(".part"), path);
    }
}

Profile* ProfileStore::find(const QString& id) {
    for (Profile& profile : m_profiles) {
        if (profile.id == id) {
            return &profile;
        }
    }
    return nullptr;
}

const Profile* ProfileStore::profile(const QString& id) const {
    for (const Profile& p : m_profiles) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

QString ProfileStore::folderOf(const QString& profileId) const {
    return QDir(m_root).filePath(profileId);
}

QString ProfileStore::addProfile(const QString& name) {
    QString id;
    for (int n = 1; id.isEmpty() || find(id) != nullptr; ++n) {
        id = QStringLiteral("profile-%1").arg(n);
    }
    Profile profile;
    profile.id = id;
    profile.name = name.trimmed().isEmpty() ? tr("New profile") : name.trimmed();
    m_profiles.push_back(std::move(profile));
    save();
    emit changed();
    lookUpFide(id);
    return id;
}

void ProfileStore::renameProfile(const QString& id, const QString& name) {
    if (Profile* profile = find(id); profile != nullptr && !name.trimmed().isEmpty()) {
        profile->name = name.trimmed();
        // Another name: another person, perhaps.
        profile->fide.reset();
        profile->fideCandidates.clear();
        profile->fideChosen = false;
        profile->fideLooked = false;
        save();
        emit changed();
        lookUpFide(id);
    }
}

void ProfileStore::removeProfile(const QString& id) {
    const Profile* profile = find(id);
    if (profile == nullptr || profile->main || busy(id)) {
        return;
    }
    std::erase_if(m_profiles, [&](const Profile& p) { return p.id == id; });
    save();
    QDir(folderOf(id)).removeRecursively();
    emit changed();
}

QString ProfileStore::addAccount(const QString& profileId, AccountSite site,
                                 const QString& username) {
    Profile* profile = find(profileId);
    const QString name = username.trimmed();
    if (profile == nullptr || name.isEmpty()) {
        return {};
    }
    QString id = siteName(site) + u'-' + name.toLower();
    if (std::any_of(profile->accounts.begin(), profile->accounts.end(),
                    [&](const Account& a) { return a.id == id; })) {
        return {};
    }
    Account account;
    account.id = id;
    account.site = site;
    account.username = name;
    profile->accounts.push_back(account);
    save();
    emit changed();
    sync(profileId);
    return id;
}

void ProfileStore::removeAccount(const QString& profileId, const QString& accountId) {
    Profile* profile = find(profileId);
    if (profile == nullptr || busy(profileId)) {
        return;
    }
    std::erase_if(profile->accounts, [&](const Account& a) { return a.id == accountId; });
    save();
    QDir(QDir(folderOf(profileId)).filePath(accountId)).removeRecursively();
    QDir(QDir(folderOf(profileId)).filePath(QStringLiteral("db/") + accountId)).removeRecursively();
    profile->parts.erase(accountId);
    emit changed();
    build(profileId);
}

void ProfileStore::addOtbGames(const QString& profileId, const QStringList& files) {
    if (find(profileId) == nullptr || busy(profileId) || files.isEmpty()) {
        return;
    }
    setStatus(profileId, tr("Adding games…"));
    const QString otb = QDir(folderOf(profileId)).filePath(QString::fromLatin1(kOtb));
    const auto future = QtConcurrent::run([files, otb] {
        QDir().mkpath(otb);
        for (const QString& file : files) {
            const QFileInfo info(file);
            QString target = QDir(otb).filePath(info.completeBaseName() + QStringLiteral(".pgn"));
            for (int n = 2; QFileInfo::exists(target); ++n) {
                target = QDir(otb).filePath(
                    QStringLiteral("%1 %2.pgn").arg(info.completeBaseName()).arg(n));
            }
            QFile::copy(file, target);
        }
    });
    auto* watcher =
        new QFutureWatcher<void>(this); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher, profileId] {
        watcher->deleteLater();
        build(profileId);
    });
    watcher->setFuture(future);
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

QString ProfileStore::status(const QString& profileId) const {
    const auto found = m_status.find(profileId);
    return found != m_status.end() ? found->second : QString();
}

bool ProfileStore::busy(const QString& profileId) const {
    return !status(profileId).isEmpty();
}

void ProfileStore::setStatus(const QString& profileId, const QString& status) {
    if (status.isEmpty()) {
        m_status.erase(profileId);
    } else {
        m_status[profileId] = status;
    }
    emit progress(profileId);
}

void ProfileStore::setServers(const QUrl& lichess, const QUrl& chessCom, const QUrl& fide) {
    m_lichess = lichess;
    m_chessCom = chessCom;
    if (fide.isValid()) {
        m_fide = fide;
    }
}

// ---- FIDE --------------------------------------------------------------------------------------

void ProfileStore::lookUpFide(const QString& profileId) {
    const Profile* profile = find(profileId);
    if (profile == nullptr || !worthLookingUp(profile->name)) {
        return;
    }
    // FIDE's own search, as its rating page asks it (an HTML table of everyone who fits).
    QUrl url = m_fide.resolved(QUrl(QStringLiteral("/incl_search_l.php")));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("search"), profile->name);
    query.addQueryItem(QStringLiteral("simple"), QStringLiteral("1"));
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setRawHeader("X-Requested-With", "XMLHttpRequest");
    request.setRawHeader("Referer", "https://ratings.fide.com/");
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    request.setTransferTimeout(15000); // FIDE's site is sometimes slow or away
    QNetworkReply* reply = m_network->get(request);
    const QString name = profile->name;
    if (Profile* looking = find(profileId)) {
        looking->fideLooking = true;
        looking->fideError.clear();
        emit changed();
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply, profileId, name] {
        reply->deleteLater();
        Profile* found = find(profileId);
        if (found == nullptr || found->name != name) {
            return; // renamed meanwhile: that name's lookup is under way
        }
        found->fideLooking = false;
        if (reply->error() != QNetworkReply::NoError) {
            // Tried again on the next start, or with "Try Again".
            found->fideError = tr("FIDE's rating site isn't answering right now.");
            emit changed();
            return;
        }
        std::vector<FidePlayer> matches =
            rankFideMatches(parseFideSearch(QString::fromUtf8(reply->readAll())), name);
        if (matches.size() > 8) {
            matches.resize(8);
        }
        found->fideCandidates = matches;
        found->fideLooked = true;
        if (!found->fideChosen) {
            found->fide = matches.empty() ? std::nullopt : std::optional(matches.front());
        }
        save();
        emit changed();
    });
}

void ProfileStore::chooseFide(const QString& profileId, int fideId) {
    Profile* profile = find(profileId);
    if (profile == nullptr) {
        return;
    }
    profile->fide.reset();
    for (const FidePlayer& candidate : profile->fideCandidates) {
        if (candidate.id == fideId) {
            profile->fide = candidate;
        }
    }
    profile->fideChosen = true;
    save();
    emit changed();
}

void ProfileStore::wait() {
    for (QFuture<void>& build : m_builds) {
        build.waitForFinished();
    }
    m_builds.clear();
}

// ---- Downloads ---------------------------------------------------------------------------------

void ProfileStore::sync(const QString& profileId) {
    const Profile* profile = find(profileId);
    if (profile == nullptr || busy(profileId)) {
        return;
    }
    const auto job = std::make_shared<Job>();
    job->profileId = profileId;
    for (const Account& account : profile->accounts) {
        job->accounts.push_back(account.id);
    }
    setStatus(profileId, tr("Starting…"));
    next(job);
}

void ProfileStore::next(const std::shared_ptr<Job>& job) {
    Profile* profile = find(job->profileId);
    if (profile == nullptr) {
        return;
    }
    while (!job->accounts.empty()) {
        const QString id = job->accounts.front();
        job->accounts.pop_front();
        for (Account& account : profile->accounts) {
            if (account.id != id) {
                continue;
            }
            QDir().mkpath(QDir(folderOf(job->profileId)).filePath(account.id));
            if (account.site == AccountSite::Lichess) {
                downloadLichess(job, account);
            } else {
                downloadChessCom(job, account);
            }
            return;
        }
    }
    build(job->profileId);
}

void ProfileStore::downloadLichess(const std::shared_ptr<Job>& job, Account& account) {
    // All finished games since the last download, as PGN (main lines, no clocks or evals).
    QUrl url = m_lichess.resolved(QUrl(QStringLiteral("/api/games/user/") + account.username));
    QUrlQuery query;
    if (account.lichessSince > 0) {
        query.addQueryItem(QStringLiteral("since"), QString::number(account.lichessSince));
    }
    query.addQueryItem(QStringLiteral("clocks"), QStringLiteral("false"));
    query.addQueryItem(QStringLiteral("evals"), QStringLiteral("false"));
    query.addQueryItem(QStringLiteral("opening"), QStringLiteral("true"));
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/x-chess-pgn");
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    // Signed in, Lichess sends games faster (and your own games faster still).
    if (const QByteArray token = m_lichessToken ? m_lichessToken() : QByteArray();
        !token.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + token);
    }
    const qint64 started = QDateTime::currentMSecsSinceEpoch();
    const QString target = QDir(QDir(folderOf(job->profileId)).filePath(account.id))
                               .filePath(QStringLiteral("%1.pgn").arg(started));
    const auto file = std::make_shared<QFile>(target + QStringLiteral(".part"));
    if (!file->open(QIODevice::WriteOnly)) {
        emit failed(job->profileId, tr("Couldn't write the downloaded games (is the disk full?)."));
        next(job);
        return;
    }
    QNetworkReply* reply = m_network->get(request);
    const auto games = std::make_shared<int>(0);
    const QString name = account.username;
    setStatus(job->profileId, tr("Downloading %1's games from Lichess…").arg(name));
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, file, games, job, name] {
        const QByteArray data = reply->readAll();
        file->write(data);
        *games += static_cast<int>(data.count("[Event "));
        setStatus(job->profileId,
                  tr("Downloading %1's games from Lichess… %2").arg(name).arg(*games));
    });
    const QString accountId = account.id;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, file, target, job, accountId, name, started] {
                reply->deleteLater();
                file->write(reply->readAll());
                file->close();
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (reply->error() != QNetworkReply::NoError || status >= 400) {
                    file->remove();
                    emit failed(job->profileId, downloadError(QStringLiteral("Lichess"), name,
                                                              status, reply->errorString()));
                } else {
                    if (file->size() == 0) {
                        file->remove(); // nothing new
                    } else {
                        file->rename(target);
                    }
                    if (Profile* profile = find(job->profileId)) {
                        for (Account& a : profile->accounts) {
                            if (a.id == accountId) {
                                a.lichessSince = started;
                                a.synced = QDateTime::currentDateTime();
                            }
                        }
                        save();
                    }
                }
                fetchRatings(job, accountId);
            });
}

void ProfileStore::downloadChessCom(const std::shared_ptr<Job>& job, Account& account) {
    // chess.com keeps games in monthly archives: the list, then every month not downloaded yet
    // (the current month again each time: it is still being played).
    const QString user = account.username.toLower();
    QNetworkRequest request(
        m_chessCom.resolved(QUrl(QStringLiteral("/pub/player/%1/games/archives").arg(user))));
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    QNetworkReply* reply = m_network->get(request);
    const QString accountId = account.id;
    const QString name = account.username;
    setStatus(job->profileId, tr("Looking up %1's games on chess.com…").arg(name));
    connect(reply, &QNetworkReply::finished, this, [this, reply, job, accountId, name] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status >= 400) {
            emit failed(job->profileId, downloadError(QStringLiteral("chess.com"), name, status,
                                                      reply->errorString()));
            next(job);
            return;
        }
        const Profile* profile = find(job->profileId);
        const Account* account = nullptr;
        if (profile != nullptr) {
            for (const Account& a : profile->accounts) {
                if (a.id == accountId) {
                    account = &a;
                }
            }
        }
        if (account == nullptr) {
            next(job);
            return;
        }
        const QJsonArray archives = QJsonDocument::fromJson(reply->readAll())
                                        .object()[QStringLiteral("archives")]
                                        .toArray();
        const auto months = std::make_shared<std::deque<QString>>();
        for (const auto& value : archives) {
            const QString url = value.toString();
            const QString month = url.section(u'/', -2); // "2026/10"
            if (!account->chessComMonths.contains(month) || value == archives.last()) {
                months->push_back(url);
            }
        }
        const QString folder = QDir(folderOf(job->profileId)).filePath(accountId);
        const auto fetchNext = std::make_shared<std::function<void()>>();
        const auto done = std::make_shared<int>(0);
        const auto total = static_cast<int>(months->size());
        *fetchNext = [this, months, folder, job, accountId, name, fetchNext, done, total,
                      archives] {
            if (months->empty()) {
                if (Profile* p = find(job->profileId)) {
                    for (Account& a : p->accounts) {
                        if (a.id != accountId) {
                            continue;
                        }
                        a.synced = QDateTime::currentDateTime();
                        // Every month but the last is complete now.
                        for (const auto& value : archives) {
                            const QString month = value.toString().section(u'/', -2);
                            if (value != archives.last() && !a.chessComMonths.contains(month)) {
                                a.chessComMonths << month;
                            }
                        }
                    }
                    save();
                }
                // The cycle ends: this function lets go of itself, which destroys everything it
                // holds. So what is still needed is copied out first, and only copies are used.
                // NOLINTNEXTLINE(performance-unnecessary-copy-initialization): see above
                const std::shared_ptr<Job> finished = job;
                // NOLINTNEXTLINE(performance-unnecessary-copy-initialization): see above
                const QString account = accountId;
                ProfileStore* store = this;
                *fetchNext = nullptr;
                store->fetchRatings(finished, account);
                return;
            }
            const QString url = months->front();
            months->pop_front();
            setStatus(job->profileId, tr("Downloading %1's games from chess.com… %2 of %3 months")
                                          .arg(name)
                                          .arg(++*done)
                                          .arg(total));
            QNetworkRequest monthRequest(QUrl(url + QStringLiteral("/pgn")));
            monthRequest.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
            QNetworkReply* monthReply = m_network->get(monthRequest);
            connect(
                monthReply, &QNetworkReply::finished, this, [monthReply, url, folder, fetchNext] {
                    monthReply->deleteLater();
                    if (monthReply->error() == QNetworkReply::NoError) {
                        QFile file(QDir(folder).filePath(url.section(u'/', -2).replace(u'/', u'-') +
                                                         QStringLiteral(".pgn")));
                        if (file.open(QIODevice::WriteOnly)) {
                            file.write(monthReply->readAll());
                        }
                    }
                    if (*fetchNext) {
                        (*fetchNext)();
                    }
                });
        };
        (*fetchNext)();
    });
}

void ProfileStore::fetchRatings(const std::shared_ptr<Job>& job, const QString& accountId) {
    // The account's current ratings by time control (and its title, on Lichess).
    const Profile* profile = find(job->profileId);
    const Account* account = nullptr;
    if (profile != nullptr) {
        for (const Account& a : profile->accounts) {
            if (a.id == accountId) {
                account = &a;
            }
        }
    }
    if (account == nullptr) {
        next(job);
        return;
    }
    const bool lichess = account->site == AccountSite::Lichess;
    const QUrl url =
        lichess ? m_lichess.resolved(QUrl(QStringLiteral("/api/user/") + account->username))
                : m_chessCom.resolved(QUrl(
                      QStringLiteral("/pub/player/%1/stats").arg(account->username.toLower())));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, job, accountId, lichess] {
        reply->deleteLater();
        Profile* found = find(job->profileId);
        if (found != nullptr && reply->error() == QNetworkReply::NoError) {
            const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
            for (Account& a : found->accounts) {
                if (a.id != accountId) {
                    continue;
                }
                const std::map<QString, int> before = a.ratings;
                a.ratings.clear();
                a.siteGames = 0;
                if (lichess) {
                    a.siteGames =
                        object[QStringLiteral("count")].toObject()[QStringLiteral("all")].toInt();
                    const QJsonObject perfs = object[QStringLiteral("perfs")].toObject();
                    for (const char* speed :
                         {"bullet", "blitz", "rapid", "classical", "correspondence"}) {
                        const QJsonObject perf = perfs[QString::fromLatin1(speed)].toObject();
                        // Provisional ratings (few games) and unplayed ones are left out.
                        if (perf[QStringLiteral("games")].toInt() > 0 &&
                            !perf[QStringLiteral("prov")].toBool()) {
                            a.ratings[QString::fromLatin1(speed)] =
                                perf[QStringLiteral("rating")].toInt();
                        }
                    }
                    a.title = object[QStringLiteral("title")].toString();
                } else {
                    for (const auto& [key, speed] :
                         {std::pair{"chess_bullet", "bullet"}, std::pair{"chess_blitz", "blitz"},
                          std::pair{"chess_rapid", "rapid"}, std::pair{"chess_daily", "daily"}}) {
                        const QJsonObject type = object[QString::fromLatin1(key)].toObject();
                        const int rating = type[QStringLiteral("last")]
                                               .toObject()[QStringLiteral("rating")]
                                               .toInt();
                        if (rating > 0) {
                            a.ratings[QString::fromLatin1(speed)] = rating;
                        }
                        const QJsonObject record = type[QStringLiteral("record")].toObject();
                        a.siteGames += record[QStringLiteral("win")].toInt() +
                                       record[QStringLiteral("loss")].toInt() +
                                       record[QStringLiteral("draw")].toInt();
                    }
                }
                // How each rating moved since the last sync (kept until it moves again).
                for (const auto& [speed, rating] : a.ratings) {
                    if (const auto old = before.find(speed);
                        old != before.end() && old->second != rating) {
                        a.ratingChanges[speed] = rating - old->second;
                    }
                }
            }
            save();
        }
        next(job);
    });
}

QStringList ProfileStore::namesOf(const Profile& profile, const QString& part) {
    // Who the profile is in its games: the account's username, or (over the board) its own
    // name and its FIDE name, as "First Last" and as "Last, First".
    QStringList names;
    const auto person = [&names](const QString& name) {
        const QStringList words = name.split(u' ', Qt::SkipEmptyParts);
        if (name.contains(u',')) { // "Ott, Lukas"
            names << name.simplified();
            const QString last = name.section(u',', 0, 0).trimmed();
            const QString first = name.section(u',', 1).trimmed();
            names << first + u' ' + last << last + u' ' + first;
        } else if (words.size() >= 2) { // "Lukas Ott"
            const QString& last = words.last();
            const QString first = words.mid(0, words.size() - 1).join(u' ');
            names << name.simplified() << last + QStringLiteral(", ") + first
                  << last + u' ' + first;
        } else if (!words.isEmpty()) {
            names << words.front();
        }
    };
    for (const Account& account : profile.accounts) {
        if (part.isEmpty() || part == account.id) {
            names << account.username;
        }
    }
    if (part.isEmpty() || part == QString::fromLatin1(kOtb)) {
        person(profile.name);
        if (profile.fide) {
            person(profile.fide->name);
        }
    }
    names.removeDuplicates();
    return names;
}

void ProfileStore::computeRecords(const QString& profileId) {
    const Profile* profile = find(profileId);
    if (profile == nullptr) {
        return;
    }
    // Each part's results with the names that are the profile there; in the background.
    std::vector<std::tuple<QString, std::shared_ptr<const gamedb::Database>, QStringList>> work;
    if (profile->all) {
        work.emplace_back(QString(), profile->all, namesOf(*profile, {}));
    }
    for (const auto& [part, db] : profile->parts) {
        work.emplace_back(part, db, namesOf(*profile, part));
    }
    using Records = std::map<QString, gamedb::Record>;
    auto* watcher =
        new QFutureWatcher<Records>(this); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    connect(watcher, &QFutureWatcher<Records>::finished, this, [this, watcher, profileId] {
        watcher->deleteLater();
        if (Profile* p = find(profileId)) {
            p->records = watcher->result();
            emit changed();
        }
    });
    watcher->setFuture(QtConcurrent::run([work = std::move(work)] {
        Records records;
        for (const auto& [part, db, names] : work) {
            std::vector<std::string> list;
            for (const QString& name : names) {
                list.push_back(name.toStdString());
            }
            records[part] = db->record(list);
        }
        return records;
    }));
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

// ---- Databases ---------------------------------------------------------------------------------

void ProfileStore::build(const QString& profileId) {
    const Profile* profile = find(profileId);
    if (profile == nullptr) {
        return;
    }
    setStatus(profileId, tr("Building the database…"));
    QStringList parts{QString::fromLatin1(kOtb)};
    for (const Account& account : profile->accounts) {
        parts << account.id;
    }
    const QString folder = folderOf(profileId);
    const QString name = profile->name;
    const auto future = QtConcurrent::run([folder, parts, name] {
        QStringList everything;
        for (const QString& part : parts) {
            const QStringList files = pgnFiles(QDir(folder).filePath(part));
            const QString db = QDir(folder).filePath(QStringLiteral("db/") + part);
            if (files.isEmpty()) {
                QDir(db).removeRecursively();
                continue;
            }
            everything << files;
            buildDatabase(files, db, name);
        }
        const QString all =
            QDir(folder).filePath(QStringLiteral("db/") + QString::fromLatin1(kAll));
        if (everything.isEmpty()) {
            QDir(all).removeRecursively();
        } else {
            buildDatabase(everything, all, name);
        }
    });
    m_builds.push_back(future);
    auto* watcher =
        new QFutureWatcher<void>(this); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher, profileId] {
        watcher->deleteLater();
        openDatabases(profileId);
        setStatus(profileId, {});
        emit changed();
    });
    watcher->setFuture(future);
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

void ProfileStore::openDatabases(const QString& profileId) {
    Profile* profile = find(profileId);
    if (profile == nullptr) {
        return;
    }
    const QString db = QDir(folderOf(profileId)).filePath(QStringLiteral("db"));
    const auto open = [](const QString& folder) -> std::shared_ptr<const gamedb::Database> {
        if (!QFileInfo::exists(folder)) {
            return nullptr;
        }
        auto opened = gamedb::Database::open(folder.toStdString());
        return opened.ok() ? std::shared_ptr<const gamedb::Database>(std::move(*opened)) : nullptr;
    };
    profile->all = open(QDir(db).filePath(QString::fromLatin1(kAll)));
    profile->parts.clear();
    QStringList parts{QString::fromLatin1(kOtb)};
    for (const Account& account : profile->accounts) {
        parts << account.id;
    }
    for (const QString& part : parts) {
        if (auto opened = open(QDir(db).filePath(part))) {
            profile->parts[part] = std::move(opened);
        }
    }
    computeRecords(profileId);
}

} // namespace kiebitz::app
