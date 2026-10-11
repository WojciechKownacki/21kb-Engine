#pragma once

#include "engine/assets/AssetImportTypes.hpp"
#include "engine/scene/SkeletalMeshGltfImportPublisher.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kb::assets { class AssetManager; }

namespace kb::render {

struct RenderPreparedMeshImportArtifacts {
    std::vector<kb::scene::SkeletalMeshImportArtifact> artifacts;
    std::unordered_map<std::string, std::uint64_t> materialAssetIds;
};

// The textures and materials a model source brings along (Import textures / Import materials),
// planned before the model's own import and published beside it, for the editor's import and
// kb_cli import alike.
class RenderMeshImportArtifacts final {
public:
    RenderMeshImportArtifacts() = delete;

    [[nodiscard]] static std::optional<RenderPreparedMeshImportArtifacts> Prepare(
        kb::assets::AssetManager& manager,
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& destinationVirtualFolder,
        const kb::assets::AssetImportOptions& options,
        std::string* error = nullptr);

    [[nodiscard]] static bool PublishStandalone(
        kb::assets::AssetManager& manager,
        std::span<const kb::scene::SkeletalMeshImportArtifact> artifacts,
        std::string* error = nullptr);

    [[nodiscard]] static std::uint64_t ResolveMaterial(std::string_view name, void* userData) noexcept;

    // Static model (and any other source) import: the source's textures and materials when the
    // options ask for them, then AssetImportService; a failed publication removes the new import.
    [[nodiscard]] static kb::assets::AssetImportResult ImportStaticFiles(
        kb::assets::AssetManager& manager,
        std::span<const std::filesystem::path> sourceFiles,
        const std::filesystem::path& destinationVirtualFolder,
        const kb::assets::AssetImportOptions& options);
};

} // namespace kb::render
