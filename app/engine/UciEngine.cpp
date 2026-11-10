#include "UciEngine.hpp"

#include <QProcess>

namespace chessgui::app {

UciEngine::UciEngine(EngineEntry entry, QObject* parent)
    : AnalysisSource(parent), m_entry(std::move(entry)) {
    m_publishTimer.setSingleShot(true);
    m_publishTimer.setInterval(100);
    connect(&m_publishTimer, &QTimer::timeout, this, &UciEngine::publish);
}

UciEngine::~UciEngine() {
    if (m_process == nullptr || m_process->state() == QProcess::NotRunning) {
        return;
    }
    // Ask politely, then insist: closing must not hang on a stuck engine.
    m_process->disconnect(this);
    m_process->write("stop\nquit\n");
    if (!m_process->waitForFinished(300)) {
        m_process->kill();
        m_process->waitForFinished(200);
    }
}

void UciEngine::analyse(const core::Position& position) {
    m_wanted = true;
    m_next = position;
    switch (m_state) {
    case State::Off:
        startProcess();
        break;
    case State::Ready:
        go();
        break;
    case State::Searching:
        send("stop");
        m_state = State::Stopping;
        break;
    case State::Starting:
    case State::Stopping:
        break; // goes ahead once the engine is ready / has stopped
    }
}

void UciEngine::stop() {
    m_wanted = false;
    m_next.reset();
    m_publishTimer.stop();
    m_lines.clear();
    if (m_state == State::Searching) {
        send("stop");
        m_state = State::Stopping;
    }
}

void UciEngine::setMultiPv(int lines) {
    if (lines == m_multiPv) {
        return;
    }
    m_multiPv = lines;
    if (m_state == State::Ready) {
        sendOptions();
    } else if (m_state == State::Searching) {
        // Options may only change between searches: stop, set it, and search again.
        m_optionsChanged = true;
        if (!m_next) {
            m_next = m_searching;
        }
        send("stop");
        m_state = State::Stopping;
    } else if (m_state == State::Stopping) {
        m_optionsChanged = true;
    }
}

void UciEngine::startProcess() {
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &UciEngine::readOutput);
    const auto lost = [this](const QString& message) {
        if (m_process == nullptr) {
            return; // already handled (a crash reports both an error and the exit)
        }
        m_process->deleteLater();
        m_process = nullptr;
        m_state = State::Off;
        m_wanted = false;
        m_lines.clear();
        m_buffer.clear();
        emit failed(message);
    };
    connect(m_process, &QProcess::errorOccurred, this, [this, lost](QProcess::ProcessError error) {
        lost(error == QProcess::FailedToStart ? tr("%1 couldn't be started.").arg(m_entry.name)
                                              : tr("%1 stopped unexpectedly.").arg(m_entry.name));
    });
    connect(m_process, &QProcess::finished, this,
            [this, lost] { lost(tr("%1 stopped unexpectedly.").arg(m_entry.name)); });
    // "uci" only once the process runs: writing to one that failed to start would report a
    // write error first, hiding the real reason.
    connect(m_process, &QProcess::started, this, [this] { send("uci"); });
    m_state = State::Starting;
    m_process->start(m_entry.path, {});
}

void UciEngine::readOutput() {
    m_buffer += m_process->readAllStandardOutput().toStdString();
    std::size_t start = 0;
    for (std::size_t end = m_buffer.find('\n'); end != std::string::npos;
         end = m_buffer.find('\n', start)) {
        std::string_view line(m_buffer.data() + start, end - start);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        handleLine(line);
        start = end + 1;
    }
    m_buffer.erase(0, start);
}

void UciEngine::handleLine(std::string_view line) {
    switch (m_state) {
    case State::Starting:
        if (auto name = engine::parseIdName(line)) {
            m_engineName = QString::fromStdString(*name);
        } else if (auto option = engine::parseOption(line)) {
            m_options.insert(option->name);
        } else if (line == "uciok") {
            sendOptions();
            send("isready");
        } else if (line == "readyok") {
            m_state = State::Ready;
            if (m_wanted && m_next) {
                go();
            }
        }
        break;
    case State::Searching:
        if (auto info = engine::parseInfo(line)) {
            if (info->multiPv >= 1 && info->multiPv <= m_multiPv) {
                const auto index = static_cast<std::size_t>(info->multiPv - 1);
                if (m_lines.size() <= index) {
                    m_lines.resize(index + 1);
                }
                m_lines[index] = std::move(*info);
                if (!m_publishTimer.isActive()) {
                    m_publishTimer.start();
                }
            }
        } else if (engine::isBestMove(line)) {
            // The search ended by itself (mate or stalemate on the board): keep what it found.
            m_state = State::Ready;
            publish();
        }
        break;
    case State::Stopping:
        if (engine::isBestMove(line)) {
            m_state = State::Ready;
            if (m_optionsChanged) {
                sendOptions();
            }
            if (m_wanted && m_next) {
                go();
            }
        }
        break; // anything else still belongs to the old position
    case State::Off:
    case State::Ready:
        break;
    }
}

void UciEngine::sendOptions() {
    m_optionsChanged = false;
    const auto set = [this](std::string_view name, int value) {
        if (m_options.contains(name)) {
            send(engine::setOptionCommand(name, std::to_string(value)));
        }
    };
    set("Threads", m_entry.threads);
    set("Hash", m_entry.hashMb);
    set("MultiPV", m_multiPv);
}

void UciEngine::go() {
    if (!m_next) {
        return;
    }
    m_searching = *m_next;
    m_next.reset();
    m_lines.clear();
    m_publishTimer.stop();
    emit updated({m_searching, {}}); // the old lines are for another position
    send(engine::positionCommand(m_searching));
    send("go infinite");
    m_state = State::Searching;
}

void UciEngine::send(std::string_view command) {
    if (m_process == nullptr) {
        return;
    }
    std::string line(command);
    line += '\n';
    m_process->write(line.data(), static_cast<qint64>(line.size()));
}

void UciEngine::publish() {
    m_publishTimer.stop();
    if (!m_wanted) {
        return;
    }
    // Only complete lines: a deeper search reports line 1 before lines 2 and 3.
    std::vector<engine::Info> lines;
    for (const auto& line : m_lines) {
        if (line.pv.empty()) {
            break;
        }
        lines.push_back(line);
    }
    emit updated({m_searching, std::move(lines)});
}

} // namespace chessgui::app
