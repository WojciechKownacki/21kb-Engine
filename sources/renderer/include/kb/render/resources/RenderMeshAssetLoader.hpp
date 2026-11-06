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

// Whether a mesh read from its source gets the levels of detail the cooker would bake.
enum class RenderMeshLodBuild : std::uint8_t {
    // The geometry as authored: for the cooker, which bakes its own chain, and for tools that read triangles.
    None,
    // The cooker's chain, built when the mesh loads, so a project that runs uncooked draws coarser levels too.
    Runtime,
};

class RenderMeshAssetLoader final : public kb::assets::IAssetLoader {
public:
    explicit RenderMeshAssetLoader(RenderMeshLodBuild lodBuild = RenderMeshLodBuild::None) noexcept : lodBuild_(lodBuild) {}
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] std::vector<std::string> BakedAssetTypes() const override;
    [[nodiscard]] bool AllowsConcurrentLoad() const noexcept override { return true; }
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest& request) override;
    [[nodiscard]] std::vector<kb::assets::AssetId> DiscoverDependencies(
        const kb::assets::AssetMetadata& metadata,
        const kb::assets::AssetRegistry& registry) const override;

private:
    RenderMeshLodBuild lodBuild_ = RenderMeshLodBuild::None;
};

} // namespace kb::render
