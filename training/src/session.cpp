#include "kiebitz/training/session.hpp"

#include <algorithm>

namespace kiebitz::training {

std::string_view key(Part part) noexcept {
    switch (part) {
    case Part::Openings:
        return "openings";
    case Part::Middlegame:
        return "middlegame";
    case Part::Tactics:
        return "tactics";
    case Part::Endgames:
        return "endgames";
    case Part::SelfStudy:
        break;
    }
    return "selfstudy";
}

std::vector<Step> defaultPlan() {
    return {{Part::Openings, 15, true},
            {Part::Middlegame, 10, true},
            {Part::Tactics, 15, true},
            {Part::Endgames, 10, true},
            {Part::SelfStudy, 10, true}};
}

Session::Session(const std::vector<Step>& plan) {
    // In the parts' own order, whatever order the plan lists them in.
    for (const Part part : kAllParts) {
        for (const Step& step : plan) {
            if (step.part == part && step.on && step.minutes > 0) {
                m_steps.push_back(step);
                break;
            }
        }
    }
}

Part Session::part() const {
    return finished() ? Part::SelfStudy : m_steps[m_index].part;
}

const Step* Session::upcoming() const {
    return m_index + 1 < m_steps.size() ? &m_steps[m_index + 1] : nullptr;
}

std::int64_t Session::leftMs() const {
    if (finished()) {
        return 0;
    }
    const std::int64_t total = std::int64_t{m_steps[m_index].minutes} * 60'000;
    return std::max<std::int64_t>(0, total - m_used);
}

void Session::elapse(std::int64_t ms) {
    if (!finished() && ms > 0) {
        m_used += ms;
    }
}

void Session::next() {
    if (!finished()) {
        ++m_index;
        m_used = 0;
    }
}

} // namespace kiebitz::training
