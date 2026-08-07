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

namespace kb::editor {

struct EditorPreparedMeshImportArtifacts {
    std::vector<kb::scene::SkeletalMeshImportArtifact> artifacts;
    std::unordered_map<std::string, std::uint64_t> materialAssetIds;
};

class EditorMeshImportArtifacts final {
public:
    EditorMeshImportArtifacts() = delete;

    [[nodiscard]] static std::optional<EditorPreparedMeshImportArtifacts> Prepare(
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
};

} // namespace kb::editor
