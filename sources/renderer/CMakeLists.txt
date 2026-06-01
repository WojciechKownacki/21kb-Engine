add_library(kb_renderer STATIC
    include/kb/render/BgfxContext.hpp
    include/kb/render/DisplayConfig.hpp
    include/kb/render/Renderer.hpp
    include/kb/render/RenderSurface.hpp
    include/kb/render/ShaderLoader.hpp
    include/kb/render/SimpleTriangle.hpp
    include/kb/render/ViewIdPolicy.hpp
    src/BgfxContext.cpp
    src/DisplayConfig.cpp
    src/Renderer.cpp
    src/ShaderLoader.cpp
    src/SimpleTriangle.cpp
)

add_custom_target(kb_renderer_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/prebuilt_shaders
        ${CMAKE_BINARY_DIR}/shaders
    COMMENT "Stage prebuilt bgfx shaders"
    VERBATIM
)
add_dependencies(kb_renderer kb_renderer_shaders)

target_include_directories(kb_renderer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(kb_renderer
    PUBLIC
        bgfx
        bx
        bimg
)

if(WIN32)
    target_link_libraries(kb_renderer PUBLIC d3d11 dxgi d3dcompiler user32 gdi32)
endif()

target_compile_definitions(kb_renderer
    PRIVATE
        $<$<BOOL:${WIN32}>:NOMINMAX>
        $<$<BOOL:${WIN32}>:WIN32_LEAN_AND_MEAN>
)

kb_apply_quality_gates(kb_renderer)
