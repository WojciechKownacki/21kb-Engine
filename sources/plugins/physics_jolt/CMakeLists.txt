set(KB_PHYSICS_JOLT_PLUGIN_LIBRARY_TYPE STATIC)
if(KB_BUILD_PROVIDER_MODULES_AS_DLL)
    set(KB_PHYSICS_JOLT_PLUGIN_LIBRARY_TYPE SHARED)
endif()

add_library(kb_physics_jolt_plugin ${KB_PHYSICS_JOLT_PLUGIN_LIBRARY_TYPE}
    JoltPhysicsModule.hpp
    JoltPhysicsModule.cpp
    JoltPhysicsSceneSystem.hpp
    JoltPhysicsSceneSystem.cpp
)

target_include_directories(kb_physics_jolt_plugin
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(kb_physics_jolt_plugin
    PRIVATE
        kb_engine
        Jolt
)

if(NOT KB_BUILD_PROVIDER_MODULES_AS_DLL)
    target_compile_definitions(kb_physics_jolt_plugin
        PUBLIC
            KB_ENGINE_MODULE_STATIC_LINK
    )
endif()

kb_apply_quality_gates(kb_physics_jolt_plugin)

if(BUILD_TESTING AND KB_BUILD_PROVIDER_MODULES_AS_DLL)
    add_executable(kb_physics_state_tests tests/RuntimeStateTests.cpp)
    target_link_libraries(kb_physics_state_tests PRIVATE kb_engine)
    target_include_directories(kb_physics_state_tests PRIVATE ${PROJECT_SOURCE_DIR}/sources/engine/tests)
    target_compile_definitions(kb_physics_state_tests PRIVATE
        "KB_PHYSICS_JOLT_PLUGIN_PATH=\"$<TARGET_FILE:kb_physics_jolt_plugin>\"")
    add_dependencies(kb_physics_state_tests kb_physics_jolt_plugin)
    kb_apply_quality_gates(kb_physics_state_tests)
    add_test(NAME kb_physics_state_tests COMMAND $<TARGET_FILE:kb_physics_state_tests>)
    set_tests_properties(kb_physics_state_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 60)
endif()

if(BUILD_TESTING AND NOT DISABLE_CUSTOM_ALLOCATOR)
    add_executable(kb_physics_memory_tests tests/MemoryTelemetryTests.cpp)
    target_link_libraries(kb_physics_memory_tests PRIVATE Jolt)
    kb_apply_quality_gates(kb_physics_memory_tests)
    add_test(NAME kb_physics_memory_tests COMMAND $<TARGET_FILE:kb_physics_memory_tests>)
endif()
