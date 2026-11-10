#pragma once

#include "AnalysisSource.hpp"
#include "EngineSettings.hpp"

#include <QTimer>

#include <optional>
#include <set>
#include <string>
#include <string_view>

class QProcess;

namespace chessgui::app {

/// A UCI engine running as a separate process, analysing without a time limit ("go infinite").
///
/// The process starts on the first analyse() (never earlier) and stays up until this object
/// goes away. Talking to it never blocks: QProcess reports output through signals.
///
/// A new position stops the running search first. Lines that arrive after "stop" but before the
/// engine's "bestmove" still belong to the old position and are dropped.
class UciEngine : public AnalysisSource {
    Q_OBJECT

public:
    explicit UciEngine(EngineEntry entry, QObject* parent = nullptr);
    ~UciEngine() override;
    UciEngine(const UciEngine&) = delete;
    UciEngine& operator=(const UciEngine&) = delete;
    UciEngine(UciEngine&&) = delete;
    UciEngine& operator=(UciEngine&&) = delete;

    void analyse(const core::Position& position) override;
    void stop() override;
    void setMultiPv(int lines) override;

    /// The engine process is running (it starts lazily).
    [[nodiscard]] bool isStarted() const noexcept { return m_state != State::Off; }
    /// The name the engine gave itself ("Stockfish 19"); empty until it has started.
    [[nodiscard]] const QString& engineName() const noexcept { return m_engineName; }

private:
    enum class State : std::uint8_t {
        Off,       ///< no process
        Starting,  ///< waiting for "uciok" and "readyok"
        Ready,     ///< idle
        Searching, ///< "go infinite" sent
        Stopping,  ///< "stop" sent, waiting for "bestmove"
    };

    void startProcess();
    void readOutput();
    void handleLine(std::string_view line);
    void sendOptions();
    void go();
    void send(std::string_view command);
    void publish();

    EngineEntry m_entry;
    QProcess* m_process = nullptr;
    State m_state = State::Off;
    QString m_engineName;
    std::set<std::string, std::less<>> m_options; ///< the options the engine announced
    int m_multiPv = 3;
    bool m_optionsChanged = false; ///< MultiPV changed during a search: apply before the next

    bool m_wanted = false;                ///< analyse() was called and stop() wasn't
    std::optional<core::Position> m_next; ///< to search once the engine is free
    core::Position m_searching;           ///< the position being searched
    std::vector<engine::Info> m_lines;    ///< by MultiPV index
    QTimer m_publishTimer;                ///< updates go out at most every 100 ms
    std::string m_buffer;                 ///< output not yet ended by a newline
};

} // namespace chessgui::app
