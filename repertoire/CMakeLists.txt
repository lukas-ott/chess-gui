# kiebitz_repertoire: repertoire trees (games merged by position), book checks and opening
# names. Like core and pgn, it must never depend on Qt; the Repertoire page lives in app/.

add_library(kiebitz_repertoire
    src/drill.cpp
    src/gaps.cpp
    src/openings.cpp
    src/summary.cpp
    src/theory.cpp
    src/tree.cpp)
add_library(kiebitz::repertoire ALIAS kiebitz_repertoire)

target_include_directories(kiebitz_repertoire PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(kiebitz_repertoire PUBLIC kiebitz::core)
kiebitz_set_warnings(kiebitz_repertoire)
