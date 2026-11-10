// A tiny UCI engine for the UI tests: it answers at once and predictably, so tests can check
// exact lines. Line k (1-based) is the k-th legal move in UCI order, then the reply that sorts
// first; its score is 50 - 10·(k-1) centipawns for the side to move. It reports depths 1 to 3
// and then waits for "stop", like "go infinite".

#include "kiebitz/core/position.hpp"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace {

using kiebitz::core::Move;
using kiebitz::core::Position;

std::vector<Move> sortedMoves(const Position& position) {
    auto moves = position.legalMoves();
    std::sort(moves.begin(), moves.end(),
              [](const Move& a, const Move& b) { return a.uci() < b.uci(); });
    return moves;
}

void say(const std::string& line) {
    std::cout << line << '\n' << std::flush;
}

int run() {
    Position position;
    int multiPv = 1;
    bool searching = false;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "uci") {
            say("id name FakeFish 1");
            say("id author the tests");
            say("option name Threads type spin default 1 min 1 max 64");
            say("option name Hash type spin default 16 min 1 max 1024");
            say("option name MultiPV type spin default 1 min 1 max 5");
            say("uciok");
        } else if (line == "isready") {
            say("readyok");
        } else if (line.starts_with("setoption name MultiPV value ")) {
            multiPv = std::stoi(line.substr(29));
        } else if (line.starts_with("position fen ")) {
            position = Position::fromFen(line.substr(13)).value();
        } else if (line.starts_with("go")) {
            const auto moves = sortedMoves(position);
            if (moves.empty()) {
                say("bestmove (none)");
                continue;
            }
            searching = true;
            for (int depth = 1; depth <= 3; ++depth) {
                for (int k = 1; k <= std::min<int>(multiPv, static_cast<int>(moves.size())); ++k) {
                    const Move& move = moves[static_cast<std::size_t>(k - 1)];
                    Position after = position;
                    after.play(move);
                    std::string pv = move.uci();
                    if (const auto replies = sortedMoves(after); !replies.empty()) {
                        pv += " " + replies.front().uci();
                    }
                    say("info depth " + std::to_string(depth) + " multipv " + std::to_string(k) +
                        " score cp " + std::to_string(50 - (10 * (k - 1))) + " nodes " +
                        std::to_string(1000 * depth) + " nps 100000 pv " + pv);
                }
            }
        } else if (line == "stop") {
            if (searching) {
                searching = false;
                say("bestmove " + sortedMoves(position).front().uci());
            }
        } else if (line == "quit") {
            return 0;
        }
    }
    return 0;
}
} // namespace

int main() {
    try {
        return run();
    } catch (...) {
        return 1;
    }
}
