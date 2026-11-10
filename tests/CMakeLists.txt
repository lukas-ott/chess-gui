include(GoogleTest)

add_executable(chessgui_tests
    test_annotations.cpp
    test_engine.cpp
    test_fen.cpp
    test_game.cpp
    test_perft.cpp
    test_pgn.cpp
    test_san.cpp
    test_types.cpp
    test_version.cpp)
target_link_libraries(chessgui_tests PRIVATE chessgui::pgn chessgui::engine GTest::gtest_main)
chessgui_set_warnings(chessgui_tests)

gtest_discover_tests(chessgui_tests DISCOVERY_TIMEOUT 30)

# UI tests: the board and window, driven with real mouse events in an offscreen window.
if(CHESSGUI_BUILD_APP)
    add_executable(chessgui_ui_tests
        ui/main.cpp
        ui/test_board_geometry.cpp
        ui/test_board_view.cpp
        ui/test_documents.cpp
        ui/test_engine.cpp
        ui/test_main_window.cpp
        ui/test_piece_set.cpp)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    target_link_libraries(chessgui_ui_tests PRIVATE chessgui_ui Qt6::Test GTest::gtest)
    chessgui_set_warnings(chessgui_ui_tests)
    # A predictable stand-in for a UCI engine (see fake_engine.cpp).
    add_executable(chessgui_fake_engine fake_engine.cpp)
    target_link_libraries(chessgui_fake_engine PRIVATE chessgui::core)
    chessgui_set_warnings(chessgui_fake_engine)
    add_dependencies(chessgui_ui_tests chessgui_fake_engine)
    target_compile_definitions(chessgui_ui_tests PRIVATE
        CHESSGUI_FAKE_ENGINE="$<TARGET_FILE:chessgui_fake_engine>")
    gtest_discover_tests(chessgui_ui_tests
        DISCOVERY_TIMEOUT 30
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # The real app, started and quit straight away (as the startup check in CI does). Quitting
    # used to crash; in the debug preset ASan names any memory error on the way out.
    add_test(NAME App.StartsAndQuitsCleanly COMMAND chessgui)
    set_tests_properties(App.StartsAndQuitsCleanly PROPERTIES
        TIMEOUT 60
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;CHESSGUI_STARTUP_EXIT=1;\
CHESSGUI_SETTINGS_DIR=${CMAKE_CURRENT_BINARY_DIR}/app-test-settings")
endif()
