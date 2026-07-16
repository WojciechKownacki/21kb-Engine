add_library(kb_renderer STATIC
    include/kb/render/BgfxContext.hpp
    include/kb/render/DisplayConfig.hpp
    include/kb/render/Renderer.hpp
    include/kb/render/RendererCapabilityReport.hpp
    include/kb/render/RenderSurface.hpp
    include/kb/render/SceneDeferredLightingPass.hpp
    include/kb/render/SceneDepthPolicy.hpp
    include/kb/render/SceneFramebuffer.hpp
    include/kb/render/SceneGBuffer.hpp
    include/kb/render/SceneGBufferContract.hpp
    include/kb/render/SceneRenderTarget.hpp
    include/kb/render/SceneRenderTargetFormat.hpp
    include/kb/render/ShaderManifest.hpp
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
    include/kb/render/overlay/SceneGizmoPass.hpp
    include/kb/render/overlay/EditorLightWireframe.hpp
    include/kb/render/overlay/PhysicsDebugLine.hpp
    include/kb/render/overlay/SceneGridPass.hpp
    include/kb/render/overlay/SelectionOutlineCompositePass.hpp
    include/kb/render/post/PostProcessChain.hpp
    include/kb/render/post/PostProcessRenderTarget.hpp
    include/kb/render/post/SceneExposureMeter.hpp
    include/kb/render/post/SceneDisplayCompositeRenderer.hpp
    include/kb/render/post/ScenePostProcessSettings.hpp
    include/kb/render/post/ScenePostProcessRenderer.hpp
    include/kb/render/post/ScenePostProcessTargets.hpp
    include/kb/render/resources/RenderHandles.hpp
    include/kb/render/resources/NativeWindowFramebuffer.hpp
    include/kb/render/resources/RenderMaterialAssetLoader.hpp
    include/kb/render/resources/RenderMaterialAssetWriter.hpp
    include/kb/render/resources/RenderMaterialCookPayload.hpp
    include/kb/render/resources/RenderMaterialFunctionAssetLoader.hpp
    include/kb/render/resources/RenderMaterialGraphAssetLoader.hpp
    include/kb/render/resources/RenderMaterialGraphDocument.hpp
    include/kb/render/resources/RenderMaterialGraphVariantKey.hpp
    include/kb/render/resources/RenderMaterialParameterCollection.hpp
    include/kb/render/resources/RenderMaterialInstanceAssetLoader.hpp
    include/kb/render/resources/RenderMaterialInstanceAssetWriter.hpp
    include/kb/render/resources/RenderMaterialTypeAssetLoader.hpp
    include/kb/render/resources/RenderMaterialTypeSchema.hpp
    include/kb/render/resources/PostProcessProfileAssetLoader.hpp
    include/kb/render/resources/BuiltInParticleQuadMesh.hpp
    include/kb/render/resources/RenderMeshAssetBuilder.hpp
    include/kb/render/resources/RenderMeshAssetLoader.hpp
    include/kb/render/resources/RenderResourceRegistry.hpp
    include/kb/render/resources/RenderResources.hpp
    include/kb/render/resources/RenderTextureAssetLoader.hpp
    include/kb/render/resources/detail/RenderResourceSlotPool.hpp
    include/kb/render/runtime/RuntimeFrameResourceReferences.hpp
    include/kb/render/runtime/RuntimeMaterialResolver.hpp
    include/kb/render/runtime/RuntimeRenderAssetDiscovery.hpp
    include/kb/render/runtime/RuntimeRenderResourceCache.hpp
    include/kb/render/runtime/RuntimeRenderResourceCacheTypes.hpp
    include/kb/render/scene/EcsRenderSceneSynchronizer.hpp
    include/kb/render/scene/SceneParticleRenderSynchronizer.hpp
    include/kb/render/scene/RenderBridgeTelemetry.hpp
    include/kb/render/scene/SceneGpuDrivenCullingPass.hpp
    include/kb/render/scene/SceneGpuDrivenFeatureState.hpp
    include/kb/render/scene/SceneGpuDrivenFrameResources.hpp
    include/kb/render/scene/SceneGpuDrivenParityValidator.hpp
    include/kb/render/scene/MeshPipeline.hpp
    include/kb/render/scene/MeshPassType.hpp
    include/kb/render/scene/batch/SceneMeshBatch.hpp
    include/kb/render/scene/batch/SceneMeshBatchBuilder.hpp
    include/kb/render/scene/cache/SceneCachedDrawCommand.hpp
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
    src/RendererCapabilityReport.cpp
    src/renderer/RendererExposureSubmitter.cpp
    src/renderer/RendererExposureSubmitter.hpp
    src/renderer/RendererEditorOverlaySubmitter.cpp
    src/renderer/RendererEditorOverlaySubmitter.hpp
    src/renderer/RendererFinalCompositeSubmitter.cpp
    src/renderer/RendererFinalCompositeSubmitter.hpp
    src/renderer/RendererMeshPassSubmitter.cpp
    src/renderer/RendererMeshPassSubmitter.hpp
    src/renderer/RendererMatrixMath.cpp
    src/renderer/RendererMatrixMath.hpp
    src/renderer/RendererPostProcessSubmitter.cpp
    src/renderer/RendererPostProcessSubmitter.hpp
    src/renderer/RendererRuntimeResourceStatsBuilder.cpp
    src/renderer/RendererRuntimeResourceStatsBuilder.hpp
    src/renderer/RendererSceneLightingConfigResolver.cpp
    src/renderer/RendererSceneLightingConfigResolver.hpp
    src/renderer/RendererShadowSubmitter.cpp
    src/renderer/RendererShadowSubmitter.hpp
    src/renderer/RendererTemporalJitter.cpp
    src/renderer/RendererTemporalJitter.hpp
    src/renderer/RendererViewConfigurator.cpp
    src/renderer/RendererViewConfigurator.hpp
    src/renderer/RendererViewportPassResolver.cpp
    src/renderer/RendererViewportPassResolver.hpp
    src/SceneDeferredLightingPass.cpp
    src/SceneFramebuffer.cpp
    src/SceneGBuffer.cpp
    src/SceneRenderTarget.cpp
    src/SceneRenderTargetFormat.cpp
    src/ShaderManifest.cpp
    src/ShaderLoader.cpp
    src/MaterialProgramRegistry.cpp
    src/frame/EditorRenderPassSubmitter.cpp
    src/frame/FinalCompositePass.cpp
    src/frame/FullscreenTexturePass.cpp
    src/frame/RenderFramePipeline.cpp
    src/frame/RenderFrameState.cpp
    src/frame/RenderPassGraph.cpp
    src/frame/RenderPassGraphCompiler.cpp
    src/frame/RenderPassGraphCompiler.hpp
    src/frame/RenderPassGraphValidator.cpp
    src/frame/RenderPassGraphValidator.hpp
    src/frame/RenderPassKind.cpp
    src/overlay/SceneGizmoPass.cpp
    src/overlay/SceneGridPass.cpp
    src/overlay/SelectionBoxOverlayPass.cpp
    src/overlay/SelectionOutlineCompositePass.cpp
    src/post/PostProcessChain.cpp
    src/post/PostProcessRenderTarget.cpp
    src/post/SceneExposureGpuReadback.cpp
    src/post/SceneExposureGpuReadback.hpp
    src/post/SceneExposureMeter.cpp
    src/post/SceneExposureHistogramBuilder.cpp
    src/post/SceneExposureHistogramBuilder.hpp
    src/post/SceneDisplayCompositeRenderer.cpp
    src/post/ScenePostProcessRenderer.cpp
    src/post/ScenePostProcessSubmitter.cpp
    src/post/ScenePostProcessTargets.cpp
    src/resources/NativeWindowFramebuffer.cpp
    src/resources/RenderMaterialAssetLoader.cpp
    src/resources/RenderMaterialAssetFieldParser.cpp
    src/resources/RenderMaterialAssetFieldParser.hpp
    src/resources/RenderMaterialAssetParser.cpp
    src/resources/RenderMaterialAssetParser.hpp
    src/resources/RenderMaterialAssetWriter.cpp
    src/resources/RenderMaterialCookPayload.cpp
    src/resources/RenderMaterialFunctionAssetLoader.cpp
    src/resources/RenderMaterialGraphAssetLoader.cpp
    src/resources/RenderMaterialGraphDocument.cpp
    src/resources/RenderMaterialGraphCompiler.cpp
    src/resources/RenderMaterialGraphFunctionInlining.cpp
    src/resources/RenderMaterialGraphPinLayout.cpp
    src/resources/RenderMaterialGraphNodeCatalog.cpp
    src/resources/RenderMaterialGraphNodeParser.cpp
    src/resources/RenderMaterialGraphEnumStrings.cpp
    src/resources/RenderMaterialGraphRuntimeState.cpp
    src/resources/RenderMaterialGraphArtifactCache.cpp
    src/resources/RenderMaterialGraphPinTypes.cpp
    src/resources/RenderMaterialGraphPinIds.cpp
    src/resources/RenderMaterialParameterCollection.cpp
    src/resources/RenderMaterialGraphProgramBindingBuilder.cpp
    src/resources/RenderMaterialGraphShaderArtifact.cpp
    src/resources/RenderMaterialSemanticHash.cpp
    src/resources/RenderMaterialGraphFieldParser.cpp
    src/resources/RenderMaterialGraphFieldParser.hpp
    src/resources/RenderMaterialInstanceAssetLoader.cpp
    src/resources/RenderMaterialInstanceAssetWriter.cpp
    src/resources/RenderMaterialTypeAssetLoader.cpp
    src/resources/PostProcessProfileAssetLoader.cpp
    src/resources/RenderMaterialTextureFieldParser.cpp
    src/resources/RenderMaterialTextureFieldParser.hpp
    src/resources/RenderMaterialResourceBuilder.cpp
    src/resources/RenderMaterialResourceBuilder.hpp
    src/resources/BuiltInPbrMaterialType.cpp
    src/resources/RenderMeshDescGeometry.cpp
    src/resources/RenderMeshDescGeometry.hpp
    src/resources/RenderMeshDescValidator.cpp
    src/resources/RenderMeshDescValidator.hpp
    src/resources/RenderMeshAssetFinalizer.cpp
    src/resources/RenderMeshAssetFinalizer.hpp
    src/resources/RenderMeshAssetBuilder.cpp
    src/resources/RenderMeshFbxImporter.cpp
    src/resources/RenderMeshFbxImporter.hpp
    src/resources/RenderMeshGltfImporter.cpp
    src/resources/RenderMeshGltfImporter.hpp
    src/resources/RenderMeshGltfMaterialImporter.cpp
    src/resources/RenderMeshGltfMaterialImporter.hpp
    src/resources/RenderMeshGltfTransforms.cpp
    src/resources/RenderMeshGltfTransforms.hpp
    src/resources/RenderMeshAssetLoader.cpp
    src/resources/BuiltInParticleQuadMesh.cpp
    src/resources/RenderMeshObjImporter.cpp
    src/resources/RenderMeshObjImporter.hpp
    src/resources/RenderMeshObjMaterialResolver.cpp
    src/resources/RenderMeshObjMaterialResolver.hpp
    src/resources/RenderMeshResourceBuilder.cpp
    src/resources/RenderMeshResourceBuilder.hpp
    src/resources/RenderResourceRegistry.cpp
    src/resources/RenderResourceReleaser.cpp
    src/resources/RenderResourceReleaser.hpp
    src/resources/RenderStaticMeshVertexFormat.cpp
    src/resources/RenderTextureAssetLoader.cpp
    src/resources/RenderTextureResourceBuilder.cpp
    src/resources/RenderTextureResourceBuilder.hpp
    src/runtime/RuntimeFrameResourceReferences.cpp
    src/runtime/RuntimeMaterialGraphDependencyLoader.cpp
    src/runtime/RuntimeMaterialGraphDependencyLoader.hpp
    src/runtime/RuntimeMaterialResolver.cpp
    src/runtime/RuntimeMaterialResourceEnsurer.cpp
    src/runtime/RuntimeMaterialResourceEnsurer.hpp
    src/runtime/RuntimeMeshResourceEnsurer.cpp
    src/runtime/RuntimeMeshResourceEnsurer.hpp
    src/runtime/RuntimeRenderAssetDiscovery.cpp
    src/runtime/RuntimeRenderResourceCache.cpp
    src/runtime/RuntimeRenderResourceLifecycle.cpp
    src/runtime/RuntimeRenderResourceLifecycle.hpp
    src/runtime/RuntimeRenderResourcePruner.cpp
    src/runtime/RuntimeRenderResourcePruner.hpp
    src/runtime/RuntimeTextureResourceEnsurer.cpp
    src/runtime/RuntimeTextureResourceEnsurer.hpp
    src/scene/EcsRenderSceneSynchronizer.cpp
    src/scene/SceneParticleRenderSynchronizer.cpp
    src/scene/SceneRenderVisibilityPublisher.cpp
    src/scene/SceneRenderVisibilityPublisher.hpp
    src/renderer/RendererScreenCapture.cpp
    src/renderer/RendererScreenCapture.hpp
    src/scene/EcsRenderTransformResolver.cpp
    src/scene/EcsRenderTransformResolver.hpp
    src/scene/SceneRenderWorldTransformReader.cpp
    src/scene/SceneRenderWorldTransformReader.hpp
    src/scene/SceneGpuDrivenCullingPass.cpp
    src/scene/SceneGpuDrivenFrameResources.cpp
    src/scene/MeshPipeline.cpp
    src/scene/batch/SceneMeshBatchBuilder.cpp
    src/scene/batch/SceneMeshBatchSourceResolver.cpp
    src/scene/batch/SceneMeshBatchSourceResolver.hpp
    src/scene/cache/SceneCachedDrawCommand.cpp
    src/scene/cache/SceneCachedDrawCommandMaterializer.cpp
    src/scene/cache/SceneCachedDrawCommandMaterializer.hpp
    src/scene/cache/SceneMaterialTextureDependencySignature.cpp
    src/scene/cache/SceneMaterialTextureDependencySignature.hpp
    src/scene/RenderInstanceBuffer.cpp
    src/scene/RenderScene.cpp
    src/scene/RenderSceneProxyConverters.cpp
    src/scene/RenderSceneProxyConverters.hpp
    src/scene/RenderSceneProxyDirtyTracker.cpp
    src/scene/RenderSceneProxyDirtyTracker.hpp
    src/scene/SceneRenderResourceMap.cpp
    src/scene/SceneMeshSubmitter.cpp
    src/scene/SceneMeshSubmitter.hpp
    src/scene/lighting/SceneForwardLightSelector.cpp
    src/scene/lighting/SceneForwardLightSelector.hpp
    src/scene/lighting/SceneLightingPacker.cpp
    src/scene/lighting/SceneLightingPacker.hpp
    src/scene/pipeline/MeshPipelineCommandBuilder.cpp
    src/scene/pipeline/MeshPipelineCommandBuilder.hpp
    src/scene/pipeline/MeshPipelineGpuDrivenRecorder.cpp
    src/scene/pipeline/MeshPipelineGpuDrivenRecorder.hpp
    src/scene/pipeline/MeshPipelinePassPolicy.cpp
    src/scene/pipeline/MeshPipelinePassPolicy.hpp
    src/scene/pipeline/MeshPipelineResourceResolver.cpp
    src/scene/pipeline/MeshPipelineResourceResolver.hpp
    src/scene/pipeline/MeshPipelineVisibility.cpp
    src/scene/pipeline/MeshPipelineVisibility.hpp
    src/scene/pass/MeshPassProcessor.cpp
    src/scene/pass/MeshPassProcessor.hpp
    src/scene/submit/SceneMeshDrawCommandSubmitter.cpp
    src/scene/submit/SceneMeshDrawCommandSubmitter.hpp
    src/scene/submit/SceneMeshMaterialBindingResolver.cpp
    src/scene/submit/SceneMeshMaterialBindingResolver.hpp
    src/scene/submit/SceneMeshPassResources.cpp
    src/scene/submit/SceneMeshPassResources.hpp
    src/scene/SceneRenderExtractor.cpp
    src/scene/SceneRenderer.cpp
    src/scene/RenderSceneStore.cpp
    src/scene/SceneTransformMatrices.cpp
    src/scene/SceneTransformMatrices.hpp
    src/scene/SceneLightColor.cpp
    src/scene/SceneLightColor.hpp
    src/shadow/DirectionalShadowPassPlanner.cpp
    src/shadow/DirectionalShadowLightSelector.cpp
    src/shadow/DirectionalShadowLightSelector.hpp
    src/shadow/ShadowCasterBoundsCollector.cpp
    src/shadow/ShadowCasterBoundsCollector.hpp
    src/shadow/ShadowMapResource.cpp
    ${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake/bgfx/3rdparty/meshoptimizer/src/allocator.cpp
    ${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake/bgfx/3rdparty/meshoptimizer/src/indexgenerator.cpp
    ${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake/bgfx/3rdparty/meshoptimizer/src/overdrawoptimizer.cpp
    ${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake/bgfx/3rdparty/meshoptimizer/src/vcacheoptimizer.cpp
    ${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake/bgfx/3rdparty/meshoptimizer/src/vfetchoptimizer.cpp
)

set(KB_RENDERER_SHADER_PROFILES
    dxbc
    dxil
    spirv
    glsl
    essl
    metal
)

set(KB_RENDERER_SHADER_MANIFEST
    cs_hzb_downsample.sc
    cs_hzb_seed.sc
    cs_instance_cull.sc
    cs_instance_cull_clear.sc
    cs_instance_cull_finalize.sc
    cs_levg_meshlet_clear.sc
    cs_levg_meshlet_cull.sc
    cs_levg_meshlet_finalize.sc
    cs_massive_fill.sc
    fs_editor_gizmo.sc
    fs_editor_gizmo_resolve.sc
    fs_editor_grid.sc
    fs_editor_selection_outline.sc
    fs_editor_viewport_grid.sc
    fs_leui_rect.sc
    fs_lit.sc
    fs_deferred_lighting.sc
    fs_mesh.sc
    fs_mesh_gbuffer_instanced.sc
    fs_mesh_instanced.sc
    fs_mesh_selection_instanced.sc
    fs_mesh_shadow_instanced.sc
    fs_post_bloom_blur.sc
    fs_post_bloom_combine.sc
    fs_post_bloom_prefilter.sc
    fs_post_exposure_luminance.sc
    fs_post_fxaa.sc
    fs_post_motion_vectors.sc
    fs_post_taa_resolve.sc
    fs_present_tex.sc
    vs_editor_gizmo.sc
    vs_editor_grid.sc
    vs_editor_selection_outline.sc
    vs_editor_viewport_grid.sc
    vs_leui_rect.sc
    vs_lit.sc
    vs_lit_trs.sc
    vs_lit_trs_gpu_cull.sc
    vs_mesh.sc
    vs_mesh_instanced.sc
    vs_mesh_shadow_instanced.sc
    vs_present.sc
)

file(GLOB_RECURSE KB_RENDERER_PREBUILT_SHADER_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/prebuilt_shaders/*"
)
set(KB_RENDERER_SHADER_RUNTIME_DIR "${CMAKE_BINARY_DIR}/shaders")
set(KB_RENDERER_SHADER_STAGE_STAMP "${KB_RENDERER_SHADER_RUNTIME_DIR}/.kb_renderer_shaders.stamp")
set(KB_RENDERER_SHADER_MANIFEST_STAMP "${KB_RENDERER_SHADER_RUNTIME_DIR}/.kb_renderer_shader_manifest.stamp")
set_property(GLOBAL PROPERTY KB_RENDERER_SHADER_STAGE_STAMP "${KB_RENDERER_SHADER_STAGE_STAMP}")
set_property(GLOBAL PROPERTY KB_RENDERER_SHADER_RUNTIME_DIR "${KB_RENDERER_SHADER_RUNTIME_DIR}")

if(KB_GENERATE_RENDERER_SHADERS AND TARGET bgfx::shaderc AND COMMAND bgfx_compile_shaders)
    set(KB_RENDERER_GENERATED_SHADER_DIR ${CMAKE_BINARY_DIR}/generated_shaders)
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
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/vs_editor_grid.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/vs_editor_gizmo.sc
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
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/fs_mesh_gbuffer_instanced.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/fs_mesh_instanced.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/fs_mesh_selection_instanced.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/fs_mesh_shadow_instanced.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/fs_present_tex.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/fs_post_bloom_prefilter.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/fs_post_bloom_blur.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/fs_post_bloom_combine.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/fs_post_exposure_luminance.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/fs_post_fxaa.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/fs_post_motion_vectors.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/fs_post_taa_resolve.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/fs_deferred_lighting.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/fs_editor_gizmo.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/fs_editor_grid.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/fs_editor_selection_outline.sc
        VARYING_DEF ${KB_RENDERER_SHADER_VARYING_DEF}
        OUTPUT_DIR ${KB_RENDERER_GENERATED_SHADER_DIR}
        INCLUDE_DIRS
            ${KB_RENDERER_SHADER_INCLUDE_DIR}
            ${KB_RENDERER_BGFX_SHADER_INCLUDE_DIR}
        OUT_FILES_VAR KB_RENDERER_GENERATED_FRAGMENT_SHADERS
    )

    bgfx_compile_shaders(
        TYPE COMPUTE
        SHADERS
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/cs_instance_cull_clear.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/cs_instance_cull.sc
            ${KB_RENDERER_SHADER_INCLUDE_DIR}/cs_instance_cull_finalize.sc
        VARYING_DEF ${KB_RENDERER_SHADER_VARYING_DEF}
        OUTPUT_DIR ${KB_RENDERER_GENERATED_SHADER_DIR}
        INCLUDE_DIRS
            ${KB_RENDERER_SHADER_INCLUDE_DIR}
            ${KB_RENDERER_BGFX_SHADER_INCLUDE_DIR}
        OUT_FILES_VAR KB_RENDERER_GENERATED_COMPUTE_SHADERS
    )

    add_custom_target(kb_renderer_generated_shaders
        DEPENDS
            ${KB_RENDERER_GENERATED_VERTEX_SHADERS}
            ${KB_RENDERER_GENERATED_FRAGMENT_SHADERS}
            ${KB_RENDERER_GENERATED_COMPUTE_SHADERS}
    )

    add_custom_command(
        OUTPUT "${KB_RENDERER_SHADER_STAGE_STAMP}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            ${KB_RENDERER_SHADER_RUNTIME_DIR}
            ${KB_RENDERER_GENERATED_SHADER_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/prebuilt_shaders
            ${KB_RENDERER_SHADER_RUNTIME_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${KB_RENDERER_GENERATED_SHADER_DIR}
            ${KB_RENDERER_SHADER_RUNTIME_DIR}
        COMMAND ${CMAKE_COMMAND} -E touch "${KB_RENDERER_SHADER_STAGE_STAMP}"
        DEPENDS
            ${KB_RENDERER_PREBUILT_SHADER_FILES}
            ${KB_RENDERER_GENERATED_VERTEX_SHADERS}
            ${KB_RENDERER_GENERATED_FRAGMENT_SHADERS}
            ${KB_RENDERER_GENERATED_COMPUTE_SHADERS}
        COMMENT "Generate and stage bgfx runtime shaders"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT "${KB_RENDERER_SHADER_STAGE_STAMP}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            ${KB_RENDERER_SHADER_RUNTIME_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/prebuilt_shaders
            ${KB_RENDERER_SHADER_RUNTIME_DIR}
        COMMAND ${CMAKE_COMMAND} -E touch "${KB_RENDERER_SHADER_STAGE_STAMP}"
        DEPENDS ${KB_RENDERER_PREBUILT_SHADER_FILES}
        COMMENT "Stage prebuilt bgfx shaders"
        VERBATIM
    )
endif()

add_custom_target(kb_renderer_shaders ALL
    DEPENDS "${KB_RENDERER_SHADER_STAGE_STAMP}"
)

add_custom_command(
    OUTPUT "${KB_RENDERER_SHADER_MANIFEST_STAMP}"
    COMMAND ${CMAKE_COMMAND}
        "-DKB_SHADER_ROOT=${CMAKE_BINARY_DIR}/shaders"
        "-DKB_SHADER_PROFILES=$<JOIN:${KB_RENDERER_SHADER_PROFILES},$<SEMICOLON>>"
        "-DKB_SHADER_MANIFEST=$<JOIN:${KB_RENDERER_SHADER_MANIFEST},$<SEMICOLON>>"
        -P "${CMAKE_SOURCE_DIR}/CMake/ValidateBgfxShaderManifest.cmake"
    COMMAND ${CMAKE_COMMAND} -E touch "${KB_RENDERER_SHADER_MANIFEST_STAMP}"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/CMake/ValidateBgfxShaderManifest.cmake"
    COMMENT "Validate bgfx shader manifest variants"
    VERBATIM
)

add_custom_target(kb_renderer_shader_manifest ALL
    DEPENDS "${KB_RENDERER_SHADER_MANIFEST_STAMP}"
)
add_dependencies(kb_renderer_shader_manifest kb_renderer_shaders)

add_dependencies(kb_renderer kb_renderer_shader_manifest)

target_include_directories(kb_renderer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake/bgfx/3rdparty
        ${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake/bimg/3rdparty
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
        tests/RenderMaterialTypeSchemaTests.cpp
        tests/GraphShaderArtifactCookTests.cpp
        tests/MaterialProgramRegistryTests.cpp
        tests/SceneMeshPassProgramSelectionTests.cpp
        tests/GraphForwardGpuRenderTests.cpp
        tests/RendererCapabilityReportTests.cpp
        tests/RendererRuntimeSubmitTests.cpp
        tests/MeshPipelineTests.cpp
        tests/SceneDisplayCompositeTests.cpp
        tests/SceneExposureMeterTests.cpp
        tests/SceneDepthPolicyTests.cpp
        tests/RenderSceneSyncTests.cpp
        tests/SceneRenderTargetFormatTests.cpp
        tests/SceneRenderExtractorTests.cpp
        tests/ShaderManifestTests.cpp
    )

    target_link_libraries(kb_renderer_tests
        PRIVATE
            kb_renderer
    )

    target_include_directories(kb_renderer_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(NAME kb_renderer_tests COMMAND $<TARGET_FILE:kb_renderer_tests>)
    set_tests_properties(kb_renderer_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

    set(KB_GRAPH_SHADERC_TOOL "" CACHE FILEPATH "Path to bgfx shaderc used by the graph shader cook tests")
    set(_kb_graph_shaderc_tool "")
    if(TARGET bgfx::shaderc)
        set(_kb_graph_shaderc_tool "$<PATH:CMAKE_PATH,$<TARGET_FILE:bgfx::shaderc>>")
        add_dependencies(kb_renderer_tests bgfx::shaderc)
    elseif(KB_GRAPH_SHADERC_TOOL)
        cmake_path(CONVERT "${KB_GRAPH_SHADERC_TOOL}" TO_CMAKE_PATH_LIST _kb_graph_shaderc_tool NORMALIZE)
    endif()
    if(_kb_graph_shaderc_tool)
        target_compile_definitions(kb_renderer_tests
            PRIVATE
                KB_TEST_GRAPH_SHADERC_PATH="${_kb_graph_shaderc_tool}"
                KB_TEST_GRAPH_SHADER_VARYING_DEF="${CMAKE_CURRENT_SOURCE_DIR}/shaders/varying.def.sc"
                KB_TEST_GRAPH_SHADER_INCLUDE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
                KB_TEST_GRAPH_BGFX_SHADER_INCLUDE_DIR="${CMAKE_SOURCE_DIR}/third_party/bgfx.cmake/bgfx/src"
                KB_TEST_GRAPH_SHADER_CACHE_DIR="${CMAKE_BINARY_DIR}/graph_shader_cook_tests"
        )
    endif()

    kb_apply_quality_gates(kb_renderer_tests)
endif()
