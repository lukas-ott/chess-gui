#pragma once

#include "chessgui/core/position.hpp"
#include "chessgui/engine/uci.hpp"

#include <QObject>
#include <QString>

#include <vector>

namespace chessgui::app {

/// What an analysis source currently knows about a position.
struct AnalysisUpdate {
    core::Position position;         ///< the position the lines belong to
    std::vector<engine::Info> lines; ///< best first; scores for the side to move
};

/// Something that analyses positions: a UCI engine process or the Lichess cloud. The engine panel
/// talks to either through this interface.
class AnalysisSource : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    /// Analyses this position from now on (replacing any earlier one) until stop().
    virtual void analyse(const core::Position& position) = 0;
    virtual void stop() = 0;
    /// How many lines (1–5) to show.
    virtual void setMultiPv(int lines) = 0;

signals:
    /// New or deeper lines. An update with no lines clears the old ones.
    void updated(const chessgui::app::AnalysisUpdate& update);
    /// A short note on what's going on ("Not in the Lichess cloud"); empty clears it.
    void statusChanged(const QString& status);
    /// Something went wrong for good (the engine crashed or couldn't start).
    void failed(const QString& message);
};

} // namespace chessgui::app
