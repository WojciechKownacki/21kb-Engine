if(NOT WIN32)
    return()
endif()

add_executable(kb_render_smoke
    main.cpp
)

target_link_libraries(kb_render_smoke
    PRIVATE
        kb_renderer
        user32
)

set_target_properties(kb_render_smoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

kb_apply_quality_gates(kb_render_smoke)
kb_target_copy_bgfx_runtime_shaders(kb_render_smoke)
