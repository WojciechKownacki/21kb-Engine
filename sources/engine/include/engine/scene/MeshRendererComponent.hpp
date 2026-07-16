#pragma once

#include <array>
#include <cstdint>

namespace kb::scene {

inline constexpr std::uint32_t kMaxMeshRendererMaterialSlotOverrides = 8U;

struct MeshRendererComponent {
    std::uint64_t meshAssetId = 0;
    std::uint64_t materialAssetId = 0;
    std::array<std::uint64_t, kMaxMeshRendererMaterialSlotOverrides> materialSlotAssetIds{};
    std::uint32_t materialSlotOverrideCount = 0;
    bool castsShadow = true;
    bool receivesShadow = true;
    // LIB-136: which render layer this mesh belongs to, checked against a
    // camera's CameraComponent::cullingMask (mirrors ColliderComponent::layer's
    // single-bit-by-default convention, but a deliberately SEPARATE namespace -
    // rendering and physics layers never share bits). Bit 0 set by default
    // ("Default" layer) so every mesh authored before LIB-136 keeps rendering
    // under every camera's default all-bits cullingMask exactly as before.
    std::uint32_t layer = 1U;
    // LIB-139: a live handle from kb::scene::SceneMaterialInstances::Create,
    // or 0 for "no runtime instance assigned". Deliberately NOT persisted
    // (no binary codec / prefab text format wiring, unlike every field
    // above) and NOT in the generic script reflection table (same LIB-082
    // reasoning as meshAssetId/materialAssetId - see
    // ScriptMeshRendererApi.cpp's MeshRenderer.SetMaterialInstance) - a
    // runtime instance handle is only ever meaningful for the lifetime of
    // the live Scene that created it, so saving/loading it into a scene
    // document or prefab would just be a dangling reference on reload. When
    // set (nonzero), this WINS over materialAssetId/materialSlotAssetIds -
    // see EcsRenderSceneSynchronizer::SyncMesh's resolution order.
    std::uint64_t materialInstanceHandle = 0U;
};

} // namespace kb::scene
