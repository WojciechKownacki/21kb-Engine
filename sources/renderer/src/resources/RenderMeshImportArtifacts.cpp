#include "kb/render/resources/RenderMeshImportArtifacts.hpp"

#include "engine/assets/AssetImportService.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/scene/SkeletalMeshFbxImportPlanner.hpp"
#include "engine/scene/SkeletalMeshFbxImportPublisher.hpp"
#include "engine/scene/SkeletalMeshGltfImportPlanner.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMeshSourceImport.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <fstream>
#include <memory>
#include <set>
#include <system_error>
#include <utility>

namespace kb::render {
namespace {

template <typename T>
[[nodiscard]] std::optional<T> Fail(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return std::nullopt;
}

[[nodiscard]] bool FailBool(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return false;
}

[[nodiscard]] std::string Sanitize(std::string_view text, std::string_view fallback) {
    std::string output;
    output.reserve(text.size());
    for (char character : text) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (std::isalnum(value) || character == '-' || character == '_') output.push_back(character);
        else if (!output.empty() && output.back() != '_') output.push_back('_');
    }
    while (!output.empty() && output.back() == '_') output.pop_back();
    return output.empty() ? std::string{ fallback } : output;
}

[[nodiscard]] std::uint64_t Hash(std::string_view text) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char value : text) { hash ^= value; hash *= 1099511628211ULL; }
    return hash;
}

[[nodiscard]] std::string Hex8(std::uint64_t value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string output(8U, '0');
    for (std::size_t index = 0U; index < output.size(); ++index) {
        output[output.size() - index - 1U] = digits[value & 0xFU];
        value >>= 4U;
    }
    return output;
}

[[nodiscard]] std::filesystem::path TextureVirtualPath(
    const std::filesystem::path& destinationVirtualFolder,
    const std::filesystem::path& sourcePath,
    const kb::render::RenderMeshSourceTexture& texture) {
    const std::string source = Sanitize(sourcePath.stem().string(), "Mesh");
    const std::filesystem::path keyPath{ texture.key };
    const std::string textureName = Sanitize(keyPath.stem().string(), "Texture");
    const std::string extension = texture.extension.empty() ? ".png" : texture.extension;
    return destinationVirtualFolder / "Textures" /
        (source + "_" + textureName + "_" + Hex8(Hash(texture.key)) + extension);
}

[[nodiscard]] bool WriteBytes(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    if (!output.is_open()) return false;
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

[[nodiscard]] bool CopyFile(const std::filesystem::path& source, const std::filesystem::path& target) {
    std::error_code error;
    return std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, error) && !error;
}

void BindTexture(
    const std::string& key,
    std::uint64_t& assetId,
    std::string& unresolvedPath,
    const std::unordered_map<std::string, std::uint64_t>& textureIds) {
    if (key.empty()) return;
    const auto found = textureIds.find(key);
    if (found != textureIds.end()) {
        assetId = found->second;
        unresolvedPath.clear();
    } else {
        unresolvedPath = key;
    }
}

[[nodiscard]] kb::render::RenderMaterialAssetData MaterialAsset(
    const kb::render::RenderMeshEmbeddedMaterial& source,
    const std::unordered_map<std::string, std::uint64_t>& textureIds) {
    kb::render::RenderMaterialAssetData output{};
    output.desc = source.desc;
    BindTexture(source.albedoTexturePath, output.desc.albedoTextureAssetId, output.albedoTexturePath, textureIds);
    BindTexture(source.normalTexturePath, output.desc.normalTextureAssetId, output.normalTexturePath, textureIds);
    BindTexture(source.metallicRoughnessTexturePath, output.desc.metallicRoughnessTextureAssetId, output.metallicRoughnessTexturePath, textureIds);
    BindTexture(source.occlusionTexturePath, output.desc.occlusionTextureAssetId, output.occlusionTexturePath, textureIds);
    BindTexture(source.emissiveTexturePath, output.desc.emissiveTextureAssetId, output.emissiveTexturePath, textureIds);
    BindTexture(source.clearcoatTexturePath, output.desc.clearcoatTextureAssetId, output.clearcoatTexturePath, textureIds);
    BindTexture(source.clearcoatRoughnessTexturePath, output.desc.clearcoatRoughnessTextureAssetId, output.clearcoatRoughnessTexturePath, textureIds);
    BindTexture(source.sheenColorTexturePath, output.desc.sheenColorTextureAssetId, output.sheenColorTexturePath, textureIds);
    BindTexture(source.transmissionTexturePath, output.desc.transmissionTextureAssetId, output.transmissionTexturePath, textureIds);
    BindTexture(source.thicknessTexturePath, output.desc.thicknessTextureAssetId, output.thicknessTexturePath, textureIds);
    BindTexture(source.anisotropyTexturePath, output.desc.anisotropyTextureAssetId, output.anisotropyTexturePath, textureIds);
    BindTexture(source.decalTexturePath, output.desc.decalTextureAssetId, output.decalTexturePath, textureIds);
    BindTexture(source.layerMaskTexturePath, output.desc.layerMaskTextureAssetId, output.layerMaskTexturePath, textureIds);
    return output;
}

struct PublishedEntry {
    std::filesystem::path target;
    std::filesystem::path stage;
    std::filesystem::path backup;
    bool existed = false;
    bool installed = false;
};

[[nodiscard]] std::filesystem::path Sibling(const std::filesystem::path& target, std::string_view suffix) {
    static std::atomic<std::uint64_t> serial{ 0U };
    const auto value = serial.fetch_add(1U, std::memory_order_relaxed);
    return target.parent_path() / (target.stem().string() + ".mesh-import-" +
        std::to_string(value) + "." + std::string{ suffix } + target.extension().string());
}

void Remove(const std::filesystem::path& path) noexcept {
    std::error_code error;
    std::filesystem::remove(path, error);
}

[[nodiscard]] bool MoveReplace(const std::filesystem::path& source, const std::filesystem::path& target) noexcept {
#if defined(_WIN32)
    return MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code error;
    std::filesystem::rename(source, target, error);
    return !error;
#endif
}

void Rollback(std::vector<PublishedEntry>& entries) noexcept {
    for (auto iterator = entries.rbegin(); iterator != entries.rend(); ++iterator) {
        if (iterator->installed) Remove(iterator->target);
        if (iterator->existed) static_cast<void>(MoveReplace(iterator->backup, iterator->target));
        Remove(iterator->stage);
        Remove(iterator->backup);
    }
}

} // namespace

std::optional<RenderPreparedMeshImportArtifacts> RenderMeshImportArtifacts::Prepare(
    kb::assets::AssetManager& manager,
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& destinationVirtualFolder,
    const kb::assets::AssetImportOptions& options,
    std::string* error) {
    if (error != nullptr) error->clear();
    RenderPreparedMeshImportArtifacts result{};
    if (!options.mesh.importTextures && !options.mesh.importMaterials) return result;
    auto manifest = kb::render::RenderMeshSourceImport::Inspect(sourcePath, error);
    if (!manifest) return std::nullopt;
    if (options.mesh.importMaterials && !options.mesh.importTextures &&
        std::ranges::any_of(manifest->materials, [](const kb::render::RenderMeshEmbeddedMaterial& material) {
            return !material.albedoTexturePath.empty() || !material.normalTexturePath.empty() ||
                !material.metallicRoughnessTexturePath.empty() || !material.occlusionTexturePath.empty() ||
                !material.emissiveTexturePath.empty() || !material.clearcoatTexturePath.empty() ||
                !material.clearcoatRoughnessTexturePath.empty() || !material.sheenColorTexturePath.empty() ||
                !material.transmissionTexturePath.empty() || !material.thicknessTexturePath.empty() ||
                !material.anisotropyTexturePath.empty() || !material.decalTexturePath.empty() ||
                !material.layerMaskTexturePath.empty();
        })) {
        return Fail<RenderPreparedMeshImportArtifacts>(error,
            "Material import requires texture import because the source materials reference images.");
    }

    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderTextureAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()));

    std::unordered_map<std::string, std::uint64_t> textureIds;
    if (options.mesh.importTextures) {
        for (const kb::render::RenderMeshSourceTexture& texture : manifest->textures) {
            if (!texture.IsEmbedded()) {
                std::error_code fileError;
                if (!std::filesystem::is_regular_file(texture.sourcePath, fileError) || fileError) {
                    return Fail<RenderPreparedMeshImportArtifacts>(error,
                        "Referenced mesh texture does not exist: " + texture.sourcePath.string());
                }
            }
            const std::filesystem::path virtualPath = TextureVirtualPath(destinationVirtualFolder, sourcePath, texture);
            const std::uint64_t assetId = kb::assets::MakeAssetId(
                kb::assets::NormalizeAssetPath(virtualPath) + ":RenderTexture").value;
            textureIds.emplace(texture.key, assetId);
            const std::vector<std::byte> embedded = texture.embeddedBytes;
            const std::filesystem::path external = texture.sourcePath;
            result.artifacts.push_back({
                .virtualPath = virtualPath,
                .expectedAssetType = "RenderTexture",
                .write = [embedded, external](const std::filesystem::path& path) {
                    return embedded.empty() ? CopyFile(external, path) : WriteBytes(path, embedded);
                },
            });
        }
    }

    if (options.mesh.importMaterials) {
        const std::filesystem::path meshVirtualPath = destinationVirtualFolder /
            (sourcePath.stem().string() + ".21kb");
        for (const kb::render::RenderMeshEmbeddedMaterial& material : manifest->materials) {
            const std::filesystem::path virtualPath = kb::render::RenderMeshSourceImport::GeneratedMaterialVirtualPath(
                meshVirtualPath, sourcePath.filename().string(), material.name);
            const std::uint64_t assetId = kb::assets::MakeAssetId(
                kb::assets::NormalizeAssetPath(virtualPath) + ":RenderMaterial").value;
            result.materialAssetIds.emplace(material.name, assetId);
            kb::render::RenderMaterialAssetData data = MaterialAsset(material, textureIds);
            result.artifacts.push_back({
                .virtualPath = virtualPath,
                .expectedAssetType = "RenderMaterial",
                .write = [data = std::move(data)](const std::filesystem::path& path) {
                    return kb::render::RenderMaterialAssetWriter::Save(path, data);
                },
            });
        }
    }

    std::set<std::string> uniquePaths;
    for (const kb::scene::SkeletalMeshImportArtifact& artifact : result.artifacts) {
        const std::string normalized = kb::assets::NormalizeAssetPath(artifact.virtualPath);
        if (!uniquePaths.insert(normalized).second) return Fail<RenderPreparedMeshImportArtifacts>(error,
            "Mesh import generated two auxiliary assets at the same virtual path.");
        if (const kb::assets::AssetMetadata* existing = manager.Registry().FindByPath(artifact.virtualPath);
            existing != nullptr && existing->type != artifact.expectedAssetType) {
            return Fail<RenderPreparedMeshImportArtifacts>(error,
                "Mesh import auxiliary path is occupied by an incompatible asset: " + normalized);
        }
    }
    return result;
}

bool RenderMeshImportArtifacts::PublishStandalone(
    kb::assets::AssetManager& manager,
    std::span<const kb::scene::SkeletalMeshImportArtifact> artifacts,
    std::string* error) {
    if (error != nullptr) error->clear();
    if (artifacts.empty()) return true;
    std::vector<PublishedEntry> entries;
    entries.reserve(artifacts.size());
    for (const auto& artifact : artifacts) {
        const auto target = manager.Mounts().Resolve(artifact.virtualPath);
        if (!target || !artifact.write) { Rollback(entries); return FailBool(error, "Mesh import auxiliary destination is not mounted."); }
        std::error_code folderError;
        std::filesystem::create_directories(target->parent_path(), folderError);
        if (folderError) { Rollback(entries); return FailBool(error, "Mesh import auxiliary destination folder could not be created."); }
        PublishedEntry entry{ .target = *target, .stage = Sibling(*target, "stage"), .backup = Sibling(*target, "backup") };
        Remove(entry.stage); Remove(entry.backup);
        if (!artifact.write(entry.stage)) { Rollback(entries); return FailBool(error, "Mesh import auxiliary staging write failed."); }
        entries.push_back(std::move(entry));
    }
    for (PublishedEntry& entry : entries) {
        std::error_code existsError;
        entry.existed = std::filesystem::exists(entry.target, existsError) && !existsError;
        if (entry.existed && !MoveReplace(entry.target, entry.backup)) { Rollback(entries); return FailBool(error, "Mesh import could not preserve an existing auxiliary asset."); }
        if (!MoveReplace(entry.stage, entry.target)) { Rollback(entries); return FailBool(error, "Mesh import could not publish an auxiliary asset."); }
        entry.installed = true;
    }
    static_cast<void>(manager.DiscoverMountedAssets());
    for (std::size_t index = 0U; index < artifacts.size(); ++index) {
        const auto* metadata = manager.Registry().FindByPath(artifacts[index].virtualPath);
        if (metadata == nullptr || metadata->type != artifacts[index].expectedAssetType) {
            Rollback(entries);
            static_cast<void>(manager.DiscoverMountedAssets());
            return FailBool(error, "Mesh import auxiliary asset failed runtime-type validation.");
        }
    }
    for (PublishedEntry& entry : entries) Remove(entry.backup);
    return true;
}

std::uint64_t RenderMeshImportArtifacts::ResolveMaterial(std::string_view name, void* userData) noexcept {
    if (userData == nullptr) return 0U;
    const auto& ids = *static_cast<const std::unordered_map<std::string, std::uint64_t>*>(userData);
    const auto found = ids.find(std::string{ name });
    return found == ids.end() ? 0U : found->second;
}

namespace {

[[nodiscard]] std::string LowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

[[nodiscard]] bool CarriesSourceMaterials(const std::filesystem::path& path) {
    const std::string extension = LowerExtension(path);
    return extension == ".gltf" || extension == ".glb" || extension == ".fbx";
}

// The textures and materials a model's import published, as the registry now knows them.
[[nodiscard]] std::vector<kb::assets::AssetId> PublishedArtifactIds(
    const kb::assets::AssetManager& manager,
    std::span<const kb::scene::SkeletalMeshImportArtifact> artifacts) {
    std::vector<kb::assets::AssetId> ids;
    for (const kb::scene::SkeletalMeshImportArtifact& artifact : artifacts) {
        if (const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(artifact.virtualPath)) {
            ids.push_back(metadata->id);
        }
    }
    return ids;
}

} // namespace

kb::assets::AssetImportResult RenderMeshImportArtifacts::ImportStaticFiles(
    kb::assets::AssetManager& manager,
    std::span<const std::filesystem::path> sourceFiles,
    const std::filesystem::path& destinationVirtualFolder,
    const kb::assets::AssetImportOptions& options) {
    kb::assets::AssetImportResult result{};
    result.items.reserve(sourceFiles.size());
    for (const std::filesystem::path& sourcePath : sourceFiles) {
        const bool meshSource = CarriesSourceMaterials(sourcePath);
        std::string error;
        std::optional<RenderPreparedMeshImportArtifacts> prepared;
        if (meshSource && (options.mesh.importTextures || options.mesh.importMaterials)) {
            prepared = Prepare(manager, sourcePath, destinationVirtualFolder, options, &error);
            if (!prepared) {
                result.items.push_back({
                    .sourcePath = sourcePath,
                    .category = kb::assets::AssetImportCategory::Model,
                    .status = kb::assets::AssetImportItemStatus::Failed,
                    .error = error.empty() ? "Static mesh auxiliary asset planning failed." : std::move(error),
                });
                continue;
            }
        }
        const std::array<std::filesystem::path, 1U> oneSource{ sourcePath };
        kb::assets::AssetImportResult imported = kb::assets::AssetImportService::ImportFiles(
            manager, oneSource, destinationVirtualFolder, options);
        if (imported.items.empty()) continue;
        kb::assets::AssetImportItemResult item = std::move(imported.items.front());
        if (item.Succeeded() && prepared.has_value() && !PublishStandalone(manager, prepared->artifacts, &error)) {
            if (item.status == kb::assets::AssetImportItemStatus::Created) {
                std::error_code removeError;
                std::filesystem::remove(item.assetPhysicalPath, removeError);
                removeError.clear();
                if (!item.metaPhysicalPath.empty()) std::filesystem::remove(item.metaPhysicalPath, removeError);
                static_cast<void>(manager.DiscoverMountedAssets());
            }
            item.status = kb::assets::AssetImportItemStatus::Failed;
            item.error = error.empty() ? "Static mesh auxiliary asset publication failed." : std::move(error);
        }
        if (item.Succeeded() && prepared.has_value()) {
            item.producedAssets = PublishedArtifactIds(manager, prepared->artifacts);
        }
        result.items.push_back(std::move(item));
    }
    return result;
}

namespace {

[[nodiscard]] bool IsGltfSkeletalSource(const std::filesystem::path& path) {
    const std::string extension = LowerExtension(path);
    return extension == ".gltf" || extension == ".glb";
}

// One source through a planner/publisher pair: glTF and FBX differ only in their options and in
// what their planners read.
template <typename Planner, typename Publisher, typename Options>
[[nodiscard]] kb::assets::AssetImportItemResult ImportSkeletalSource(
    kb::assets::AssetManager& manager,
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& destinationVirtualFolder,
    const kb::assets::AssetImportOptions& options,
    Options importOptions,
    std::string_view format) {
    kb::assets::AssetImportItemResult item{};
    item.sourcePath = sourcePath;
    item.category = kb::assets::AssetImportCategory::Model;
    const auto fail = [&](std::string error, std::string_view fallback) {
        item.status = kb::assets::AssetImportItemStatus::Failed;
        item.error = error.empty() ? "Skeletal " + std::string{ format } + " " + std::string{ fallback } : std::move(error);
        return item;
    };
    std::string error;
    auto prepared = RenderMeshImportArtifacts::Prepare(manager, sourcePath, destinationVirtualFolder, options, &error);
    if (!prepared) return fail(std::move(error), "auxiliary asset planning failed.");
    importOptions.combineMeshes = options.mesh.combineMeshes;
    if (options.mesh.importMaterials) {
        importOptions.materialResolver = &RenderMeshImportArtifacts::ResolveMaterial;
        importOptions.materialResolverUserData = &prepared->materialAssetIds;
    }
    const auto plan = Planner::Plan(manager, sourcePath, destinationVirtualFolder, importOptions, &error);
    if (!plan) return fail(std::move(error), "import planning failed.");
    const bool meshAlreadyExisted = manager.Registry().FindByPath(plan->meshVirtualPath) != nullptr;
    const auto published = Publisher::PublishWithArtifacts(manager, *plan, prepared->artifacts, &error);
    if (!published) return fail(std::move(error), "asset publication failed.");
    // A source of animations only names its skeleton as the item; the clips follow it.
    const bool animationsOnly = !published->meshAssetId.IsValid();
    const kb::assets::AssetMetadata* metadata =
        manager.Registry().Find(animationsOnly ? published->skeletonAssetId : published->meshAssetId);
    if (metadata == nullptr) return fail({}, "import completed without a registered Skeletal Mesh asset.");
    item.id = metadata->id;
    item.assetPhysicalPath = metadata->physicalPath;
    item.virtualPath = metadata->virtualPath;
    item.assetHash = metadata->contentHash;
    item.status = (animationsOnly ? plan->reusesSkeleton : meshAlreadyExisted)
        ? kb::assets::AssetImportItemStatus::Reused : kb::assets::AssetImportItemStatus::Created;
    if (!animationsOnly) item.producedAssets.push_back(published->skeletonAssetId);
    item.producedAssets.insert(item.producedAssets.end(),
        published->animationClipAssetIds.begin(), published->animationClipAssetIds.end());
    for (const kb::assets::AssetId artifact : PublishedArtifactIds(manager, prepared->artifacts)) {
        item.producedAssets.push_back(artifact);
    }
    return item;
}

} // namespace

kb::assets::AssetImportResult RenderMeshImportArtifacts::ImportSkeletalFiles(
    kb::assets::AssetManager& manager,
    std::span<const std::filesystem::path> sourceFiles,
    const std::filesystem::path& destinationVirtualFolder,
    const kb::assets::AssetImportOptions& options) {
    kb::assets::AssetImportResult result{};
    result.items.reserve(sourceFiles.size());
    for (const std::filesystem::path& sourcePath : sourceFiles) {
        if (IsGltfSkeletalSource(sourcePath)) {
            result.items.push_back(ImportSkeletalSource<kb::scene::SkeletalMeshGltfImportPlanner,
                kb::scene::SkeletalMeshGltfImportPublisher>(manager, sourcePath, destinationVirtualFolder, options,
                kb::scene::SkeletalMeshGltfImportOptions{}, "glTF"));
        } else if (LowerExtension(sourcePath) == ".fbx") {
            kb::scene::SkeletalMeshFbxImportOptions fbxOptions{};
            fbxOptions.importMaterialSlots = options.mesh.importMaterialSlots;
            result.items.push_back(ImportSkeletalSource<kb::scene::SkeletalMeshFbxImportPlanner,
                kb::scene::SkeletalMeshFbxImportPublisher>(manager, sourcePath, destinationVirtualFolder, options,
                fbxOptions, "FBX"));
        } else {
            result.items.push_back({
                .sourcePath = sourcePath,
                .category = kb::assets::AssetImportCategory::Model,
                .status = kb::assets::AssetImportItemStatus::Unsupported,
                .error = "Skeletal Mesh import supports FBX, glTF (.gltf) and GLB (.glb) sources only.",
            });
        }
    }
    return result;
}

} // namespace kb::render
