add_executable(kb_openworld_perf main.cpp)
target_link_libraries(kb_openworld_perf PRIVATE kb_engine psapi)
target_compile_definitions(kb_openworld_perf PRIVATE
    "PERF_PHYSICS_DLL=\"$<TARGET_FILE:kb_physics_jolt_plugin>\""
    "PERF_LIGHTING_DLL=\"$<TARGET_FILE:kb_basic_lighting_plugin>\"")
add_dependencies(kb_openworld_perf kb_physics_jolt_plugin kb_basic_lighting_plugin)
set_target_properties(kb_openworld_perf PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
kb_apply_quality_gates(kb_openworld_perf)
