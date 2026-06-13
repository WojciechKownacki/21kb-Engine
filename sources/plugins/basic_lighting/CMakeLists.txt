set(KB_BASIC_LIGHTING_PLUGIN_LIBRARY_TYPE STATIC)
if(KB_BUILD_PROVIDER_MODULES_AS_DLL)
    set(KB_BASIC_LIGHTING_PLUGIN_LIBRARY_TYPE SHARED)
endif()

add_library(kb_basic_lighting_plugin ${KB_BASIC_LIGHTING_PLUGIN_LIBRARY_TYPE}
    BasicLightingModule.hpp
    BasicLightingModule.cpp
)

target_include_directories(kb_basic_lighting_plugin
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(kb_basic_lighting_plugin
    PRIVATE
        kb_engine
)

if(NOT KB_BUILD_PROVIDER_MODULES_AS_DLL)
    target_compile_definitions(kb_basic_lighting_plugin
        PUBLIC
            KB_ENGINE_MODULE_STATIC_LINK
    )
endif()

kb_apply_quality_gates(kb_basic_lighting_plugin)
