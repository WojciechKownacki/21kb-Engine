function(_kb_ensure_bgfx_runtime_shaders_bundle_target)
    get_property(_existing_bundle_stamp GLOBAL PROPERTY KB_BGFX_RUNTIME_SHADERS_BUNDLE_STAMP)
    if(_existing_bundle_stamp)
        return()
    endif()

    get_property(_kb_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(_kb_multi_config)
        set(_shader_dst "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/shaders")
    else()
        set(_shader_dst "${CMAKE_BINARY_DIR}/bin/shaders")
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
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/dxbc" "${_shader_dst}/dxbc"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/dxil" "${_shader_dst}/dxil"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/spirv" "${_shader_dst}/spirv"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/metal" "${_shader_dst}/metal"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/essl" "${_shader_dst}/essl"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_shader_src}/glsl" "${_shader_dst}/glsl"
        COMMAND ${CMAKE_COMMAND} -E touch "${_bundle_stamp}"
        DEPENDS "${_renderer_shader_stage_stamp}"
        COMMENT "Copy bgfx shader runtime trees"
        VERBATIM
    )
    set_source_files_properties("${_bundle_stamp}" PROPERTIES GENERATED TRUE)
    set_property(GLOBAL PROPERTY KB_BGFX_RUNTIME_SHADERS_BUNDLE_STAMP "${_bundle_stamp}")
endfunction()

function(kb_target_copy_bgfx_runtime_shaders target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "kb_target_copy_bgfx_runtime_shaders: target '${target_name}' does not exist")
    endif()

    _kb_ensure_bgfx_runtime_shaders_bundle_target()
    get_property(_bundle_stamp GLOBAL PROPERTY KB_BGFX_RUNTIME_SHADERS_BUNDLE_STAMP)
    target_sources(${target_name} PRIVATE "${_bundle_stamp}")
    if(TARGET kb_renderer_shaders)
        add_dependencies(${target_name} kb_renderer_shaders)
    endif()
endfunction()
