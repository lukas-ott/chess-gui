find_package(Qt6 6.5 REQUIRED COMPONENTS Concurrent Network Svg Widgets)
qt_standard_project_setup()

# The UI as a library, so UI tests and benchmarks can use it without the app's main().
qt_add_library(kiebitz_ui STATIC
    MainWindow.cpp
    MainWindow.hpp
    MainWindowKeys.hpp
    MainWindowSetup.cpp
    Migration.cpp
    Migration.hpp
    board/BoardGeometry.cpp
    board/BoardGeometry.hpp
    board/BoardPage.cpp
    board/BoardPage.hpp
    board/BoardTheme.cpp
    board/BoardTheme.hpp
    board/BoardView.cpp
    board/BoardView.hpp
    board/EvalBar.cpp
    board/EvalBar.hpp
    board/PieceSet.cpp
    board/PieceSet.hpp
    databases/AddDatabaseDialog.cpp
    databases/AddDatabaseDialog.hpp
    databases/DatabaseManager.cpp
    databases/DatabaseManager.hpp
    databases/DatabasesView.cpp
    databases/DatabasesView.hpp
    databases/GamesModel.cpp
    databases/GamesModel.hpp
    document/GameDocument.cpp
    document/Documents.cpp
    document/Documents.hpp
    explorer/ExplorerPanel.cpp
    explorer/ExplorerPanel.hpp
    explorer/LichessExplorer.cpp
    explorer/LichessExplorer.hpp
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
    library/NewFileDialog.cpp
    library/NewFileDialog.hpp
    notation/NotationView.cpp
    notation/NotationView.hpp
    notation/MovesCard.cpp
    notation/MovesCard.hpp
    panels/GameInfoPanel.cpp
    panels/GameInfoPanel.hpp
    repertoire/RepertoireStore.cpp
    repertoire/RepertoireStore.hpp
    puzzles/LichessPuzzles.cpp
    puzzles/LichessPuzzles.hpp
    puzzles/PuzzleStats.cpp
    puzzles/PuzzleStats.hpp
    puzzles/PuzzlesView.cpp
    puzzles/PuzzlesView.hpp
    profiles/AddAccountDialog.cpp
    profiles/AddAccountDialog.hpp
    profiles/Fide.cpp
    profiles/Fide.hpp
    profiles/FideCard.cpp
    profiles/FideCard.hpp
    profiles/LichessLogin.cpp
    profiles/LichessLogin.hpp
    profiles/ProfileStore.cpp
    profiles/ProfileStore.hpp
    profiles/ProfilesView.cpp
    profiles/ProfilesView.hpp
    repertoire/PracticeStats.cpp
    repertoire/PracticeStats.hpp
    repertoire/Practice.cpp
    repertoire/Practice.hpp
    repertoire/GapSource.hpp
    repertoire/RepertoirePanel.cpp
    repertoire/RepertoirePanel.hpp
    settings/Settings.cpp
    settings/Settings.hpp
    settings/SettingsView.cpp
    settings/SettingsView.hpp
    settings/Shortcuts.cpp
    settings/Shortcuts.hpp
    settings/ShortcutsPage.cpp
    settings/ShortcutsPage.hpp
    sound/Sounds.cpp
    sound/Sounds.hpp
    style/AppStyle.cpp
    style/AppStyle.hpp
    style/Icons.cpp
    style/Icons.hpp
    style/Layout.hpp
    style/PageHeader.cpp
    style/PageHeader.hpp
    widgets/ActivityChart.cpp
    widgets/ActivityChart.hpp
    widgets/Card.hpp
    widgets/DateRange.cpp
    widgets/DateRange.hpp
    widgets/ElidedLabel.hpp
    widgets/EmptyState.cpp
    widgets/EmptyState.hpp
    widgets/Message.cpp
    widgets/Message.hpp
    widgets/NameDialog.cpp
    widgets/NameDialog.hpp
    widgets/ResultBar.cpp
    widgets/ResultBar.hpp
    widgets/SortCycle.hpp
    widgets/SortableItem.hpp
    widgets/Stepper.cpp
    widgets/Toggle.cpp
    widgets/Toggle.hpp
    util/WhenReady.hpp
    widgets/Stepper.hpp)
target_include_directories(kiebitz_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(kiebitz_ui PUBLIC Qt6::Widgets Qt6::Svg Qt6::Concurrent Qt6::Network
    kiebitz::pgn kiebitz::engine kiebitz::gamedb kiebitz::library kiebitz::repertoire
    kiebitz::puzzles)
if(APPLE)
    # The board's sounds play as macOS sound effects (no audio stream of our own).
    target_link_libraries(kiebitz_ui PUBLIC "-framework AudioToolbox" "-framework CoreFoundation")
endif()
kiebitz_set_warnings(kiebitz_ui)

# Piece SVGs are compiled into the binary (":/pieces/<set>/wK.svg"), so the app has no loose
# files to find at run time.
file(GLOB piece_svgs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/pieces/*/*.svg")
qt_add_resources(kiebitz_ui "pieces"
    PREFIX "/pieces"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/pieces"
    FILES ${piece_svgs})

# Opening names (lichess-org/chess-openings, CC0), read on first use by RepertoireStore.
qt_add_resources(kiebitz_ui "openings"
    PREFIX "/openings"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/openings"
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/openings/openings.tsv")

# The board's sounds: Lichess's sound sets by Enigmahack (AGPLv3+, see resources/sounds/LICENSE.md),
# as WAV files.
file(GLOB sound_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/sounds/*/*.wav")
qt_add_resources(kiebitz_ui "sounds"
    PREFIX "/sounds"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/sounds"
    FILES ${sound_files})

# The app icon (made from resources/icon/Kiebitz.svg by scripts/make_icon.sh).
set(kiebitz_icon "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/Kiebitz.icns")
set_source_files_properties("${kiebitz_icon}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
qt_add_executable(kiebitz MACOSX_BUNDLE main.cpp "${kiebitz_icon}")

set_target_properties(kiebitz PROPERTIES
    OUTPUT_NAME "Kiebitz"
    MACOSX_BUNDLE_BUNDLE_NAME "Kiebitz"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.lukasott.kiebitz"
    MACOSX_BUNDLE_ICON_FILE "Kiebitz.icns"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

target_link_libraries(kiebitz PRIVATE kiebitz_ui)
qt_add_resources(kiebitz "icon"
    PREFIX "/icon"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon"
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/Kiebitz.png")
kiebitz_set_warnings(kiebitz)
