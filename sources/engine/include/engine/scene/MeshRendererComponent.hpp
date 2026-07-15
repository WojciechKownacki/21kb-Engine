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
};

} // namespace kb::scene
