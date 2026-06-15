execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE KB_ECS_BENCHMARK_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE KB_ECS_BENCHMARK_GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

if(KB_ECS_BENCHMARK_GIT_COMMIT STREQUAL "")
    set(KB_ECS_BENCHMARK_GIT_COMMIT "unknown")
endif()

if(KB_ECS_BENCHMARK_GIT_BRANCH STREQUAL "")
    set(KB_ECS_BENCHMARK_GIT_BRANCH "unknown")
endif()

add_executable(kb_ecs_benchmarks
    main.cpp
)

target_link_libraries(kb_ecs_benchmarks
    PRIVATE
        kb_engine
)

target_compile_definitions(kb_ecs_benchmarks
    PRIVATE
        "KB_ECS_BENCHMARK_GIT_COMMIT=\"${KB_ECS_BENCHMARK_GIT_COMMIT}\""
        "KB_ECS_BENCHMARK_GIT_BRANCH=\"${KB_ECS_BENCHMARK_GIT_BRANCH}\""
        "KB_ECS_BENCHMARK_BUILD_CONFIG=\"$<CONFIG>\""
)

kb_apply_quality_gates(kb_ecs_benchmarks)
