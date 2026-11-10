include(GoogleTest)

add_executable(chessgui_tests
    test_annotations.cpp
    test_fen.cpp
    test_game.cpp
    test_perft.cpp
    test_pgn.cpp
    test_san.cpp
    test_types.cpp
    test_version.cpp)
target_link_libraries(chessgui_tests PRIVATE chessgui::pgn GTest::gtest_main) # pgn brings core
chessgui_set_warnings(chessgui_tests)

gtest_discover_tests(chessgui_tests DISCOVERY_TIMEOUT 30)

# UI tests: the board and window, driven with real mouse events in an offscreen window.
if(CHESSGUI_BUILD_APP)
    add_executable(chessgui_ui_tests
        ui/main.cpp
        ui/test_board_geometry.cpp
        ui/test_board_view.cpp
        ui/test_documents.cpp
        ui/test_main_window.cpp
        ui/test_piece_set.cpp)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    target_link_libraries(chessgui_ui_tests PRIVATE chessgui_ui Qt6::Test GTest::gtest)
    chessgui_set_warnings(chessgui_ui_tests)
    gtest_discover_tests(chessgui_ui_tests
        DISCOVERY_TIMEOUT 30
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
