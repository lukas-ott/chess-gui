#pragma once

#include <QByteArray>
#include <QNetworkRequest>

class QNetworkReply;
class QUrl;

/// How Kiebitz talks to Lichess (its API, explorer and cloud), in one place: the same client
/// name on every request, the token when signed in, and Lichess's rule for API clients that
/// after a 429 ("too many requests") they wait a full minute. That pause is shared by all of
/// Kiebitz, every thread: Lichess counts requests per machine, not per feature.
namespace kiebitz::app::lichess {

/// How long a request may go without any data before it gives up (the explorer, the cloud).
inline constexpr int kTimeoutMs = 15000;

/// "Kiebitz/0.2.0 (+https://github.com/lukas-ott/kiebitz)". Lichess and chess.com ask API
/// clients to say who they are; nothing about the user goes in it.
[[nodiscard]] QByteArray userAgent();

/// A request with Kiebitz's name and, if `token` isn't empty, the sign-in. `timeoutMs` 0: none
/// (a download that streams for minutes).
[[nodiscard]] QNetworkRequest request(const QUrl& url, const QByteArray& token = {},
                                      int timeoutMs = 0);

/// The reply's HTTP status (0: no answer). A 429 starts the minute's pause.
int status(const QNetworkReply* reply);

/// Starts the minute's pause (status() does this on a 429).
void pause();
/// How long the pause still lasts, in ms; 0: requests may go.
[[nodiscard]] qint64 pauseLeftMs();
/// Ends the pause at once (tests, which share one process).
void endPause();

} // namespace kiebitz::app::lichess
