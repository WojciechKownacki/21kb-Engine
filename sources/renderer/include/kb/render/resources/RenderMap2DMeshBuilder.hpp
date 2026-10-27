#pragma once

#include "engine/assets/Map2DAsset.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace kb::render {

// The render mesh of a 2D tile map: one textured quad per sprite, the stack of a tile lifted a
// little per layer so alpha-masked sprites cover what lies under them. Material slot 0 is the
// background, slot 1 + i draws sheet i with its material.
class RenderMap2DMeshBuilder final {
public:
    [[nodiscard]] static std::optional<RenderMeshAssetData> BuildOccluders(const kb::assets::Map2DAsset& map, float drawOffset = 0, double seconds = 0);
    RenderMap2DMeshBuilder() = delete;
    static constexpr float kLayerLift = 0.005F;
    // Greater than a cell's complete ground/border/item stack.
    static constexpr float kSortingOrderLift = 0.5F;
    static constexpr std::int32_t kMaximumSortingOrder = 1000;

    [[nodiscard]] static std::optional<RenderMeshAssetData> Build(const kb::assets::Map2DAsset& map, float drawOffset = 0.0F, double seconds = 0.0);
    // The asset id of a material given by its /Game path (0 for an empty path).
    [[nodiscard]] static std::uint64_t MaterialAssetId(std::string_view virtualPath);
};

} // namespace kb::render
