add_executable(kiebitz_bench bench_core.cpp bench_engine.cpp bench_perft.cpp bench_pgn.cpp)
target_link_libraries(kiebitz_bench PRIVATE kiebitz::pgn kiebitz::engine
    benchmark::benchmark_main)
kiebitz_set_warnings(kiebitz_bench)

if(KIEBITZ_BUILD_APP)
    add_executable(kiebitz_bench_ui bench_board.cpp)
    target_link_libraries(kiebitz_bench_ui PRIVATE kiebitz_ui benchmark::benchmark)
    kiebitz_set_warnings(kiebitz_bench_ui)
endif()
