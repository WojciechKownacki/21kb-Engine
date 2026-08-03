#pragma once

#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/resources/RenderSkinningPaletteAllocator.hpp"
#include "kb/render/scene/MeshPipeline.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "scene/lighting/SceneLightingPacker.hpp"
#include "scene/submit/SceneMeshPassResources.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <span>

namespace kb::render {

struct SceneMeshDrawCommandSubmitDesc {
    bgfx::ViewId viewId = 0;
    std::span<const MeshDrawCommand> commands{};
    MeshPassType pass = MeshPassType::BaseOpaque;
    const RenderResourceRegistry& resources;
    const SceneRenderResourceMap& resourceMap;
    const PackedSceneLighting& lighting;
    const std::array<float, 4>& cameraPosition;
    const std::array<float, 4>& frameTime;
    const std::array<float, 4>& dynamicParameter;
    const SceneRenderShadowMapBinding* shadowMap = nullptr;
    // MAT-80/#18b: opaque scene depth texture bound to depth-sampling graph materials in the transparent pass.
    bgfx::TextureHandle sceneDepthTexture = BGFX_INVALID_HANDLE;
    // MAT-31: opaque scene color snapshot bound to color-sampling graph materials in the transparent pass.
    bgfx::TextureHandle sceneColorTexture = BGFX_INVALID_HANDLE;
    const RenderSkinningPaletteAllocator* skinningPaletteAllocator = nullptr;
    const SceneMeshPassResources& passResources;
    SceneRenderDiagnostics* diagnostics = nullptr;
    SceneRenderSubmitStats& stats;
};

class SceneMeshDrawCommandSubmitter {
public:
    SceneMeshDrawCommandSubmitter() = delete;

    static void Submit(const SceneMeshDrawCommandSubmitDesc& desc);
};

} // namespace kb::render
