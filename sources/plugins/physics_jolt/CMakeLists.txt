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
