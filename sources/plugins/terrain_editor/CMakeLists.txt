set(KB_TERRAIN_EDITOR_PLUGIN_LIBRARY_TYPE STATIC)
if(KB_BUILD_PROVIDER_MODULES_AS_DLL)
    set(KB_TERRAIN_EDITOR_PLUGIN_LIBRARY_TYPE SHARED)
endif()

add_library(kb_terrain_editor_core STATIC
    TerrainBrush.hpp
    TerrainBrush.cpp
    TerrainHeightmapImporter.hpp
    TerrainHeightmapImporter.cpp
    TerrainLayerPainter.hpp
    TerrainLayerPainter.cpp
)

target_include_directories(kb_terrain_editor_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake/bimg/3rdparty
)
target_link_libraries(kb_terrain_editor_core PUBLIC kb_engine PRIVATE bimg_decode)
kb_apply_quality_gates(kb_terrain_editor_core)

add_library(kb_terrain_editor_plugin ${KB_TERRAIN_EDITOR_PLUGIN_LIBRARY_TYPE}
    TerrainEditorModule.hpp
    TerrainEditorModule.cpp
)

target_include_directories(kb_terrain_editor_plugin
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(kb_terrain_editor_plugin
    PRIVATE
        kb_terrain_editor_core
)

if(NOT KB_BUILD_PROVIDER_MODULES_AS_DLL)
    target_compile_definitions(kb_terrain_editor_plugin
        PUBLIC
            KB_ENGINE_MODULE_STATIC_LINK
    )
endif()

kb_apply_quality_gates(kb_terrain_editor_plugin)

if(BUILD_TESTING AND KB_BUILD_RENDERER)
    add_executable(kb_terrain_editor_tests TerrainEditorTests.cpp)
    target_link_libraries(kb_terrain_editor_tests PRIVATE kb_terrain_editor_core kb_renderer)
    kb_apply_quality_gates(kb_terrain_editor_tests)
    add_test(NAME kb_terrain_editor_tests COMMAND kb_terrain_editor_tests)
endif()
