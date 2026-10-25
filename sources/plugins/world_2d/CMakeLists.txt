set(KB_WORLD2D_PLUGIN_LIBRARY_TYPE STATIC)
if(KB_BUILD_PROVIDER_MODULES_AS_DLL)
    set(KB_WORLD2D_PLUGIN_LIBRARY_TYPE SHARED)
endif()

# Map editing (tilesets, brushes, automatic borders) shared by the editor window and kb_cli.
add_library(kb_world2d_core STATIC
    Map2DTileset.hpp
    Map2DTileset.cpp
    Map2DEditing.hpp
    Map2DEditing.cpp
    Map2DView.hpp
    Map2DView.cpp
    Map2DCollision.cpp
    Map2DRuntime.hpp
    Map2DRuntime.cpp
)
target_include_directories(kb_world2d_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kb_world2d_core PUBLIC kb_engine kb_renderer)
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
endif()
