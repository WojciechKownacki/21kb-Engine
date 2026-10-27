set(KB_WORLD2D_PLUGIN_LIBRARY_TYPE STATIC)
if(KB_BUILD_PROVIDER_MODULES_AS_DLL)
    set(KB_WORLD2D_PLUGIN_LIBRARY_TYPE SHARED)
endif()

# Map editing (tilesets, brushes, automatic borders) shared by the editor window and kb_cli.
add_library(kb_world2d_core STATIC
    Camera2D.hpp
    Camera2D.cpp
    Map2DTileset.hpp
    Map2DTileset.cpp
    Map2DEditing.hpp
    Map2DEditing.cpp
    Map2DView.hpp
    Map2DView.cpp
    Map2DCollision.cpp
    Map2DRuntime.hpp
    Map2DRuntime.cpp
    Sprite2DRuntime.hpp
    Sprite2DRuntime.cpp
    Physics2DRuntime.hpp
    Physics2DRuntime.cpp
)
target_include_directories(kb_world2d_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kb_world2d_core PUBLIC kb_engine kb_renderer)
target_link_libraries(kb_world2d_core PRIVATE box2d)
kb_apply_quality_gates(kb_world2d_core)

add_library(kb_world2d_plugin ${KB_WORLD2D_PLUGIN_LIBRARY_TYPE}
    World2DModule.hpp
    World2DModule.cpp
)
target_include_directories(kb_world2d_plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kb_world2d_plugin PRIVATE kb_world2d_core flecs::flecs_static)

if(NOT KB_BUILD_PROVIDER_MODULES_AS_DLL)
    target_compile_definitions(kb_world2d_plugin
        PUBLIC
            KB_ENGINE_MODULE_STATIC_LINK
    )
endif()

kb_apply_quality_gates(kb_world2d_plugin)

if(BUILD_TESTING)
    add_executable(kb_world2d_tests tests/World2DTests.cpp)
    target_link_libraries(kb_world2d_tests PRIVATE kb_world2d_core)
    add_test(NAME kb_world2d_tests COMMAND $<TARGET_FILE:kb_world2d_tests>)
    kb_apply_quality_gates(kb_world2d_tests)

    add_executable(kb_world2d_sprite_tests tests/Sprite2DTests.cpp)
    target_link_libraries(kb_world2d_sprite_tests PRIVATE kb_world2d_core)
    add_test(NAME kb_world2d_sprite_tests COMMAND $<TARGET_FILE:kb_world2d_sprite_tests>)
    kb_apply_quality_gates(kb_world2d_sprite_tests)

    add_executable(kb_world2d_physics_tests tests/Physics2DTests.cpp)
    target_link_libraries(kb_world2d_physics_tests PRIVATE kb_world2d_core)
    add_test(NAME kb_world2d_physics_tests COMMAND $<TARGET_FILE:kb_world2d_physics_tests>)
    kb_apply_quality_gates(kb_world2d_physics_tests)
endif()
