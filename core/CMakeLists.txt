# chessgui_core: pure C++ chess logic. Must never depend on Qt.

add_library(chessgui_core
    src/annotations.cpp
    src/fen.cpp
    src/fen.hpp
    src/game.cpp
    src/perft.cpp
    src/position.cpp
    src/types.cpp
    src/version.cpp)
add_library(chessgui::core ALIAS chessgui_core)

target_include_directories(chessgui_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(chessgui_core PRIVATE thirdparty::chess)
target_compile_definitions(chessgui_core PRIVATE CHESSGUI_VERSION="${PROJECT_VERSION}")
chessgui_set_warnings(chessgui_core)
