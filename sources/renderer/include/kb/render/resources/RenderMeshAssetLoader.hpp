#pragma once

#include "engine/assets/IAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

namespace kb::render {

// A packaged baked mesh streams its finer levels of detail when they hold at least this much
// geometry; a smaller mesh loads whole.
inline constexpr std::uint64_t kStreamedMeshMinimumBytes = 64U * 1024U;

// The mesh from baked level `firstLod` down, built from a streamed mesh's layout and the encoded
// chunks of levels firstLod..end, in order: decoded, checked against the fragments the pack
// declares, and finalized exactly as the loader finalizes a mesh. `out.streaming` describes the
// new data, so it can be grown again.
[[nodiscard]] bool AssembleStreamedMeshLevels(
    const RenderMeshStreamingLayout& layout,
    std::uint32_t firstLod,
    std::span<const std::vector<std::uint8_t>> chunks,
    RenderMeshAssetData& out);

class RenderMeshAssetLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] std::vector<std::string> BakedAssetTypes() const override;
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;
    [[nodiscard]] std::vector<kb::assets::AssetId> DiscoverDependencies(
        const kb::assets::AssetMetadata& metadata,
        const kb::assets::AssetRegistry& registry) const override;
};

} // namespace kb::render
