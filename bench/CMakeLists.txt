add_executable(chessgui_bench bench_core.cpp bench_perft.cpp)
target_link_libraries(chessgui_bench PRIVATE chessgui::core benchmark::benchmark_main)
chessgui_set_warnings(chessgui_bench)
