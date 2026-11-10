# kiebitz_engine: the UCI protocol (reading what engines print, scores, lines in SAN) and
# choosing an engine download. Like core and pgn, it must never depend on Qt; running the engine
# process and the network live in app/engine.

add_library(kiebitz_engine
    src/release.cpp
    src/score.cpp
    src/uci.cpp)
add_library(kiebitz::engine ALIAS kiebitz_engine)

target_include_directories(kiebitz_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(kiebitz_engine PUBLIC kiebitz::core)
kiebitz_set_warnings(kiebitz_engine)
