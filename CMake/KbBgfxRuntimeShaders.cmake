function(_kb_ensure_bgfx_runtime_shaders_bundle_target)
    if(TARGET kb_bgfx_runtime_shaders_bundle)
        return()
    endif()

    get_property(_kb_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(_kb_multi_config)
        set(_shader_dst "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/shaders")
    else()
        set(_shader_dst "${CMAKE_BINARY_DIR}/bin/shaders")
    endif()

    set(_shader_src "${CMAKE_BINARY_DIR}/shaders")

    add_custom_target(kb_bgfx_runtime_shaders_bundle
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${_shader_dst}/dxbc"
            "${_shader_dst}/dxil"
            "${_shader_dst}/spirv"
            "${_shader_dst}/metal"
            "${_shader_dst}/essl"
            "${_shader_dst}/glsl"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/dxbc" "${_shader_dst}/dxbc"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/dxil" "${_shader_dst}/dxil"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/spirv" "${_shader_dst}/spirv"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/metal" "${_shader_dst}/metal"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/essl" "${_shader_dst}/essl"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/glsl" "${_shader_dst}/glsl"
        COMMENT "Copy bgfx shader runtime trees"
        VERBATIM
    )

    if(TARGET kb_renderer_shaders)
        add_dependencies(kb_bgfx_runtime_shaders_bundle kb_renderer_shaders)
    endif()
endfunction()

function(kb_target_copy_bgfx_runtime_shaders target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "kb_target_copy_bgfx_runtime_shaders: target '${target_name}' does not exist")
    endif()

    _kb_ensure_bgfx_runtime_shaders_bundle_target()
    add_dependencies(${target_name} kb_bgfx_runtime_shaders_bundle)
endfunction()
