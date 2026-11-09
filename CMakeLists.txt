cmake_minimum_required(VERSION 3.25)

project(ChessGui
    VERSION 0.1.0
    DESCRIPTION "Fast native chess GUI for opening preparation"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CHESSGUI_BUILD_APP "Build the Qt application" ON)
option(CHESSGUI_BUILD_TESTS "Build unit tests" ON)
option(CHESSGUI_BUILD_BENCH "Build benchmarks" OFF)
option(CHESSGUI_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(CHESSGUI_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerOptions)
include(Dependencies)

add_subdirectory(core)

if(CHESSGUI_BUILD_APP)
    add_subdirectory(app)
endif()

if(CHESSGUI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(CHESSGUI_BUILD_BENCH)
    add_subdirectory(bench)
endif()
