#include "kiebitz/puzzles/store.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <numeric>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zstd.h>

namespace kiebitz::puzzles {
namespace {

// The file: a header, the theme names, then per puzzle (sorted by rating) its rating, its theme
// ids and where its record is, then the records. Little-endian, as every Mac is.
//
// A record: the occupied squares (64 bits, a1 = bit 0), a nibble per piece on them in square
// order (colour << 3 | piece type), flags (bit 0 Black to move, bits 1-4 castling KQkq), the en
// passant square (0xFF none), the move number (16 bits), the id (length + chars) and the moves
// (count + 16 bits each: from | to << 6 | promotion << 12), the opponent's first.
constexpr std::array<char, 8> kMagic{'K', 'B', 'Z', 'P', 'U', 'Z', '0', '1'};
constexpr std::size_t kTagsPerPuzzle = 8;
constexpr std::uint8_t kNoTag = 0xFF;
constexpr std::size_t kMaxThemes = 255;

constexpr std::uint64_t squareBit(int square) {
    return std::uint64_t{1} << static_cast<unsigned>(square);
}

/// Bytes of the file as text (the id, a theme's name).
std::string text(const std::uint8_t* bytes, std::size_t size) {
    return {bytes, bytes + size};
}

struct Header {
    std::array<char, 8> magic{};
    std::uint64_t count = 0;
    std::uint64_t themesAt = 0;
    std::uint64_t ratingsAt = 0;
    std::uint64_t tagsAt = 0;
    std::uint64_t offsetsAt = 0;
    std::uint64_t blobAt = 0;
    std::uint64_t blobSize = 0;
};

template <typename T> void append(std::string& out, T value) {
    std::array<char, sizeof(T)> bytes{};
    std::memcpy(bytes.data(), &value, sizeof(T));
    out.append(bytes.data(), bytes.size());
}

template <typename T> bool take(const std::uint8_t*& at, const std::uint8_t* end, T& value) {
    if (static_cast<std::size_t>(end - at) < sizeof(T)) {
        return false;
    }
    std::memcpy(&value, at, sizeof(T));
    at += sizeof(T);
    return true;
}

std::uint8_t pieceNibble(char c) {
    const bool black = std::islower(static_cast<unsigned char>(c)) != 0;
    std::uint8_t type = 0;
    switch (std::tolower(static_cast<unsigned char>(c))) {
    case 'p':
        type = 1;
        break;
    case 'n':
        type = 2;
        break;
    case 'b':
        type = 3;
        break;
    case 'r':
        type = 4;
        break;
    case 'q':
        type = 5;
        break;
    case 'k':
        type = 6;
        break;
    default:
        return 0;
    }
    return static_cast<std::uint8_t>((black ? 8U : 0U) | type);
}

char pieceChar(std::uint8_t nibble) {
    static constexpr std::array<char, 7> kChars{'?', 'p', 'n', 'b', 'r', 'q', 'k'};
    const auto type = static_cast<std::uint8_t>(static_cast<unsigned>(nibble) & 7U);
    if (type == 0 || type > 6) {
        return '?';
    }
    const char c = kChars[type];
    return (static_cast<unsigned>(nibble) & 8U) != 0
               ? c
               : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

std::vector<std::string_view> split(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    std::size_t begin = 0;
    while (true) {
        const std::size_t end = text.find(separator, begin);
        parts.push_back(text.substr(begin, end == std::string_view::npos ? end : end - begin));
        if (end == std::string_view::npos) {
            return parts;
        }
        begin = end + 1;
    }
}

std::optional<int> squareIndex(std::string_view name) {
    if (name.size() != 2 || name[0] < 'a' || name[0] > 'h' || name[1] < '1' || name[1] > '8') {
        return std::nullopt;
    }
    return ((name[1] - '1') * 8) + (name[0] - 'a');
}

/// A puzzle line as a record, straight from its text (no move generation: the import stays
/// fast; a puzzle that doesn't play is skipped when it's picked).
struct Encoded {
    std::string record;
    int rating = 0;
    std::vector<std::string_view> themes;
};

std::optional<Encoded> encodeLine(std::string_view line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.remove_suffix(1);
    }
    const std::vector<std::string_view> fields = split(line, ',');
    if (fields.size() < 8 || fields[0].empty() || fields[0].size() > 32) {
        return std::nullopt;
    }
    Encoded encoded;
    if (std::from_chars(fields[3].data(), fields[3].data() + fields[3].size(), encoded.rating).ec !=
            std::errc() ||
        encoded.rating < 0 || encoded.rating > 65535) {
        return std::nullopt;
    }
    const std::vector<std::string_view> fen = split(fields[1], ' ');
    if (fen.size() < 4) {
        return std::nullopt;
    }
    // The board.
    std::uint64_t occupied = 0;
    std::array<std::uint8_t, 64> pieces{};
    const std::vector<std::string_view> ranks = split(fen[0], '/');
    if (ranks.size() != 8) {
        return std::nullopt;
    }
    for (int r = 0; r < 8; ++r) {
        int file = 0;
        for (const char c : ranks[static_cast<std::size_t>(r)]) {
            if (c >= '1' && c <= '8') {
                file += c - '0';
                continue;
            }
            const std::uint8_t nibble = pieceNibble(c);
            if (nibble == 0 || file > 7) {
                return std::nullopt;
            }
            const int square = ((7 - r) * 8) + file;
            occupied |= squareBit(square);
            pieces[static_cast<std::size_t>(square)] = nibble;
            ++file;
        }
        if (file != 8) {
            return std::nullopt;
        }
    }
    std::string& out = encoded.record;
    append(out, occupied);
    std::uint8_t pending = 0;
    bool half = false;
    for (int square = 0; square < 64; ++square) {
        if ((occupied & squareBit(square)) == 0) {
            continue;
        }
        const std::uint8_t nibble = pieces[static_cast<std::size_t>(square)];
        if (!half) {
            pending = nibble;
        } else {
            out += static_cast<char>(static_cast<unsigned>(pending) |
                                     (static_cast<unsigned>(nibble) << 4U));
        }
        half = !half;
    }
    if (half) {
        out += static_cast<char>(pending);
    }
    // Side, castling, en passant, move number.
    unsigned flags = fen[1] == "b" ? 1U : 0U;
    for (const char c : fen[2]) {
        switch (c) {
        case 'K':
            flags |= 2U;
            break;
        case 'Q':
            flags |= 4U;
            break;
        case 'k':
            flags |= 8U;
            break;
        case 'q':
            flags |= 16U;
            break;
        default:
            break;
        }
    }
    out += static_cast<char>(flags);
    const auto ep = squareIndex(fen[3]);
    out += static_cast<char>(ep ? static_cast<std::uint8_t>(*ep) : std::uint8_t{0xFF});
    int fullmove = 1;
    if (fen.size() >= 6) {
        (void)std::from_chars(fen[5].data(), fen[5].data() + fen[5].size(), fullmove);
    }
    append(out, static_cast<std::uint16_t>(std::clamp(fullmove, 1, 65535)));
    // Id and moves.
    out += static_cast<char>(fields[0].size());
    out += fields[0];
    std::vector<std::uint16_t> moves;
    for (const std::string_view uci : split(fields[2], ' ')) {
        if (uci.empty()) {
            continue;
        }
        const auto move = core::Move::fromUci(uci);
        if (!move || moves.size() == 255) {
            return std::nullopt;
        }
        moves.push_back(
            static_cast<std::uint16_t>(static_cast<unsigned>(move->from.index()) |
                                       (static_cast<unsigned>(move->to.index()) << 6U) |
                                       (static_cast<unsigned>(move->promotion) << 12U)));
    }
    if (moves.size() < 2) {
        return std::nullopt;
    }
    out += static_cast<char>(moves.size());
    for (const std::uint16_t move : moves) {
        append(out, move);
    }
    for (const std::string_view theme : split(fields[7], ' ')) {
        if (!theme.empty()) {
            encoded.themes.push_back(theme);
        }
    }
    return encoded;
}

std::uint64_t alignUp(std::uint64_t value) {
    return (value + 7) & ~std::uint64_t{7};
}

/// A file written through a buffer; false once anything failed.
class Output {
public:
    explicit Output(const std::string& path) : m_file(std::fopen(path.c_str(), "wb")) {}
    ~Output() {
        if (m_file != nullptr) {
            std::fclose(m_file);
        }
    }
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;
    Output(Output&&) = delete;
    Output& operator=(Output&&) = delete;

    [[nodiscard]] bool ok() const { return m_file != nullptr && m_ok; }
    [[nodiscard]] std::uint64_t written() const { return m_written; }
    void write(const void* data, std::size_t size) {
        if (m_file == nullptr || size == 0) {
            return;
        }
        m_ok = m_ok && std::fwrite(data, 1, size, m_file) == size;
        m_written += size;
    }
    void pad() {
        static constexpr std::array<char, 8> kZeros{};
        write(kZeros.data(), alignUp(m_written) - m_written);
    }
    bool seekStart() { return m_file != nullptr && std::fseek(m_file, 0, SEEK_SET) == 0; }
    bool close() {
        const bool closed = m_file != nullptr && std::fclose(m_file) == 0;
        m_file = nullptr;
        return closed && m_ok;
    }

private:
    std::FILE* m_file;
    bool m_ok = true;
    std::uint64_t m_written = 0;
};

/// A read-only mapping of a whole file.
struct Mapping {
    void* data = nullptr;
    std::size_t size = 0;
    bool open(const std::string& path) {
        const int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) {
            return false;
        }
        struct stat status{};
        const bool sized = fstat(fd, &status) == 0;
        size = sized ? static_cast<std::size_t>(status.st_size) : 0;
        if (sized && size > 0) {
            void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
            data = mapped == MAP_FAILED ? nullptr : mapped;
        }
        ::close(fd);
        return sized && (size == 0 || data != nullptr);
    }
    void close() {
        if (data != nullptr) {
            munmap(data, size);
            data = nullptr;
        }
    }
};

} // namespace

// ---- PuzzleDb ----------------------------------------------------------------------------------

std::unique_ptr<PuzzleDb> PuzzleDb::open(const std::string& path, std::string* error) {
    const auto fail = [&](const char* message) -> std::unique_ptr<PuzzleDb> {
        if (error != nullptr) {
            *error = message;
        }
        return nullptr;
    };
    Mapping mapping;
    if (!mapping.open(path) || mapping.size < sizeof(Header)) {
        mapping.close();
        return fail("The puzzle file can't be read.");
    }
    std::unique_ptr<PuzzleDb> db(new PuzzleDb);
    db->m_data = mapping.data;
    db->m_size = mapping.size;
    const auto* bytes = static_cast<const std::uint8_t*>(mapping.data);
    Header header;
    std::memcpy(&header, bytes, sizeof(Header));
    const std::uint64_t size = mapping.size;
    const std::uint64_t count = header.count;
    if (header.magic != kMagic || header.themesAt > size || header.ratingsAt + (count * 2) > size ||
        header.tagsAt + (count * kTagsPerPuzzle) > size || header.offsetsAt + (count * 4) > size ||
        header.blobAt + header.blobSize > size) {
        return fail("This isn't a puzzle file.");
    }
    db->m_count = count;
    // The sections start at multiples of 8, so they're arrays of their type.
    if (header.ratingsAt % 8 != 0 || header.offsetsAt % 8 != 0) {
        return fail("This isn't a puzzle file.");
    }
    db->m_ratings = static_cast<const std::uint16_t*>(mapping.data) + (header.ratingsAt / 2);
    db->m_tags = bytes + header.tagsAt;
    db->m_offsets = static_cast<const std::uint32_t*>(mapping.data) + (header.offsetsAt / 4);
    db->m_blob = bytes + header.blobAt;
    db->m_blobSize = header.blobSize;
    const std::uint8_t* at = bytes + header.themesAt;
    const std::uint8_t* end = bytes + header.ratingsAt;
    std::uint32_t themes = 0;
    if (!take(at, end, themes)) {
        return fail("This isn't a puzzle file.");
    }
    for (std::uint32_t i = 0; i < themes; ++i) {
        std::uint8_t length = 0;
        if (!take(at, end, length) || end - at < length) {
            return fail("This isn't a puzzle file.");
        }
        db->m_themes.push_back(text(at, length));
        at += length;
    }
    return db;
}

PuzzleDb::~PuzzleDb() {
    if (m_data != nullptr) {
        munmap(m_data, m_size);
    }
}

int PuzzleDb::rating(std::size_t index) const {
    return index < m_count ? m_ratings[index] : 0;
}

std::pair<int, int> PuzzleDb::ratingRange() const {
    return m_count == 0 ? std::pair{0, 0}
                        : std::pair<int, int>{m_ratings[0], m_ratings[m_count - 1]};
}

std::pair<std::size_t, std::size_t> PuzzleDb::range(int low, int high) const {
    const std::uint16_t* begin = m_ratings;
    const std::uint16_t* end = m_ratings + m_count;
    const auto* first = std::lower_bound(begin, end, std::clamp(low, 0, 65535));
    const auto* last = std::upper_bound(begin, end, std::clamp(high, 0, 65535));
    return {static_cast<std::size_t>(first - begin),
            static_cast<std::size_t>(std::max(first, last) - begin)};
}

std::size_t PuzzleDb::count(int low, int high) const {
    const auto [first, last] = range(low, high);
    return last - first;
}

bool PuzzleDb::hasTheme(std::size_t index, int theme) const {
    if (theme < 0) {
        return true;
    }
    const std::uint8_t* tags = m_tags + (index * kTagsPerPuzzle);
    return std::find(tags, tags + kTagsPerPuzzle, static_cast<std::uint8_t>(theme)) !=
           tags + kTagsPerPuzzle;
}

std::optional<std::size_t> PuzzleDb::pick(int low, int high, int theme,
                                          std::mt19937_64& random) const {
    const auto [first, last] = range(low, high);
    if (first >= last) {
        return std::nullopt;
    }
    std::uniform_int_distribution<std::size_t> any(first, last - 1);
    // Most themes are common: a few random tries find one.
    for (int attempt = 0; attempt < 64; ++attempt) {
        const std::size_t index = any(random);
        if (hasTheme(index, theme)) {
            return index;
        }
    }
    // A rare one: look through the range from a random place.
    const std::size_t from = any(random);
    for (std::size_t step = 0; step < last - first; ++step) {
        const std::size_t index = first + ((from - first + step) % (last - first));
        if (hasTheme(index, theme)) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<Puzzle> PuzzleDb::puzzle(std::size_t index) const {
    if (index >= m_count || m_offsets[index] >= m_blobSize) {
        return std::nullopt;
    }
    const std::uint8_t* at = m_blob + m_offsets[index];
    const std::uint8_t* end = m_blob + m_blobSize;
    std::uint64_t occupied = 0;
    if (!take(at, end, occupied)) {
        return std::nullopt;
    }
    const auto pieces = static_cast<std::size_t>(std::popcount(occupied));
    if (static_cast<std::size_t>(end - at) < (pieces + 1) / 2) {
        return std::nullopt;
    }
    std::array<char, 64> board{};
    std::size_t n = 0;
    for (int square = 0; square < 64; ++square) {
        if ((occupied & squareBit(square)) == 0) {
            continue;
        }
        const std::uint8_t byte = at[n / 2];
        board[static_cast<std::size_t>(square)] = pieceChar(static_cast<std::uint8_t>(
            (n % 2 == 0 ? static_cast<unsigned>(byte) : static_cast<unsigned>(byte) >> 4U) &
            0x0FU));
        ++n;
    }
    at += (pieces + 1) / 2;
    std::uint8_t flags = 0;
    std::uint8_t ep = 0;
    std::uint16_t fullmove = 1;
    std::uint8_t idLength = 0;
    if (!take(at, end, flags) || !take(at, end, ep) || !take(at, end, fullmove) ||
        !take(at, end, idLength) || end - at < idLength) {
        return std::nullopt;
    }
    std::string fen;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            const char c =
                board[(static_cast<std::size_t>(rank) * 8) + static_cast<std::size_t>(file)];
            if (c == 0) {
                ++empty;
                continue;
            }
            if (empty > 0) {
                fen += static_cast<char>('0' + empty);
                empty = 0;
            }
            fen += c;
        }
        if (empty > 0) {
            fen += static_cast<char>('0' + empty);
        }
        if (rank > 0) {
            fen += '/';
        }
    }
    fen += (static_cast<unsigned>(flags) & 1U) != 0 ? " b " : " w ";
    std::string castling;
    for (const auto& [bit, c] :
         {std::pair{2U, 'K'}, std::pair{4U, 'Q'}, std::pair{8U, 'k'}, std::pair{16U, 'q'}}) {
        if ((static_cast<unsigned>(flags) & bit) != 0) {
            castling += c;
        }
    }
    fen += castling.empty() ? std::string("-") : castling;
    fen += ' ';
    fen += ep < 64 ? core::Square::fromIndex(ep).name() : std::string("-");
    fen += " 0 " + std::to_string(fullmove);
    Puzzle puzzle;
    puzzle.id = text(at, idLength);
    at += idLength;
    std::uint8_t moves = 0;
    if (!take(at, end, moves)) {
        return std::nullopt;
    }
    auto start = core::Position::fromFen(fen);
    if (!start) {
        return std::nullopt;
    }
    puzzle.game = core::Game(*start);
    puzzle.opponentFirst = true;
    puzzle.rating = m_ratings[index];
    core::Position position = *start;
    core::NodeId node = core::Game::root();
    for (std::uint8_t i = 0; i < moves; ++i) {
        std::uint16_t packed = 0;
        if (!take(at, end, packed)) {
            return std::nullopt;
        }
        const unsigned bits = packed;
        const core::Move move{core::Square::fromIndex(static_cast<int>(bits & 63U)),
                              core::Square::fromIndex(static_cast<int>((bits >> 6U) & 63U)),
                              static_cast<core::PieceType>((bits >> 12U) & 7U)};
        if (!position.isLegal(move)) {
            return std::nullopt;
        }
        node = puzzle.game.addMove(node, position, move);
        position.play(move);
    }
    const std::uint8_t* tags = m_tags + (index * kTagsPerPuzzle);
    for (std::size_t i = 0; i < kTagsPerPuzzle && tags[i] != kNoTag; ++i) {
        if (tags[i] < m_themes.size()) {
            puzzle.themes.push_back(m_themes[tags[i]]);
        }
    }
    return puzzle;
}

std::optional<std::size_t> PuzzleDb::find(std::string_view id) const {
    for (std::size_t index = 0; index < m_count; ++index) {
        const std::uint8_t* at = m_blob + m_offsets[index];
        const std::uint8_t* end = m_blob + m_blobSize;
        std::uint64_t occupied = 0;
        if (!take(at, end, occupied)) {
            continue;
        }
        at += ((static_cast<std::size_t>(std::popcount(occupied)) + 1) / 2) + 4;
        if (at >= end) {
            continue;
        }
        const std::uint8_t length = *at++;
        if (end - at >= length &&
            std::equal(at, at + length, id.begin(), id.end(),
                       [](std::uint8_t a, char b) { return static_cast<char>(a) == b; })) {
            return index;
        }
    }
    return std::nullopt;
}

// ---- Import ------------------------------------------------------------------------------------

PuzzleImportResult importLichess(const std::function<std::size_t(char*, std::size_t)>& read,
                                 const std::string& path, const PuzzleImportOptions& options) {
    PuzzleImportResult result;
    const std::string part = path + ".part";
    const std::string blobPath = path + ".records";
    const auto cleanUp = [&] {
        std::remove(part.c_str());
        std::remove(blobPath.c_str());
    };
    const auto cancelled = [&] {
        return options.cancel != nullptr && options.cancel->load(std::memory_order_relaxed);
    };

    // 1. The records in the input's order, to a scratch file; ratings and themes in memory.
    std::vector<std::uint16_t> ratings;
    std::vector<std::uint8_t> tags;
    std::vector<std::uint64_t> offsets;
    std::map<std::string, std::uint8_t, std::less<>> themeIds;
    std::vector<std::string> themes;
    PuzzleImportProgress progress;
    {
        Output records(blobPath);
        if (!records.ok()) {
            result.error = "Couldn't write the puzzle file.";
            return result;
        }
        std::string pending; // a line not yet complete
        const auto takeLine = [&](std::string_view line) {
            if (line.empty() || line.starts_with("PuzzleId")) {
                return;
            }
            auto encoded = encodeLine(line);
            if (!encoded) {
                ++result.skipped;
                return;
            }
            offsets.push_back(records.written());
            records.write(encoded->record.data(), encoded->record.size());
            ratings.push_back(static_cast<std::uint16_t>(encoded->rating));
            std::array<std::uint8_t, kTagsPerPuzzle> ids{};
            ids.fill(kNoTag);
            std::size_t n = 0;
            for (const std::string_view theme : encoded->themes) {
                auto it = themeIds.find(theme);
                if (it == themeIds.end()) {
                    if (themes.size() >= kMaxThemes) {
                        continue;
                    }
                    it = themeIds.emplace(std::string(theme), themes.size()).first;
                    themes.emplace_back(theme);
                }
                if (n < kTagsPerPuzzle) {
                    ids[n++] = it->second;
                }
            }
            tags.insert(tags.end(), ids.begin(), ids.end());
            ++progress.puzzles;
            if (options.progress && progress.puzzles % 65536 == 0) {
                options.progress(progress);
            }
        };
        const auto takeText = [&](const char* data, std::size_t size) {
            pending.append(data, size);
            std::size_t begin = 0;
            while (true) {
                const std::size_t end = pending.find('\n', begin);
                if (end == std::string::npos) {
                    break;
                }
                takeLine(std::string_view(pending).substr(begin, end - begin));
                begin = end + 1;
            }
            pending.erase(0, begin);
        };

        std::vector<char> input(std::size_t{1} << 20U);
        std::vector<char> output(ZSTD_DStreamOutSize());
        ZSTD_DCtx* zstd = nullptr;
        bool first = true;
        bool zstdEnded = true; // a frame was finished (or none started)
        while (true) {
            if (cancelled()) {
                ZSTD_freeDCtx(zstd);
                records.close();
                cleanUp();
                result.cancelled = true;
                return result;
            }
            const std::size_t got = read(input.data(), input.size());
            if (got == 0) {
                break;
            }
            progress.bytes += got;
            if (first) {
                first = false;
                // A zstd frame (28 B5 2F FD), or a skippable one (5? 2A 4D 18) as Lichess's
                // file begins with.
                const auto byte = [&](std::size_t i) -> unsigned {
                    return static_cast<unsigned char>(input[i]);
                };
                const bool frame = got >= 4 && byte(0) == 0x28 && byte(1) == 0xB5 &&
                                   byte(2) == 0x2F && byte(3) == 0xFD;
                const bool skippable = got >= 4 && (byte(0) & 0xF0U) == 0x50 && byte(1) == 0x2A &&
                                       byte(2) == 0x4D && byte(3) == 0x18;
                if (frame || skippable) {
                    zstd = ZSTD_createDCtx();
                }
            }
            if (zstd == nullptr) {
                takeText(input.data(), got);
                continue;
            }
            ZSTD_inBuffer in{input.data(), got, 0};
            while (in.pos < in.size) {
                ZSTD_outBuffer out{output.data(), output.size(), 0};
                const std::size_t status = ZSTD_decompressStream(zstd, &out, &in);
                if (ZSTD_isError(status) != 0) {
                    ZSTD_freeDCtx(zstd);
                    records.close();
                    cleanUp();
                    result.error = "The puzzle download is damaged.";
                    return result;
                }
                zstdEnded = status == 0;
                takeText(output.data(), out.pos);
            }
        }
        ZSTD_freeDCtx(zstd);
        if (!zstdEnded) {
            records.close();
            cleanUp();
            result.error = "The puzzle download is incomplete.";
            return result;
        }
        if (!pending.empty()) {
            takeLine(pending);
        }
        if (!records.close()) {
            cleanUp();
            result.error = "Couldn't write the puzzle file (is the disk full?).";
            return result;
        }
    }
    if (ratings.empty()) {
        cleanUp();
        result.error = "There are no puzzles in this file.";
        return result;
    }
    if (offsets.back() > 0xFFFFFFF0U) {
        cleanUp();
        result.error = "The puzzle file is too large.";
        return result;
    }

    // 2. Sorted by rating (in the input's order within a rating), into the file.
    std::vector<std::uint32_t> order(ratings.size());
    std::iota(order.begin(), order.end(), 0U);
    std::stable_sort(order.begin(), order.end(),
                     [&](std::uint32_t a, std::uint32_t b) { return ratings[a] < ratings[b]; });
    Mapping blob;
    if (!blob.open(blobPath)) {
        cleanUp();
        result.error = "Couldn't read back the puzzles.";
        return result;
    }
    const auto* blobBytes = static_cast<const std::uint8_t*>(blob.data);
    const auto recordSize = [&](std::uint32_t i) {
        const std::uint64_t next = i + 1 < offsets.size() ? offsets[i + 1] : blob.size;
        return static_cast<std::size_t>(next - offsets[i]);
    };

    Output file(part);
    Header header;
    header.magic = kMagic;
    header.count = ratings.size();
    file.write(&header, sizeof(header));
    header.themesAt = file.written();
    const auto themeCount = static_cast<std::uint32_t>(themes.size());
    file.write(&themeCount, sizeof(themeCount));
    for (const std::string& theme : themes) {
        const auto length = static_cast<std::uint8_t>(std::min<std::size_t>(theme.size(), 255));
        file.write(&length, 1);
        file.write(theme.data(), length);
    }
    file.pad();
    header.ratingsAt = file.written();
    {
        std::vector<std::uint16_t> sorted(order.size());
        for (std::size_t i = 0; i < order.size(); ++i) {
            sorted[i] = ratings[order[i]];
        }
        file.write(sorted.data(), sorted.size() * sizeof(std::uint16_t));
    }
    file.pad();
    header.tagsAt = file.written();
    for (const std::uint32_t i : order) {
        file.write(&tags[static_cast<std::size_t>(i) * kTagsPerPuzzle], kTagsPerPuzzle);
    }
    file.pad();
    header.offsetsAt = file.written();
    {
        std::vector<std::uint32_t> sortedOffsets(order.size());
        std::uint64_t at = 0;
        for (std::size_t i = 0; i < order.size(); ++i) {
            sortedOffsets[i] = static_cast<std::uint32_t>(at);
            at += recordSize(order[i]);
        }
        file.write(sortedOffsets.data(), sortedOffsets.size() * sizeof(std::uint32_t));
    }
    file.pad();
    header.blobAt = file.written();
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (i % 65536 == 0 && cancelled()) {
            blob.close();
            file.close();
            cleanUp();
            result.cancelled = true;
            return result;
        }
        file.write(blobBytes + offsets[order[i]], recordSize(order[i]));
    }
    header.blobSize = file.written() - header.blobAt;
    blob.close();
    const bool headerWritten = file.seekStart();
    file.write(&header, sizeof(header));
    if (!headerWritten || !file.close()) {
        cleanUp();
        result.error = "Couldn't write the puzzle file (is the disk full?).";
        return result;
    }
    std::remove(blobPath.c_str());
    if (std::rename(part.c_str(), path.c_str()) != 0) {
        cleanUp();
        result.error = "Couldn't write the puzzle file.";
        return result;
    }
    result.puzzles = ratings.size();
    if (options.progress) {
        options.progress(progress);
    }
    return result;
}

} // namespace kiebitz::puzzles
