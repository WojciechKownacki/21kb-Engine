#pragma once

#include "engine/assets/IAssetLoader.hpp"
#include "engine/math/EngineMath.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kb::assets {

struct TerrainAsset;

inline constexpr std::string_view kCollisionMeshAssetType = "CollisionMesh";
inline constexpr std::string_view kCollisionMeshAssetExtension = ".kbcollision";

// Immutable, local-space triangle geometry. Independent of graphics and physics SDKs.
struct CollisionMeshAsset {
    std::vector<kb::math::Vec3> positions;
    std::vector<std::uint32_t> indices;
};

[[nodiscard]] bool ValidateCollisionMesh(const CollisionMeshAsset& mesh, std::string& error);
[[nodiscard]] std::optional<CollisionMeshAsset> ReadCollisionMesh(
    std::span<const std::uint8_t> bytes, std::string& error);
[[nodiscard]] bool WriteCollisionMesh(
    const std::filesystem::path& path, const CollisionMeshAsset& mesh, std::string& error);
[[nodiscard]] std::optional<CollisionMeshAsset> BuildTerrainCollisionMesh(
    const TerrainAsset& terrain, std::string& error);

class CollisionMeshAssetLoader final : public IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override;
    [[nodiscard]] std::type_index PayloadType() const noexcept override;
    [[nodiscard]] std::vector<std::string> Extensions() const override;
    [[nodiscard]] AssetLoadResult Load(const AssetLoadRequest& request) override;
};

} // namespace kb::assets
