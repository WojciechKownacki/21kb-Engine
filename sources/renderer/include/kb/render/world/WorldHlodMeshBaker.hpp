#pragma once

#include "engine/world/WorldCellBuilder.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace kb::assets {
class AssetManager;
}

namespace kb::render {

struct RenderMeshAssetData;

// Builds a cell's HLOD proxy: every placed mesh of the cell is transformed into
// the cell's local space, merged per final material, welded, simplified with
// meshoptimizer towards the world's triangle ratio and written as a Wavefront
// OBJ document with one material group per slot. At most eight material slots
// are produced (the renderer's per-slot override limit); the triangles of any
// further materials join the eighth slot.
class WorldHlodMeshBaker final : public kb::world::IWorldHlodBaker {
public:
    // `assets` must have the RenderMesh loader registered and the meshes discovered.
    explicit WorldHlodMeshBaker(kb::assets::AssetManager& assets) noexcept;

    [[nodiscard]] kb::world::WorldHlodResult Bake(const kb::world::WorldHlodRequest& request) override;

private:
    kb::assets::AssetManager& assets_;
    std::unordered_map<std::uint64_t, std::shared_ptr<const RenderMeshAssetData>> meshes_;
};

} // namespace kb::render
