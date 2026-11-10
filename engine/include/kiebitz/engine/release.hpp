#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Choosing which file of a Stockfish release to download (the release lists one per platform).
namespace kiebitz::engine {

enum class Cpu : std::uint8_t { Arm64, X86_64 };

/// The CPU this program was built for.
[[nodiscard]] constexpr Cpu thisCpu() noexcept {
#if defined(__aarch64__) || defined(__arm64__)
    return Cpu::Arm64;
#else
    return Cpu::X86_64;
#endif
}

/// The index of the best macOS download among a release's file names, or nullopt if none fits.
/// Prefers a build for this CPU, then a universal one; only .tar, .tar.gz and .zip archives.
/// Stockfish has shipped "stockfish-macos-m1-apple-silicon.tar" and, since 19,
/// "stockfish-macos-universal.tar.gz".
[[nodiscard]] std::optional<std::size_t> pickMacDownload(const std::vector<std::string>& names,
                                                         Cpu cpu);

/// The hex SHA-256 from a GitHub asset digest ("sha256:ab12..."), or nullopt for anything else.
[[nodiscard]] std::optional<std::string> sha256FromDigest(std::string_view digest);

} // namespace kiebitz::engine
