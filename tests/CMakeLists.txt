include(GoogleTest)

add_executable(kiebitz_tests
    test_annotations.cpp
    test_engine.cpp
    test_fen.cpp
    test_game.cpp
    test_gamedb.cpp
    test_library.cpp
    test_perft.cpp
    test_pgn.cpp
    test_puzzles.cpp
    test_repertoire.cpp
    test_san.cpp
    test_types.cpp
    test_version.cpp)
target_link_libraries(kiebitz_tests PRIVATE kiebitz::engine kiebitz::gamedb kiebitz::library
    kiebitz::repertoire kiebitz::puzzles kiebitz::pgn thirdparty::zstd
    GTest::gtest_main)
target_compile_definitions(kiebitz_tests PRIVATE
    KIEBITZ_OPENINGS_TSV="${PROJECT_SOURCE_DIR}/app/resources/openings/openings.tsv")
kiebitz_set_warnings(kiebitz_tests)

gtest_discover_tests(kiebitz_tests DISCOVERY_TIMEOUT 30)

# UI tests: the board and window, driven with real mouse events in an offscreen window.
if(KIEBITZ_BUILD_APP)
    add_executable(kiebitz_ui_tests
        ui/main.cpp
        ui/test_board_geometry.cpp
        ui/test_board_view.cpp
        ui/test_databases.cpp
        ui/test_documents.cpp
        ui/test_engine.cpp
        ui/test_main_window.cpp
        ui/test_migration.cpp
        ui/test_piece_set.cpp
        ui/test_profiles.cpp
        ui/test_puzzles.cpp
        ui/test_settings.cpp)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    target_link_libraries(kiebitz_ui_tests PRIVATE kiebitz_ui Qt6::Test GTest::gtest)
    kiebitz_set_warnings(kiebitz_ui_tests)
    # A predictable stand-in for a UCI engine (see fake_engine.cpp).
    add_executable(kiebitz_fake_engine fake_engine.cpp)
    target_link_libraries(kiebitz_fake_engine PRIVATE kiebitz::core)
    kiebitz_set_warnings(kiebitz_fake_engine)
    add_dependencies(kiebitz_ui_tests kiebitz_fake_engine)
    target_compile_definitions(kiebitz_ui_tests PRIVATE
        KIEBITZ_FAKE_ENGINE="$<TARGET_FILE:kiebitz_fake_engine>")
    gtest_discover_tests(kiebitz_ui_tests
        DISCOVERY_TIMEOUT 30
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # The real app, started and quit straight away (as the startup check in CI does). Quitting
    # used to crash; in the debug preset ASan names any memory error on the way out.
    add_test(NAME App.StartsAndQuitsCleanly COMMAND kiebitz)
    set_tests_properties(App.StartsAndQuitsCleanly PROPERTIES
        TIMEOUT 60
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;KIEBITZ_STARTUP_EXIT=1;\
KIEBITZ_SETTINGS_DIR=${CMAKE_CURRENT_BINARY_DIR}/app-test-settings")
endif()
