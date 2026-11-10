#include "kiebitz/engine/release.hpp"

#include <algorithm>
#include <cctype>

namespace kiebitz::engine {
namespace {

std::string lower(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

bool contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

bool isArchive(std::string_view name) {
    return name.ends_with(".tar") || name.ends_with(".tar.gz") || name.ends_with(".tgz") ||
           name.ends_with(".zip");
}

/// Higher is better; 0 means unusable.
int rank(std::string_view name, Cpu cpu) {
    if (!contains(name, "macos") || !isArchive(name)) {
        return 0;
    }
    const bool arm =
        contains(name, "apple-silicon") || contains(name, "arm") || contains(name, "m1");
    const bool universal = contains(name, "universal");
    const bool x86 = contains(name, "x86");
    if (cpu == Cpu::Arm64) {
        if (arm) {
            return 3;
        }
        return universal ? 2 : 0;
    }
    if (universal) {
        return 3;
    }
    // Intel builds come in levels (x86-64, -sse41-popcnt, -avx2, -bmi2, ...): the plain one runs
    // on every Intel Mac, so it is the safe pick.
    if (x86) {
        return name.ends_with("x86-64.tar") || name.ends_with("x86-64.tar.gz") ? 2 : 1;
    }
    return 0;
}

} // namespace

std::optional<std::size_t> pickMacDownload(const std::vector<std::string>& names, Cpu cpu) {
    std::optional<std::size_t> best;
    int bestRank = 0;
    for (std::size_t i = 0; i < names.size(); ++i) {
        const int r = rank(lower(names[i]), cpu);
        if (r > bestRank) {
            bestRank = r;
            best = i;
        }
    }
    return best;
}

std::optional<std::string> sha256FromDigest(std::string_view digest) {
    constexpr std::string_view kPrefix = "sha256:";
    if (!digest.starts_with(kPrefix)) {
        return std::nullopt;
    }
    std::string hex = lower(digest.substr(kPrefix.size()));
    if (hex.size() != 64 || !std::all_of(hex.begin(), hex.end(), [](char c) {
            return std::isxdigit(static_cast<unsigned char>(c)) != 0;
        })) {
        return std::nullopt;
    }
    return hex;
}

} // namespace kiebitz::engine
