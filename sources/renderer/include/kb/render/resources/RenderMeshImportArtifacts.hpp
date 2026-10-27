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

    // Reimport: replaces an imported static model with a changed source, in place (same id and path, so every reference keeps working), and
    // republishes the textures and materials the options ask for. What the last import wrote is kept in `<asset>.importrecord`: a texture or
    // material edited since then is a local override, and options.conflictPolicy says what happens to it (report and change nothing, keep
    // it, or overwrite it). Whatever is overwritten is backed up first (AssetImportBackups, item.backupId). options.dryRun writes nothing and
    // only fills item.changes. The static and skeletal imports above apply the same rule when they publish over assets that exist.
    [[nodiscard]] static kb::assets::AssetImportItemResult ReimportStatic(
        kb::assets::AssetManager& manager,
        kb::assets::AssetId id,
        const std::filesystem::path& sourcePath,
        const kb::assets::AssetImportOptions& options);

    // Dispatch by the existing asset type and preserve its path and identity,
    // even when the replacement source has a different filename.
    [[nodiscard]] static kb::assets::AssetImportItemResult Reimport(
        kb::assets::AssetManager& manager,
        kb::assets::AssetId id,
        const std::filesystem::path& sourcePath,
        const kb::assets::AssetImportOptions& options);

    // Skeletal import of FBX, glTF and GLB sources: a Skeleton (reused when the project already has
    // the same one), the Skeletal Mesh and one Animation Clip per source animation, with the
    // source's textures and materials when the options ask for them. Each item's id is the
    // Skeletal Mesh; producedAssets lists the skeleton, the clips and the textures and materials.
    // The editor's Import and kb_cli import both come here.
    [[nodiscard]] static kb::assets::AssetImportResult ImportSkeletalFiles(
        kb::assets::AssetManager& manager,
        std::span<const std::filesystem::path> sourceFiles,
        const std::filesystem::path& destinationVirtualFolder,
        const kb::assets::AssetImportOptions& options);
};

} // namespace kb::render
