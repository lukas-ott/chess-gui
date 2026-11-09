include(GoogleTest)

add_executable(chessgui_tests
    test_perft.cpp
    test_version.cpp)
target_link_libraries(chessgui_tests PRIVATE chessgui::core GTest::gtest_main)
chessgui_set_warnings(chessgui_tests)

gtest_discover_tests(chessgui_tests DISCOVERY_TIMEOUT 30)
