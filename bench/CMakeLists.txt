add_executable(kiebitz_bench bench_core.cpp bench_engine.cpp bench_gamedb.cpp bench_library.cpp bench_perft.cpp
    bench_pgn.cpp bench_puzzles.cpp bench_repertoire.cpp)
target_link_libraries(kiebitz_bench PRIVATE kiebitz::engine kiebitz::gamedb kiebitz::library kiebitz::repertoire
    kiebitz::puzzles benchmark::benchmark_main)
target_compile_definitions(kiebitz_bench PRIVATE
    KIEBITZ_OPENINGS_TSV="${PROJECT_SOURCE_DIR}/app/resources/openings/openings.tsv")
kiebitz_set_warnings(kiebitz_bench)

if(KIEBITZ_BUILD_APP)
    add_executable(kiebitz_bench_ui bench_board.cpp)
    target_link_libraries(kiebitz_bench_ui PRIVATE kiebitz_ui benchmark::benchmark)
    kiebitz_set_warnings(kiebitz_bench_ui)
endif()
