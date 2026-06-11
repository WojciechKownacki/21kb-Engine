add_library(kb_physics_jolt_plugin SHARED
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

kb_apply_quality_gates(kb_physics_jolt_plugin)
