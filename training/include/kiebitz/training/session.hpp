#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace kiebitz::training {

/// The parts of a training plan, in the order a session runs them.
enum class Part : std::uint8_t { Openings, Middlegame, Tactics, Endgames, SelfStudy };
inline constexpr std::size_t kParts = 5;
inline constexpr std::array kAllParts{Part::Openings, Part::Middlegame, Part::Tactics,
                                      Part::Endgames, Part::SelfStudy};

/// A key for a part that stays the same (settings, the log): "openings", "middlegame", ...
[[nodiscard]] std::string_view key(Part part) noexcept;

/// One part of the plan: how long, and whether it's in it at all.
struct Step {
    Part part = Part::Openings;
    int minutes = 0;
    bool on = true;
};

/// The plan a new user starts with: an hour, a bit of everything.
[[nodiscard]] std::vector<Step> defaultPlan();

/// A training session: the plan's parts (those on, with minutes) one after the other. Time is
/// given to it (elapse()); it counts on past a part's minutes until next() is called, so a
/// puzzle can be finished.
class Session {
public:
    explicit Session(const std::vector<Step>& plan);

    /// Nothing to do: every part off or at 0 minutes.
    [[nodiscard]] bool empty() const noexcept { return m_steps.empty(); }
    /// Past the last part.
    [[nodiscard]] bool finished() const noexcept { return m_index >= m_steps.size(); }
    [[nodiscard]] Part part() const;
    /// Which part this is (0-based) of how many.
    [[nodiscard]] std::size_t index() const noexcept { return m_index; }
    [[nodiscard]] std::size_t count() const noexcept { return m_steps.size(); }
    /// The part after this one, if any.
    [[nodiscard]] const Step* upcoming() const;

    /// Time spent in the current part, and how much of its minutes is left (0 once up).
    [[nodiscard]] std::int64_t usedMs() const noexcept { return m_used; }
    [[nodiscard]] std::int64_t leftMs() const;
    [[nodiscard]] bool timeUp() const { return !finished() && leftMs() == 0; }

    void elapse(std::int64_t ms);
    /// On to the next part (or finished).
    void next();

private:
    std::vector<Step> m_steps;
    std::size_t m_index = 0;
    std::int64_t m_used = 0;
};

} // namespace kiebitz::training
