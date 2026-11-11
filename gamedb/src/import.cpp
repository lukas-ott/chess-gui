#include "kiebitz/gamedb/import.hpp"

#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace kiebitz::gamedb {
namespace {

/// Games to read, as one block of text and where each game is in it.
struct Batch {
    std::uint64_t sequence = 0;
    std::string text;
    std::vector<std::pair<std::size_t, std::size_t>> games; ///< offset, length
    std::uint64_t bytes = 0; ///< input read up to the end of this batch
};

struct Scanned {
    std::vector<ScannedGame> games;
    std::uint64_t bytes = 0;
};

constexpr std::size_t kBatchGames = 2000;
constexpr std::size_t kBatchBytes = 4U << 20U;

} // namespace

ImportResult importPgn(DatabaseWriter& writer,
                       const std::function<std::size_t(char* buffer, std::size_t size)>& read,
                       const ImportOptions& options) {
    const unsigned threads =
        options.threads > 0 ? options.threads : std::max(1U, std::thread::hardware_concurrency());
    const auto cancelled = [&] {
        return options.cancel != nullptr && options.cancel->load(std::memory_order_relaxed);
    };

    std::mutex mutex;
    std::condition_variable work;          // a batch to read, or the end
    std::condition_variable room;          // the queues have room again
    std::condition_variable finished;      // a read batch for the writer
    std::deque<Batch> pending;             // batches to read
    std::map<std::uint64_t, Scanned> done; // read batches, by sequence
    bool inputEnded = false;
    bool stop = false;
    const std::size_t maxQueued = static_cast<std::size_t>(threads) * 3;

    // Readers: turn text into games.
    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (unsigned t = 0; t < threads; ++t) {
        workers.emplace_back([&] {
            while (true) {
                Batch batch;
                {
                    std::unique_lock lock(mutex);
                    work.wait(lock, [&] { return !pending.empty() || inputEnded || stop; });
                    if (stop || pending.empty()) {
                        return;
                    }
                    batch = std::move(pending.front());
                    pending.pop_front();
                    room.notify_all();
                }
                Scanned scanned;
                scanned.bytes = batch.bytes;
                scanned.games.reserve(batch.games.size());
                for (const auto& [offset, length] : batch.games) {
                    scanned.games.push_back(
                        scanGame(std::string_view(batch.text).substr(offset, length)));
                }
                const std::scoped_lock lock(mutex);
                done.emplace(batch.sequence, std::move(scanned));
                finished.notify_all();
            }
        });
    }

    // The writer: stores the games in the order of the input.
    ImportResult result;
    std::uint64_t sequences = 0; // batches handed out (known once the input ended)
    bool allHandedOut = false;
    std::thread storer([&] {
        std::uint64_t next = 0;
        while (true) {
            Scanned scanned;
            {
                std::unique_lock lock(mutex);
                finished.wait(lock, [&] {
                    return stop || done.contains(next) || (allHandedOut && next == sequences);
                });
                if (stop || !done.contains(next)) {
                    return;
                }
                scanned = std::move(done.at(next));
                done.erase(next);
                room.notify_all();
            }
            for (const ScannedGame& game : scanned.games) {
                if (game.skipped) {
                    ++result.counts.skipped;
                    continue;
                }
                writer.add(game);
                ++result.counts.games;
                if (game.damaged) {
                    ++result.counts.damaged;
                }
            }
            result.counts.bytes = scanned.bytes;
            if (options.progress) {
                options.progress(result.counts);
            }
            ++next;
        }
    });

    // This thread: read the input and cut it into batches of games.
    GameSplitter splitter;
    Batch batch;
    std::uint64_t bytes = 0;
    const auto handOut = [&] {
        if (batch.games.empty()) {
            return;
        }
        batch.sequence = sequences++;
        batch.bytes = bytes;
        std::unique_lock lock(mutex);
        room.wait(lock, [&] { return stop || (pending.size() + done.size()) < maxQueued; });
        pending.push_back(std::move(batch));
        batch = Batch();
        work.notify_one();
    };
    const GameSplitter::Sink sink = [&](std::string_view game) {
        batch.games.emplace_back(batch.text.size(), game.size());
        batch.text.append(game);
        if (batch.games.size() >= kBatchGames || batch.text.size() >= kBatchBytes) {
            handOut();
        }
    };
    std::vector<char> buffer(1U << 20U);
    while (!cancelled()) {
        const std::size_t got = read(buffer.data(), buffer.size());
        if (got == 0) {
            break;
        }
        bytes += got;
        splitter.feed(std::string_view(buffer.data(), got), sink);
    }
    if (cancelled()) {
        result.cancelled = true;
        const std::scoped_lock lock(mutex);
        stop = true;
    } else {
        splitter.finish(sink);
        handOut();
    }
    {
        const std::scoped_lock lock(mutex);
        inputEnded = true;
        allHandedOut = true;
        work.notify_all();
        finished.notify_all();
        room.notify_all();
    }
    for (auto& worker : workers) {
        worker.join();
    }
    {
        const std::scoped_lock lock(mutex);
        finished.notify_all();
    }
    storer.join();
    return result;
}

} // namespace kiebitz::gamedb
