#pragma once

#include "kiebitz/training/session.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <optional>
#include <vector>

namespace kiebitz::app {

class TrainingHistory;

/// The training plan and the session that runs it: the parts one after the other, timed, the
/// time going into the log. The window shows each part's page when it starts (partStarted)
/// and tells you when its time is up (timeUp); Next goes on.
class TrainingSession : public QObject {
    Q_OBJECT

public:
    /// Which repertoire files the openings part shows in the Library.
    enum class Side : std::uint8_t { White, Black, All };

    explicit TrainingSession(TrainingHistory& stats, QObject* parent = nullptr);
    ~TrainingSession() override;
    TrainingSession(const TrainingSession&) = delete;
    TrainingSession& operator=(const TrainingSession&) = delete;
    TrainingSession(TrainingSession&&) = delete;
    TrainingSession& operator=(TrainingSession&&) = delete;

    /// The plan as it is saved (the default one the first time), in the parts' order.
    [[nodiscard]] static std::vector<training::Step> plan();
    static void savePlan(const std::vector<training::Step>& plan);
    [[nodiscard]] static Side openingsSide();
    static void setOpeningsSide(Side side);
    /// "Openings", "Middlegame", ...
    [[nodiscard]] static QString name(training::Part part);

    [[nodiscard]] bool running() const { return m_session && !m_session->finished(); }
    [[nodiscard]] bool paused() const noexcept { return m_paused; }
    /// The session running, or null.
    [[nodiscard]] const training::Session* session() const {
        return m_session && !m_session->finished() ? &*m_session : nullptr;
    }
    /// Seconds trained this session that aren't in the log yet (it's written every so often).
    [[nodiscard]] int unsavedSeconds() const { return static_cast<int>(m_unsaved / 1000); }

    /// Starts the saved plan. False if it has nothing in it.
    bool start();
    void setPaused(bool paused);
    /// On to the next part (or the end).
    void next();
    void stop();
    /// As if this much time had passed (the clock does this every second; tests call it).
    void advance(qint64 ms);

signals:
    /// Every second while it runs, and whenever anything else changes.
    void changed();
    void partStarted(kiebitz::training::Part part);
    void timeUp(kiebitz::training::Part part);
    /// The last part is done, or the session was stopped.
    void ended();

private:
    void tick();
    /// Writes the time not yet in the log.
    void flush();

    TrainingHistory& m_stats;
    std::optional<training::Session> m_session;
    QTimer m_tick;
    QElapsedTimer m_clock;
    bool m_paused = false;
    qint64 m_unsaved = 0; ///< ms trained this part, not yet in the log
    bool m_upSaid = false;
};

} // namespace kiebitz::app
