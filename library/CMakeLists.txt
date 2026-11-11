# kiebitz_library: the library's cache of game indexes (SQLite). Like core and pgn, it must never
# depend on Qt; listing and watching the folder live in app/library.

add_library(kiebitz_library
    src/file_kind.cpp
    src/index_cache.cpp
    src/training_stats.cpp)
add_library(kiebitz::library ALIAS kiebitz_library)

target_include_directories(kiebitz_library PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(kiebitz_library PUBLIC kiebitz::pgn PRIVATE thirdparty::sqlite)
kiebitz_set_warnings(kiebitz_library)
