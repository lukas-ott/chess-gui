# kiebitz_puzzles: tactics puzzles: solving one (the moves you must find, the opponent's
# answers), the Lichess puzzle database (a compact file of millions of puzzles, picked by
# rating and theme) and its import. Like core, it must never depend on Qt; the Puzzles page
# lives in app/.

add_library(kiebitz_puzzles
    src/history.cpp
    src/puzzle.cpp
    src/store.cpp)
add_library(kiebitz::puzzles ALIAS kiebitz_puzzles)

target_include_directories(kiebitz_puzzles PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(kiebitz_puzzles
    PUBLIC kiebitz::core
    PRIVATE thirdparty::zstd thirdparty::sqlite)
kiebitz_set_warnings(kiebitz_puzzles)
