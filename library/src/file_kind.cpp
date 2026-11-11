#include "kiebitz/library/file_kind.hpp"

#include <array>
#include <cctype>

namespace kiebitz::library {
namespace {

constexpr std::array<std::string_view, 5> kNames{"Game", "Repertoire", "Tournament", "Puzzle",
                                                 "Other"};

bool sameText(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string_view nameOf(FileKind kind) noexcept {
    return kNames[static_cast<std::size_t>(kind)];
}

std::optional<FileKind> kindFromName(std::string_view name) noexcept {
    for (std::size_t i = 0; i < kNames.size(); ++i) {
        if (sameText(name, kNames[i])) {
            return static_cast<FileKind>(i);
        }
    }
    return std::nullopt;
}

FileKind kindOf(std::span<const pgn::GameEntry> games) {
    for (const auto& game : games) {
        if (const auto kind = kindFromName(game.header(kFileTypeHeader))) {
            return *kind;
        }
    }
    for (const auto& game : games) {
        if (!game.header("Repertoire").empty()) {
            return FileKind::Repertoire;
        }
    }
    return FileKind::Game;
}

} // namespace kiebitz::library
