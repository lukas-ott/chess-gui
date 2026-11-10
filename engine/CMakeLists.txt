# chessgui_engine: the UCI protocol (reading what engines print, scores, lines in SAN) and
# choosing an engine download. Like core and pgn, it must never depend on Qt; running the engine
# process and the network live in app/engine.

add_library(chessgui_engine
    src/release.cpp
    src/score.cpp
    src/uci.cpp)
add_library(chessgui::engine ALIAS chessgui_engine)

target_include_directories(chessgui_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(chessgui_engine PUBLIC chessgui::core)
chessgui_set_warnings(chessgui_engine)
