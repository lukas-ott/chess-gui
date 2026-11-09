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

if(CHESSGUI_BUILD_TESTS)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
        URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
        SYSTEM)
    FetchContent_MakeAvailable(googletest)
endif()

if(CHESSGUI_BUILD_BENCH)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(benchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.9.4.tar.gz
        URL_HASH SHA256=b334658edd35efcf06a99d9be21e4e93e092bd5f95074c1673d5c8705d95c104
        SYSTEM)
    FetchContent_MakeAvailable(benchmark)
endif()
