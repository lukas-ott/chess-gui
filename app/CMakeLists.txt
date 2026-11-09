find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

qt_add_executable(chessgui MACOSX_BUNDLE
    main.cpp
    MainWindow.cpp
    MainWindow.hpp
    BoardView.cpp
    BoardView.hpp)

set_target_properties(chessgui PROPERTIES
    OUTPUT_NAME "ChessGui"
    MACOSX_BUNDLE_BUNDLE_NAME "ChessGui"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.lukasott.chessgui"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

target_link_libraries(chessgui PRIVATE Qt6::Widgets chessgui::core)
chessgui_set_warnings(chessgui)
