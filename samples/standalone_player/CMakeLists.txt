if(NOT WIN32)
    return()
endif()

add_executable(kb_standalone_player
    main.cpp
    StandaloneInputRuntime.cpp
    StandaloneInputRuntime.hpp
)

target_link_libraries(kb_standalone_player
    PRIVATE
        kb_renderer
        user32
        gdi32
)

add_dependencies(kb_standalone_player kb_21kb_particle_plugin)
if(KB_BUILD_PROVIDER_MODULES_AS_DLL)
    add_custom_command(TARGET kb_standalone_player POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:kb_21kb_particle_plugin>
            $<TARGET_FILE_DIR:kb_standalone_player>
        VERBATIM
    )
endif()

set_target_properties(kb_standalone_player PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

kb_apply_quality_gates(kb_standalone_player)
kb_target_copy_bgfx_runtime_shaders(kb_standalone_player)
kb_target_stage_material_graph_shader_cache(kb_standalone_player)

if(BUILD_TESTING)
    add_test(
        NAME kb_standalone_player_camera_runtime
        COMMAND $<TARGET_FILE:kb_standalone_player> --camera-runtime-self-test --renderer=noop
    )
    set_tests_properties(
        kb_standalone_player_camera_runtime
        PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()

set(_kb_standalone_graph_shaderc_tool "")
if(TARGET bgfx::shaderc)
    set(_kb_standalone_graph_shaderc_tool "$<PATH:CMAKE_PATH,$<TARGET_FILE:bgfx::shaderc>>")
    add_dependencies(kb_standalone_player bgfx::shaderc)
elseif(KB_GRAPH_SHADERC_TOOL)
    cmake_path(CONVERT "${KB_GRAPH_SHADERC_TOOL}" TO_CMAKE_PATH_LIST _kb_standalone_graph_shaderc_tool NORMALIZE)
endif()

if(_kb_standalone_graph_shaderc_tool)
    target_compile_definitions(kb_standalone_player
        PRIVATE
            KB_STANDALONE_GRAPH_SHADERC_PATH="${_kb_standalone_graph_shaderc_tool}"
            KB_STANDALONE_GRAPH_SHADER_VARYING_DEF="${CMAKE_SOURCE_DIR}/sources/renderer/shaders/varying.def.sc"
            KB_STANDALONE_GRAPH_SHADER_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/sources/renderer/shaders"
            KB_STANDALONE_GRAPH_BGFX_SHADER_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake/bgfx/src"
    )
endif()
