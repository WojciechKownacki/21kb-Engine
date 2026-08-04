#pragma once

#include "kb/render/resources/RenderResources.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kb::scene {
struct SkeletalMeshAsset;
}

namespace kb::render {

// Owns the CPU data referenced by desc until it is registered with bgfx.
// paletteBoneIds is the stable, sorted address space used by every LOD.
struct SkeletalMeshRenderResourceData {
    std::vector<RenderStaticMeshVertexSkinned> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<RenderMeshSectionDesc> sections;
    std::vector<RenderMeshLodDesc> lods;
    std::vector<RenderMaterialSlotDesc> materialSlots;
    std::vector<std::uint64_t> paletteBoneIds;
    RenderBoundsSphere bounds{};
    bool dynamicVertexBuffer = false;
    RenderMeshDesc desc{};

    RenderMeshDesc& RefreshDesc() noexcept;
};

class SkeletalMeshRenderResourceBuilder final {
public:
    [[nodiscard]] static std::optional<SkeletalMeshRenderResourceData> Build(
        const kb::scene::SkeletalMeshAsset& asset,
        std::span<const std::string> morphTargetNames = {},
        std::span<const float> morphWeights = {});
};

} // namespace kb::render
