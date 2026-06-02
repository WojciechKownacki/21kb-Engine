add_library(kb_renderer STATIC
    include/kb/render/BgfxContext.hpp
    include/kb/render/DisplayConfig.hpp
    include/kb/render/Renderer.hpp
    include/kb/render/RenderSurface.hpp
    include/kb/render/SceneDepthPolicy.hpp
    include/kb/render/SceneFramebuffer.hpp
    include/kb/render/SceneRenderTarget.hpp
    include/kb/render/SceneRenderTargetFormat.hpp
    include/kb/render/ShaderLoader.hpp
    include/kb/render/frame/FinalCompositePass.hpp
    include/kb/render/frame/FullscreenTexturePass.hpp
    include/kb/render/frame/EditorRenderPassSubmitter.hpp
    include/kb/render/frame/RenderFrameDesc.hpp
    include/kb/render/frame/RenderFramePipeline.hpp
    include/kb/render/frame/RenderFrameState.hpp
    include/kb/render/frame/RenderGraphResource.hpp
    include/kb/render/frame/RenderPassDesc.hpp
    include/kb/render/frame/RenderPassGraph.hpp
    include/kb/render/frame/RenderPassKind.hpp
    include/kb/render/frame/RenderSceneSubmitDesc.hpp
    include/kb/render/frame/RenderTargetDesc.hpp
    include/kb/render/frame/RenderViewportDesc.hpp
    include/kb/render/frame/RenderViewportViewIds.hpp
    include/kb/render/post/PostProcessChain.hpp
    include/kb/render/post/PostProcessRenderTarget.hpp
    include/kb/render/post/SceneDisplayCompositeRenderer.hpp
    include/kb/render/post/ScenePostProcessRenderer.hpp
    include/kb/render/post/ScenePostProcessTargets.hpp
    include/kb/render/resources/RenderHandles.hpp
    include/kb/render/resources/NativeWindowFramebuffer.hpp
    include/kb/render/resources/RenderMaterialAssetLoader.hpp
    include/kb/render/resources/RenderMeshAssetBuilder.hpp
    include/kb/render/resources/RenderMeshAssetLoader.hpp
    include/kb/render/resources/RenderResourceRegistry.hpp
    include/kb/render/resources/RenderResources.hpp
    include/kb/render/resources/RenderTextureAssetLoader.hpp
    include/kb/render/runtime/RuntimeFrameResourceReferences.hpp
    include/kb/render/runtime/RuntimeMaterialResolver.hpp
    include/kb/render/runtime/RuntimeRenderAssetDiscovery.hpp
    include/kb/render/runtime/RuntimeRenderResourceCache.hpp
    include/kb/render/runtime/RuntimeRenderResourceCacheTypes.hpp
    include/kb/render/scene/EcsRenderSceneSynchronizer.hpp
    include/kb/render/scene/MeshPipeline.hpp
    include/kb/render/scene/RenderInstanceBuffer.hpp
    include/kb/render/scene/RenderProxyId.hpp
    include/kb/render/scene/RenderScene.hpp
    include/kb/render/scene/SceneRenderResourceMap.hpp
    include/kb/render/scene/SceneRenderExtractor.hpp
    include/kb/render/scene/SceneRenderTypes.hpp
    include/kb/render/scene/SceneRenderer.hpp
    include/kb/render/scene/RenderSceneStore.hpp
    include/kb/render/shadow/DirectionalShadowPassPlanner.hpp
    include/kb/render/shadow/ShadowMapResource.hpp
    include/kb/render/ViewIdPolicy.hpp
    src/BgfxContext.cpp
    src/DisplayConfig.cpp
    src/Renderer.cpp
    src/SceneFramebuffer.cpp
    src/SceneRenderTarget.cpp
    src/SceneRenderTargetFormat.cpp
    src/ShaderLoader.cpp
    src/frame/EditorRenderPassSubmitter.cpp
    src/frame/FinalCompositePass.cpp
    src/frame/FullscreenTexturePass.cpp
    src/frame/RenderFramePipeline.cpp
    src/frame/RenderFrameState.cpp
    src/frame/RenderPassGraph.cpp
    src/frame/RenderPassKind.cpp
    src/post/PostProcessChain.cpp
    src/post/PostProcessRenderTarget.cpp
    src/post/SceneDisplayCompositeRenderer.cpp
    src/post/ScenePostProcessRenderer.cpp
    src/post/ScenePostProcessTargets.cpp
    src/resources/NativeWindowFramebuffer.cpp
    src/resources/RenderMaterialAssetLoader.cpp
    src/resources/RenderMeshAssetBuilder.cpp
    src/resources/RenderMeshAssetLoader.cpp
    src/resources/RenderResourceRegistry.cpp
    src/resources/RenderTextureAssetLoader.cpp
    src/runtime/RuntimeFrameResourceReferences.cpp
    src/runtime/RuntimeMaterialResolver.cpp
    src/runtime/RuntimeRenderAssetDiscovery.cpp
    src/runtime/RuntimeRenderResourceCache.cpp
    src/scene/EcsRenderSceneSynchronizer.cpp
    src/scene/EcsRenderTransformResolver.cpp
    src/scene/EcsRenderTransformResolver.hpp
    src/scene/MeshPipeline.cpp
    src/scene/RenderInstanceBuffer.cpp
    src/scene/RenderScene.cpp
    src/scene/SceneRenderResourceMap.cpp
    src/scene/SceneMeshSubmitter.cpp
    src/scene/SceneMeshSubmitter.hpp
    src/scene/SceneRenderExtractor.cpp
    src/scene/SceneRenderer.cpp
    src/scene/RenderSceneStore.cpp
    src/scene/SceneTransformMatrices.cpp
    src/scene/SceneTransformMatrices.hpp
    src/shadow/DirectionalShadowPassPlanner.cpp
    src/shadow/ShadowMapResource.cpp
    ${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake/bgfx/3rdparty/meshoptimizer/src/allocator.cpp
    ${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake/bgfx/3rdparty/meshoptimizer/src/indexgenerator.cpp
    ${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake/bgfx/3rdparty/meshoptimizer/src/overdrawoptimizer.cpp
    ${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake/bgfx/3rdparty/meshoptimizer/src/vcacheoptimizer.cpp
    ${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake/bgfx/3rdparty/meshoptimizer/src/vfetchoptimizer.cpp
)

if(TARGET bgfx::shaderc AND COMMAND bgfx_compile_shaders)
    set(KB_RENDERER_GENERATED_SHADER_DIR ${CMAKE_BINARY_DIR}/generated_shaders)
    set(KB_RENDERER_SHADER_RUNTIME_DIR ${CMAKE_BINARY_DIR}/shaders)
    set(KB_RENDERER_SHADER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
    set(KB_RENDERER_BGFX_SHADER_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake/bgfx/src)
    set(KB_RENDERER_SHADER_VARYING_DEF ${KB_RENDERER_SHADER_INCLUDE_DIR}/varying.def.sc)

    bgfx_compile_shaders(
        TYPE VERTEX
        SHADERS
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/vs_mesh.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/vs_mesh_instanced.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/vs_mesh_shadow_instanced.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/vs_present.sc
        VARYING_DEF ${KB_RENDERER_SHADER_VARYING_DEF}
        OUTPUT_DIR ${KB_RENDERER_GENERATED_SHADER_DIR}
        INCLUDE_DIRS
            ${KB_RENDERER_SHADER_INCLUDE_DIR}
            ${KB_RENDERER_BGFX_SHADER_INCLUDE_DIR}
        OUT_FILES_VAR KB_RENDERER_GENERATED_VERTEX_SHADERS
    )

    bgfx_compile_shaders(
        TYPE FRAGMENT
        SHADERS
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/fs_mesh.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/fs_mesh_instanced.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/fs_mesh_shadow_instanced.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/fs_present_tex.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/fs_post_bloom_prefilter.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/fs_post_bloom_blur.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/fs_post_bloom_combine.sc
        VARYING_DEF ${KB_RENDERER_SHADER_VARYING_DEF}
        OUTPUT_DIR ${KB_RENDERER_GENERATED_SHADER_DIR}
        INCLUDE_DIRS
            ${KB_RENDERER_SHADER_INCLUDE_DIR}
            ${KB_RENDERER_BGFX_SHADER_INCLUDE_DIR}
        OUT_FILES_VAR KB_RENDERER_GENERATED_FRAGMENT_SHADERS
    )

    add_custom_target(kb_renderer_generated_shaders
        DEPENDS
            ${KB_RENDERER_GENERATED_VERTEX_SHADERS}
            ${KB_RENDERER_GENERATED_FRAGMENT_SHADERS}
    )

    add_custom_target(kb_renderer_shaders ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory
            ${KB_RENDERER_SHADER_RUNTIME_DIR}
            ${KB_RENDERER_GENERATED_SHADER_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/prebuilt_shaders
            ${KB_RENDERER_SHADER_RUNTIME_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${KB_RENDERER_GENERATED_SHADER_DIR}
            ${KB_RENDERER_SHADER_RUNTIME_DIR}
        DEPENDS kb_renderer_generated_shaders
        COMMENT "Generate and stage bgfx runtime shaders"
        VERBATIM
    )
else()
    add_custom_target(kb_renderer_shaders ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/prebuilt_shaders
            ${CMAKE_BINARY_DIR}/shaders
        COMMENT "Stage prebuilt bgfx shaders"
        VERBATIM
    )
endif()
add_dependencies(kb_renderer kb_renderer_shaders)

target_include_directories(kb_renderer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake/bgfx/3rdparty
)

target_link_libraries(kb_renderer
    PUBLIC
        kb_engine
        bgfx
        bx
        bimg
        bimg_decode
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

if(BUILD_TESTING)
    add_executable(kb_renderer_tests
        tests/RendererTests.cpp
        tests/RendererTestSupport.hpp
        tests/FinalCompositePassTests.cpp
        tests/PostProcessChainTests.cpp
        tests/RenderFramePipelineTests.cpp
        tests/RenderResourceRegistryTests.cpp
        tests/RendererRuntimeSubmitTests.cpp
        tests/MeshPipelineTests.cpp
        tests/SceneDisplayCompositeTests.cpp
        tests/SceneDepthPolicyTests.cpp
        tests/RenderSceneSyncTests.cpp
        tests/SceneRenderTargetFormatTests.cpp
        tests/SceneRenderExtractorTests.cpp
    )

    target_link_libraries(kb_renderer_tests
        PRIVATE
            kb_renderer
    )

    add_test(NAME kb_renderer_tests COMMAND kb_renderer_tests)

    kb_apply_quality_gates(kb_renderer_tests)
endif()
