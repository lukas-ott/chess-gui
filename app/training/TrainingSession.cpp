#include "TrainingSession.hpp"

#include "TrainingHistory.hpp"

#include <QSettings>

namespace kiebitz::app {
namespace {

constexpr qint64 kFlushMs = 15'000; ///< how often the time goes into the log

QString settingsKey(training::Part part, const char* what) {
    return QStringLiteral("training/%1/%2")
        .arg(QString::fromLatin1(training::key(part).data(),
                                 static_cast<qsizetype>(training::key(part).size())),
             QLatin1String(what));
}

} // namespace

TrainingSession::TrainingSession(TrainingHistory& stats, QObject* parent)
    : QObject(parent), m_stats(stats) {
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, &TrainingSession::tick);
}

TrainingSession::~TrainingSession() = default; // the window stops it first (closeEvent)

std::vector<training::Step> TrainingSession::plan() {
    const QSettings settings;
    std::vector<training::Step> result = training::defaultPlan();
    for (training::Step& step : result) {
        step.minutes = settings.value(settingsKey(step.part, "minutes"), step.minutes).toInt();
        step.on = settings.value(settingsKey(step.part, "on"), step.on).toBool();
    }
    return result;
}

void TrainingSession::savePlan(const std::vector<training::Step>& plan) {
    QSettings settings;
    for (const training::Step& step : plan) {
        settings.setValue(settingsKey(step.part, "minutes"), step.minutes);
        settings.setValue(settingsKey(step.part, "on"), step.on);
    }
}

TrainingSession::Side TrainingSession::openingsSide() {
    const QString side = QSettings().value(QStringLiteral("training/openingsSide")).toString();
    if (side == u"white") {
        return Side::White;
    }
    return side == u"black" ? Side::Black : Side::All;
}

void TrainingSession::setOpeningsSide(Side side) {
    QString value = QStringLiteral("all");
    if (side == Side::White) {
        value = QStringLiteral("white");
    } else if (side == Side::Black) {
        value = QStringLiteral("black");
    }
    QSettings().setValue(QStringLiteral("training/openingsSide"), value);
}

QString TrainingSession::name(training::Part part) {
    switch (part) {
    case training::Part::Openings:
        return tr("Openings");
    case training::Part::Middlegame:
        return tr("Middlegame");
    case training::Part::Tactics:
        return tr("Tactics");
    case training::Part::Endgames:
        return tr("Endgames");
    case training::Part::SelfStudy:
        break;
    }
    return tr("Self-study");
}

bool TrainingSession::start() {
    stop();
    training::Session session(plan());
    if (session.empty()) {
        return false;
    }
    const training::Part first = session.part();
    m_session = std::move(session);
    m_paused = false;
    m_upSaid = false;
    m_unsaved = 0;
    m_clock.start();
    m_tick.start();
    emit partStarted(first);
    emit changed();
    return true;
}

void TrainingSession::setPaused(bool paused) {
    if (!running() || paused == m_paused) {
        return;
    }
    if (paused) {
        tick(); // the time up to now counts
        m_tick.stop();
        flush();
    } else {
        m_clock.restart();
        m_tick.start();
    }
    m_paused = paused;
    emit changed();
}

void TrainingSession::next() {
    if (!m_session || m_session->finished()) {
        return;
    }
    training::Session& session = *m_session;
    if (!m_paused) {
        tick();
    }
    flush();
    session.next();
    m_upSaid = false;
    if (session.finished()) {
        stop();
        return;
    }
    if (m_paused) { // a new part starts running
        m_paused = false;
        m_clock.restart();
        m_tick.start();
    }
    emit partStarted(session.part());
    emit changed();
}

void TrainingSession::stop() {
    if (!m_session) {
        return;
    }
    if (running() && !m_paused) {
        tick();
    }
    flush();
    m_tick.stop();
    m_session.reset();
    m_paused = false;
    emit changed();
    emit ended();
}

void TrainingSession::tick() {
    if (running() && !m_paused) {
        advance(m_clock.restart());
    }
}

void TrainingSession::advance(qint64 ms) {
    if (!m_session || m_session->finished() || m_paused || ms <= 0) {
        return;
    }
    training::Session& session = *m_session;
    session.elapse(ms);
    m_unsaved += ms;
    if (m_unsaved >= kFlushMs) {
        flush();
    }
    if (session.timeUp() && !m_upSaid) {
        m_upSaid = true;
        emit timeUp(session.part());
    }
    emit changed();
}

void TrainingSession::flush() {
    if (!m_session || m_unsaved < 1000) {
        return;
    }
    const auto seconds = static_cast<int>(m_unsaved / 1000);
    m_stats.add(m_session->part(), seconds);
    m_unsaved -= qint64{seconds} * 1000;
}

} // namespace kiebitz::app
