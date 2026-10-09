#pragma once

#include "engine/navigation/NavGeometryCollector.hpp"

namespace kb::assets {
class AssetManager;
}

namespace kb::render {

// Geometry for navigation mesh baking with the renderer's mesh loaders: the finest level of
// detail of every RenderMesh asset (imported static meshes), terrain heightfields and collision
// meshes through the engine's own readers. `assets` must have the RenderMesh loader registered.
class RenderNavGeometrySource final : public kb::navigation::AssetNavGeometrySource {
public:
    explicit RenderNavGeometrySource(kb::assets::AssetManager& assets) noexcept;

    [[nodiscard]] std::shared_ptr<const kb::navigation::NavSourceMesh> RenderMesh(std::uint64_t assetId) override;
};

} // namespace kb::render
