# kiebitz_pgn: reading and writing PGN files. Like core, it must never depend on Qt.

add_library(kiebitz_pgn
    src/reader.cpp
    src/tokenizer.cpp
    src/tokenizer.hpp
    src/writer.cpp)
add_library(kiebitz::pgn ALIAS kiebitz_pgn)

target_include_directories(kiebitz_pgn PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(kiebitz_pgn PUBLIC kiebitz::core)
kiebitz_set_warnings(kiebitz_pgn)
