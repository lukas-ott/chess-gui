# chessgui_pgn: reading and writing PGN files. Like core, it must never depend on Qt.

add_library(chessgui_pgn
    src/reader.cpp
    src/tokenizer.cpp
    src/tokenizer.hpp
    src/writer.cpp)
add_library(chessgui::pgn ALIAS chessgui_pgn)

target_include_directories(chessgui_pgn PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(chessgui_pgn PUBLIC chessgui::core)
chessgui_set_warnings(chessgui_pgn)
