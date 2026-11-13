# Third-party dependencies, pinned to exact versions and fetched at configure time.
# Qt is NOT fetched: it comes from your system install (Homebrew: `brew install qt`).
#
# Every archive is checked against a SHA-256 hash, so a changed or tampered download fails
# the configure step. When bumping a version, update its hash too:
#   curl -sSL <url> | shasum -a 256

include(FetchContent)

# chess-library (MIT, header-only): board representation, move generation, PGN parsing.
# The project has no release tags, so it is pinned to a commit on master.
FetchContent_Declare(chess_library
    URL https://github.com/Disservin/chess-library/archive/430266434aeb42f19bb5baa84597d671824889ff.tar.gz
    URL_HASH SHA256=a7b51ec63767224331310fc293c80cb6121cf3ac4546deb5a1208df95c5d8213
    SOURCE_SUBDIR do-not-add-subdirectory)  # header-only: we only need the sources, not its CMake project
FetchContent_MakeAvailable(chess_library)

add_library(chess_library INTERFACE)
add_library(thirdparty::chess ALIAS chess_library)
target_include_directories(chess_library SYSTEM INTERFACE "${chess_library_SOURCE_DIR}/include")

# SQLite (public domain): the library's cache of game indexes. The amalgamation is one C file,
# built here with the settings the cache needs (thread-safe, no extension loading).
FetchContent_Declare(sqlite
    URL https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip
    URL_HASH SHA256=1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032)
FetchContent_MakeAvailable(sqlite)

enable_language(C)
add_library(sqlite3 STATIC "${sqlite_SOURCE_DIR}/sqlite3.c")
add_library(thirdparty::sqlite ALIAS sqlite3)
target_include_directories(sqlite3 SYSTEM INTERFACE "${sqlite_SOURCE_DIR}")
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0 SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_OMIT_DEPRECATED SQLITE_OMIT_SHARED_CACHE)
set_target_properties(sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED)
target_link_libraries(sqlite3 PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

# zstd (BSD): reads the Lichess puzzle database, which comes zstd-compressed (macOS's own tar
# can't). Only the library's C sources are built, without its CMake project or assembly.
FetchContent_Declare(zstd
    URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
    URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
    SOURCE_SUBDIR do-not-add-subdirectory)
FetchContent_MakeAvailable(zstd)

file(GLOB KIEBITZ_ZSTD_SOURCES
    "${zstd_SOURCE_DIR}/lib/common/*.c"
    "${zstd_SOURCE_DIR}/lib/compress/*.c"
    "${zstd_SOURCE_DIR}/lib/decompress/*.c")
add_library(zstd STATIC ${KIEBITZ_ZSTD_SOURCES})
add_library(thirdparty::zstd ALIAS zstd)
target_include_directories(zstd SYSTEM INTERFACE "${zstd_SOURCE_DIR}/lib")
target_include_directories(zstd PRIVATE "${zstd_SOURCE_DIR}/lib" "${zstd_SOURCE_DIR}/lib/common")
target_compile_definitions(zstd PRIVATE ZSTD_DISABLE_ASM XXH_NAMESPACE=ZSTD_)
set_target_properties(zstd PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(KIEBITZ_BUILD_TESTS)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
        URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
        SYSTEM)
    FetchContent_MakeAvailable(googletest)
endif()

if(KIEBITZ_BUILD_BENCH)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(benchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.9.4.tar.gz
        URL_HASH SHA256=b334658edd35efcf06a99d9be21e4e93e092bd5f95074c1673d5c8705d95c104
        SYSTEM)
    FetchContent_MakeAvailable(benchmark)
endif()
