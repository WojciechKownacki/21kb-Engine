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
};

} // namespace kb::scene
