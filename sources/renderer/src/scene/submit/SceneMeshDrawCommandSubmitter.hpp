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
#include <vector>

namespace kb::render {

class SceneMeshInstanceBufferPool {
public:
    SceneMeshInstanceBufferPool() = default;
    ~SceneMeshInstanceBufferPool();
    SceneMeshInstanceBufferPool(const SceneMeshInstanceBufferPool&) = delete;
    SceneMeshInstanceBufferPool& operator=(const SceneMeshInstanceBufferPool&) = delete;

    [[nodiscard]] bgfx::DynamicVertexBufferHandle Upload(
        std::span<const SceneRenderMeshInstance> instances,
        const RenderMaterialResource* material,
        bool encodeShadowReceiver);
    void EndFrame() noexcept { usedSlots_ = 0U; }
    void Shutdown() noexcept;

private:
    struct Slot {
        bgfx::DynamicVertexBufferHandle buffer = BGFX_INVALID_HANDLE;
        std::uint32_t capacity = 0U;
    };
    std::vector<Slot> slots_;
    std::size_t usedSlots_ = 0U;
};

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
    std::array<float, 16> motionVectorPreviousViewProjection{};
    const RenderSkinningPaletteAllocator* skinningPaletteAllocator = nullptr;
    const SceneMeshPassResources& passResources;
    SceneMeshInstanceBufferPool* instanceBufferPool = nullptr;
    SceneRenderDiagnostics* diagnostics = nullptr;
    SceneRenderSubmitStats& stats;
};

class SceneMeshDrawCommandSubmitter {
public:
    SceneMeshDrawCommandSubmitter() = delete;

    static void Submit(const SceneMeshDrawCommandSubmitDesc& desc);
};

} // namespace kb::render
