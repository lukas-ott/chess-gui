# kiebitz_gamedb: large game databases (millions of games): a compact store, a streaming PGN
# import and an opening explorer over them. Like core, it must never depend on Qt; the
# Databases page lives in app/.

find_package(Threads REQUIRED)

add_library(kiebitz_gamedb
    src/codec.cpp
    src/codec.hpp
    src/database.cpp
    src/import.cpp
    src/scan.cpp)
add_library(kiebitz::gamedb ALIAS kiebitz_gamedb)

target_include_directories(kiebitz_gamedb PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(kiebitz_gamedb
    PUBLIC kiebitz::core
    PRIVATE kiebitz::sql thirdparty::chess Threads::Threads)
kiebitz_set_warnings(kiebitz_gamedb)
