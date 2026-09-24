find_package(benchmark CONFIG REQUIRED)

file(GLOB BENCHMARK_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/bench_*.cpp")

function(shmscope_add_benchmark target)
    add_executable(${target} ${ARGN})

    target_link_libraries(${target} PRIVATE
            shmscope_core
            shmscope::warnings
            benchmark::benchmark_main
    )
endfunction()

if(BENCHMARK_SOURCES)
    shmscope_add_benchmark(shmscope_benchmarks ${BENCHMARK_SOURCES})
endif()
