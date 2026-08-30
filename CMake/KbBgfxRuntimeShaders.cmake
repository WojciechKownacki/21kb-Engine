function(_kb_ensure_bgfx_runtime_shaders_bundle_target)
    get_property(_existing_bundle_target GLOBAL PROPERTY KB_BGFX_RUNTIME_SHADERS_BUNDLE_TARGET)
    if(_existing_bundle_target)
        return()
    endif()

    get_property(_kb_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(_kb_multi_config)
        set(_shader_dst "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/shaders")
    else()
        set(_shader_dst "${CMAKE_BINARY_DIR}/bin/shaders")
    endif()

    if(WIN32)
        set(_shader_platform "windows")
    elseif(ANDROID)
        set(_shader_platform "android")
    elseif(EMSCRIPTEN)
        set(_shader_platform "asm.js")
    elseif(APPLE)
        set(_shader_platform "osx")
    else()
        set(_shader_platform "linux")
    endif()

    set(_shader_src "${CMAKE_BINARY_DIR}/shaders")
    set(_bundle_stamp "${_shader_dst}/.kb_bgfx_runtime_shaders_bundle.stamp")
    get_property(_renderer_shader_stage_stamp GLOBAL PROPERTY KB_RENDERER_SHADER_STAGE_STAMP)

    add_custom_command(
        OUTPUT "${_bundle_stamp}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${_shader_dst}/dxbc"
            "${_shader_dst}/dxil"
            "${_shader_dst}/spirv"
            "${_shader_dst}/metal"
            "${_shader_dst}/essl"
            "${_shader_dst}/glsl"
            "${_shader_dst}/${_shader_platform}/dxbc"
            "${_shader_dst}/${_shader_platform}/dxil"
            "${_shader_dst}/${_shader_platform}/spirv"
            "${_shader_dst}/${_shader_platform}/metal"
            "${_shader_dst}/${_shader_platform}/essl"
            "${_shader_dst}/${_shader_platform}/glsl"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/dxbc" "${_shader_dst}/dxbc"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/dxil" "${_shader_dst}/dxil"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/spirv" "${_shader_dst}/spirv"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/metal" "${_shader_dst}/metal"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/essl" "${_shader_dst}/essl"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/glsl" "${_shader_dst}/glsl"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/dxbc" "${_shader_dst}/${_shader_platform}/dxbc"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/dxil" "${_shader_dst}/${_shader_platform}/dxil"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/spirv" "${_shader_dst}/${_shader_platform}/spirv"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/metal" "${_shader_dst}/${_shader_platform}/metal"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/essl" "${_shader_dst}/${_shader_platform}/essl"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/glsl" "${_shader_dst}/${_shader_platform}/glsl"
        COMMAND ${CMAKE_COMMAND} -E touch "${_bundle_stamp}"
        DEPENDS "${_renderer_shader_stage_stamp}"
        COMMENT "Copy bgfx shader runtime trees"
        VERBATIM
    )
    add_custom_target(kb_bgfx_runtime_shaders_bundle
        DEPENDS "${_bundle_stamp}"
    )
    if(TARGET kb_renderer_shaders)
        add_dependencies(kb_bgfx_runtime_shaders_bundle kb_renderer_shaders)
    endif()
    set_property(GLOBAL PROPERTY KB_BGFX_RUNTIME_SHADERS_BUNDLE_STAMP "${_bundle_stamp}")
    set_property(GLOBAL PROPERTY KB_BGFX_RUNTIME_SHADERS_BUNDLE_TARGET kb_bgfx_runtime_shaders_bundle)
endfunction()

function(kb_target_copy_bgfx_runtime_shaders target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "kb_target_copy_bgfx_runtime_shaders: target '${target_name}' does not exist")
    endif()

    _kb_ensure_bgfx_runtime_shaders_bundle_target()
    get_property(_bundle_target GLOBAL PROPERTY KB_BGFX_RUNTIME_SHADERS_BUNDLE_TARGET)
    if(TARGET kb_renderer_shaders)
        add_dependencies(${target_name} kb_renderer_shaders)
    endif()
    add_dependencies(${target_name} ${_bundle_target})
endfunction()

set(KB_MATERIAL_GRAPH_SHADER_CACHE_SOURCE
    "${CMAKE_SOURCE_DIR}/Project/.cache/graph_shaders"
    CACHE PATH
    "Project material graph shader cache staged next to standalone runtime targets"
)

function(kb_target_stage_material_graph_shader_cache target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "kb_target_stage_material_graph_shader_cache: target '${target_name}' does not exist")
    endif()

    add_custom_command(
        TARGET ${target_name}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DKB_GRAPH_SHADER_CACHE_SOURCE=${KB_MATERIAL_GRAPH_SHADER_CACHE_SOURCE}"
            "-DKB_GRAPH_SHADER_CACHE_DEST=$<TARGET_FILE_DIR:${target_name}>/.cache/graph_shaders"
            -P "${CMAKE_SOURCE_DIR}/CMake/StageMaterialGraphShaderCache.cmake"
        COMMENT "Stage Material Graph shader cache for ${target_name}"
        VERBATIM
    )
endfunction()
