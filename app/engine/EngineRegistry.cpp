#include "EngineRegistry.hpp"

#include "StockfishDownload.hpp"
#include "chessgui/engine/uci.hpp"

#include <QProcess>
#include <QTimer>

#include <algorithm>
#include <memory>

namespace chessgui::app {

EngineRegistry::EngineRegistry(QObject* parent) : QObject(parent), m_engines(engines::load()) {}

const EngineEntry* EngineRegistry::find(const QString& name) const {
    const auto it = std::find_if(m_engines.begin(), m_engines.end(),
                                 [&](const auto& e) { return e.name == name; });
    return it == m_engines.end() ? nullptr : &*it;
}

void EngineRegistry::add(const EngineEntry& entry) {
    const auto it = std::find_if(m_engines.begin(), m_engines.end(),
                                 [&](const auto& e) { return e.name == entry.name; });
    const bool isNew = it == m_engines.end();
    if (isNew) {
        m_engines.push_back(entry);
    } else {
        *it = entry;
    }
    engines::save(m_engines);
    emit changed();
    if (isNew) {
        emit added(entry.name);
    }
}

void EngineRegistry::remove(const QString& name) {
    if (std::erase_if(m_engines, [&](const auto& e) { return e.name == name; }) > 0) {
        engines::save(m_engines);
        emit changed();
    }
}

void EngineRegistry::addFromFile(const QString& path) {
    auto* process = new QProcess(this);
    const auto name = std::make_shared<QString>();
    connect(process, &QProcess::readyReadStandardOutput, this, [process, name] {
        while (process->canReadLine()) {
            const QByteArray line = process->readLine().trimmed();
            if (auto id = engine::parseIdName(line.toStdString())) {
                *name = QString::fromStdString(*id);
            }
            if (line == "uciok") {
                process->write("quit\n");
            }
        }
    });
    connect(process, &QProcess::finished, this, [this, process, path, name] {
        process->deleteLater();
        if (name->isEmpty()) {
            emit failed(tr("That program didn't answer as a UCI chess engine."));
            return;
        }
        EngineEntry entry;
        entry.name = *name;
        entry.path = path;
        add(entry);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            process->deleteLater();
            emit failed(tr("That file can't be run as a program."));
        }
    });
    connect(process, &QProcess::started, process, [process] { process->write("uci\n"); });
    QTimer::singleShot(5000, process, [process] { process->kill(); }); // never hang on it
    process->start(path, {});
}

StockfishDownload* EngineRegistry::download() {
    if (m_download == nullptr) {
        m_download = new StockfishDownload(engines::downloadFolder(), this);
        connect(m_download, &StockfishDownload::finished, this,
                [this](const QString& name, const QString& path) {
                    EngineEntry entry;
                    entry.name = name;
                    entry.path = path;
                    add(entry);
                });
        connect(m_download, &StockfishDownload::failed, this, &EngineRegistry::failed);
    }
    return m_download;
}

void EngineRegistry::downloadStockfish() {
    download()->start();
}

bool EngineRegistry::isDownloading() const {
    return m_download != nullptr && m_download->isRunning();
}

} // namespace chessgui::app
