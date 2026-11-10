add_executable(chessgui_bench bench_core.cpp bench_perft.cpp)
target_link_libraries(chessgui_bench PRIVATE chessgui::core benchmark::benchmark_main)
chessgui_set_warnings(chessgui_bench)

if(CHESSGUI_BUILD_APP)
    add_executable(chessgui_bench_ui bench_board.cpp)
    target_link_libraries(chessgui_bench_ui PRIVATE chessgui_ui benchmark::benchmark)
    chessgui_set_warnings(chessgui_bench_ui)
endif()
