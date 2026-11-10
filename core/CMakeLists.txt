# kiebitz_core: pure C++ chess logic. Must never depend on Qt.

add_library(kiebitz_core
    src/annotations.cpp
    src/fen.cpp
    src/fen.hpp
    src/game.cpp
    src/perft.cpp
    src/position.cpp
    src/types.cpp
    src/version.cpp)
add_library(kiebitz::core ALIAS kiebitz_core)

target_include_directories(kiebitz_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(kiebitz_core PRIVATE thirdparty::chess)
target_compile_definitions(kiebitz_core PRIVATE KIEBITZ_VERSION="${PROJECT_VERSION}")
kiebitz_set_warnings(kiebitz_core)
