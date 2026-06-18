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

option(KB_ECS_BENCHMARKS_ENABLE_LTO "Enable link-time optimization for ECS benchmark targets" OFF)
set(KB_ECS_BENCHMARKS_PGO_MODE "Off" CACHE STRING "ECS benchmark PGO mode: Off, Generate, or Use")
set_property(CACHE KB_ECS_BENCHMARKS_PGO_MODE PROPERTY STRINGS Off Generate Use)
set(KB_ECS_BENCHMARKS_PGO_PROFILE_DIR "${CMAKE_BINARY_DIR}/ecs-benchmarks-pgo" CACHE PATH "Profile data directory for ECS benchmark PGO builds")
set(KB_ECS_BENCHMARKS_PGO_TRAINING_ARGS "--smoke" CACHE STRING "Arguments passed to the ECS benchmark during PGO training")

set(_kb_ecs_benchmarks_pgo_mode "${KB_ECS_BENCHMARKS_PGO_MODE}")
if(NOT _kb_ecs_benchmarks_pgo_mode MATCHES "^(Off|Generate|Use)$")
    message(FATAL_ERROR "KB_ECS_BENCHMARKS_PGO_MODE must be Off, Generate, or Use")
endif()

set(_kb_ecs_benchmarks_profile_dir "${KB_ECS_BENCHMARKS_PGO_PROFILE_DIR}")
if(NOT IS_ABSOLUTE "${_kb_ecs_benchmarks_profile_dir}")
    cmake_path(ABSOLUTE_PATH _kb_ecs_benchmarks_profile_dir BASE_DIRECTORY "${CMAKE_BINARY_DIR}" NORMALIZE)
endif()
file(MAKE_DIRECTORY "${_kb_ecs_benchmarks_profile_dir}")

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
        "KB_ECS_BENCHMARK_COMPILER_ID=\"${CMAKE_CXX_COMPILER_ID}\""
        "KB_ECS_BENCHMARK_COMPILER_VERSION=\"${CMAKE_CXX_COMPILER_VERSION}\""
        "KB_ECS_BENCHMARK_CXX_FLAGS=\"${CMAKE_CXX_FLAGS}\""
        "KB_ECS_BENCHMARK_CXX_FLAGS_DEBUG=\"${CMAKE_CXX_FLAGS_DEBUG}\""
        "KB_ECS_BENCHMARK_CXX_FLAGS_RELEASE=\"${CMAKE_CXX_FLAGS_RELEASE}\""
        "KB_ECS_BENCHMARK_CXX_FLAGS_RELWITHDEBINFO=\"${CMAKE_CXX_FLAGS_RELWITHDEBINFO}\""
        "KB_ECS_BENCHMARK_CXX_FLAGS_MINSIZEREL=\"${CMAKE_CXX_FLAGS_MINSIZEREL}\""
)

target_include_directories(kb_ecs_benchmarks
    PRIVATE
        ${CMAKE_SOURCE_DIR}/sources/engine/src/private
)

kb_apply_quality_gates(kb_ecs_benchmarks)

add_library(kb_ecs_kernel_report_kernels OBJECT
    KernelReportKernels.cpp
    KernelReportKernels.hpp
)

target_include_directories(kb_ecs_kernel_report_kernels
    PRIVATE
        ${CMAKE_SOURCE_DIR}/sources/engine/include
)

kb_apply_quality_gates(kb_ecs_kernel_report_kernels)

add_executable(kb_ecs_kernel_report
    KernelReportMain.cpp
    KernelReportKernels.hpp
    $<TARGET_OBJECTS:kb_ecs_kernel_report_kernels>
)

target_include_directories(kb_ecs_kernel_report
    PRIVATE
        ${CMAKE_SOURCE_DIR}/sources/engine/include
)

target_compile_definitions(kb_ecs_kernel_report
    PRIVATE
        "KB_ECS_KERNEL_REPORT_GIT_COMMIT=\"${KB_ECS_BENCHMARK_GIT_COMMIT}\""
        "KB_ECS_KERNEL_REPORT_GIT_BRANCH=\"${KB_ECS_BENCHMARK_GIT_BRANCH}\""
        "KB_ECS_KERNEL_REPORT_BUILD_CONFIG=\"$<CONFIG>\""
        "KB_ECS_KERNEL_REPORT_COMPILER_ID=\"${CMAKE_CXX_COMPILER_ID}\""
        "KB_ECS_KERNEL_REPORT_COMPILER_VERSION=\"${CMAKE_CXX_COMPILER_VERSION}\""
)

kb_apply_quality_gates(kb_ecs_kernel_report)

if(MSVC)
    target_compile_options(kb_ecs_kernel_report_kernels PRIVATE "$<$<CONFIG:Debug,RelWithDebInfo>:/Z7>")
    target_compile_options(kb_ecs_kernel_report PRIVATE "$<$<CONFIG:Debug,RelWithDebInfo>:/Z7>")
endif()

set(_kb_ecs_kernel_report_assembly_dir "${CMAKE_CURRENT_BINARY_DIR}/kernel-report/assembly")
set(_kb_ecs_kernel_report_source "${CMAKE_CURRENT_SOURCE_DIR}/KernelReportKernels.cpp")

if(MSVC)
    set(_kb_ecs_kernel_report_assembly "${_kb_ecs_kernel_report_assembly_dir}/KernelReportKernels.asm")
    set(_kb_ecs_kernel_report_object "${_kb_ecs_kernel_report_assembly_dir}/KernelReportKernels.obj")
    add_custom_command(
        OUTPUT "${_kb_ecs_kernel_report_assembly}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_kb_ecs_kernel_report_assembly_dir}"
        COMMAND ${CMAKE_CXX_COMPILER}
            /nologo
            /std:c++20
            /EHsc
            /O2
            /DNDEBUG
            "/I${CMAKE_SOURCE_DIR}/sources/engine/include"
            /c
            /FAcs
            "/Fa${_kb_ecs_kernel_report_assembly}"
            "/Fo${_kb_ecs_kernel_report_object}"
            "${_kb_ecs_kernel_report_source}"
        DEPENDS
            "${_kb_ecs_kernel_report_source}"
            "${CMAKE_CURRENT_SOURCE_DIR}/KernelReportKernels.hpp"
        VERBATIM
    )
else()
    set(_kb_ecs_kernel_report_assembly "${_kb_ecs_kernel_report_assembly_dir}/KernelReportKernels.s")
    add_custom_command(
        OUTPUT "${_kb_ecs_kernel_report_assembly}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_kb_ecs_kernel_report_assembly_dir}"
        COMMAND ${CMAKE_CXX_COMPILER}
            -std=c++20
            -O3
            -DNDEBUG
            "-I${CMAKE_SOURCE_DIR}/sources/engine/include"
            -S
            "${_kb_ecs_kernel_report_source}"
            -o
            "${_kb_ecs_kernel_report_assembly}"
        DEPENDS
            "${_kb_ecs_kernel_report_source}"
            "${CMAKE_CURRENT_SOURCE_DIR}/KernelReportKernels.hpp"
        VERBATIM
    )
endif()

add_custom_target(kb_ecs_kernel_report_assembly
    DEPENDS "${_kb_ecs_kernel_report_assembly}"
)

add_custom_target(kb_ecs_kernel_report_run
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/kernel-report"
    COMMAND "$<TARGET_FILE:kb_ecs_kernel_report>"
        --output "${CMAKE_CURRENT_BINARY_DIR}/kernel-report/ecs_kernel_report.json"
        --assembly-dir "${_kb_ecs_kernel_report_assembly_dir}"
        --elements 32768
        --iterations 8
        --samples 5
        --warmup 1
    DEPENDS
        kb_ecs_kernel_report
        kb_ecs_kernel_report_assembly
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    USES_TERMINAL
    VERBATIM
)

if(BUILD_TESTING)
    add_test(
        NAME kb_ecs_market_hot_path_guard
        COMMAND ${CMAKE_COMMAND}
            "-DHOT_PATH_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
            "-DSCENE_RENDER_SYNC_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/scene/components/SceneMeshRendererIterationDispatcher.cpp"
            "-DSCENE_INPUT_ACTIVATION_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/scene/SceneInputActivation.cpp"
            "-DSCENE_BEHAVIOUR_ITERATION_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/scene/components/SceneBehaviourIteration.cpp"
            "-DSCENE_CAMERA_SYNC_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/scene/components/SceneCameraIteration.cpp"
            "-DSCENE_LIGHT_SYNC_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/scene/components/SceneLightIteration.cpp"
            "-DSCENE_TRANSFORM_ITERATION_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/scene/components/SceneTransformIteration.cpp"
            "-DSCENE_TRANSFORM_HIERARCHY_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/scene/transform/SceneTransformHierarchySystem.cpp"
            "-DSCENE_TRANSFORM_COMPONENT_HEADER=${CMAKE_SOURCE_DIR}/sources/engine/include/engine/scene/TransformComponent.hpp"
            "-DSCENE_HIERARCHY_TEST_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/tests/SceneHierarchyTests.cpp"
            "-DSCENE_PREFAB_INSTANCE_REGISTRY_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/scene/prefab/ScenePrefabInstanceRegistry.cpp"
            "-DSCENE_PREFAB_INSTANCE_REGISTRY_HEADER=${CMAKE_SOURCE_DIR}/sources/engine/src/private/scene/prefab/ScenePrefabInstanceRegistry.hpp"
            "-DSCENE_PREFAB_INSTANCE_HEADER=${CMAKE_SOURCE_DIR}/sources/engine/include/engine/scene/ScenePrefabInstance.hpp"
            "-DSCENE_PREFAB_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/scene/prefab/ScenePrefab.cpp"
            "-DSCENE_PREFAB_BULK_INSTANTIATION_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/scene/prefab/ScenePrefabBulkInstantiationService.cpp"
            "-DSCENE_PREFAB_REGISTERED_INSTANTIATION_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/scene/prefab/ScenePrefabRegisteredInstantiationService.cpp"
            "-DSCENE_HISTORY_SERVICE_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/scene/SceneHistoryService.cpp"
            "-DSCENE_ENTITY_NAMING_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/scene/entities/SceneEntityNaming.cpp"
            "-DSCENE_ENTITY_NAME_SERVICE_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/scene/entities/SceneEntityNameService.cpp"
            "-DSCENE_ENTITY_DESTRUCTION_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/scene/entities/SceneEntityDestructionService.cpp"
            "-DSCENE_STATE_HEADER=${CMAKE_SOURCE_DIR}/sources/engine/src/private/scene/SceneState.hpp"
            "-DSCENE_PREFAB_INSTANTIATION_SETTINGS_HEADER=${CMAKE_SOURCE_DIR}/sources/engine/include/engine/scene/ScenePrefabInstantiationSettings.hpp"
            "-DSCENE_HIERARCHY_CACHE_HEADER=${CMAKE_SOURCE_DIR}/sources/engine/src/private/scene/hierarchy/SceneHierarchyCache.hpp"
            "-DSCENE_PREFAB_CONNECTION_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/scene/modules/ScenePrefabConnectionModule.cpp"
            "-DSCENE_PREFAB_INSTANTIATION_TEST_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/tests/ScenePrefabInstantiationTests.cpp"
            "-DSCENE_TRANSFORM_ROOT_COLLECTOR_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/scene/transform/SceneTransformRootCollector.cpp"
            "-DSCENE_HIERARCHY_ROOT_COLLECTOR_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/scene/hierarchy/SceneHierarchyRootCollector.cpp"
            "-DSCENE_COMPONENT_ACCESS_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/scene/components/SceneComponentAccess.cpp"
            "-DCOMPONENT_STORAGE_QUERY_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/ecs/ComponentStorageQuery.cpp"
            "-DWORLD_COMPONENTS_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/ecs/WorldComponents.cpp"
            "-DWORLD_ENTITIES_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/ecs/WorldEntities.cpp"
            "-DWORLD_ENTITY_CATALOG_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/ecs/WorldEntityCatalog.cpp"
            "-DWORLD_ENTITY_CATALOG_HEADER=${CMAKE_SOURCE_DIR}/sources/engine/src/private/ecs/world/WorldEntityCatalog.hpp"
            "-DWORLD_COMPONENT_API_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/include/engine/ecs/world/WorldComponentApi.inl"
            "-DBENCHMARK_CMAKE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
            "-DMARKET_SUMMARY_ASSERT_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketSummary.cmake"
            "-DCOMPONENT_STORAGE_ITERATION_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/ecs/ComponentStorageIteration.cpp"
            "-DCOMPONENT_PAIR_ITERATION_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/ecs/ComponentStoragePairIteration.cpp"
            "-DCOMPONENT_SNAPSHOT_CAPTURE_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/ecs/ComponentSnapshotCapture.cpp"
            "-DQUERY_STATE_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/ecs/QueryState.cpp"
            "-DQUERY_TABLE_BATCH_DISPATCHER_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/ecs/query/QueryTableBatchDispatcher.cpp"
            "-DQUERY_PREFETCH_HEADER=${CMAKE_SOURCE_DIR}/sources/engine/include/engine/ecs/QueryPrefetch.hpp"
            "-DECS_COMMAND_BUFFER_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/ecs/CommandBuffer.cpp"
            "-DECS_WORLD_RELATIONS_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/ecs/WorldRelations.cpp"
            "-DECS_RELATION_STORAGE_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/ecs/RelationStorage.cpp"
            "-DECS_RELATION_STORAGE_HEADER=${CMAKE_SOURCE_DIR}/sources/engine/src/private/ecs/relation/RelationStorage.hpp"
            "-DECS_COMMAND_BUFFER_TEST_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/tests/EcsCommandBufferTests.cpp"
            "-DECS_NATIVE_STORAGE_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/src/ecs/NativeArchetypeStorage.cpp"
            "-DECS_CHUNK_SIZE_PROFILE_HEADER=${CMAKE_SOURCE_DIR}/sources/engine/include/engine/ecs/ChunkSizeProfile.hpp"
            "-DECS_QUERY_EXECUTION_SETTINGS_HEADER=${CMAKE_SOURCE_DIR}/sources/engine/include/engine/ecs/QueryExecutionSettings.hpp"
            "-DECS_WORLD_PROFILE_HEADER=${CMAKE_SOURCE_DIR}/sources/engine/include/engine/ecs/WorldProfile.hpp"
            "-DECS_CONFIG_TEST_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/tests/EcsConfigTests.cpp"
            "-DECS_INSPECTION_TEST_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/tests/EcsInspectionTests.cpp"
            "-DECS_KERNEL_TEST_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/tests/EcsKernelTests.cpp"
            "-DECS_QUERY_TEST_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/tests/EcsQueryTests.cpp"
            "-DECS_NATIVE_STORAGE_TEST_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/tests/EcsNativeArchetypeStorageTests.cpp"
            "-DECS_WORKER_POOL_TEST_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/tests/EcsWorkerPoolTests.cpp"
            "-DECS_RELATION_TEST_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/tests/EcsRelationTests.cpp"
            "-DDETERMINISTIC_REPLAY_SOURCE=${CMAKE_SOURCE_DIR}/sources/engine/tests/EcsDeterministicReplayTests.cpp"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketHotPath.cmake"
    )
    set_tests_properties(kb_ecs_market_hot_path_guard PROPERTIES
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_public_external_name_guard
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_ROOT=${CMAKE_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertPublicNamingGuard.cmake"
    )
    set_tests_properties(kb_public_external_name_guard PROPERTIES
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --smoke
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_benchmark_smoke.json"
    )
    set_tests_properties(kb_ecs_benchmark_smoke PROPERTIES
        LABELS "ecs;benchmark;smoke"
        TIMEOUT 300
    )

    add_test(
        NAME kb_ecs_benchmark_rejects_invalid_chunk_size
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --benchmark-category ecs_chunk_iteration
            --entities 10
            --frames 1
            --warmup 0
            --chunk-size 12KB
            --world-profile balanced32k
            --execution-policy single_thread
            --validation off
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_invalid_chunk_size.json"
    )
    set_tests_properties(kb_ecs_benchmark_rejects_invalid_chunk_size PROPERTIES
        LABELS "ecs;benchmark;regression"
        WILL_FAIL TRUE
        TIMEOUT 30
    )

    add_test(
        NAME kb_ecs_market_smoke_100k
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --entities 100000
            --hierarchy-entities 25000
            --structural-changes 4000
            --frames 1
            --warmup 0
            --chunk-size 32KB
            --world-profile balanced32k
            --execution-policy parallel_chunks
            --validation off
            --market-scale
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_market_smoke_100k.json"
    )
    set_tests_properties(kb_ecs_market_smoke_100k PROPERTIES
        LABELS "ecs;benchmark;smoke"
        TIMEOUT 120
    )

    add_test(
        NAME kb_ecs_market_smoke_100k_telemetry_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_smoke_100k.json"
            "-DRESULT_NAME=position_velocity_batch_read"
            "-DREQUIRED_POSITIVE_FIELDS=time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,workload_bytes_per_frame,workload_bytes_per_second,execution_worker_count,worker_utilization_percent,storage_chunks,storage_capacity,storage_used_bytes,storage_occupancy_percent,telemetry_entity_count,telemetry_bytes_per_entity,telemetry_query_executions,telemetry_query_batches,telemetry_query_entities_visited,telemetry_query_bytes_touched,telemetry_query_elapsed_nanoseconds,telemetry_query_bytes_per_second,telemetry_query_parallel_executions,telemetry_query_worker_slots,telemetry_query_worker_active_slots,telemetry_query_worker_utilization_percent"
            "-DREQUIRED_EXACT_FIELDS=entities=100000,storage_chunk_size_bytes=32768,execution_prefetch_distance=24,telemetry_compat_mutable_iterations=0,telemetry_compat_mutable_entities_visited=0"
            "-DREQUIRED_STRING_FIELDS=world_profile=balanced32k,execution_policy=parallel_chunks,query_reduction_mode=none"
            "-DREQUIRED_RUN_POSITIVE_FIELDS=chunk_size_bytes,simd_backend_lane_count,thread_count"
            "-DREQUIRED_RUN_EXACT_FIELDS=chunk_size_bytes=32768"
            "-DREQUIRED_RUN_STRING_FIELDS=build_config=$<CONFIG>,world_profile=balanced32k,chunk_size_profile=32KB"
            "-DREQUIRED_RUN_NONEMPTY_STRING_FIELDS=commit,branch,compiler_id,compiler_version,compiler_flags,cpu,simd_backend"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_market_smoke_100k_telemetry_guard PROPERTIES
        DEPENDS kb_ecs_market_smoke_100k
        LABELS "ecs;benchmark;regression"
    )

    if(WIN32)
        add_test(
            NAME kb_ecs_market_runner_smoke_10k_100k
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/run-ecs-market-benchmark.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -Config "$<CONFIG>"
                -SkipConfigure
                -NoBuild
                -Scales 10000,100000
                -OutputDir "${CMAKE_CURRENT_BINARY_DIR}/ecs_market_runner_smoke_10k_100k"
                -Frames 1
                -Warmup 0
                -ChunkSizes 32KB
                -WorldProfiles balanced32k
                -ExecutionPolicies parallel_chunks
                -BenchmarkCategories ecs_chunk_iteration
                -MaxMemoryPercent 70
        )
        set_tests_properties(kb_ecs_market_runner_smoke_10k_100k PROPERTIES
            LABELS "ecs;benchmark;smoke;regression"
            TIMEOUT 120
        )

        add_test(
            NAME kb_ecs_market_runner_smoke_10k_100k_manifest
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_runner_smoke_10k_100k/21kb_ecs_market_manifest.json"
                "-DREQUIRED_STATUS=completed"
                "-DREQUIRED_CATEGORY=ecs_chunk_iteration"
                "-DREQUIRED_MODEL=dense_archetype_layout"
                "-DREQUIRED_FIELD_PATTERNS=world_profile=balanced32k,chunk_size=32KB,execution_policy=parallel_chunks"
                "-DREQUIRED_NUMERIC_FIELDS=measured_memory_bytes,chunk_capacity,estimated_chunks,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_NONEMPTY_STRING_FIELDS=commit,branch,build_config,compiler_id,compiler_version,compiler_flags,cpu,run_simd_backend"
                "-DREQUIRED_EXACT_NUMERIC_FIELDS=scale=10000,scale=100000"
                "-DREQUIRED_BOOL_FIELDS=measured_memory_guard_pass=true"
                "-DREQUIRED_EXACT_ROWS=2"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketManifest.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_smoke_10k_100k_manifest PROPERTIES
            DEPENDS kb_ecs_market_runner_smoke_10k_100k
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_smoke_10k_100k_summary
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_runner_smoke_10k_100k/21kb_ecs_market_summary.csv"
                "-DREQUIRED_MIN_ROWS=2"
                "-DREQUIRED_COLUMNS=engine,scale,world_profile,chunk_size_profile,chunk_size_bytes,requested_category,benchmark,category,dataset,entities,frames,warmup_frames,execution_policy,query_reduction_mode,simd_backend,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,workload_bytes_per_frame,workload_bytes_per_second,telemetry_entity_count,telemetry_query_entities_visited,telemetry_query_bytes_touched,telemetry_query_elapsed_nanoseconds,telemetry_query_bytes_per_second,telemetry_compat_mutable_iterations,telemetry_compat_mutable_entities_visited,storage_occupancy_percent,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,measured_memory_bytes,measured_memory_guard_pass,cpu,run_simd_backend,run_simd_backend_lane_count,thread_count,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_NONEMPTY_COLUMNS=cpu,run_simd_backend,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_POSITIVE_COLUMNS=entities,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,workload_bytes_per_frame,workload_bytes_per_second,telemetry_entity_count,telemetry_query_entities_visited,telemetry_query_bytes_touched,telemetry_query_elapsed_nanoseconds,telemetry_query_bytes_per_second,storage_occupancy_percent,measured_memory_bytes,simd_backend_lane_count,execution_worker_count,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_ZERO_COLUMNS=telemetry_compat_mutable_iterations,telemetry_compat_mutable_entities_visited"
                "-DREQUIRED_TRUE_COLUMNS=measured_memory_guard_pass,frame_budget_pass_avg,frame_budget_pass_p95"
                "-DREQUIRED_CONSTANT_FIELDS=engine=21kb,world_profile=balanced32k,chunk_size_profile=32KB,requested_category=ecs_chunk_iteration,execution_policy=parallel_chunks,query_reduction_mode=none,build_config=Release"
                "-DREQUIRED_COLUMN_VALUES=scale=10000,scale=100000"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketSummary.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_smoke_10k_100k_summary PROPERTIES
            DEPENDS kb_ecs_market_runner_smoke_10k_100k
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_memory_guard_scene_sync
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/run-ecs-market-benchmark.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -Config "$<CONFIG>"
                -SkipConfigure
                -NoBuild
                -Scales 100000000
                -OutputDir "${CMAKE_CURRENT_BINARY_DIR}/ecs_market_memory_guard_scene_sync"
                -Frames 1
                -Warmup 0
                -ChunkSizes 8KB
                -WorldProfiles mobile8k
                -ExecutionPolicies single_thread
                -BenchmarkCategories scene_render_sync
                -MaxMemoryPercent 0.000001
        )
        set_tests_properties(kb_ecs_market_runner_memory_guard_scene_sync PROPERTIES
            LABELS "ecs;benchmark;regression"
            TIMEOUT 60
        )

        add_test(
            NAME kb_ecs_market_runner_memory_guard_scene_sync_manifest
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_memory_guard_scene_sync/21kb_ecs_market_manifest.json"
                "-DREQUIRED_STATUS=skipped"
                "-DREQUIRED_CATEGORY=scene_render_sync"
                "-DREQUIRED_MODEL=scene_renderer_component_footprint"
                "-DREQUIRED_BOOL_FIELDS=measured_memory_guard_pass=false"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketManifest.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_memory_guard_scene_sync_manifest PROPERTIES
            DEPENDS kb_ecs_market_runner_memory_guard_scene_sync
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_mobile_memory_guard
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/run-ecs-market-benchmark.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -Config "$<CONFIG>"
                -SkipConfigure
                -NoBuild
                -Scales 100000000
                -OutputDir "${CMAKE_CURRENT_BINARY_DIR}/ecs_market_mobile_memory_guard"
                -Frames 1
                -Warmup 0
                -ChunkSizes 4KB,8KB,16KB
                -WorldProfiles mobile4k,mobile8k,mobile16k
                -ExecutionPolicies parallel_chunks
                -BenchmarkCategories ecs_chunk_iteration
                -MaxMemoryPercent 0.000001
        )
        set_tests_properties(kb_ecs_market_runner_mobile_memory_guard PROPERTIES
            LABELS "ecs;benchmark;regression"
            TIMEOUT 60
        )

        add_test(
            NAME kb_ecs_market_runner_mobile_memory_guard_manifest
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_mobile_memory_guard/21kb_ecs_market_manifest.json"
                "-DREQUIRED_STATUS=skipped"
                "-DREQUIRED_CATEGORY=ecs_chunk_iteration"
                "-DREQUIRED_MODEL=dense_archetype_layout"
                "-DREQUIRED_FIELD_PATTERNS=world_profile=mobile4k,world_profile=mobile8k,world_profile=mobile16k,chunk_size=4KB,chunk_size=8KB,chunk_size=16KB,execution_policy=parallel_chunks"
                "-DREQUIRED_BOOL_FIELDS=measured_memory_guard_pass=false"
                "-DREQUIRED_EXACT_ROWS=9"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketManifest.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_mobile_memory_guard_manifest PROPERTIES
            DEPENDS kb_ecs_market_runner_mobile_memory_guard
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_mobile_sustained_smoke
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/run-ecs-market-benchmark.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -Config "$<CONFIG>"
                -SkipConfigure
                -NoBuild
                -Scales 1000
                -OutputDir "${CMAKE_CURRENT_BINARY_DIR}/ecs_market_mobile_sustained_smoke"
                -Frames 1
                -Warmup 0
                -ChunkSizes 16KB
                -WorldProfiles mobile16k
                -ExecutionPolicies parallel_chunks
                -BenchmarkCategories ecs_chunk_iteration
                -SustainedSeconds 1
                -TargetFps 60
                -MaxMemoryPercent 70
        )
        set_tests_properties(kb_ecs_market_runner_mobile_sustained_smoke PROPERTIES
            LABELS "ecs;benchmark;regression"
            TIMEOUT 60
        )

        add_test(
            NAME kb_ecs_market_runner_mobile_sustained_manifest
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_mobile_sustained_smoke/21kb_ecs_market_manifest.json"
                "-DREQUIRED_STATUS=completed"
                "-DREQUIRED_CATEGORY=ecs_chunk_iteration"
                "-DREQUIRED_MODEL=dense_archetype_layout"
                "-DREQUIRED_FIELD_PATTERNS=world_profile=mobile16k,chunk_size=16KB,execution_policy=parallel_chunks"
                "-DREQUIRED_NUMERIC_FIELDS=sustained_seconds,target_fps,target_frame_ms,measured_memory_bytes,run_simd_backend_lane_count,thread_count,host_logical_core_count,host_physical_core_count,host_ram_bytes"
                "-DREQUIRED_NONEMPTY_STRING_FIELDS=commit,branch,build_config,compiler_id,compiler_version,compiler_flags,cpu,run_simd_backend,host_device_model,host_cpu_model,host_os_caption,host_os_version,host_os_architecture,host_thermal_state"
                "-DREQUIRED_EXACT_NUMERIC_FIELDS=effective_frames=60,thread_count=4"
                "-DREQUIRED_BOOL_FIELDS=measured_memory_guard_pass=true"
                "-DREQUIRED_EXACT_ROWS=1"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketManifest.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_mobile_sustained_manifest PROPERTIES
            DEPENDS kb_ecs_market_runner_mobile_sustained_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_mobile_sustained_summary
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_mobile_sustained_smoke/21kb_ecs_market_summary.csv"
                "-DREQUIRED_EXACT_ROWS=2"
                "-DREQUIRED_COLUMNS=engine,scale,world_profile,chunk_size_profile,chunk_size_bytes,requested_category,benchmark,category,entities,frames,effective_frames,sustained_seconds,target_fps,target_frame_ms,sustained_fps_avg,frame_budget_pass_avg,frame_budget_pass_p95,execution_policy,query_reduction_mode,simd_backend,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,workload_bytes_per_frame,workload_bytes_per_second,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,measured_memory_bytes,measured_memory_guard_pass,memory_guard_bytes,host_device_model,host_cpu_model,host_logical_core_count,host_physical_core_count,host_ram_bytes,host_os_caption,host_os_version,host_os_architecture,host_thermal_state,cpu,run_simd_backend,run_simd_backend_lane_count,thread_count,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_NONEMPTY_COLUMNS=host_device_model,host_cpu_model,host_os_caption,host_os_version,host_os_architecture,host_thermal_state,cpu,run_simd_backend,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_POSITIVE_COLUMNS=entities,effective_frames,sustained_seconds,target_fps,target_frame_ms,sustained_fps_avg,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,workload_bytes_per_frame,workload_bytes_per_second,measured_memory_bytes,memory_guard_bytes,host_logical_core_count,host_physical_core_count,host_ram_bytes,simd_backend_lane_count,execution_worker_count,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_TRUE_COLUMNS=measured_memory_guard_pass,frame_budget_pass_avg,frame_budget_pass_p95"
                "-DREQUIRED_CONSTANT_FIELDS=engine=21kb,scale=1000,world_profile=mobile16k,chunk_size_profile=16KB,requested_category=ecs_chunk_iteration,execution_policy=parallel_chunks,query_reduction_mode=none,effective_frames=60,sustained_seconds=1,target_fps=60,execution_worker_count=4,thread_count=4,build_config=Release"
                "-DREQUIRED_COLUMN_VALUES=benchmark=position_velocity_batch_read,benchmark=position_velocity_linear_update"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketSummary.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_mobile_sustained_summary PROPERTIES
            DEPENDS kb_ecs_market_runner_mobile_sustained_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_chunk_sweep_smoke
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/run-ecs-market-benchmark.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -Config "$<CONFIG>"
                -SkipConfigure
                -NoBuild
                -Scales 1000
                -OutputDir "${CMAKE_CURRENT_BINARY_DIR}/ecs_market_chunk_sweep_smoke"
                -Frames 1
                -Warmup 0
                -ChunkSweep
                -WorldProfiles balanced32k
                -ExecutionPolicies single_thread
                -BenchmarkCategories raw_kernel
                -MaxMemoryPercent 70
        )
        set_tests_properties(kb_ecs_market_runner_chunk_sweep_smoke PROPERTIES
            LABELS "ecs;benchmark;regression"
            TIMEOUT 60
        )

        add_test(
            NAME kb_ecs_market_runner_chunk_sweep_manifest
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_chunk_sweep_smoke/21kb_ecs_market_manifest.json"
                "-DREQUIRED_STATUS=completed"
                "-DREQUIRED_CATEGORY=raw_kernel"
                "-DREQUIRED_MODEL=raw_dense_buffers"
                "-DREQUIRED_FIELD_PATTERNS=chunk_size=4KB,chunk_size=8KB,chunk_size=16KB,chunk_size=32KB,chunk_size=64KB,chunk_size=128KB,chunk_size=256KB,chunk_size=512KB,execution_policy=single_thread"
                "-DREQUIRED_NUMERIC_FIELDS=measured_memory_bytes,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_EXACT_NUMERIC_FIELDS=chunk_capacity=0,estimated_chunks=0"
                "-DREQUIRED_NONEMPTY_STRING_FIELDS=commit,branch,build_config,compiler_id,compiler_version,compiler_flags,cpu,run_simd_backend"
                "-DREQUIRED_BOOL_FIELDS=measured_memory_guard_pass=true,chunk_sweep_enabled=true"
                "-DREQUIRED_EXACT_ROWS=8"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketManifest.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_chunk_sweep_manifest PROPERTIES
            DEPENDS kb_ecs_market_runner_chunk_sweep_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_chunk_sweep_summary
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_chunk_sweep_smoke/21kb_ecs_market_summary.csv"
                "-DREQUIRED_EXACT_ROWS=8"
                "-DREQUIRED_COLUMNS=engine,scale,world_profile,chunk_size_profile,chunk_size_bytes,requested_category,chunk_sweep_enabled,benchmark,category,entities,frames,effective_frames,execution_policy,query_reduction_mode,simd_backend,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,workload_bytes_per_frame,workload_bytes_per_second,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,memory_estimate_model,memory_estimate_category,estimated_memory_bytes,chunk_capacity,estimated_chunks,measured_memory_bytes,measured_memory_guard_pass,cpu,run_simd_backend,run_simd_backend_lane_count,thread_count,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_NONEMPTY_COLUMNS=cpu,run_simd_backend,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_POSITIVE_COLUMNS=entities,effective_frames,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,workload_bytes_per_frame,workload_bytes_per_second,estimated_memory_bytes,measured_memory_bytes,simd_backend_lane_count,execution_worker_count,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_TRUE_COLUMNS=chunk_sweep_enabled,measured_memory_guard_pass,frame_budget_pass_avg,frame_budget_pass_p95"
                "-DREQUIRED_CONSTANT_FIELDS=engine=21kb,scale=1000,world_profile=balanced32k,requested_category=raw_kernel,benchmark=raw_position_velocity_linear_update,category=raw_kernel,execution_policy=raw_kernel,query_reduction_mode=none,memory_estimate_model=raw_dense_buffers,memory_estimate_category=raw_kernel,chunk_capacity=0,estimated_chunks=0,build_config=Release"
                "-DREQUIRED_COLUMN_VALUES=chunk_size_profile=4KB,chunk_size_profile=8KB,chunk_size_profile=16KB,chunk_size_profile=32KB,chunk_size_profile=64KB,chunk_size_profile=128KB,chunk_size_profile=256KB,chunk_size_profile=512KB,chunk_size_bytes=4096,chunk_size_bytes=8192,chunk_size_bytes=16384,chunk_size_bytes=32768,chunk_size_bytes=65536,chunk_size_bytes=131072,chunk_size_bytes=262144,chunk_size_bytes=524288"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketSummary.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_chunk_sweep_summary PROPERTIES
            DEPENDS kb_ecs_market_runner_chunk_sweep_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_ecs_chunk_sweep_smoke
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/run-ecs-market-benchmark.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -Config "$<CONFIG>"
                -SkipConfigure
                -NoBuild
                -Scales 1000
                -OutputDir "${CMAKE_CURRENT_BINARY_DIR}/ecs_market_ecs_chunk_sweep_smoke"
                -Frames 1
                -Warmup 0
                -ChunkSweep
                -WorldProfiles balanced32k
                -ExecutionPolicies single_thread
                -BenchmarkCategories ecs_chunk_iteration
                -MaxMemoryPercent 70
        )
        set_tests_properties(kb_ecs_market_runner_ecs_chunk_sweep_smoke PROPERTIES
            LABELS "ecs;benchmark;regression"
            TIMEOUT 90
        )

        add_test(
            NAME kb_ecs_market_runner_ecs_chunk_sweep_manifest
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_ecs_chunk_sweep_smoke/21kb_ecs_market_manifest.json"
                "-DREQUIRED_STATUS=completed"
                "-DREQUIRED_CATEGORY=ecs_chunk_iteration"
                "-DREQUIRED_MODEL=dense_archetype_layout"
                "-DREQUIRED_FIELD_PATTERNS=world_profile=balanced32k,chunk_size=4KB,chunk_size=8KB,chunk_size=16KB,chunk_size=32KB,chunk_size=64KB,chunk_size=128KB,chunk_size=256KB,chunk_size=512KB,execution_policy=single_thread"
                "-DREQUIRED_NUMERIC_FIELDS=measured_memory_bytes,chunk_capacity,estimated_chunks,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_NONEMPTY_STRING_FIELDS=commit,branch,build_config,compiler_id,compiler_version,compiler_flags,cpu,run_simd_backend"
                "-DREQUIRED_BOOL_FIELDS=measured_memory_guard_pass=true,chunk_sweep_enabled=true"
                "-DREQUIRED_EXACT_ROWS=8"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketManifest.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_ecs_chunk_sweep_manifest PROPERTIES
            DEPENDS kb_ecs_market_runner_ecs_chunk_sweep_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_ecs_chunk_sweep_summary
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_ecs_chunk_sweep_smoke/21kb_ecs_market_summary.csv"
                "-DREQUIRED_EXACT_ROWS=16"
                "-DREQUIRED_COLUMNS=engine,scale,world_profile,chunk_size_profile,chunk_size_bytes,requested_category,chunk_sweep_enabled,benchmark,category,entities,frames,effective_frames,execution_policy,query_reduction_mode,simd_backend,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,workload_bytes_per_frame,workload_bytes_per_second,telemetry_entity_count,telemetry_bytes_per_entity,telemetry_query_executions,telemetry_query_batches,telemetry_query_entities_visited,telemetry_query_bytes_touched,telemetry_query_elapsed_nanoseconds,telemetry_query_bytes_per_second,storage_chunk_size_bytes,storage_chunks,storage_capacity,storage_used_bytes,storage_occupancy_percent,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,measured_memory_bytes,measured_memory_guard_pass,cpu,run_simd_backend,run_simd_backend_lane_count,thread_count,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_NONEMPTY_COLUMNS=cpu,run_simd_backend,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_POSITIVE_COLUMNS=entities,effective_frames,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,workload_bytes_per_frame,workload_bytes_per_second,telemetry_entity_count,telemetry_bytes_per_entity,telemetry_query_executions,telemetry_query_batches,telemetry_query_entities_visited,telemetry_query_bytes_touched,telemetry_query_elapsed_nanoseconds,telemetry_query_bytes_per_second,storage_chunk_size_bytes,storage_chunks,storage_capacity,storage_used_bytes,storage_occupancy_percent,measured_memory_bytes,simd_backend_lane_count,execution_worker_count,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_TRUE_COLUMNS=chunk_sweep_enabled,measured_memory_guard_pass,frame_budget_pass_avg,frame_budget_pass_p95"
                "-DREQUIRED_CONSTANT_FIELDS=engine=21kb,scale=1000,world_profile=balanced32k,requested_category=ecs_chunk_iteration,category=ecs_chunk_iteration,entities=1000,frames=1,effective_frames=1,execution_policy=single_thread,query_reduction_mode=none,build_config=Release"
                "-DREQUIRED_COLUMN_VALUES=chunk_size_profile=4KB,chunk_size_profile=8KB,chunk_size_profile=16KB,chunk_size_profile=32KB,chunk_size_profile=64KB,chunk_size_profile=128KB,chunk_size_profile=256KB,chunk_size_profile=512KB,chunk_size_bytes=4096,chunk_size_bytes=8192,chunk_size_bytes=16384,chunk_size_bytes=32768,chunk_size_bytes=65536,chunk_size_bytes=131072,chunk_size_bytes=262144,chunk_size_bytes=524288,benchmark=position_velocity_batch_read,benchmark=position_velocity_linear_update"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketSummary.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_ecs_chunk_sweep_summary PROPERTIES
            DEPENDS kb_ecs_market_runner_ecs_chunk_sweep_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_execution_policy_matrix_smoke
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/run-ecs-market-benchmark.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -Config "$<CONFIG>"
                -SkipConfigure
                -NoBuild
                -Scales 1000
                -OutputDir "${CMAKE_CURRENT_BINARY_DIR}/ecs_market_execution_policy_matrix_smoke"
                -Frames 1
                -Warmup 0
                -ChunkSizes 32KB
                -WorldProfiles balanced32k
                -ExecutionPolicies single_thread,parallel_chunks,parallel_ranges,simd_preferred
                -ReductionModes none,per_worker
                -BenchmarkCategories ecs_chunk_iteration
                -MaxMemoryPercent 70
        )
        set_tests_properties(kb_ecs_market_runner_execution_policy_matrix_smoke PROPERTIES
            LABELS "ecs;benchmark;smoke;regression"
            TIMEOUT 120
        )

        add_test(
            NAME kb_ecs_market_runner_execution_policy_matrix_manifest
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_execution_policy_matrix_smoke/21kb_ecs_market_manifest.json"
                "-DREQUIRED_STATUS=completed"
                "-DREQUIRED_CATEGORY=ecs_chunk_iteration"
                "-DREQUIRED_MODEL=dense_archetype_layout"
                "-DREQUIRED_FIELD_PATTERNS=world_profile=balanced32k,chunk_size=32KB,execution_policy=single_thread,execution_policy=parallel_chunks,execution_policy=parallel_ranges,execution_policy=simd_preferred,reduction_mode=none,reduction_mode=per_worker"
                "-DREQUIRED_NUMERIC_FIELDS=measured_memory_bytes,chunk_capacity,estimated_chunks,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_NONEMPTY_STRING_FIELDS=commit,branch,build_config,compiler_id,compiler_version,compiler_flags,cpu,run_simd_backend"
                "-DREQUIRED_EXACT_NUMERIC_FIELDS=scale=1000"
                "-DREQUIRED_BOOL_FIELDS=measured_memory_guard_pass=true"
                "-DREQUIRED_EXACT_ROWS=8"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketManifest.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_execution_policy_matrix_manifest PROPERTIES
            DEPENDS kb_ecs_market_runner_execution_policy_matrix_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_execution_policy_matrix_summary
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_execution_policy_matrix_smoke/21kb_ecs_market_summary.csv"
                "-DREQUIRED_EXACT_ROWS=16"
                "-DREQUIRED_COLUMNS=engine,scale,world_profile,chunk_size_profile,chunk_size_bytes,requested_category,benchmark,category,entities,frames,effective_frames,execution_policy,query_reduction_mode,requested_reduction_mode,simd_backend,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,workload_bytes_per_frame,workload_bytes_per_second,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,measured_memory_bytes,measured_memory_guard_pass,cpu,run_simd_backend,run_simd_backend_lane_count,thread_count,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_NONEMPTY_COLUMNS=cpu,run_simd_backend,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_POSITIVE_COLUMNS=entities,effective_frames,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,workload_bytes_per_frame,workload_bytes_per_second,measured_memory_bytes,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_TRUE_COLUMNS=measured_memory_guard_pass,frame_budget_pass_avg,frame_budget_pass_p95"
                "-DREQUIRED_CONSTANT_FIELDS=engine=21kb,scale=1000,world_profile=balanced32k,chunk_size_profile=32KB,chunk_size_bytes=32768,requested_category=ecs_chunk_iteration,category=ecs_chunk_iteration,build_config=Release"
                "-DREQUIRED_COLUMN_VALUES=execution_policy=single_thread,execution_policy=parallel_chunks,execution_policy=parallel_ranges,execution_policy=simd_preferred,query_reduction_mode=none,query_reduction_mode=per_worker,requested_reduction_mode=none,requested_reduction_mode=per_worker,benchmark=position_velocity_batch_read,benchmark=position_velocity_linear_update"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketSummary.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_execution_policy_matrix_summary PROPERTIES
            DEPENDS kb_ecs_market_runner_execution_policy_matrix_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_transform_no_hierarchy_smoke
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/run-ecs-market-benchmark.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -Config "$<CONFIG>"
                -SkipConfigure
                -NoBuild
                -Scales 1000
                -OutputDir "${CMAKE_CURRENT_BINARY_DIR}/ecs_market_transform_no_hierarchy_smoke"
                -Frames 1
                -Warmup 0
                -ChunkSizes 32KB
                -WorldProfiles balanced32k
                -ExecutionPolicies parallel_chunks
                -BenchmarkCategories transform_no_hierarchy
                -MaxMemoryPercent 70
        )
        set_tests_properties(kb_ecs_market_runner_transform_no_hierarchy_smoke PROPERTIES
            LABELS "ecs;benchmark;smoke;regression"
            TIMEOUT 60
        )

        add_test(
            NAME kb_ecs_market_runner_transform_no_hierarchy_manifest
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_transform_no_hierarchy_smoke/21kb_ecs_market_manifest.json"
                "-DREQUIRED_STATUS=completed"
                "-DREQUIRED_CATEGORY=transform_no_hierarchy"
                "-DREQUIRED_MODEL=dense_archetype_layout"
                "-DREQUIRED_FIELD_PATTERNS=world_profile=balanced32k,chunk_size=32KB,execution_policy=parallel_chunks"
                "-DREQUIRED_NUMERIC_FIELDS=measured_memory_bytes,chunk_capacity,estimated_chunks,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_NONEMPTY_STRING_FIELDS=commit,branch,build_config,compiler_id,compiler_version,compiler_flags,cpu,run_simd_backend"
                "-DREQUIRED_EXACT_NUMERIC_FIELDS=scale=1000,effective_frames=1"
                "-DREQUIRED_BOOL_FIELDS=measured_memory_guard_pass=true"
                "-DREQUIRED_EXACT_ROWS=1"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketManifest.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_transform_no_hierarchy_manifest PROPERTIES
            DEPENDS kb_ecs_market_runner_transform_no_hierarchy_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_transform_no_hierarchy_summary
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_transform_no_hierarchy_smoke/21kb_ecs_market_summary.csv"
                "-DREQUIRED_EXACT_ROWS=1"
                "-DREQUIRED_COLUMNS=engine,scale,world_profile,chunk_size_profile,chunk_size_bytes,requested_category,benchmark,category,entities,frames,effective_frames,execution_policy,query_reduction_mode,simd_backend,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,workload_bytes_per_frame,workload_bytes_per_second,memory_estimate_model,memory_estimate_category,estimated_memory_bytes,measured_memory_bytes,measured_memory_guard_pass,chunk_capacity,estimated_chunks,telemetry_entity_count,telemetry_query_executions,telemetry_query_batches,telemetry_query_entities_visited,telemetry_query_bytes_touched,telemetry_query_elapsed_nanoseconds,telemetry_query_bytes_per_second,telemetry_query_parallel_executions,telemetry_query_worker_slots,telemetry_query_worker_active_slots,telemetry_query_worker_utilization_percent,telemetry_compat_mutable_iterations,telemetry_compat_mutable_entities_visited,storage_chunk_size_bytes,storage_chunks,storage_capacity,storage_used_bytes,storage_occupancy_percent,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,cpu,run_simd_backend,run_simd_backend_lane_count,thread_count,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_NONEMPTY_COLUMNS=cpu,run_simd_backend,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_POSITIVE_COLUMNS=entities,effective_frames,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,workload_bytes_per_frame,workload_bytes_per_second,estimated_memory_bytes,measured_memory_bytes,chunk_capacity,estimated_chunks,telemetry_entity_count,telemetry_query_executions,telemetry_query_batches,telemetry_query_entities_visited,telemetry_query_bytes_touched,telemetry_query_elapsed_nanoseconds,telemetry_query_bytes_per_second,telemetry_query_parallel_executions,telemetry_query_worker_slots,telemetry_query_worker_active_slots,telemetry_query_worker_utilization_percent,storage_chunk_size_bytes,storage_chunks,storage_capacity,storage_used_bytes,storage_occupancy_percent,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_ZERO_COLUMNS=telemetry_compat_mutable_iterations,telemetry_compat_mutable_entities_visited"
                "-DREQUIRED_TRUE_COLUMNS=measured_memory_guard_pass,frame_budget_pass_avg,frame_budget_pass_p95"
                "-DREQUIRED_CONSTANT_FIELDS=engine=21kb,scale=1000,world_profile=balanced32k,chunk_size_profile=32KB,chunk_size_bytes=32768,requested_category=transform_no_hierarchy,benchmark=transform_local_to_world_no_hierarchy,category=transform_no_hierarchy,entities=1000,frames=1,effective_frames=1,execution_policy=parallel_chunks,query_reduction_mode=none,memory_estimate_model=dense_archetype_layout,memory_estimate_category=transform_no_hierarchy,build_config=Release"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketSummary.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_transform_no_hierarchy_summary PROPERTIES
            DEPENDS kb_ecs_market_runner_transform_no_hierarchy_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_scene_render_sync_smoke
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/run-ecs-market-benchmark.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -Config "$<CONFIG>"
                -SkipConfigure
                -NoBuild
                -Scales 1000
                -OutputDir "${CMAKE_CURRENT_BINARY_DIR}/ecs_market_scene_render_sync_smoke"
                -Frames 1
                -Warmup 0
                -ChunkSizes 32KB
                -WorldProfiles balanced32k
                -ExecutionPolicies parallel_chunks
                -BenchmarkCategories scene_render_sync
                -MaxMemoryPercent 70
        )
        set_tests_properties(kb_ecs_market_runner_scene_render_sync_smoke PROPERTIES
            LABELS "ecs;benchmark;smoke;regression"
            TIMEOUT 60
        )

        add_test(
            NAME kb_ecs_market_runner_scene_render_sync_manifest
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_scene_render_sync_smoke/21kb_ecs_market_manifest.json"
                "-DREQUIRED_STATUS=completed"
                "-DREQUIRED_CATEGORY=scene_render_sync"
                "-DREQUIRED_MODEL=scene_renderer_component_footprint"
                "-DREQUIRED_FIELD_PATTERNS=world_profile=balanced32k,chunk_size=32KB,execution_policy=parallel_chunks"
                "-DREQUIRED_NUMERIC_FIELDS=measured_memory_bytes,estimated_component_payload_bytes,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_NONEMPTY_STRING_FIELDS=commit,branch,build_config,compiler_id,compiler_version,compiler_flags,cpu,run_simd_backend"
                "-DREQUIRED_EXACT_NUMERIC_FIELDS=scale=1000,effective_frames=1"
                "-DREQUIRED_BOOL_FIELDS=measured_memory_guard_pass=true"
                "-DREQUIRED_EXACT_ROWS=1"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketManifest.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_scene_render_sync_manifest PROPERTIES
            DEPENDS kb_ecs_market_runner_scene_render_sync_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_scene_render_sync_summary
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_scene_render_sync_smoke/21kb_ecs_market_summary.csv"
                "-DREQUIRED_EXACT_ROWS=2"
                "-DREQUIRED_COLUMNS=engine,scale,world_profile,chunk_size_profile,chunk_size_bytes,requested_category,benchmark,category,entities,frames,effective_frames,execution_policy,query_reduction_mode,simd_backend,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,workload_bytes_per_frame,workload_bytes_per_second,scene_render_sync_visible_renderer_count,scene_render_sync_camera_count,scene_render_sync_light_count,scene_render_sync_footprint_bytes,memory_estimate_model,memory_estimate_category,estimated_memory_bytes,measured_memory_bytes,measured_memory_guard_pass,telemetry_entity_count,telemetry_archetype_count,telemetry_compat_mutable_iterations,telemetry_compat_mutable_entities_visited,storage_chunk_size_bytes,storage_chunks,storage_capacity,storage_used_bytes,storage_occupancy_percent,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,cpu,run_simd_backend,run_simd_backend_lane_count,thread_count,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_NONEMPTY_COLUMNS=cpu,run_simd_backend,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_POSITIVE_COLUMNS=entities,effective_frames,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,workload_bytes_per_frame,workload_bytes_per_second,scene_render_sync_visible_renderer_count,scene_render_sync_camera_count,scene_render_sync_light_count,scene_render_sync_footprint_bytes,estimated_memory_bytes,measured_memory_bytes,telemetry_entity_count,telemetry_archetype_count,storage_chunk_size_bytes,storage_chunks,storage_capacity,storage_used_bytes,storage_occupancy_percent,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_ZERO_COLUMNS=telemetry_compat_mutable_iterations,telemetry_compat_mutable_entities_visited"
                "-DREQUIRED_TRUE_COLUMNS=measured_memory_guard_pass,frame_budget_pass_avg,frame_budget_pass_p95"
                "-DREQUIRED_CONSTANT_FIELDS=engine=21kb,scale=1000,world_profile=balanced32k,chunk_size_profile=32KB,chunk_size_bytes=32768,requested_category=scene_render_sync,category=scene_render_sync,frames=1,effective_frames=1,execution_policy=scene_component_visitor,query_reduction_mode=none,memory_estimate_model=scene_renderer_component_footprint,memory_estimate_category=scene_render_sync,build_config=Release"
                "-DREQUIRED_COLUMN_VALUES=benchmark=scene_visible_renderer_iteration,benchmark=scene_camera_light_iteration,entities=800,entities=88"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketSummary.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_scene_render_sync_summary PROPERTIES
            DEPENDS kb_ecs_market_runner_scene_render_sync_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_query_fanout_smoke
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/run-ecs-market-benchmark.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -Config "$<CONFIG>"
                -SkipConfigure
                -NoBuild
                -Scales 1000
                -OutputDir "${CMAKE_CURRENT_BINARY_DIR}/ecs_market_query_fanout_smoke"
                -Frames 1
                -Warmup 0
                -ChunkSizes 32KB
                -WorldProfiles balanced32k
                -ExecutionPolicies parallel_chunks
                -BenchmarkCategories query_fanout
                -MaxMemoryPercent 70
        )
        set_tests_properties(kb_ecs_market_runner_query_fanout_smoke PROPERTIES
            LABELS "ecs;benchmark;smoke;regression"
            TIMEOUT 60
        )

        add_test(
            NAME kb_ecs_market_runner_query_fanout_manifest
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_query_fanout_smoke/21kb_ecs_market_manifest.json"
                "-DREQUIRED_STATUS=completed"
                "-DREQUIRED_CATEGORY=query_fanout"
                "-DREQUIRED_MODEL=query_fanout_component_columns"
                "-DREQUIRED_FIELD_PATTERNS=world_profile=balanced32k,chunk_size=32KB,execution_policy=parallel_chunks"
                "-DREQUIRED_NUMERIC_FIELDS=measured_memory_bytes,estimated_component_payload_bytes,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_NONEMPTY_STRING_FIELDS=commit,branch,build_config,compiler_id,compiler_version,compiler_flags,cpu,run_simd_backend"
                "-DREQUIRED_EXACT_NUMERIC_FIELDS=scale=1000,effective_frames=1"
                "-DREQUIRED_BOOL_FIELDS=measured_memory_guard_pass=true"
                "-DREQUIRED_EXACT_ROWS=1"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketManifest.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_query_fanout_manifest PROPERTIES
            DEPENDS kb_ecs_market_runner_query_fanout_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_query_fanout_summary
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_query_fanout_smoke/21kb_ecs_market_summary.csv"
                "-DREQUIRED_EXACT_ROWS=8"
                "-DREQUIRED_COLUMNS=engine,scale,world_profile,chunk_size_profile,chunk_size_bytes,requested_category,benchmark,category,entities,frames,effective_frames,execution_policy,query_reduction_mode,simd_backend,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,workload_bytes_per_frame,workload_bytes_per_second,memory_estimate_model,memory_estimate_category,estimated_memory_bytes,measured_memory_bytes,measured_memory_guard_pass,telemetry_entity_count,telemetry_query_executions,telemetry_query_batches,telemetry_query_entities_visited,telemetry_query_bytes_touched,telemetry_query_elapsed_nanoseconds,telemetry_query_bytes_per_second,telemetry_compat_mutable_iterations,telemetry_compat_mutable_entities_visited,storage_chunk_size_bytes,storage_chunks,storage_capacity,storage_used_bytes,storage_occupancy_percent,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,cpu,run_simd_backend,run_simd_backend_lane_count,thread_count,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_NONEMPTY_COLUMNS=cpu,run_simd_backend,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_POSITIVE_COLUMNS=entities,effective_frames,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,workload_bytes_per_frame,workload_bytes_per_second,estimated_memory_bytes,measured_memory_bytes,telemetry_entity_count,telemetry_query_executions,telemetry_query_batches,telemetry_query_entities_visited,telemetry_query_bytes_touched,telemetry_query_elapsed_nanoseconds,telemetry_query_bytes_per_second,storage_chunk_size_bytes,storage_chunks,storage_capacity,storage_used_bytes,storage_occupancy_percent,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_TRUE_COLUMNS=measured_memory_guard_pass,frame_budget_pass_avg,frame_budget_pass_p95"
                "-DREQUIRED_ZERO_COLUMNS=telemetry_compat_mutable_iterations,telemetry_compat_mutable_entities_visited"
                "-DREQUIRED_CONSTANT_FIELDS=engine=21kb,scale=1000,world_profile=balanced32k,chunk_size_profile=32KB,chunk_size_bytes=32768,requested_category=query_fanout,category=query_fanout,entities=1000,frames=1,effective_frames=1,execution_policy=single_thread_batch,query_reduction_mode=none,memory_estimate_model=query_fanout_component_columns,memory_estimate_category=query_fanout,build_config=Release"
                "-DREQUIRED_COLUMN_VALUES=benchmark=query_fanout_hot_1_components,benchmark=query_fanout_hot_2_components,benchmark=query_fanout_hot_4_components,benchmark=query_fanout_hot_8_components,benchmark=query_fanout_cold_1_components,benchmark=query_fanout_cold_2_components,benchmark=query_fanout_cold_4_components,benchmark=query_fanout_cold_8_components,workload_bytes_per_frame=4000,workload_bytes_per_frame=8000,workload_bytes_per_frame=16000,workload_bytes_per_frame=32000"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketSummary.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_query_fanout_summary PROPERTIES
            DEPENDS kb_ecs_market_runner_query_fanout_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_sparse_query_smoke
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/run-ecs-market-benchmark.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -Config "$<CONFIG>"
                -SkipConfigure
                -NoBuild
                -Scales 1000
                -OutputDir "${CMAKE_CURRENT_BINARY_DIR}/ecs_market_sparse_query_smoke"
                -Frames 1
                -Warmup 0
                -ChunkSizes 32KB
                -WorldProfiles balanced32k
                -ExecutionPolicies parallel_chunks
                -BenchmarkCategories sparse_query
                -MaxMemoryPercent 70
        )
        set_tests_properties(kb_ecs_market_runner_sparse_query_smoke PROPERTIES
            LABELS "ecs;benchmark;smoke;regression"
            TIMEOUT 60
        )

        add_test(
            NAME kb_ecs_market_runner_sparse_query_manifest
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_sparse_query_smoke/21kb_ecs_market_manifest.json"
                "-DREQUIRED_STATUS=completed"
                "-DREQUIRED_CATEGORY=sparse_query"
                "-DREQUIRED_MODEL=sparse_query_archetype_matrix"
                "-DREQUIRED_FIELD_PATTERNS=world_profile=balanced32k,chunk_size=32KB,execution_policy=parallel_chunks"
                "-DREQUIRED_NUMERIC_FIELDS=measured_memory_bytes,estimated_component_payload_bytes,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_NONEMPTY_STRING_FIELDS=commit,branch,build_config,compiler_id,compiler_version,compiler_flags,cpu,run_simd_backend"
                "-DREQUIRED_EXACT_NUMERIC_FIELDS=scale=1000,effective_frames=1"
                "-DREQUIRED_BOOL_FIELDS=measured_memory_guard_pass=true"
                "-DREQUIRED_EXACT_ROWS=1"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketManifest.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_sparse_query_manifest PROPERTIES
            DEPENDS kb_ecs_market_runner_sparse_query_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_sparse_query_summary
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_sparse_query_smoke/21kb_ecs_market_summary.csv"
                "-DREQUIRED_EXACT_ROWS=2"
                "-DREQUIRED_COLUMNS=engine,scale,world_profile,chunk_size_profile,chunk_size_bytes,requested_category,benchmark,category,entities,frames,effective_frames,execution_policy,query_reduction_mode,simd_backend,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,workload_bytes_per_frame,workload_bytes_per_second,memory_estimate_model,memory_estimate_category,estimated_memory_bytes,measured_memory_bytes,measured_memory_guard_pass,telemetry_entity_count,telemetry_archetype_count,telemetry_query_plan_requests,telemetry_query_cache_hits,telemetry_query_cache_misses,telemetry_structural_changes,telemetry_total_structural_changes,telemetry_compat_mutable_iterations,telemetry_compat_mutable_entities_visited,storage_chunk_size_bytes,storage_chunks,storage_capacity,storage_used_bytes,storage_occupancy_percent,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,cpu,run_simd_backend,run_simd_backend_lane_count,thread_count,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_NONEMPTY_COLUMNS=cpu,run_simd_backend,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_POSITIVE_COLUMNS=entities,effective_frames,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,workload_bytes_per_frame,workload_bytes_per_second,estimated_memory_bytes,measured_memory_bytes,telemetry_entity_count,telemetry_archetype_count,telemetry_query_plan_requests,telemetry_query_cache_misses,telemetry_structural_changes,telemetry_total_structural_changes,storage_chunk_size_bytes,storage_chunks,storage_capacity,storage_used_bytes,storage_occupancy_percent,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_TRUE_COLUMNS=measured_memory_guard_pass,frame_budget_pass_avg,frame_budget_pass_p95"
                "-DREQUIRED_ZERO_COLUMNS=telemetry_compat_mutable_iterations,telemetry_compat_mutable_entities_visited"
                "-DREQUIRED_CONSTANT_FIELDS=engine=21kb,scale=1000,world_profile=balanced32k,chunk_size_profile=32KB,chunk_size_bytes=32768,requested_category=sparse_query,category=sparse_query,frames=1,effective_frames=1,query_reduction_mode=none,memory_estimate_model=sparse_query_archetype_matrix,memory_estimate_category=sparse_query,build_config=Release"
                "-DREQUIRED_COLUMN_VALUES=benchmark=sparse_query_filtering,benchmark=sparse_query_churn_cached_filtering,entities=128,entities=8320,execution_policy=single_thread_batch,execution_policy=query_cache_churn_batch,telemetry_query_cache_hits=1,telemetry_query_plan_requests=1,telemetry_query_plan_requests=2"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketSummary.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_sparse_query_summary PROPERTIES
            DEPENDS kb_ecs_market_runner_sparse_query_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_scheduler_smoke
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/run-ecs-market-benchmark.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -Config "$<CONFIG>"
                -SkipConfigure
                -NoBuild
                -Scales 1000
                -OutputDir "${CMAKE_CURRENT_BINARY_DIR}/ecs_market_scheduler_smoke"
                -Frames 1
                -Warmup 0
                -ChunkSizes 32KB
                -WorldProfiles balanced32k
                -ExecutionPolicies parallel_chunks
                -BenchmarkCategories ecs_system_scheduler
                -MaxMemoryPercent 70
        )
        set_tests_properties(kb_ecs_market_runner_scheduler_smoke PROPERTIES
            LABELS "ecs;benchmark;smoke;regression"
            TIMEOUT 60
        )

        add_test(
            NAME kb_ecs_market_runner_scheduler_manifest
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_scheduler_smoke/21kb_ecs_market_manifest.json"
                "-DREQUIRED_STATUS=completed"
                "-DREQUIRED_CATEGORY=ecs_system_scheduler"
                "-DREQUIRED_MODEL=scheduler_state_slots"
                "-DREQUIRED_FIELD_PATTERNS=world_profile=balanced32k,chunk_size=32KB,execution_policy=parallel_chunks"
                "-DREQUIRED_NUMERIC_FIELDS=measured_memory_bytes,estimated_component_payload_bytes,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_NONEMPTY_STRING_FIELDS=commit,branch,build_config,compiler_id,compiler_version,compiler_flags,cpu,run_simd_backend"
                "-DREQUIRED_EXACT_NUMERIC_FIELDS=scale=1000,effective_frames=1"
                "-DREQUIRED_BOOL_FIELDS=measured_memory_guard_pass=true"
                "-DREQUIRED_EXACT_ROWS=1"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketManifest.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_scheduler_manifest PROPERTIES
            DEPENDS kb_ecs_market_runner_scheduler_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_scheduler_summary
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_scheduler_smoke/21kb_ecs_market_summary.csv"
                "-DREQUIRED_EXACT_ROWS=3"
                "-DREQUIRED_COLUMNS=engine,scale,world_profile,chunk_size_profile,chunk_size_bytes,requested_category,benchmark,category,entities,frames,effective_frames,execution_policy,query_reduction_mode,simd_backend,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,workload_bytes_per_frame,workload_bytes_per_second,memory_estimate_model,memory_estimate_category,estimated_memory_bytes,measured_memory_bytes,measured_memory_guard_pass,telemetry_entity_count,storage_chunk_size_bytes,storage_chunks,storage_capacity,storage_used_bytes,storage_occupancy_percent,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,cpu,run_simd_backend,run_simd_backend_lane_count,thread_count,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_NONEMPTY_COLUMNS=cpu,run_simd_backend,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_POSITIVE_COLUMNS=entities,effective_frames,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,workload_bytes_per_frame,workload_bytes_per_second,estimated_memory_bytes,measured_memory_bytes,telemetry_entity_count,storage_chunk_size_bytes,storage_chunks,storage_capacity,storage_used_bytes,storage_occupancy_percent,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_TRUE_COLUMNS=measured_memory_guard_pass,frame_budget_pass_avg,frame_budget_pass_p95"
                "-DREQUIRED_CONSTANT_FIELDS=engine=21kb,scale=1000,world_profile=balanced32k,chunk_size_profile=32KB,chunk_size_bytes=32768,requested_category=ecs_system_scheduler,category=ecs_system_scheduler,frames=1,effective_frames=1,execution_policy=system_scheduler_dependency_chain,query_reduction_mode=none,memory_estimate_model=scheduler_state_slots,memory_estimate_category=ecs_system_scheduler,build_config=Release"
                "-DREQUIRED_COLUMN_VALUES=benchmark=system_chain_16_systems,benchmark=system_chain_64_systems,benchmark=system_chain_256_systems,entities=16,entities=64,entities=256,workload_bytes_per_frame=256,workload_bytes_per_frame=1024,workload_bytes_per_frame=4096"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketSummary.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_scheduler_summary PROPERTIES
            DEPENDS kb_ecs_market_runner_scheduler_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_hierarchy_smoke
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/run-ecs-market-benchmark.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -Config "$<CONFIG>"
                -SkipConfigure
                -NoBuild
                -Scales 1000
                -OutputDir "${CMAKE_CURRENT_BINARY_DIR}/ecs_market_hierarchy_smoke"
                -Frames 1
                -Warmup 0
                -ChunkSizes 32KB
                -WorldProfiles balanced32k
                -ExecutionPolicies parallel_chunks
                -BenchmarkCategories transform_hierarchy
                -MaxMemoryPercent 70
        )
        set_tests_properties(kb_ecs_market_runner_hierarchy_smoke PROPERTIES
            LABELS "ecs;benchmark;smoke;regression"
            TIMEOUT 60
        )

        add_test(
            NAME kb_ecs_market_runner_hierarchy_manifest
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_hierarchy_smoke/21kb_ecs_market_manifest.json"
                "-DREQUIRED_STATUS=completed"
                "-DREQUIRED_CATEGORY=transform_hierarchy"
                "-DREQUIRED_MODEL=hierarchy_transform_columns"
                "-DREQUIRED_FIELD_PATTERNS=world_profile=balanced32k,chunk_size=32KB,execution_policy=parallel_chunks"
                "-DREQUIRED_NUMERIC_FIELDS=measured_memory_bytes,estimated_component_payload_bytes,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_NONEMPTY_STRING_FIELDS=commit,branch,build_config,compiler_id,compiler_version,compiler_flags,cpu,run_simd_backend"
                "-DREQUIRED_EXACT_NUMERIC_FIELDS=scale=1000,hierarchy_entities=1024,effective_frames=1"
                "-DREQUIRED_BOOL_FIELDS=measured_memory_guard_pass=true"
                "-DREQUIRED_EXACT_ROWS=1"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketManifest.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_hierarchy_manifest PROPERTIES
            DEPENDS kb_ecs_market_runner_hierarchy_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_hierarchy_summary
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_hierarchy_smoke/21kb_ecs_market_summary.csv"
                "-DREQUIRED_EXACT_ROWS=23"
                "-DREQUIRED_COLUMNS=engine,scale,world_profile,chunk_size_profile,chunk_size_bytes,requested_category,benchmark,category,entities,frames,effective_frames,execution_policy,query_reduction_mode,simd_backend,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,workload_bytes_per_frame,workload_bytes_per_second,transform_hierarchy_depth,transform_hierarchy_fanout,transform_hierarchy_dirty_percent,transform_hierarchy_dirty_root_count,transform_hierarchy_expected_updated_entities,transform_hierarchy_updated_entities,transform_hierarchy_inspected_entities,transform_hierarchy_root_fast_path_count,transform_hierarchy_translated_parent_fast_path_count,transform_hierarchy_unrotated_parent_fast_path_count,transform_hierarchy_unit_scale_parent_fast_path_count,transform_hierarchy_uniform_scale_parent_fast_path_count,transform_hierarchy_static_local_rotation_fast_path_count,transform_hierarchy_sparse_flush_count,transform_hierarchy_dirty_list_flush_count,transform_hierarchy_dirty_list_flush_entities,transform_hierarchy_batch_flush_count,transform_hierarchy_flushed_entities,transform_hierarchy_dirty_frontier_count,transform_hierarchy_parallel_batch_count,transform_hierarchy_parallel_chunk_count,transform_hierarchy_parallel_entities,transform_hierarchy_worker_count,transform_hierarchy_parallel_flush_count,transform_hierarchy_parallel_flush_chunk_count,transform_hierarchy_parallel_flush_entities,transform_hierarchy_parallel_flush_worker_count,transform_hierarchy_cache_build_ns,transform_hierarchy_entry_build_ns,transform_hierarchy_kernel_apply_ns,transform_hierarchy_frontier_append_ns,transform_hierarchy_propagate_ns,transform_hierarchy_flush_write_ns,transform_hierarchy_backend_mark_ns,transform_hierarchy_update_ns,transform_hierarchy_flush_ns,transform_hierarchy_budget_limit,transform_hierarchy_budget_exhausted,memory_estimate_model,memory_estimate_category,estimated_memory_bytes,measured_memory_bytes,measured_memory_guard_pass,telemetry_entity_count,telemetry_compat_mutable_iterations,telemetry_compat_mutable_entities_visited,storage_chunk_size_bytes,storage_chunks,storage_capacity,storage_used_bytes,storage_occupancy_percent,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,cpu,run_simd_backend,run_simd_backend_lane_count,thread_count,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_NONEMPTY_COLUMNS=cpu,run_simd_backend,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_POSITIVE_COLUMNS=entities,effective_frames,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,workload_bytes_per_frame,workload_bytes_per_second,transform_hierarchy_updated_entities,transform_hierarchy_inspected_entities,estimated_memory_bytes,measured_memory_bytes,telemetry_entity_count,storage_chunk_size_bytes,storage_chunks,storage_capacity,storage_used_bytes,storage_occupancy_percent,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_TRUE_COLUMNS=measured_memory_guard_pass,frame_budget_pass_avg,frame_budget_pass_p95"
                "-DREQUIRED_ZERO_COLUMNS=telemetry_compat_mutable_iterations,telemetry_compat_mutable_entities_visited"
                "-DREQUIRED_CONSTANT_FIELDS=engine=21kb,scale=1000,world_profile=balanced32k,chunk_size_profile=32KB,chunk_size_bytes=32768,requested_category=transform_hierarchy,category=transform_hierarchy,frames=1,effective_frames=1,query_reduction_mode=none,memory_estimate_model=hierarchy_transform_columns,memory_estimate_category=transform_hierarchy,build_config=Release"
                "-DREQUIRED_COLUMN_VALUES=benchmark=transform_hierarchy_runtime_fast_paths,benchmark=transform_hierarchy_runtime_sparse_flush,benchmark=transform_hierarchy_runtime_frontier_pairs,benchmark=transform_hierarchy_runtime_frontier_depth_8,benchmark=transform_hierarchy_runtime_frontier_depth_8_dirty_4_roots,benchmark=transform_hierarchy_runtime_frontier_fanout_64,benchmark=transform_hierarchy_runtime_frontier_fanout_256,benchmark=transform_hierarchy_runtime_frontier_fanout_256_roots_64,benchmark=transform_hierarchy_runtime_frontier_fanout_256_roots_64_dirty_4_roots,benchmark=transform_hierarchy_runtime_frontier_fanout_256_roots_64_dirty_16_roots,benchmark=transform_hierarchy_runtime_frontier_fanout_256_roots_64_dirty_64_roots,benchmark=transform_hierarchy_dirty_1_percent_depth_8,benchmark=transform_hierarchy_dirty_10_percent_depth_8,benchmark=transform_hierarchy_dirty_100_percent_depth_8,benchmark=transform_hierarchy_dirty_1_percent_depth_32,benchmark=transform_hierarchy_dirty_10_percent_depth_32,benchmark=transform_hierarchy_dirty_100_percent_depth_32,benchmark=transform_hierarchy_dirty_1_percent_depth_128,benchmark=transform_hierarchy_dirty_10_percent_depth_128,benchmark=transform_hierarchy_dirty_100_percent_depth_128,benchmark=transform_hierarchy_wide_fanout_64_dirty_1_percent,benchmark=transform_hierarchy_wide_fanout_64_dirty_10_percent,benchmark=transform_hierarchy_wide_fanout_64_dirty_100_percent,execution_policy=scene_transform_fast_paths,execution_policy=scene_transform_sparse_flush,execution_policy=scene_transform_frontier_pairs,execution_policy=scene_transform_frontier_depth,execution_policy=scene_transform_frontier_multi_root,execution_policy=scene_transform_frontier_fanout,execution_policy=layered_dirty_ranges_serial,execution_policy=wide_fanout_dirty_ranges_serial,transform_hierarchy_depth=8,transform_hierarchy_depth=32,transform_hierarchy_depth=128,transform_hierarchy_fanout=64,transform_hierarchy_fanout=256,transform_hierarchy_dirty_percent=1,transform_hierarchy_dirty_percent=10,transform_hierarchy_dirty_percent=100"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketSummary.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_hierarchy_summary PROPERTIES
            DEPENDS kb_ecs_market_runner_hierarchy_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_structural_smoke
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/run-ecs-market-benchmark.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -Config "$<CONFIG>"
                -SkipConfigure
                -NoBuild
                -Scales 1000
                -OutputDir "${CMAKE_CURRENT_BINARY_DIR}/ecs_market_structural_smoke"
                -Frames 1
                -Warmup 0
                -ChunkSizes 32KB
                -WorldProfiles balanced32k
                -ExecutionPolicies parallel_chunks
                -BenchmarkCategories structural_changes
                -MaxMemoryPercent 70
        )
        set_tests_properties(kb_ecs_market_runner_structural_smoke PROPERTIES
            LABELS "ecs;benchmark;smoke;regression"
            TIMEOUT 60
        )

        add_test(
            NAME kb_ecs_market_runner_structural_manifest
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_structural_smoke/21kb_ecs_market_manifest.json"
                "-DREQUIRED_STATUS=completed"
                "-DREQUIRED_CATEGORY=structural_changes"
                "-DREQUIRED_MODEL=structural_command_churn"
                "-DREQUIRED_FIELD_PATTERNS=world_profile=balanced32k,chunk_size=32KB,execution_policy=parallel_chunks"
                "-DREQUIRED_NUMERIC_FIELDS=measured_memory_bytes,estimated_component_payload_bytes,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_NONEMPTY_STRING_FIELDS=commit,branch,build_config,compiler_id,compiler_version,compiler_flags,cpu,run_simd_backend"
                "-DREQUIRED_EXACT_NUMERIC_FIELDS=scale=1000,structural_changes=4000,effective_frames=1"
                "-DREQUIRED_BOOL_FIELDS=measured_memory_guard_pass=true"
                "-DREQUIRED_EXACT_ROWS=1"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketManifest.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_structural_manifest PROPERTIES
            DEPENDS kb_ecs_market_runner_structural_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_structural_summary
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_structural_smoke/21kb_ecs_market_summary.csv"
                "-DREQUIRED_EXACT_ROWS=1"
                "-DREQUIRED_COLUMNS=engine,scale,world_profile,chunk_size_profile,chunk_size_bytes,requested_category,benchmark,category,entities,frames,effective_frames,execution_policy,query_reduction_mode,simd_backend,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,workload_bytes_per_frame,workload_bytes_per_second,structural_command_count,structural_create_command_count,structural_bulk_create_command_count,structural_destroy_command_count,structural_component_set_command_count,structural_component_remove_command_count,structural_parent_command_count,structural_component_bytes_copied,structural_playback_slices,structural_destroy_sync_slices,structural_destroyed_entities_applied,structural_slice_structural_budget,structural_slice_destroy_budget,structural_slice_component_set_budget,structural_slice_component_remove_budget,structural_slice_component_byte_budget,memory_estimate_model,memory_estimate_category,estimated_memory_bytes,measured_memory_bytes,measured_memory_guard_pass,telemetry_entity_count,telemetry_structural_changes,telemetry_total_structural_changes,telemetry_allocated_bytes,telemetry_used_bytes,telemetry_occupancy_percent,storage_chunk_size_bytes,storage_chunks,storage_capacity,storage_used_bytes,storage_occupancy_percent,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,cpu,run_simd_backend,run_simd_backend_lane_count,thread_count,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_NONEMPTY_COLUMNS=cpu,run_simd_backend,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_POSITIVE_COLUMNS=entities,effective_frames,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,workload_bytes_per_frame,workload_bytes_per_second,structural_command_count,structural_bulk_create_command_count,structural_destroy_command_count,structural_component_set_command_count,structural_component_remove_command_count,structural_component_bytes_copied,structural_playback_slices,structural_destroy_sync_slices,structural_destroyed_entities_applied,structural_slice_structural_budget,structural_slice_destroy_budget,structural_slice_component_set_budget,structural_slice_component_remove_budget,structural_slice_component_byte_budget,estimated_memory_bytes,measured_memory_bytes,telemetry_entity_count,telemetry_structural_changes,telemetry_total_structural_changes,telemetry_allocated_bytes,telemetry_used_bytes,telemetry_occupancy_percent,storage_chunk_size_bytes,storage_chunks,storage_capacity,storage_used_bytes,storage_occupancy_percent,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_TRUE_COLUMNS=measured_memory_guard_pass,frame_budget_pass_avg,frame_budget_pass_p95"
                "-DREQUIRED_CONSTANT_FIELDS=engine=21kb,scale=1000,world_profile=balanced32k,chunk_size_profile=32KB,chunk_size_bytes=32768,requested_category=structural_changes,benchmark=structural_changes_command_buffer,category=structural_changes,entities=4000,frames=1,effective_frames=1,execution_policy=command_buffer_batched,query_reduction_mode=none,memory_estimate_model=structural_command_churn,memory_estimate_category=structural_changes,structural_command_count=4,structural_create_command_count=0,structural_bulk_create_command_count=1,structural_destroy_command_count=1000,structural_component_set_command_count=1000,structural_component_remove_command_count=1000,structural_parent_command_count=0,structural_component_bytes_copied=24000,structural_destroyed_entities_applied=1000,structural_slice_structural_budget=1,structural_slice_destroy_budget=250,structural_slice_component_set_budget=250,structural_slice_component_remove_budget=250,structural_slice_component_byte_budget=24000,build_config=Release"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketSummary.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_structural_summary PROPERTIES
            DEPENDS kb_ecs_market_runner_structural_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_prefab_spawn_smoke
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/run-ecs-market-benchmark.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -Config "$<CONFIG>"
                -SkipConfigure
                -NoBuild
                -Scales 1000
                -OutputDir "${CMAKE_CURRENT_BINARY_DIR}/ecs_market_prefab_spawn_smoke"
                -Frames 1
                -Warmup 0
                -ChunkSizes 32KB
                -WorldProfiles balanced32k
                -ExecutionPolicies parallel_chunks
                -BenchmarkCategories prefab_spawn
                -PrefabInstances 64
                -PrefabNodes 4
                -PrefabNames off
                -PrefabWorldHierarchy off
                -PrefabResultInstances off
                -MaxMemoryPercent 70
        )
        set_tests_properties(kb_ecs_market_runner_prefab_spawn_smoke PROPERTIES
            LABELS "ecs;benchmark;smoke;regression"
            TIMEOUT 60
        )

        add_test(
            NAME kb_ecs_market_runner_prefab_spawn_manifest
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_prefab_spawn_smoke/21kb_ecs_market_manifest.json"
                "-DREQUIRED_STATUS=completed"
                "-DREQUIRED_CATEGORY=prefab_spawn"
                "-DREQUIRED_MODEL=prefab_spawn_selected_matrix_max"
                "-DREQUIRED_FIELD_PATTERNS=world_profile=balanced32k,chunk_size=32KB,execution_policy=parallel_chunks"
                "-DREQUIRED_NUMERIC_FIELDS=measured_memory_bytes,estimated_component_payload_bytes,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_NONEMPTY_STRING_FIELDS=commit,branch,build_config,compiler_id,compiler_version,compiler_flags,cpu,run_simd_backend"
                "-DREQUIRED_EXACT_NUMERIC_FIELDS=scale=1000,effective_frames=1"
                "-DREQUIRED_BOOL_FIELDS=measured_memory_guard_pass=true"
                "-DREQUIRED_EXACT_ROWS=1"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketManifest.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_prefab_spawn_manifest PROPERTIES
            DEPENDS kb_ecs_market_runner_prefab_spawn_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_prefab_spawn_summary
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_prefab_spawn_smoke/21kb_ecs_market_summary.csv"
                "-DREQUIRED_EXACT_ROWS=2"
                "-DREQUIRED_COLUMNS=engine,scale,world_profile,chunk_size_profile,chunk_size_bytes,requested_category,benchmark,category,entities,frames,effective_frames,execution_policy,query_reduction_mode,simd_backend,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,workload_bytes_per_frame,workload_bytes_per_second,prefab_spawn_requested_instances,prefab_spawn_instantiated_instances,prefab_spawn_nodes_per_instance,prefab_spawn_entities_created,prefab_spawn_archetypes_touched,prefab_spawn_bulk_create_commands,prefab_spawn_component_set_commands,prefab_spawn_parent_commands,prefab_spawn_component_bytes_copied,prefab_spawn_chunks_allocated_delta,prefab_spawn_chunks_reused_delta,prefab_spawn_registered_instance_count,prefab_spawn_entity_create_ns,prefab_spawn_hierarchy_record_ns,prefab_spawn_name_assignment_ns,prefab_spawn_registry_resolve_ns,prefab_spawn_history_record_ns,memory_estimate_model,memory_estimate_category,estimated_memory_bytes,measured_memory_bytes,measured_memory_guard_pass,telemetry_entity_count,telemetry_structural_changes,telemetry_total_structural_changes,telemetry_allocated_bytes,telemetry_used_bytes,telemetry_occupancy_percent,storage_chunk_size_bytes,storage_chunks,storage_capacity,storage_used_bytes,storage_occupancy_percent,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,cpu,run_simd_backend,run_simd_backend_lane_count,thread_count,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_NONEMPTY_COLUMNS=cpu,run_simd_backend,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_POSITIVE_COLUMNS=entities,effective_frames,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,workload_bytes_per_frame,workload_bytes_per_second,prefab_spawn_requested_instances,prefab_spawn_instantiated_instances,prefab_spawn_nodes_per_instance,prefab_spawn_entities_created,prefab_spawn_archetypes_touched,prefab_spawn_bulk_create_commands,prefab_spawn_component_bytes_copied,prefab_spawn_entity_create_ns,prefab_spawn_hierarchy_record_ns,estimated_memory_bytes,measured_memory_bytes,telemetry_entity_count,telemetry_structural_changes,telemetry_total_structural_changes,telemetry_allocated_bytes,telemetry_used_bytes,telemetry_occupancy_percent,storage_chunk_size_bytes,storage_chunks,storage_capacity,storage_used_bytes,storage_occupancy_percent,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_TRUE_COLUMNS=measured_memory_guard_pass,frame_budget_pass_avg,frame_budget_pass_p95"
                "-DREQUIRED_CONSTANT_FIELDS=engine=21kb,scale=1000,world_profile=balanced32k,chunk_size_profile=32KB,chunk_size_bytes=32768,requested_category=prefab_spawn,category=prefab_spawn,entities=256,frames=1,effective_frames=1,execution_policy=prefab_bulk_spawn,query_reduction_mode=none,prefab_spawn_requested_instances=64,prefab_spawn_instantiated_instances=64,prefab_spawn_nodes_per_instance=4,prefab_spawn_entities_created=256,prefab_spawn_archetypes_touched=4,prefab_spawn_bulk_create_commands=4,prefab_spawn_component_set_commands=0,prefab_spawn_parent_commands=0,prefab_spawn_registered_instance_count=0,prefab_spawn_history_record_ns=0,memory_estimate_model=prefab_spawn_selected_matrix_max,memory_estimate_category=prefab_spawn,build_config=Release"
                "-DREQUIRED_COLUMN_VALUES=benchmark=prefab_spawn_loose_unnamed_scenehierarchy_batch_64_instances_4_nodes,benchmark=prefab_spawn_registered_unnamed_scenehierarchy_batch_64_instances_4_nodes"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketSummary.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_prefab_spawn_summary PROPERTIES
            DEPENDS kb_ecs_market_runner_prefab_spawn_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_prefab_override_smoke
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/run-ecs-market-benchmark.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -Config "$<CONFIG>"
                -SkipConfigure
                -NoBuild
                -Scales 1000
                -OutputDir "${CMAKE_CURRENT_BINARY_DIR}/ecs_market_prefab_override_smoke"
                -Frames 1
                -Warmup 0
                -ChunkSizes 32KB
                -WorldProfiles balanced32k
                -ExecutionPolicies parallel_chunks
                -BenchmarkCategories prefab_override
                -PrefabOverrideInstances 64
                -MaxMemoryPercent 70
        )
        set_tests_properties(kb_ecs_market_runner_prefab_override_smoke PROPERTIES
            LABELS "ecs;benchmark;smoke;regression"
            TIMEOUT 60
        )

        add_test(
            NAME kb_ecs_market_runner_prefab_override_manifest
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_prefab_override_smoke/21kb_ecs_market_manifest.json"
                "-DREQUIRED_STATUS=completed"
                "-DREQUIRED_CATEGORY=prefab_override"
                "-DREQUIRED_MODEL=prefab_override_selected_matrix_max"
                "-DREQUIRED_FIELD_PATTERNS=world_profile=balanced32k,chunk_size=32KB,execution_policy=parallel_chunks"
                "-DREQUIRED_NUMERIC_FIELDS=measured_memory_bytes,estimated_component_payload_bytes,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_NONEMPTY_STRING_FIELDS=commit,branch,build_config,compiler_id,compiler_version,compiler_flags,cpu,run_simd_backend"
                "-DREQUIRED_EXACT_NUMERIC_FIELDS=scale=1000,effective_frames=1"
                "-DREQUIRED_BOOL_FIELDS=measured_memory_guard_pass=true"
                "-DREQUIRED_EXACT_ROWS=1"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketManifest.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_prefab_override_manifest PROPERTIES
            DEPENDS kb_ecs_market_runner_prefab_override_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_prefab_override_summary
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_prefab_override_smoke/21kb_ecs_market_summary.csv"
                "-DREQUIRED_EXACT_ROWS=2"
                "-DREQUIRED_COLUMNS=engine,scale,world_profile,chunk_size_profile,chunk_size_bytes,requested_category,benchmark,category,entities,frames,effective_frames,execution_policy,query_reduction_mode,simd_backend,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,workload_bytes_per_frame,workload_bytes_per_second,prefab_override_instance_count,prefab_override_nodes_per_instance,prefab_override_target_nodes,prefab_override_entities_touched,prefab_override_component_bytes_touched,memory_estimate_model,memory_estimate_category,estimated_memory_bytes,measured_memory_bytes,measured_memory_guard_pass,telemetry_entity_count,telemetry_archetype_count,telemetry_structural_changes,telemetry_total_structural_changes,telemetry_allocated_bytes,telemetry_used_bytes,telemetry_occupancy_percent,storage_chunk_size_bytes,storage_chunks,storage_capacity,storage_used_bytes,storage_occupancy_percent,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,cpu,run_simd_backend,run_simd_backend_lane_count,thread_count,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_NONEMPTY_COLUMNS=cpu,run_simd_backend,build_config,compiler_id,compiler_version,compiler_flags,commit,branch"
                "-DREQUIRED_POSITIVE_COLUMNS=entities,effective_frames,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,workload_bytes_per_frame,workload_bytes_per_second,prefab_override_instance_count,prefab_override_nodes_per_instance,prefab_override_target_nodes,prefab_override_entities_touched,prefab_override_component_bytes_touched,estimated_memory_bytes,measured_memory_bytes,telemetry_entity_count,telemetry_archetype_count,telemetry_structural_changes,telemetry_total_structural_changes,telemetry_allocated_bytes,telemetry_used_bytes,telemetry_occupancy_percent,storage_chunk_size_bytes,storage_chunks,storage_capacity,storage_used_bytes,storage_occupancy_percent,simd_backend_lane_count,execution_worker_count,worker_utilization_percent,run_simd_backend_lane_count,thread_count"
                "-DREQUIRED_TRUE_COLUMNS=measured_memory_guard_pass,frame_budget_pass_avg,frame_budget_pass_p95"
                "-DREQUIRED_CONSTANT_FIELDS=engine=21kb,scale=1000,world_profile=balanced32k,chunk_size_profile=32KB,chunk_size_bytes=32768,requested_category=prefab_override,category=prefab_override,entities=64,frames=1,effective_frames=1,execution_policy=prefab_override,query_reduction_mode=none,prefab_override_instance_count=64,prefab_override_nodes_per_instance=4,prefab_override_target_nodes=64,prefab_override_entities_touched=64,memory_estimate_model=prefab_override_selected_matrix_max,memory_estimate_category=prefab_override,build_config=Release"
                "-DREQUIRED_COLUMN_VALUES=benchmark=prefab_override_revert_64_instances,benchmark=prefab_override_apply_64_instances"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketSummary.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_prefab_override_summary PROPERTIES
            DEPENDS kb_ecs_market_runner_prefab_override_smoke
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_external_baseline_import
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/run-ecs-market-benchmark.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -Config "$<CONFIG>"
                -SkipConfigure
                -NoBuild
                -Scales 1000
                -OutputDir "${CMAKE_CURRENT_BINARY_DIR}/ecs_market_external_baseline_import"
                -Frames 1
                -Warmup 0
                -ChunkSizes 32KB
                -WorldProfiles balanced32k
                -ExecutionPolicies single_thread
                -BenchmarkCategories raw_kernel
                -ExternalBaselineCsv "${CMAKE_CURRENT_SOURCE_DIR}/testdata/external_baseline_smoke.csv"
                -MaxMemoryPercent 70
        )
        set_tests_properties(kb_ecs_market_runner_external_baseline_import PROPERTIES
            LABELS "ecs;benchmark;regression"
            TIMEOUT 60
        )

        add_test(
            NAME kb_ecs_market_runner_external_baseline_manifest
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_external_baseline_import/21kb_ecs_market_manifest.json"
                "-DREQUIRED_STATUS=imported"
                "-DREQUIRED_CATEGORY=position_velocity_linear_update"
                "-DREQUIRED_MODEL=external_baseline_csv"
                "-DREQUIRED_FIELD_PATTERNS=engine=external_vector_runtime,world_profile=external,execution_policy=chunk_job,exact_code_path=chunk_job_position_velocity"
                "-DREQUIRED_BOOL_FIELDS=measured_memory_guard_pass=true"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketManifest.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_external_baseline_manifest PROPERTIES
            DEPENDS kb_ecs_market_runner_external_baseline_import
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_external_baseline_summary
            COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_market_external_baseline_import/external_baseline_summary.csv"
                "-DREQUIRED_EXACT_ROWS=1"
                "-DREQUIRED_COLUMNS=engine,scale,benchmark,dataset,entities,frames,warmup_frames,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,workload_bytes_per_frame,workload_bytes_per_second,cpu,thread_count,build_config,commit,exact_code_path,compiler_id,compiler_version,compiler_flags,memory_used_bytes,status,source_csv"
                "-DREQUIRED_NONEMPTY_COLUMNS=cpu,build_config,commit,exact_code_path,compiler_id,compiler_version,compiler_flags,status,source_csv"
                "-DREQUIRED_POSITIVE_COLUMNS=scale,entities,frames,time_ms_min,time_ms_avg,time_ms_p95,time_ms_p99,throughput_entities_per_second,workload_bytes_per_frame,workload_bytes_per_second,thread_count,memory_used_bytes"
                "-DREQUIRED_CONSTANT_FIELDS=engine=external_vector_runtime,scale=1000,benchmark=position_velocity_linear_update,dataset=dense_positions_velocities,build_config=Release,status=imported,exact_code_path=chunk_job_position_velocity,compiler_id=TestCompiler,compiler_version=1.0"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertMarketSummary.cmake"
        )
        set_tests_properties(kb_ecs_market_runner_external_baseline_summary PROPERTIES
            DEPENDS kb_ecs_market_runner_external_baseline_import
            LABELS "ecs;benchmark;regression"
        )

        add_test(
            NAME kb_ecs_market_runner_external_baseline_rejects_missing_metadata
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/tests/run-ecs-market-benchmark.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -Config "$<CONFIG>"
                -SkipConfigure
                -NoBuild
                -Scales 1000
                -OutputDir "${CMAKE_CURRENT_BINARY_DIR}/ecs_market_external_baseline_missing_metadata"
                -Frames 1
                -Warmup 0
                -ChunkSizes 32KB
                -WorldProfiles balanced32k
                -ExecutionPolicies single_thread
                -BenchmarkCategories raw_kernel
                -ExternalBaselineCsv "${CMAKE_CURRENT_SOURCE_DIR}/testdata/external_baseline_missing_metadata.csv"
                -MaxMemoryPercent 70
        )
        set_tests_properties(kb_ecs_market_runner_external_baseline_rejects_missing_metadata PROPERTIES
            LABELS "ecs;benchmark;regression"
            TIMEOUT 60
            WILL_FAIL TRUE
        )
    endif()

    add_test(
        NAME kb_ecs_benchmark_category_raw_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --entities 10000
            --frames 1
            --warmup 0
            --chunk-size 8KB
            --world-profile mobile8k
            --execution-policy parallel_chunks
            --validation off
            --market-scale
            --benchmark-category raw_kernel
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_category_raw_smoke.json"
    )
    set_tests_properties(kb_ecs_benchmark_category_raw_smoke PROPERTIES
        LABELS "ecs;benchmark;smoke"
        TIMEOUT 60
    )

    add_test(
        NAME kb_ecs_benchmark_category_raw_workload_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_raw_smoke.json"
            "-DRESULT_NAME=raw_position_velocity_linear_update"
            "-DREQUIRED_POSITIVE_FIELDS=entities,workload_bytes_per_frame"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_raw_workload_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_raw_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_mobile4k_profile_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --entities 1000
            --frames 1
            --warmup 0
            --world-profile mobile4k
            --execution-policy parallel_chunks
            --validation off
            --market-scale
            --benchmark-category ecs_chunk_iteration
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_mobile4k_profile_smoke.json"
    )
    set_tests_properties(kb_ecs_benchmark_mobile4k_profile_smoke PROPERTIES
        LABELS "ecs;benchmark;smoke"
        TIMEOUT 60
    )

    add_test(
        NAME kb_ecs_benchmark_mobile4k_profile_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_mobile4k_profile_smoke.json"
            "-DRESULT_NAME=position_velocity_batch_read"
            "-DREQUIRED_POSITIVE_FIELDS=entities,workload_bytes_per_frame"
            "-DREQUIRED_EXACT_FIELDS=storage_chunk_size_bytes=4096,execution_worker_count=2,execution_prefetch_distance=8"
            "-DREQUIRED_RUN_EXACT_FIELDS=thread_count=2,chunk_size_bytes=4096"
            "-DREQUIRED_STRING_FIELDS=world_profile=mobile4k"
            "-DREQUIRED_RUN_STRING_FIELDS=world_profile=mobile4k,chunk_size_profile=4KB"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_mobile4k_profile_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_mobile4k_profile_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_mobile8k_profile_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --entities 1000
            --frames 1
            --warmup 0
            --world-profile mobile8k
            --execution-policy parallel_chunks
            --validation off
            --market-scale
            --benchmark-category ecs_chunk_iteration
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_mobile8k_profile_smoke.json"
    )
    set_tests_properties(kb_ecs_benchmark_mobile8k_profile_smoke PROPERTIES
        LABELS "ecs;benchmark;smoke"
        TIMEOUT 60
    )

    add_test(
        NAME kb_ecs_benchmark_mobile8k_profile_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_mobile8k_profile_smoke.json"
            "-DRESULT_NAME=position_velocity_batch_read"
            "-DREQUIRED_POSITIVE_FIELDS=entities,workload_bytes_per_frame"
            "-DREQUIRED_EXACT_FIELDS=storage_chunk_size_bytes=8192,execution_worker_count=4,execution_prefetch_distance=12"
            "-DREQUIRED_RUN_EXACT_FIELDS=thread_count=4,chunk_size_bytes=8192"
            "-DREQUIRED_STRING_FIELDS=world_profile=mobile8k"
            "-DREQUIRED_RUN_STRING_FIELDS=world_profile=mobile8k,chunk_size_profile=8KB"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_mobile8k_profile_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_mobile8k_profile_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_mobile16k_profile_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --entities 1000
            --frames 1
            --warmup 0
            --world-profile mobile16k
            --execution-policy parallel_chunks
            --validation off
            --market-scale
            --benchmark-category ecs_chunk_iteration
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_mobile16k_profile_smoke.json"
    )
    set_tests_properties(kb_ecs_benchmark_mobile16k_profile_smoke PROPERTIES
        LABELS "ecs;benchmark;smoke"
        TIMEOUT 60
    )

    add_test(
        NAME kb_ecs_benchmark_mobile16k_profile_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_mobile16k_profile_smoke.json"
            "-DRESULT_NAME=position_velocity_batch_read"
            "-DREQUIRED_POSITIVE_FIELDS=entities,workload_bytes_per_frame"
            "-DREQUIRED_EXACT_FIELDS=storage_chunk_size_bytes=16384,execution_worker_count=4,execution_prefetch_distance=16"
            "-DREQUIRED_RUN_EXACT_FIELDS=thread_count=4,chunk_size_bytes=16384"
            "-DREQUIRED_STRING_FIELDS=world_profile=mobile16k"
            "-DREQUIRED_RUN_STRING_FIELDS=world_profile=mobile16k,chunk_size_profile=16KB"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_mobile16k_profile_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_mobile16k_profile_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_auto_transform_profile_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --entities 1000
            --frames 1
            --warmup 0
            --world-profile benchmarkauto
            --execution-policy parallel_chunks
            --validation off
            --market-scale
            --benchmark-category transform_no_hierarchy
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_benchmark_auto_transform_profile_smoke.json"
    )
    set_tests_properties(kb_ecs_benchmark_auto_transform_profile_smoke PROPERTIES
        LABELS "ecs;benchmark;smoke"
        TIMEOUT 60
    )

    add_test(
        NAME kb_ecs_benchmark_auto_transform_profile_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_benchmark_auto_transform_profile_smoke.json"
            "-DRESULT_NAME=transform_local_to_world_no_hierarchy"
            "-DREQUIRED_POSITIVE_FIELDS=entities,workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,chunk_tuner_recommended_capacity,chunk_tuner_recommended_chunks,chunk_tuner_recommended_occupancy_percent"
            "-DREQUIRED_EXACT_FIELDS=storage_chunk_size_bytes=32768,chunk_tuner_recommended_payload_bytes=32768"
            "-DREQUIRED_STRING_FIELDS=world_profile=benchmarkauto,chunk_tuner_recommended_profile=32KB"
            "-DREQUIRED_RUN_EXACT_FIELDS=chunk_size_bytes=32768"
            "-DREQUIRED_RUN_STRING_FIELDS=world_profile=benchmarkauto,chunk_size_profile=32KB"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_auto_transform_profile_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_auto_transform_profile_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_position_velocity_simd_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --entities 2000
            --frames 1
            --warmup 0
            --chunk-size 8KB
            --world-profile mobile8k
            --execution-policy simd_preferred
            --validation off
            --market-scale
            --benchmark-category ecs_chunk_iteration
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_position_velocity_simd_smoke.json"
    )
    set_tests_properties(kb_ecs_benchmark_position_velocity_simd_smoke PROPERTIES
        LABELS "ecs;benchmark;smoke"
        TIMEOUT 60
    )

    add_test(
        NAME kb_ecs_benchmark_position_velocity_simd_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_position_velocity_simd_smoke.json"
            "-DRESULT_NAME=position_velocity_linear_update"
            "-DREQUIRED_POSITIVE_FIELDS=entities,workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,telemetry_query_executions,telemetry_query_batches,telemetry_query_entities_visited,simd_backend_lane_count"
            "-DREQUIRED_EXACT_FIELDS=storage_chunk_size_bytes=8192,execution_prefetch_distance=12"
            "-DREQUIRED_STRING_FIELDS=world_profile=mobile8k,execution_policy=simd_preferred"
            "-DREQUIRED_NONEMPTY_STRING_FIELDS=simd_backend"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_position_velocity_simd_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_position_velocity_simd_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_hierarchy_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --hierarchy-entities 1024
            --frames 1
            --warmup 0
            --chunk-size 8KB
            --world-profile mobile8k
            --execution-policy parallel_chunks
            --validation off
            --market-scale
            --benchmark-category transform_hierarchy
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_category_hierarchy_smoke.json"
    )
    set_tests_properties(kb_ecs_benchmark_category_hierarchy_smoke PROPERTIES
        LABELS "ecs;benchmark;smoke"
        TIMEOUT 60
    )

    add_test(
        NAME kb_ecs_benchmark_category_hierarchy_telemetry_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_hierarchy_smoke.json"
            "-DRESULT_NAME=transform_hierarchy_dirty_1_percent_depth_8"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,telemetry_entity_count,telemetry_archetype_count,telemetry_bytes_per_entity,transform_hierarchy_depth,transform_hierarchy_dirty_percent,transform_hierarchy_dirty_root_count,transform_hierarchy_expected_updated_entities,transform_hierarchy_updated_entities,transform_hierarchy_inspected_entities"
            "-DREQUIRED_EXACT_FIELDS=entities=16,transform_hierarchy_depth=8,transform_hierarchy_dirty_percent=1,transform_hierarchy_expected_updated_entities=16,transform_hierarchy_updated_entities=16,transform_hierarchy_inspected_entities=16,transform_hierarchy_dirty_root_count=2"
            "-DREQUIRED_STRING_FIELDS=execution_policy=layered_dirty_ranges_serial"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_hierarchy_telemetry_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_hierarchy_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_hierarchy_wide_fanout_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_hierarchy_smoke.json"
            "-DRESULT_NAME=transform_hierarchy_wide_fanout_64_dirty_1_percent"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,telemetry_entity_count,telemetry_archetype_count,telemetry_bytes_per_entity,transform_hierarchy_depth,transform_hierarchy_fanout,transform_hierarchy_dirty_percent,transform_hierarchy_dirty_root_count,transform_hierarchy_expected_updated_entities,transform_hierarchy_updated_entities,transform_hierarchy_inspected_entities"
            "-DREQUIRED_EXACT_FIELDS=entities=65,transform_hierarchy_depth=2,transform_hierarchy_fanout=64,transform_hierarchy_dirty_percent=1,transform_hierarchy_dirty_root_count=1,transform_hierarchy_expected_updated_entities=65,transform_hierarchy_updated_entities=65,transform_hierarchy_inspected_entities=65"
            "-DREQUIRED_STRING_FIELDS=execution_policy=wide_fanout_dirty_ranges_serial"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_hierarchy_wide_fanout_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_hierarchy_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_hierarchy_fast_path_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_hierarchy_smoke.json"
            "-DRESULT_NAME=transform_hierarchy_runtime_fast_paths"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,telemetry_entity_count,transform_hierarchy_updated_entities,transform_hierarchy_inspected_entities,transform_hierarchy_root_fast_path_count,transform_hierarchy_translated_parent_fast_path_count,transform_hierarchy_unrotated_parent_fast_path_count,transform_hierarchy_unit_scale_parent_fast_path_count,transform_hierarchy_uniform_scale_parent_fast_path_count,transform_hierarchy_static_local_rotation_fast_path_count,transform_hierarchy_batch_flush_count,transform_hierarchy_flushed_entities,transform_hierarchy_entry_build_ns,transform_hierarchy_kernel_apply_ns,transform_hierarchy_update_ns,transform_hierarchy_flush_ns"
            "-DREQUIRED_EXACT_FIELDS=entities=11,transform_hierarchy_updated_entities=11,transform_hierarchy_inspected_entities=11,transform_hierarchy_root_fast_path_count=6,transform_hierarchy_translated_parent_fast_path_count=1,transform_hierarchy_unrotated_parent_fast_path_count=1,transform_hierarchy_unit_scale_parent_fast_path_count=1,transform_hierarchy_uniform_scale_parent_fast_path_count=1,transform_hierarchy_static_local_rotation_fast_path_count=1,transform_hierarchy_sparse_flush_count=0,transform_hierarchy_batch_flush_count=1,transform_hierarchy_flushed_entities=11,transform_hierarchy_budget_limit=0"
            "-DREQUIRED_STRING_FIELDS=execution_policy=scene_transform_fast_paths"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_hierarchy_fast_path_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_hierarchy_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_hierarchy_sparse_flush_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_hierarchy_smoke.json"
            "-DRESULT_NAME=transform_hierarchy_runtime_sparse_flush"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,telemetry_entity_count,transform_hierarchy_updated_entities,transform_hierarchy_inspected_entities,transform_hierarchy_root_fast_path_count,transform_hierarchy_sparse_flush_count,transform_hierarchy_flushed_entities,transform_hierarchy_dirty_frontier_count"
            "-DREQUIRED_EXACT_FIELDS=entities=1024,transform_hierarchy_depth=1,transform_hierarchy_dirty_percent=1,transform_hierarchy_dirty_root_count=1,transform_hierarchy_expected_updated_entities=1,transform_hierarchy_updated_entities=1,transform_hierarchy_inspected_entities=1,transform_hierarchy_sparse_flush_count=1,transform_hierarchy_batch_flush_count=0,transform_hierarchy_flushed_entities=1,transform_hierarchy_dirty_frontier_count=1,transform_hierarchy_budget_limit=0"
            "-DREQUIRED_STRING_FIELDS=execution_policy=scene_transform_sparse_flush"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_hierarchy_sparse_flush_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_hierarchy_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_hierarchy_frontier_pairs_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_hierarchy_smoke.json"
            "-DRESULT_NAME=transform_hierarchy_runtime_frontier_pairs"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,telemetry_entity_count,transform_hierarchy_updated_entities,transform_hierarchy_inspected_entities,transform_hierarchy_root_fast_path_count,transform_hierarchy_translated_parent_fast_path_count,transform_hierarchy_sparse_flush_count,transform_hierarchy_flushed_entities,transform_hierarchy_dirty_frontier_count"
            "-DREQUIRED_EXACT_FIELDS=entities=1024,transform_hierarchy_depth=2,transform_hierarchy_dirty_percent=1,transform_hierarchy_dirty_root_count=1,transform_hierarchy_expected_updated_entities=2,transform_hierarchy_updated_entities=2,transform_hierarchy_inspected_entities=2,transform_hierarchy_sparse_flush_count=1,transform_hierarchy_batch_flush_count=0,transform_hierarchy_flushed_entities=2,transform_hierarchy_dirty_frontier_count=2,transform_hierarchy_budget_limit=0"
            "-DREQUIRED_STRING_FIELDS=execution_policy=scene_transform_frontier_pairs"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_hierarchy_frontier_pairs_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_hierarchy_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_hierarchy_frontier_depth_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_hierarchy_smoke.json"
            "-DRESULT_NAME=transform_hierarchy_runtime_frontier_depth_8"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,telemetry_entity_count,transform_hierarchy_updated_entities,transform_hierarchy_inspected_entities,transform_hierarchy_root_fast_path_count,transform_hierarchy_translated_parent_fast_path_count,transform_hierarchy_sparse_flush_count,transform_hierarchy_flushed_entities,transform_hierarchy_dirty_frontier_count"
            "-DREQUIRED_EXACT_FIELDS=entities=1024,transform_hierarchy_depth=8,transform_hierarchy_dirty_percent=1,transform_hierarchy_dirty_root_count=1,transform_hierarchy_expected_updated_entities=8,transform_hierarchy_updated_entities=8,transform_hierarchy_inspected_entities=8,transform_hierarchy_sparse_flush_count=1,transform_hierarchy_batch_flush_count=0,transform_hierarchy_flushed_entities=8,transform_hierarchy_dirty_frontier_count=8,transform_hierarchy_budget_limit=0"
            "-DREQUIRED_STRING_FIELDS=execution_policy=scene_transform_frontier_depth"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_hierarchy_frontier_depth_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_hierarchy_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_hierarchy_frontier_multi_root_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_hierarchy_smoke.json"
            "-DRESULT_NAME=transform_hierarchy_runtime_frontier_depth_8_dirty_4_roots"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,telemetry_entity_count,transform_hierarchy_updated_entities,transform_hierarchy_inspected_entities,transform_hierarchy_root_fast_path_count,transform_hierarchy_translated_parent_fast_path_count,transform_hierarchy_sparse_flush_count,transform_hierarchy_flushed_entities,transform_hierarchy_dirty_frontier_count"
            "-DREQUIRED_EXACT_FIELDS=entities=1024,transform_hierarchy_depth=8,transform_hierarchy_dirty_percent=1,transform_hierarchy_dirty_root_count=4,transform_hierarchy_expected_updated_entities=32,transform_hierarchy_updated_entities=32,transform_hierarchy_inspected_entities=32,transform_hierarchy_sparse_flush_count=1,transform_hierarchy_batch_flush_count=0,transform_hierarchy_flushed_entities=32,transform_hierarchy_dirty_frontier_count=32,transform_hierarchy_budget_limit=0"
            "-DREQUIRED_STRING_FIELDS=execution_policy=scene_transform_frontier_multi_root"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_hierarchy_frontier_multi_root_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_hierarchy_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_hierarchy_frontier_fanout_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_hierarchy_smoke.json"
            "-DRESULT_NAME=transform_hierarchy_runtime_frontier_fanout_64"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,telemetry_entity_count,transform_hierarchy_updated_entities,transform_hierarchy_inspected_entities,transform_hierarchy_root_fast_path_count,transform_hierarchy_translated_parent_fast_path_count,transform_hierarchy_sparse_flush_count,transform_hierarchy_dirty_list_flush_count,transform_hierarchy_dirty_list_flush_entities,transform_hierarchy_flushed_entities,transform_hierarchy_dirty_frontier_count,transform_hierarchy_worker_count"
            "-DREQUIRED_EXACT_FIELDS=entities=1040,transform_hierarchy_depth=2,transform_hierarchy_fanout=64,transform_hierarchy_dirty_percent=1,transform_hierarchy_dirty_root_count=1,transform_hierarchy_expected_updated_entities=65,transform_hierarchy_updated_entities=65,transform_hierarchy_inspected_entities=65,transform_hierarchy_sparse_flush_count=1,transform_hierarchy_dirty_list_flush_count=1,transform_hierarchy_dirty_list_flush_entities=65,transform_hierarchy_batch_flush_count=0,transform_hierarchy_flushed_entities=65,transform_hierarchy_dirty_frontier_count=65,transform_hierarchy_parallel_batch_count=0,transform_hierarchy_parallel_chunk_count=0,transform_hierarchy_parallel_entities=0,transform_hierarchy_worker_count=1,transform_hierarchy_parallel_flush_count=0,transform_hierarchy_parallel_flush_chunk_count=0,transform_hierarchy_parallel_flush_entities=0,transform_hierarchy_parallel_flush_worker_count=1,transform_hierarchy_budget_limit=0"
            "-DREQUIRED_STRING_FIELDS=execution_policy=scene_transform_frontier_fanout"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_hierarchy_frontier_fanout_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_hierarchy_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_hierarchy_frontier_fanout_parallel_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_hierarchy_smoke.json"
            "-DRESULT_NAME=transform_hierarchy_runtime_frontier_fanout_256"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,telemetry_entity_count,execution_worker_count,transform_hierarchy_updated_entities,transform_hierarchy_inspected_entities,transform_hierarchy_root_fast_path_count,transform_hierarchy_translated_parent_fast_path_count,transform_hierarchy_dirty_list_flush_count,transform_hierarchy_dirty_list_flush_entities,transform_hierarchy_flushed_entities,transform_hierarchy_dirty_frontier_count,transform_hierarchy_parallel_batch_count,transform_hierarchy_parallel_chunk_count,transform_hierarchy_parallel_entities,transform_hierarchy_worker_count"
            "-DREQUIRED_EXACT_FIELDS=entities=1028,transform_hierarchy_depth=2,transform_hierarchy_fanout=256,transform_hierarchy_dirty_percent=1,transform_hierarchy_dirty_root_count=1,transform_hierarchy_expected_updated_entities=257,transform_hierarchy_updated_entities=257,transform_hierarchy_inspected_entities=257,transform_hierarchy_sparse_flush_count=0,transform_hierarchy_dirty_list_flush_count=1,transform_hierarchy_dirty_list_flush_entities=257,transform_hierarchy_batch_flush_count=0,transform_hierarchy_flushed_entities=257,transform_hierarchy_dirty_frontier_count=257,transform_hierarchy_parallel_batch_count=1,transform_hierarchy_parallel_chunk_count=2,transform_hierarchy_parallel_entities=256,transform_hierarchy_parallel_flush_count=0,transform_hierarchy_parallel_flush_chunk_count=0,transform_hierarchy_parallel_flush_entities=0,transform_hierarchy_parallel_flush_worker_count=1,transform_hierarchy_budget_limit=0"
            "-DREQUIRED_STRING_FIELDS=execution_policy=scene_transform_frontier_fanout"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_hierarchy_frontier_fanout_parallel_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_hierarchy_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_hierarchy_frontier_fanout_large_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_hierarchy_smoke.json"
            "-DRESULT_NAME=transform_hierarchy_runtime_frontier_fanout_256_roots_64"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,telemetry_entity_count,execution_worker_count,transform_hierarchy_updated_entities,transform_hierarchy_inspected_entities,transform_hierarchy_root_fast_path_count,transform_hierarchy_translated_parent_fast_path_count,transform_hierarchy_sparse_flush_count,transform_hierarchy_dirty_list_flush_count,transform_hierarchy_dirty_list_flush_entities,transform_hierarchy_flushed_entities,transform_hierarchy_dirty_frontier_count,transform_hierarchy_parallel_batch_count,transform_hierarchy_parallel_chunk_count,transform_hierarchy_parallel_entities,transform_hierarchy_worker_count"
            "-DREQUIRED_EXACT_FIELDS=entities=16448,transform_hierarchy_depth=2,transform_hierarchy_fanout=256,transform_hierarchy_dirty_percent=1,transform_hierarchy_dirty_root_count=1,transform_hierarchy_expected_updated_entities=257,transform_hierarchy_updated_entities=257,transform_hierarchy_inspected_entities=257,transform_hierarchy_sparse_flush_count=1,transform_hierarchy_dirty_list_flush_count=1,transform_hierarchy_dirty_list_flush_entities=257,transform_hierarchy_batch_flush_count=0,transform_hierarchy_flushed_entities=257,transform_hierarchy_dirty_frontier_count=257,transform_hierarchy_parallel_batch_count=1,transform_hierarchy_parallel_chunk_count=2,transform_hierarchy_parallel_entities=256,transform_hierarchy_parallel_flush_count=0,transform_hierarchy_parallel_flush_chunk_count=0,transform_hierarchy_parallel_flush_entities=0,transform_hierarchy_parallel_flush_worker_count=1,transform_hierarchy_budget_limit=0"
            "-DREQUIRED_STRING_FIELDS=execution_policy=scene_transform_frontier_fanout"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_hierarchy_frontier_fanout_large_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_hierarchy_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_hierarchy_frontier_fanout_dirty_4_roots_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_hierarchy_smoke.json"
            "-DRESULT_NAME=transform_hierarchy_runtime_frontier_fanout_256_roots_64_dirty_4_roots"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,telemetry_entity_count,execution_worker_count,transform_hierarchy_updated_entities,transform_hierarchy_inspected_entities,transform_hierarchy_root_fast_path_count,transform_hierarchy_translated_parent_fast_path_count,transform_hierarchy_sparse_flush_count,transform_hierarchy_dirty_list_flush_count,transform_hierarchy_dirty_list_flush_entities,transform_hierarchy_flushed_entities,transform_hierarchy_dirty_frontier_count,transform_hierarchy_parallel_batch_count,transform_hierarchy_parallel_chunk_count,transform_hierarchy_parallel_entities,transform_hierarchy_worker_count"
            "-DREQUIRED_EXACT_FIELDS=entities=16448,transform_hierarchy_depth=2,transform_hierarchy_fanout=256,transform_hierarchy_dirty_percent=1,transform_hierarchy_dirty_root_count=4,transform_hierarchy_expected_updated_entities=1028,transform_hierarchy_updated_entities=1028,transform_hierarchy_inspected_entities=1028,transform_hierarchy_sparse_flush_count=1,transform_hierarchy_dirty_list_flush_count=1,transform_hierarchy_dirty_list_flush_entities=1028,transform_hierarchy_batch_flush_count=0,transform_hierarchy_flushed_entities=1028,transform_hierarchy_dirty_frontier_count=1028,transform_hierarchy_parallel_batch_count=1,transform_hierarchy_parallel_chunk_count=8,transform_hierarchy_parallel_entities=1024,transform_hierarchy_parallel_flush_count=0,transform_hierarchy_parallel_flush_chunk_count=0,transform_hierarchy_parallel_flush_entities=0,transform_hierarchy_parallel_flush_worker_count=1,transform_hierarchy_budget_limit=0"
            "-DREQUIRED_STRING_FIELDS=execution_policy=scene_transform_frontier_fanout"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_hierarchy_frontier_fanout_dirty_4_roots_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_hierarchy_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_hierarchy_frontier_fanout_dirty_16_roots_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_hierarchy_smoke.json"
            "-DRESULT_NAME=transform_hierarchy_runtime_frontier_fanout_256_roots_64_dirty_16_roots"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,telemetry_entity_count,execution_worker_count,transform_hierarchy_updated_entities,transform_hierarchy_inspected_entities,transform_hierarchy_root_fast_path_count,transform_hierarchy_translated_parent_fast_path_count,transform_hierarchy_dirty_list_flush_count,transform_hierarchy_dirty_list_flush_entities,transform_hierarchy_flushed_entities,transform_hierarchy_dirty_frontier_count,transform_hierarchy_parallel_batch_count,transform_hierarchy_parallel_chunk_count,transform_hierarchy_parallel_entities,transform_hierarchy_worker_count"
            "-DREQUIRED_EXACT_FIELDS=entities=16448,transform_hierarchy_depth=2,transform_hierarchy_fanout=256,transform_hierarchy_dirty_percent=1,transform_hierarchy_dirty_root_count=16,transform_hierarchy_expected_updated_entities=4112,transform_hierarchy_updated_entities=4112,transform_hierarchy_inspected_entities=4112,transform_hierarchy_sparse_flush_count=0,transform_hierarchy_dirty_list_flush_count=1,transform_hierarchy_dirty_list_flush_entities=4112,transform_hierarchy_batch_flush_count=0,transform_hierarchy_flushed_entities=4112,transform_hierarchy_dirty_frontier_count=4112,transform_hierarchy_parallel_batch_count=1,transform_hierarchy_parallel_chunk_count=32,transform_hierarchy_parallel_entities=4096,transform_hierarchy_parallel_flush_count=0,transform_hierarchy_parallel_flush_chunk_count=0,transform_hierarchy_parallel_flush_entities=0,transform_hierarchy_parallel_flush_worker_count=1,transform_hierarchy_budget_limit=0"
            "-DREQUIRED_STRING_FIELDS=execution_policy=scene_transform_frontier_fanout"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_hierarchy_frontier_fanout_dirty_16_roots_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_hierarchy_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_hierarchy_frontier_fanout_dirty_64_roots_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_hierarchy_smoke.json"
            "-DRESULT_NAME=transform_hierarchy_runtime_frontier_fanout_256_roots_64_dirty_64_roots"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,telemetry_entity_count,execution_worker_count,transform_hierarchy_updated_entities,transform_hierarchy_inspected_entities,transform_hierarchy_root_fast_path_count,transform_hierarchy_translated_parent_fast_path_count,transform_hierarchy_flushed_entities,transform_hierarchy_dirty_frontier_count,transform_hierarchy_parallel_batch_count,transform_hierarchy_parallel_chunk_count,transform_hierarchy_parallel_entities,transform_hierarchy_worker_count,transform_hierarchy_parallel_flush_count,transform_hierarchy_parallel_flush_chunk_count,transform_hierarchy_parallel_flush_entities,transform_hierarchy_parallel_flush_worker_count,transform_hierarchy_cache_build_ns,transform_hierarchy_entry_build_ns,transform_hierarchy_kernel_apply_ns,transform_hierarchy_frontier_append_ns,transform_hierarchy_propagate_ns,transform_hierarchy_flush_write_ns,transform_hierarchy_update_ns,transform_hierarchy_flush_ns"
            "-DREQUIRED_EXACT_FIELDS=entities=16448,transform_hierarchy_depth=2,transform_hierarchy_fanout=256,transform_hierarchy_dirty_percent=1,transform_hierarchy_dirty_root_count=64,transform_hierarchy_expected_updated_entities=16448,transform_hierarchy_updated_entities=16448,transform_hierarchy_inspected_entities=16448,transform_hierarchy_sparse_flush_count=0,transform_hierarchy_dirty_list_flush_count=0,transform_hierarchy_dirty_list_flush_entities=0,transform_hierarchy_batch_flush_count=1,transform_hierarchy_flushed_entities=16448,transform_hierarchy_dirty_frontier_count=16448,transform_hierarchy_parallel_batch_count=1,transform_hierarchy_parallel_chunk_count=128,transform_hierarchy_parallel_entities=16384,transform_hierarchy_parallel_flush_count=1,transform_hierarchy_parallel_flush_entities=16448,transform_hierarchy_backend_mark_ns=0,transform_hierarchy_budget_limit=0"
            "-DREQUIRED_STRING_FIELDS=execution_policy=scene_transform_frontier_fanout"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_hierarchy_frontier_fanout_dirty_64_roots_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_hierarchy_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_transform_hierarchy_large_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --entities 100000
            --hierarchy-entities 100000
            --frames 1
            --warmup 0
            --chunk-size 32KB
            --world-profile balanced32k
            --execution-policy parallel_chunks
            --validation off
            --benchmark-category transform_hierarchy_large
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_category_transform_hierarchy_large_smoke.json"
    )
    set_tests_properties(kb_ecs_benchmark_category_transform_hierarchy_large_smoke PROPERTIES
        LABELS "ecs;benchmark;smoke"
        TIMEOUT 180
    )

    add_test(
        NAME kb_ecs_benchmark_category_transform_hierarchy_large_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_transform_hierarchy_large_smoke.json"
            "-DRESULT_NAME=transform_hierarchy_runtime_frontier_fanout_256_roots_390_dirty_390_roots"
            "-DREQUIRED_POSITIVE_FIELDS=time_ms_avg,throughput_entities_per_second,workload_bytes_per_frame,workload_bytes_per_second,storage_chunks,storage_capacity,storage_used_bytes,telemetry_entity_count,execution_worker_count,transform_hierarchy_updated_entities,transform_hierarchy_inspected_entities,transform_hierarchy_root_fast_path_count,transform_hierarchy_translated_parent_fast_path_count,transform_hierarchy_flushed_entities,transform_hierarchy_dirty_frontier_count,transform_hierarchy_parallel_batch_count,transform_hierarchy_parallel_chunk_count,transform_hierarchy_parallel_entities,transform_hierarchy_worker_count,transform_hierarchy_parallel_flush_count,transform_hierarchy_parallel_flush_chunk_count,transform_hierarchy_parallel_flush_entities,transform_hierarchy_parallel_flush_worker_count,transform_hierarchy_cache_build_ns,transform_hierarchy_entry_build_ns,transform_hierarchy_kernel_apply_ns,transform_hierarchy_frontier_append_ns,transform_hierarchy_propagate_ns,transform_hierarchy_flush_write_ns,transform_hierarchy_update_ns,transform_hierarchy_flush_ns"
            "-DREQUIRED_EXACT_FIELDS=entities=100230,transform_hierarchy_depth=2,transform_hierarchy_fanout=256,transform_hierarchy_dirty_percent=1,transform_hierarchy_dirty_root_count=390,transform_hierarchy_expected_updated_entities=100230,transform_hierarchy_updated_entities=100230,transform_hierarchy_inspected_entities=100230,transform_hierarchy_sparse_flush_count=0,transform_hierarchy_dirty_list_flush_count=0,transform_hierarchy_dirty_list_flush_entities=0,transform_hierarchy_batch_flush_count=1,transform_hierarchy_flushed_entities=100230,transform_hierarchy_dirty_frontier_count=100230,transform_hierarchy_parallel_batch_count=2,transform_hierarchy_parallel_chunk_count=784,transform_hierarchy_parallel_entities=100230,transform_hierarchy_parallel_flush_count=1,transform_hierarchy_parallel_flush_entities=100230,transform_hierarchy_backend_mark_ns=0,transform_hierarchy_budget_limit=0"
            "-DREQUIRED_STRING_FIELDS=category=transform_hierarchy_large,execution_policy=scene_transform_frontier_fanout"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_transform_hierarchy_large_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_transform_hierarchy_large_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_transform_no_hierarchy_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --entities 2000
            --frames 1
            --warmup 0
            --chunk-size 8KB
            --world-profile mobile8k
            --execution-policy parallel_chunks
            --validation off
            --benchmark-category transform_no_hierarchy
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_category_transform_no_hierarchy_smoke.json"
    )
    set_tests_properties(kb_ecs_benchmark_category_transform_no_hierarchy_smoke PROPERTIES
        LABELS "ecs;benchmark;smoke"
        TIMEOUT 60
    )

    add_test(
        NAME kb_ecs_benchmark_category_transform_no_hierarchy_telemetry_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_transform_no_hierarchy_smoke.json"
            "-DRESULT_NAME=transform_local_to_world_no_hierarchy"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,telemetry_entity_count,telemetry_archetype_count,telemetry_query_plan_requests"
            "-DREQUIRED_EXACT_FIELDS=execution_prefetch_distance=12"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_transform_no_hierarchy_telemetry_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_transform_no_hierarchy_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_transform_no_hierarchy_simd_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --entities 2000
            --frames 1
            --warmup 0
            --chunk-size 8KB
            --world-profile mobile8k
            --execution-policy simd_preferred
            --validation off
            --benchmark-category transform_no_hierarchy
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_category_transform_no_hierarchy_simd_smoke.json"
    )
    set_tests_properties(kb_ecs_benchmark_category_transform_no_hierarchy_simd_smoke PROPERTIES
        LABELS "ecs;benchmark;smoke"
        TIMEOUT 60
    )

    add_test(
        NAME kb_ecs_benchmark_category_transform_no_hierarchy_simd_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_transform_no_hierarchy_simd_smoke.json"
            "-DRESULT_NAME=transform_local_to_world_no_hierarchy"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,telemetry_entity_count,telemetry_query_executions,telemetry_query_batches,telemetry_query_entities_visited"
            "-DREQUIRED_EXACT_FIELDS=execution_prefetch_distance=12"
            "-DREQUIRED_STRING_FIELDS=execution_policy=simd_preferred"
            "-DREQUIRED_NONEMPTY_STRING_FIELDS=simd_backend"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_transform_no_hierarchy_simd_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_transform_no_hierarchy_simd_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_scene_render_sync_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --entities 2000
            --frames 1
            --warmup 0
            --chunk-size 8KB
            --world-profile mobile8k
            --execution-policy parallel_chunks
            --validation off
            --benchmark-category scene_render_sync
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_category_scene_render_sync_smoke.json"
    )
    set_tests_properties(kb_ecs_benchmark_category_scene_render_sync_smoke PROPERTIES
        LABELS "ecs;benchmark;smoke"
        TIMEOUT 60
    )

    add_test(
        NAME kb_ecs_benchmark_category_scene_render_sync_telemetry_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_scene_render_sync_smoke.json"
            "-DRESULT_NAME=scene_visible_renderer_iteration"
            "-DREQUIRED_POSITIVE_FIELDS=entities,workload_bytes_per_frame,storage_chunks,storage_capacity,scene_render_sync_visible_renderer_count,scene_render_sync_footprint_bytes,telemetry_entity_count,telemetry_archetype_count"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_scene_render_sync_telemetry_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_scene_render_sync_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_scene_camera_light_sync_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_scene_render_sync_smoke.json"
            "-DRESULT_NAME=scene_camera_light_iteration"
            "-DREQUIRED_POSITIVE_FIELDS=entities,workload_bytes_per_frame,storage_chunks,storage_capacity,scene_render_sync_camera_count,scene_render_sync_light_count,scene_render_sync_footprint_bytes,telemetry_entity_count,telemetry_archetype_count"
            "-DREQUIRED_STRING_FIELDS=execution_policy=scene_component_visitor"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_scene_camera_light_sync_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_scene_render_sync_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_query_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --entities 2000
            --frames 1
            --warmup 0
            --chunk-size 8KB
            --world-profile mobile8k
            --execution-policy parallel_chunks
            --validation off
            --benchmark-category query_fanout
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_category_query_smoke.json"
    )
    set_tests_properties(kb_ecs_benchmark_category_query_smoke PROPERTIES
        LABELS "ecs;benchmark;smoke"
        TIMEOUT 60
    )

    add_test(
        NAME kb_ecs_benchmark_category_query_telemetry_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_query_smoke.json"
            "-DRESULT_NAME=query_fanout_hot_8_components"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,telemetry_entity_count,telemetry_query_plan_requests,telemetry_query_cache_misses,telemetry_allocated_bytes,telemetry_used_bytes,telemetry_occupancy_percent"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_query_telemetry_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_query_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_sparse_query_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --smoke
            --frames 1
            --warmup 0
            --chunk-size 8KB
            --world-profile mobile8k
            --execution-policy parallel_chunks
            --validation off
            --benchmark-category sparse_query
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_category_sparse_query_smoke.json"
    )
    set_tests_properties(kb_ecs_benchmark_category_sparse_query_smoke PROPERTIES
        LABELS "ecs;benchmark;smoke"
        TIMEOUT 60
    )

    add_test(
        NAME kb_ecs_benchmark_category_sparse_query_telemetry_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_sparse_query_smoke.json"
            "-DRESULT_NAME=sparse_query_filtering"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,telemetry_entity_count,telemetry_archetype_count,telemetry_query_plan_requests,telemetry_query_cache_misses"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_sparse_query_telemetry_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_sparse_query_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_sparse_query_churn_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_sparse_query_smoke.json"
            "-DRESULT_NAME=sparse_query_churn_cached_filtering"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,telemetry_entity_count,telemetry_archetype_count,telemetry_query_plan_requests,telemetry_query_cache_hits,telemetry_structural_changes,telemetry_total_structural_changes"
            "-DREQUIRED_STRING_FIELDS=execution_policy=query_cache_churn_batch"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_sparse_query_churn_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_sparse_query_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_scheduler_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --smoke
            --frames 1
            --warmup 0
            --chunk-size 8KB
            --world-profile mobile8k
            --execution-policy parallel_chunks
            --validation off
            --benchmark-category ecs_system_scheduler
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_category_scheduler_smoke.json"
    )
    set_tests_properties(kb_ecs_benchmark_category_scheduler_smoke PROPERTIES
        LABELS "ecs;benchmark;smoke"
        TIMEOUT 60
    )

    add_test(
        NAME kb_ecs_benchmark_category_scheduler_telemetry_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_scheduler_smoke.json"
            "-DRESULT_NAME=system_chain_16_systems"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,telemetry_entity_count,telemetry_bytes_per_entity,execution_worker_count,worker_utilization_percent"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_scheduler_telemetry_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_scheduler_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_prefab_spawn_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --smoke
            --frames 1
            --warmup 0
            --chunk-size 8KB
            --world-profile mobile8k
            --execution-policy parallel_chunks
            --validation off
            --benchmark-category prefab_spawn
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_category_prefab_spawn_smoke.json"
    )
    set_tests_properties(kb_ecs_benchmark_category_prefab_spawn_smoke PROPERTIES
        LABELS "ecs;benchmark;smoke"
        TIMEOUT 60
    )

    add_test(
        NAME kb_ecs_benchmark_category_prefab_spawn_telemetry_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_prefab_spawn_smoke.json"
            "-DRESULT_NAME=prefab_spawn_registered_named_scenehierarchy_instances_64_instances_4_nodes"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,prefab_spawn_requested_instances,prefab_spawn_instantiated_instances,prefab_spawn_entities_created,prefab_spawn_archetypes_touched,prefab_spawn_bulk_create_commands,prefab_spawn_component_bytes_copied,prefab_spawn_registered_instance_count,prefab_spawn_entity_create_ns,prefab_spawn_hierarchy_record_ns,prefab_spawn_name_assignment_ns,prefab_spawn_registry_resolve_ns,prefab_spawn_history_record_ns,telemetry_entity_count,telemetry_archetype_count,telemetry_structural_changes,telemetry_total_structural_changes,telemetry_allocated_bytes,telemetry_used_bytes,telemetry_occupancy_percent"
            "-DREQUIRED_EXACT_FIELDS=storage_chunk_size_bytes=8192"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_prefab_spawn_telemetry_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_prefab_spawn_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_prefab_spawn_batch_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --prefab-instances 64
            --prefab-nodes 4
            --prefab-names off
            --prefab-world-hierarchy off
            --prefab-result-instances off
            --frames 1
            --warmup 0
            --chunk-size 8KB
            --world-profile mobile8k
            --execution-policy single_thread
            --validation off
            --benchmark-category prefab_spawn
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_category_prefab_spawn_batch_smoke.json"
    )
    set_tests_properties(kb_ecs_benchmark_category_prefab_spawn_batch_smoke PROPERTIES
        LABELS "ecs;benchmark;smoke"
        TIMEOUT 60
    )

    add_test(
        NAME kb_ecs_benchmark_category_prefab_spawn_batch_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_prefab_spawn_batch_smoke.json"
            "-DRESULT_NAME=prefab_spawn_registered_unnamed_scenehierarchy_batch_64_instances_4_nodes"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,prefab_spawn_requested_instances,prefab_spawn_instantiated_instances,prefab_spawn_entities_created,prefab_spawn_archetypes_touched,prefab_spawn_bulk_create_commands,prefab_spawn_component_bytes_copied,prefab_spawn_entity_create_ns,prefab_spawn_hierarchy_record_ns,prefab_spawn_registry_resolve_ns,telemetry_entity_count,telemetry_archetype_count,telemetry_structural_changes,telemetry_total_structural_changes,telemetry_allocated_bytes,telemetry_used_bytes,telemetry_occupancy_percent"
            "-DREQUIRED_ZERO_FIELDS=prefab_spawn_registered_instance_count,prefab_spawn_name_assignment_ns,prefab_spawn_history_record_ns"
            "-DREQUIRED_EXACT_FIELDS=storage_chunk_size_bytes=8192"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_prefab_spawn_batch_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_prefab_spawn_batch_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_prefab_registry_record_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --prefab-instances 64
            --prefab-nodes 4
            --frames 1
            --warmup 0
            --chunk-size 8KB
            --world-profile mobile8k
            --execution-policy single_thread
            --validation off
            --benchmark-category prefab_registry_record
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_category_prefab_registry_record_smoke.json"
    )
    set_tests_properties(kb_ecs_benchmark_category_prefab_registry_record_smoke PROPERTIES
        LABELS "ecs;benchmark;smoke"
        TIMEOUT 60
    )

    add_test(
        NAME kb_ecs_benchmark_category_prefab_registry_record_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_prefab_registry_record_smoke.json"
            "-DRESULT_NAME=prefab_registry_record_dense_64_instances_4_nodes"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,prefab_spawn_requested_instances,prefab_spawn_instantiated_instances,prefab_spawn_nodes_per_instance,prefab_spawn_entities_created,prefab_spawn_component_bytes_copied,prefab_spawn_registered_instance_count,telemetry_entity_count,telemetry_archetype_count,telemetry_allocated_bytes,telemetry_used_bytes,telemetry_occupancy_percent"
            "-DREQUIRED_ZERO_FIELDS=prefab_spawn_archetypes_touched,prefab_spawn_bulk_create_commands,prefab_spawn_component_set_commands,prefab_spawn_parent_commands,prefab_spawn_entity_create_ns,prefab_spawn_hierarchy_record_ns,prefab_spawn_name_assignment_ns,prefab_spawn_registry_resolve_ns,prefab_spawn_history_record_ns"
            "-DREQUIRED_STRING_FIELDS=category=prefab_registry_record,execution_policy=prefab_registry_record"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_prefab_registry_record_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_prefab_registry_record_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_prefab_override_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --smoke
            --frames 1
            --warmup 0
            --chunk-size 8KB
            --world-profile mobile8k
            --execution-policy parallel_chunks
            --validation off
            --benchmark-category prefab_override
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_category_prefab_override_smoke.json"
    )
    set_tests_properties(kb_ecs_benchmark_category_prefab_override_smoke PROPERTIES
        LABELS "ecs;benchmark;smoke"
        TIMEOUT 60
    )

    add_test(
        NAME kb_ecs_benchmark_category_prefab_override_telemetry_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_prefab_override_smoke.json"
            "-DRESULT_NAME=prefab_override_revert_64_instances"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,prefab_override_instance_count,prefab_override_nodes_per_instance,prefab_override_target_nodes,prefab_override_entities_touched,prefab_override_component_bytes_touched,telemetry_entity_count,telemetry_archetype_count,telemetry_structural_changes,telemetry_total_structural_changes,telemetry_allocated_bytes,telemetry_used_bytes,telemetry_occupancy_percent"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_prefab_override_telemetry_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_prefab_override_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_structural_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --entities 2000
            --structural-changes 4000
            --frames 1
            --warmup 0
            --chunk-size 8KB
            --world-profile mobile8k
            --execution-policy parallel_chunks
            --validation off
            --benchmark-category structural_changes
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_category_structural_smoke.json"
    )
    set_tests_properties(kb_ecs_benchmark_category_structural_smoke PROPERTIES
        LABELS "ecs;benchmark;smoke"
        TIMEOUT 60
    )

    add_test(
        NAME kb_ecs_benchmark_category_structural_telemetry_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_structural_smoke.json"
            "-DRESULT_NAME=structural_changes_command_buffer"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,telemetry_entity_count,telemetry_structural_changes,telemetry_total_structural_changes,telemetry_allocated_bytes,telemetry_used_bytes,telemetry_occupancy_percent,structural_command_count,structural_bulk_create_command_count,structural_destroy_command_count,structural_component_set_command_count,structural_component_remove_command_count,structural_component_bytes_copied,structural_playback_slices,structural_destroy_sync_slices,structural_destroyed_entities_applied,structural_slice_structural_budget,structural_slice_destroy_budget,structural_slice_component_set_budget,structural_slice_component_remove_budget,structural_slice_component_byte_budget"
            "-DREQUIRED_MIN_FIELDS=structural_playback_slices=12,structural_destroy_sync_slices=4"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_structural_telemetry_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_structural_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_category_native_storage_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --entities 2000
            --frames 1
            --warmup 0
            --chunk-size 8KB
            --world-profile mobile8k
            --execution-policy parallel_chunks
            --validation off
            --benchmark-category ecs_chunk_iteration
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_category_native_storage_smoke.json"
    )
    set_tests_properties(kb_ecs_benchmark_category_native_storage_smoke PROPERTIES
        LABELS "ecs;benchmark;smoke"
        TIMEOUT 60
    )

    add_test(
        NAME kb_ecs_benchmark_category_native_storage_telemetry_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_native_storage_smoke.json"
            "-DRESULT_NAME=native_storage_bulk_create_adopt_destroy"
            "-DREQUIRED_POSITIVE_FIELDS=workload_bytes_per_frame,storage_chunks,storage_capacity,storage_used_bytes,telemetry_entity_count,telemetry_bytes_per_entity,telemetry_chunk_pool_trim_count,storage_maintenance_free_chunks_before,storage_maintenance_allocated_chunks_before,storage_maintenance_chunks_released"
            "-DREQUIRED_ZERO_FIELDS=storage_maintenance_free_chunks_after,storage_maintenance_fragmented_chunks_before,storage_maintenance_fragmented_chunks_after,storage_maintenance_empty_chunks_before,storage_maintenance_empty_chunks_after"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_category_native_storage_telemetry_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_native_storage_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_chunk_tuner_report_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_category_native_storage_smoke.json"
            "-DRESULT_NAME=position_velocity_batch_read"
            "-DREQUIRED_POSITIVE_FIELDS=chunk_tuner_recommended_payload_bytes,chunk_tuner_recommended_capacity,chunk_tuner_recommended_chunks,chunk_tuner_recommended_occupancy_percent,telemetry_query_executions,telemetry_query_batches,telemetry_query_entities_visited,telemetry_query_bytes_touched,telemetry_query_elapsed_nanoseconds,telemetry_query_bytes_per_second,telemetry_query_parallel_executions,telemetry_query_worker_slots,telemetry_query_worker_active_slots,telemetry_query_worker_utilization_percent"
            "-DREQUIRED_STRING_FIELDS=chunk_tuner_workload=mobile_thermal"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkTelemetry.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_chunk_tuner_report_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_category_native_storage_smoke
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_kernel_report_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_kernel_report>"
            --output "${CMAKE_CURRENT_BINARY_DIR}/kernel-report-smoke.json"
            --elements 1024
            --iterations 1
            --samples 1
            --warmup 1
    )
    set_tests_properties(kb_ecs_kernel_report_smoke PROPERTIES
        LABELS "ecs;benchmark;smoke;kernel"
        TIMEOUT 60
    )

    add_test(
        NAME kb_ecs_kernel_report_backend_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/kernel-report-smoke.json"
            "-DREQUIRED_BACKENDS=scalar,neon,sse2,avx2,avx512"
            "-DREQUIRED_KERNELS=movement,transform_local_to_world,bounds_world_aabb,physics_proxy"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertKernelReport.cmake"
    )
    set_tests_properties(kb_ecs_kernel_report_backend_guard PROPERTIES
        DEPENDS kb_ecs_kernel_report_smoke
        LABELS "ecs;benchmark;regression;kernel"
    )

    add_test(
        NAME kb_ecs_benchmark_regression_gate_pass
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --compare-before "${CMAKE_CURRENT_SOURCE_DIR}/testdata/ecs_benchmark_gate_before.json"
            --compare-after "${CMAKE_CURRENT_SOURCE_DIR}/testdata/ecs_benchmark_gate_after_pass.json"
            --compare-output "${CMAKE_CURRENT_BINARY_DIR}/ecs_benchmark_gate_pass.json"
            --fail-on-regression 5
    )
    set_tests_properties(kb_ecs_benchmark_regression_gate_pass PROPERTIES
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_regression_gate_pass_report_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_benchmark_gate_pass.json"
            "-DFORBID_REGRESSIONS=true"
            "-DREQUIRED_MIN_RESULTS=5"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkComparison.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_regression_gate_pass_report_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_regression_gate_pass
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_benchmark_regression_gate_detects_regression
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --compare-before "${CMAKE_CURRENT_SOURCE_DIR}/testdata/ecs_benchmark_gate_before.json"
            --compare-after "${CMAKE_CURRENT_SOURCE_DIR}/testdata/ecs_benchmark_gate_after_regressed.json"
            --compare-output "${CMAKE_CURRENT_BINARY_DIR}/ecs_benchmark_gate_regressed.json"
            --fail-on-regression 5
    )
    set_tests_properties(kb_ecs_benchmark_regression_gate_detects_regression PROPERTIES
        LABELS "ecs;benchmark;regression"
        WILL_FAIL TRUE
    )

    add_test(
        NAME kb_ecs_benchmark_regression_gate_regressed_report_guard
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_benchmark_gate_regressed.json"
            "-DREQUIRED_REGRESSION_METRICS=time_ms_avg,time_ms_p95,throughput_entities_per_second"
            "-DREQUIRED_MIN_REGRESSIONS=15"
            "-DREQUIRED_MIN_RESULTS=5"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertBenchmarkComparison.cmake"
    )
    set_tests_properties(kb_ecs_benchmark_regression_gate_regressed_report_guard PROPERTIES
        DEPENDS kb_ecs_benchmark_regression_gate_detects_regression
        LABELS "ecs;benchmark;regression"
    )

    add_test(
        NAME kb_ecs_kernel_report_regression_gate_pass
        COMMAND ${CMAKE_COMMAND}
            "-DBEFORE=${CMAKE_CURRENT_SOURCE_DIR}/testdata/ecs_kernel_report_gate_before.json"
            "-DAFTER=${CMAKE_CURRENT_SOURCE_DIR}/testdata/ecs_kernel_report_gate_after_pass.json"
            "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_kernel_report_gate_pass.json"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompareKernelReport.cmake"
    )
    set_tests_properties(kb_ecs_kernel_report_regression_gate_pass PROPERTIES
        LABELS "ecs;benchmark;regression;kernel"
    )

    add_test(
        NAME kb_ecs_kernel_report_regression_gate_detects_regression
        COMMAND ${CMAKE_COMMAND}
            "-DBEFORE=${CMAKE_CURRENT_SOURCE_DIR}/testdata/ecs_kernel_report_gate_before.json"
            "-DAFTER=${CMAKE_CURRENT_SOURCE_DIR}/testdata/ecs_kernel_report_gate_after_regressed.json"
            "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/ecs_kernel_report_gate_regressed.json"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompareKernelReport.cmake"
    )
    set_tests_properties(kb_ecs_kernel_report_regression_gate_detects_regression PROPERTIES
        LABELS "ecs;benchmark;regression;kernel"
        WILL_FAIL TRUE
    )
endif()

if(WIN32)
    add_custom_target(kb_ecs_market_release_matrix
        COMMAND powershell.exe
            -NoProfile
            -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/tests/run-ecs-market-benchmark.ps1"
            -BuildDir "${CMAKE_BINARY_DIR}"
            -Config "$<CONFIG>"
            -SkipConfigure
            -NoBuild
            -Scales 1000000,10000000,100000000
            -OutputDir "${CMAKE_SOURCE_DIR}/Saved/Benchmarks/EcsMarketRelease"
            -Frames 5
            -Warmup 1
            -ChunkSizes 8KB,16KB,32KB,64KB
            -WorldProfiles mobile8k,balanced32k,desktop64k
            -ExecutionPolicies parallel_chunks,simd_preferred
            -ReductionModes none,per_worker
            -BenchmarkCategories ecs_chunk_iteration,transform_no_hierarchy,structural_changes,prefab_spawn
            -MaxMemoryPercent 70
        DEPENDS kb_ecs_benchmarks
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        USES_TERMINAL
        VERBATIM
    )

    add_custom_target(kb_ecs_market_full_scale_chunk_sweep
        COMMAND powershell.exe
            -NoProfile
            -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/tests/run-ecs-market-benchmark.ps1"
            -BuildDir "${CMAKE_BINARY_DIR}"
            -Config "$<CONFIG>"
            -SkipConfigure
            -NoBuild
            -Scales 10000,100000,1000000,10000000,100000000
            -OutputDir "${CMAKE_SOURCE_DIR}/Saved/Benchmarks/EcsMarketFullScaleChunkSweep"
            -Frames 5
            -Warmup 1
            -ChunkSweep
            -WorldProfiles mobile8k,balanced32k,desktop64k
            -ExecutionPolicies single_thread,parallel_chunks,simd_preferred
            -ReductionModes none,per_worker
            -BenchmarkCategories raw_kernel,ecs_chunk_iteration
            -MaxMemoryPercent 70
        DEPENDS kb_ecs_benchmarks
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        USES_TERMINAL
        VERBATIM
    )
endif()

add_custom_target(kb_ecs_benchmarks_full
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/full"
    COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
        --output "${CMAKE_CURRENT_BINARY_DIR}/full/ecs_benchmark_full.json"
    DEPENDS kb_ecs_benchmarks
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    USES_TERMINAL
    VERBATIM
)

function(kb_ecs_benchmarks_enable_ipo target_name)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _kb_ecs_benchmarks_ipo_supported OUTPUT _kb_ecs_benchmarks_ipo_error LANGUAGES CXX)
    if(NOT _kb_ecs_benchmarks_ipo_supported)
        message(FATAL_ERROR "IPO/LTO is not supported for ${target_name}: ${_kb_ecs_benchmarks_ipo_error}")
    endif()

    set_property(TARGET ${target_name} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endfunction()

function(kb_ecs_benchmarks_apply_pgo target_name)
    if(_kb_ecs_benchmarks_pgo_mode STREQUAL "Off")
        return()
    endif()

    get_target_property(_kb_ecs_benchmarks_target_type ${target_name} TYPE)

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /GL)

        if(_kb_ecs_benchmarks_target_type STREQUAL "EXECUTABLE" OR _kb_ecs_benchmarks_target_type STREQUAL "SHARED_LIBRARY" OR _kb_ecs_benchmarks_target_type STREQUAL "MODULE_LIBRARY")
            set(_kb_ecs_benchmarks_pgd "${_kb_ecs_benchmarks_profile_dir}/kb_ecs_benchmarks.pgd")
            if(_kb_ecs_benchmarks_pgo_mode STREQUAL "Generate")
                target_link_options(${target_name} PRIVATE /LTCG "/GENPROFILE:PGD=${_kb_ecs_benchmarks_pgd}")
                get_filename_component(_kb_ecs_benchmarks_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
                set(_kb_ecs_benchmarks_pgort "${_kb_ecs_benchmarks_compiler_dir}/pgort140.dll")
                if(EXISTS "${_kb_ecs_benchmarks_pgort}")
                    add_custom_command(
                        TARGET ${target_name}
                        POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_kb_ecs_benchmarks_pgort}" "$<TARGET_FILE_DIR:${target_name}>/pgort140.dll"
                    )
                endif()
            elseif(_kb_ecs_benchmarks_pgo_mode STREQUAL "Use")
                target_link_options(${target_name} PRIVATE /LTCG "/USEPROFILE:PGD=${_kb_ecs_benchmarks_pgd}")
            endif()
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        if(_kb_ecs_benchmarks_pgo_mode STREQUAL "Generate")
            target_compile_options(${target_name} PRIVATE "-fprofile-generate=${_kb_ecs_benchmarks_profile_dir}")
            if(_kb_ecs_benchmarks_target_type STREQUAL "EXECUTABLE" OR _kb_ecs_benchmarks_target_type STREQUAL "SHARED_LIBRARY" OR _kb_ecs_benchmarks_target_type STREQUAL "MODULE_LIBRARY")
                target_link_options(${target_name} PRIVATE "-fprofile-generate=${_kb_ecs_benchmarks_profile_dir}")
            endif()
        elseif(_kb_ecs_benchmarks_pgo_mode STREQUAL "Use")
            target_compile_options(${target_name} PRIVATE "-fprofile-use=${_kb_ecs_benchmarks_profile_dir}")
            if(_kb_ecs_benchmarks_target_type STREQUAL "EXECUTABLE" OR _kb_ecs_benchmarks_target_type STREQUAL "SHARED_LIBRARY" OR _kb_ecs_benchmarks_target_type STREQUAL "MODULE_LIBRARY")
                target_link_options(${target_name} PRIVATE "-fprofile-use=${_kb_ecs_benchmarks_profile_dir}")
            endif()
        endif()
    else()
        message(FATAL_ERROR "PGO for ECS benchmarks is not configured for ${CMAKE_CXX_COMPILER_ID}")
    endif()
endfunction()

set(_kb_ecs_benchmarks_enable_ipo OFF)
if(KB_ECS_BENCHMARKS_ENABLE_LTO)
    set(_kb_ecs_benchmarks_enable_ipo ON)
endif()
if(MSVC AND NOT _kb_ecs_benchmarks_pgo_mode STREQUAL "Off")
    set(_kb_ecs_benchmarks_enable_ipo OFF)
endif()

foreach(_kb_ecs_benchmarks_optimized_target IN ITEMS kb_engine kb_ecs_benchmarks)
    if(TARGET ${_kb_ecs_benchmarks_optimized_target})
        if(_kb_ecs_benchmarks_enable_ipo)
            kb_ecs_benchmarks_enable_ipo(${_kb_ecs_benchmarks_optimized_target})
        endif()
        kb_ecs_benchmarks_apply_pgo(${_kb_ecs_benchmarks_optimized_target})
    endif()
endforeach()

if(_kb_ecs_benchmarks_pgo_mode STREQUAL "Generate")
    separate_arguments(_kb_ecs_benchmarks_pgo_training_args NATIVE_COMMAND "${KB_ECS_BENCHMARKS_PGO_TRAINING_ARGS}")
    add_custom_target(kb_ecs_benchmarks_pgo_train
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_kb_ecs_benchmarks_profile_dir}"
        COMMAND ${CMAKE_COMMAND} -E env "VCPROFILE_PATH=${_kb_ecs_benchmarks_profile_dir}" "$<TARGET_FILE:kb_ecs_benchmarks>" ${_kb_ecs_benchmarks_pgo_training_args} --output "${_kb_ecs_benchmarks_profile_dir}/ecs_benchmarks_pgo_training.json"
        DEPENDS kb_ecs_benchmarks
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()
