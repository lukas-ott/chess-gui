cmake_minimum_required(VERSION 3.25)

# Oldest macOS the app runs on (CMAKE_OSX_DEPLOYMENT_TARGET) is deliberately not set here, so it
# defaults to the macOS of the Mac doing the build. That matches Homebrew's Qt, whose libraries
# are built for the machine's macOS (QtSvg on macOS 26 needs 26), so the app never claims to run
# somewhere its Qt can't. To support older macOS (the spec aims for 13), build against the
# official Qt installer and pass e.g. -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0.

project(Kiebitz
    VERSION 0.1.0
    DESCRIPTION "Fast native chess GUI for opening preparation"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(KIEBITZ_BUILD_APP "Build the Qt application" ON)
option(KIEBITZ_BUILD_TESTS "Build unit tests" ON)
option(KIEBITZ_BUILD_BENCH "Build benchmarks" OFF)
option(KIEBITZ_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(KIEBITZ_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerOptions)
include(Dependencies)

add_subdirectory(core)
add_subdirectory(pgn)
add_subdirectory(engine)
add_subdirectory(library)
add_subdirectory(repertoire)
add_subdirectory(gamedb)

if(KIEBITZ_BUILD_APP)
    add_subdirectory(app)
endif()

if(KIEBITZ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(KIEBITZ_BUILD_BENCH)
    add_subdirectory(bench)
endif()
