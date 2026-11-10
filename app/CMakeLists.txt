find_package(Qt6 6.5 REQUIRED COMPONENTS Concurrent Network Svg Widgets)
qt_standard_project_setup()

# The UI as a library, so UI tests and benchmarks can use it without the app's main().
qt_add_library(chessgui_ui STATIC
    MainWindow.cpp
    MainWindow.hpp
    board/BoardGeometry.cpp
    board/BoardGeometry.hpp
    board/BoardTheme.cpp
    board/BoardTheme.hpp
    board/BoardView.cpp
    board/BoardView.hpp
    board/EvalBar.cpp
    board/EvalBar.hpp
    board/PieceSet.cpp
    board/PieceSet.hpp
    document/GameDocument.cpp
    document/GameDocument.hpp
    document/PgnIo.cpp
    document/PgnIo.hpp
    engine/AnalysisSource.hpp
    engine/CloudEval.cpp
    engine/CloudEval.hpp
    engine/EnginePanel.cpp
    engine/EnginePanel.hpp
    engine/EngineRegistry.cpp
    engine/EngineRegistry.hpp
    engine/EngineSection.cpp
    engine/EngineSection.hpp
    engine/EngineSettings.cpp
    engine/EngineSettings.hpp
    engine/EnginesView.cpp
    engine/EnginesView.hpp
    engine/StockfishDownload.cpp
    engine/StockfishDownload.hpp
    engine/UciEngine.cpp
    engine/UciEngine.hpp
    library/Library.cpp
    library/Library.hpp
    library/LibraryView.cpp
    library/LibraryView.hpp
    notation/NotationView.cpp
    notation/NotationView.hpp
    panels/GameInfoPanel.cpp
    panels/GameInfoPanel.hpp
    style/AppStyle.cpp
    style/AppStyle.hpp
    style/Icons.cpp
    style/Icons.hpp)
target_include_directories(chessgui_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(chessgui_ui PUBLIC Qt6::Widgets Qt6::Svg Qt6::Concurrent Qt6::Network
    chessgui::pgn chessgui::engine)
chessgui_set_warnings(chessgui_ui)

# Piece SVGs are compiled into the binary (":/pieces/<set>/wK.svg"), so the app has no loose
# files to find at run time.
file(GLOB piece_svgs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/pieces/*/*.svg")
qt_add_resources(chessgui_ui "pieces"
    PREFIX "/pieces"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/pieces"
    FILES ${piece_svgs})

qt_add_executable(chessgui MACOSX_BUNDLE main.cpp)

set_target_properties(chessgui PROPERTIES
    OUTPUT_NAME "ChessGui"
    MACOSX_BUNDLE_BUNDLE_NAME "ChessGui"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.lukasott.chessgui"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

target_link_libraries(chessgui PRIVATE chessgui_ui)
chessgui_set_warnings(chessgui)
