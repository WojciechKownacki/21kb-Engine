#include "scene/EditorMeshImportArtifacts.hpp"

#include "engine/assets/AssetManager.hpp"
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

namespace kb::editor {
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

std::optional<EditorPreparedMeshImportArtifacts> EditorMeshImportArtifacts::Prepare(
    kb::assets::AssetManager& manager,
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& destinationVirtualFolder,
    const kb::assets::AssetImportOptions& options,
    std::string* error) {
    if (error != nullptr) error->clear();
    EditorPreparedMeshImportArtifacts result{};
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
        return Fail<EditorPreparedMeshImportArtifacts>(error,
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
                    return Fail<EditorPreparedMeshImportArtifacts>(error,
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
        if (!uniquePaths.insert(normalized).second) return Fail<EditorPreparedMeshImportArtifacts>(error,
            "Mesh import generated two auxiliary assets at the same virtual path.");
        if (const kb::assets::AssetMetadata* existing = manager.Registry().FindByPath(artifact.virtualPath);
            existing != nullptr && existing->type != artifact.expectedAssetType) {
            return Fail<EditorPreparedMeshImportArtifacts>(error,
                "Mesh import auxiliary path is occupied by an incompatible asset: " + normalized);
        }
    }
    return result;
}

bool EditorMeshImportArtifacts::PublishStandalone(
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

std::uint64_t EditorMeshImportArtifacts::ResolveMaterial(std::string_view name, void* userData) noexcept {
    if (userData == nullptr) return 0U;
    const auto& ids = *static_cast<const std::unordered_map<std::string, std::uint64_t>*>(userData);
    const auto found = ids.find(std::string{ name });
    return found == ids.end() ? 0U : found->second;
}

} // namespace kb::editor
