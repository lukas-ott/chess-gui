#include "Lichess.hpp"

#include "kiebitz/core/version.hpp"

#include <QNetworkReply>
#include <QUrl>

#include <algorithm>
#include <atomic>
#include <chrono>

namespace kiebitz::app::lichess {
namespace {

constexpr qint64 kPauseMs = 60'000;

/// When the pause ends (steady clock, ms); 0: none so far.
std::atomic<qint64>& pauseEnd() {
    static std::atomic<qint64> end{0};
    return end;
}

qint64 nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace

QByteArray userAgent() {
    const std::string_view version = core::version();
    return QByteArray("Kiebitz/") +
           QByteArray(version.data(), static_cast<qsizetype>(version.size())) +
           " (+https://github.com/lukas-ott/kiebitz)";
}

QNetworkRequest request(const QUrl& url, const QByteArray& token, int timeoutMs) {
    QNetworkRequest result(url);
    result.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    if (timeoutMs > 0) {
        result.setTransferTimeout(timeoutMs);
    }
    if (!token.isEmpty()) {
        result.setRawHeader("Authorization", "Bearer " + token);
    }
    return result;
}

int status(const QNetworkReply* reply) {
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (code == 429) {
        pause();
    }
    return code;
}

void pause() {
    pauseEnd().store(nowMs() + kPauseMs);
}

void endPause() {
    pauseEnd().store(0);
}

qint64 pauseLeftMs() {
    return std::max<qint64>(0, pauseEnd().load() - nowMs());
}

} // namespace kiebitz::app::lichess
