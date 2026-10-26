#include "kb/render/resources/RenderMeshImportArtifacts.hpp"

#include "engine/assets/AssetImportRecord.hpp"
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

// What an import is about to write, compared with what is there: refuse, keep or overwrite the files edited since the last import, and keep
// the files a reimport replaces so that it can be undone.
struct GuardTarget {
    std::string virtualPath;
    std::string assetType;
    std::uint64_t newHash = 0U; // 0 when the content is not known before it is written
};

[[nodiscard]] std::uint64_t StagedHash(const kb::scene::SkeletalMeshImportArtifact& artifact) {
    const std::filesystem::path stage = std::filesystem::temp_directory_path() / ("kb_import_stage_" + Hex8(Hash(kb::assets::NormalizeAssetPath(artifact.virtualPath))) +
        "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tmp");
    std::uint64_t hash = 0U;
    if (artifact.write && artifact.write(stage)) hash = kb::assets::HashAssetFile(stage);
    Remove(stage);
    return hash;
}

[[nodiscard]] std::string ConflictSummary(const std::vector<kb::assets::AssetImportChange>& conflicts) {
    std::string text;
    for (std::size_t index = 0U; index < conflicts.size() && index < 4U; ++index) {
        text += (index == 0U ? "" : ", ") + conflicts[index].virtualPath.generic_string();
    }
    if (conflicts.size() > 4U) text += " and " + std::to_string(conflicts.size() - 4U) + " more";
    return text;
}

enum class GuardOutcome : std::uint8_t { Proceed, Stop };

// `required` are targets that cannot be left out (the model itself); `alwaysBackup` are files the import replaces whatever their state.
// On Stop the item says why (a dry run, a refusal, or everything kept).
[[nodiscard]] GuardOutcome Guard(
    kb::assets::AssetManager& manager,
    const kb::assets::AssetImportOptions& options,
    const kb::assets::AssetImportRecord* recorded,
    std::vector<kb::scene::SkeletalMeshImportArtifact>& artifacts,
    const std::vector<GuardTarget>& required,
    const std::vector<std::filesystem::path>& alwaysBackup,
    kb::assets::AssetImportItemResult& item) {
    std::vector<kb::assets::AssetImportPlanEntry> targets;
    for (const GuardTarget& target : required) targets.push_back({ target.virtualPath, target.assetType, target.newHash });
    for (const kb::scene::SkeletalMeshImportArtifact& artifact : artifacts) {
        targets.push_back({ kb::assets::NormalizeAssetPath(artifact.virtualPath), artifact.expectedAssetType, StagedHash(artifact) });
    }
    item.changes = kb::assets::PlanImportChanges(manager, recorded, targets);
    std::vector<kb::assets::AssetImportChange> overridden;
    for (const kb::assets::AssetImportChange& change : item.changes) {
        if (change.kind == kb::assets::AssetImportChangeKind::LocalOverride) overridden.push_back(change);
    }
    if (options.dryRun) {
        item.conflicts = overridden;
        return GuardOutcome::Stop;
    }
    if (!overridden.empty()) {
        item.conflicts = overridden;
        if (options.conflictPolicy == kb::assets::AssetImportConflictPolicy::Report) {
            item.status = kb::assets::AssetImportItemStatus::Failed;
            item.error = std::to_string(overridden.size()) + " asset(s) were edited since the last import and would be overwritten: " + ConflictSummary(overridden) +
                ". Nothing was changed; reimport with keep-local to leave them or overwrite to replace them (the old files are backed up).";
            return GuardOutcome::Stop;
        }
        if (options.conflictPolicy == kb::assets::AssetImportConflictPolicy::KeepLocal) {
            const bool requiredEdited = std::ranges::any_of(overridden, [&](const kb::assets::AssetImportChange& change) {
                return std::ranges::any_of(required, [&](const GuardTarget& target) { return target.virtualPath == change.virtualPath.generic_string(); });
            });
            if (requiredEdited) {
                // The model and its skeleton are published together: kept as they are means nothing is updated.
                item.status = kb::assets::AssetImportItemStatus::Reused;
                return GuardOutcome::Stop;
            }
            std::erase_if(artifacts, [&](const kb::scene::SkeletalMeshImportArtifact& artifact) {
                const std::string path = kb::assets::NormalizeAssetPath(artifact.virtualPath);
                return std::ranges::any_of(overridden, [&](const kb::assets::AssetImportChange& change) { return change.virtualPath.generic_string() == path; });
            });
        }
    }
    // The files that will be replaced are kept first.
    std::vector<kb::assets::AssetImportBackupFile> files;
    std::set<std::string> listed;
    const auto add = [&](const std::filesystem::path& physical, const std::string& virtualPath) {
        if (listed.insert(physical.generic_string()).second) files.push_back({ virtualPath, physical });
    };
    for (const kb::assets::AssetImportChange& change : item.changes) {
        const bool replaced = change.kind == kb::assets::AssetImportChangeKind::Changed ||
            (change.kind == kb::assets::AssetImportChangeKind::LocalOverride && options.conflictPolicy == kb::assets::AssetImportConflictPolicy::Overwrite);
        const bool published = std::ranges::any_of(artifacts, [&](const kb::scene::SkeletalMeshImportArtifact& artifact) {
            return kb::assets::NormalizeAssetPath(artifact.virtualPath) == change.virtualPath.generic_string();
        }) || std::ranges::any_of(required, [&](const GuardTarget& target) { return target.virtualPath == change.virtualPath.generic_string(); });
        if (!replaced || !published) continue;
        if (const auto physical = manager.Mounts().Resolve(change.virtualPath)) add(*physical, change.virtualPath.generic_string());
    }
    // The model and its record are replaced whenever anything changes; when nothing does, nothing is kept.
    const bool anyChange = std::ranges::any_of(item.changes, [](const kb::assets::AssetImportChange& change) {
        return change.kind == kb::assets::AssetImportChangeKind::Added || change.kind == kb::assets::AssetImportChangeKind::Changed ||
            change.kind == kb::assets::AssetImportChangeKind::LocalOverride;
    });
    if (anyChange) {
        for (const std::filesystem::path& path : alwaysBackup) add(path, {});
    }
    if (!files.empty()) {
        std::string error;
        const std::optional<std::string> id = kb::assets::AssetImportBackups::Create(manager, files, &error);
        if (!id.has_value()) {
            item.status = kb::assets::AssetImportItemStatus::Failed;
            item.error = "The files the reimport replaces could not be backed up (" + error + "); nothing was changed.";
            return GuardOutcome::Stop;
        }
        item.backupId = *id;
    }
    return GuardOutcome::Proceed;
}

// Writes the record of what this import left: the entries of the last record for what it did not touch, the hash now for what it wrote.
void WriteRecord(
    kb::assets::AssetManager& manager,
    const std::filesystem::path& mainPhysicalPath,
    const std::filesystem::path& sourcePath,
    const kb::assets::AssetImportOptions& options,
    const std::optional<kb::assets::AssetImportRecord>& previous,
    std::span<const std::pair<std::filesystem::path, std::string>> written) {
    kb::assets::AssetImportRecord record = previous.value_or(kb::assets::AssetImportRecord{});
    record.sourceName = sourcePath.filename().string();
    record.sourceHash = kb::assets::HashAssetFile(sourcePath);
    record.optionFlags = kb::assets::AssetImportOptionFlags(options);
    record.transform = options.mesh.transform;
    for (const auto& [virtualPath, type] : written) {
        const auto physical = manager.Mounts().Resolve(virtualPath);
        record.Set(kb::assets::NormalizeAssetPath(virtualPath), type, physical.has_value() ? kb::assets::HashAssetFile(*physical) : 0U);
    }
    std::string ignored;
    static_cast<void>(kb::assets::AssetImportRecordIO::Write(kb::assets::AssetImportRecordIO::PathFor(mainPhysicalPath), record, &ignored));
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
        if (options.dryRun) {
            result.items.push_back({
                .sourcePath = sourcePath,
                .category = kb::assets::AssetImportCategory::Model,
                .status = kb::assets::AssetImportItemStatus::Failed,
                .error = "A dry run plans a reimport of an existing asset (import --reimport) or a character import; a new static import has nothing to compare.",
            });
            continue;
        }
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
        std::optional<kb::assets::AssetImportRecord> previousRecord;
        if (item.Succeeded() && prepared.has_value()) {
            previousRecord = kb::assets::AssetImportRecordIO::Read(kb::assets::AssetImportRecordIO::PathFor(item.assetPhysicalPath));
            if (Guard(manager, options, previousRecord ? &*previousRecord : nullptr, prepared->artifacts, {}, {}, item) == GuardOutcome::Stop) {
                result.items.push_back(std::move(item));
                continue;
            }
        }
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
            std::vector<std::pair<std::filesystem::path, std::string>> written;
            for (const kb::scene::SkeletalMeshImportArtifact& artifact : prepared->artifacts) written.emplace_back(artifact.virtualPath, artifact.expectedAssetType);
            WriteRecord(manager, item.assetPhysicalPath, sourcePath, options, previousRecord, written);
        }
        result.items.push_back(std::move(item));
    }
    return result;
}

kb::assets::AssetImportItemResult RenderMeshImportArtifacts::ReimportStatic(
    kb::assets::AssetManager& manager,
    kb::assets::AssetId id,
    const std::filesystem::path& sourcePath,
    const kb::assets::AssetImportOptions& options) {
    kb::assets::AssetImportItemResult item{};
    item.sourcePath = sourcePath;
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
    if (metadata == nullptr || metadata->physicalPath.empty()) {
        item.status = kb::assets::AssetImportItemStatus::Missing;
        item.error = "The asset to reimport is not in the project.";
        return item;
    }
    // What the container itself would become, without writing it.
    kb::assets::AssetImportOptions plan = options;
    plan.dryRun = true;
    kb::assets::AssetImportItemResult container = kb::assets::AssetImportService::ReimportFile(manager, id, sourcePath, plan);
    if (container.status != kb::assets::AssetImportItemStatus::Updated && container.status != kb::assets::AssetImportItemStatus::Reused) return container;
    item = container;
    const std::filesystem::path physical = metadata->physicalPath;
    const std::filesystem::path virtualPath = metadata->virtualPath;
    const std::optional<kb::assets::AssetImportRecord> previous = kb::assets::AssetImportRecordIO::Read(kb::assets::AssetImportRecordIO::PathFor(physical));

    std::string error;
    std::optional<RenderPreparedMeshImportArtifacts> prepared;
    if (CarriesSourceMaterials(sourcePath) && (options.mesh.importTextures || options.mesh.importMaterials)) {
        prepared = Prepare(manager, sourcePath, virtualPath.parent_path(), options, &error);
        if (!prepared) {
            item.status = kb::assets::AssetImportItemStatus::Failed;
            item.error = error.empty() ? "Static mesh auxiliary asset planning failed." : std::move(error);
            return item;
        }
    }
    std::vector<kb::scene::SkeletalMeshImportArtifact> artifacts = prepared.has_value() ? prepared->artifacts : std::vector<kb::scene::SkeletalMeshImportArtifact>{};
    const std::string mainPath = kb::assets::NormalizeAssetPath(virtualPath);
    const std::vector<GuardTarget> required{ { mainPath, "RenderMesh", container.assetHash } };
    const GuardOutcome outcome = Guard(manager, options, previous ? &*previous : nullptr, artifacts, required, { physical, kb::assets::AssetImportRecordIO::PathFor(physical) }, item);
    if (outcome == GuardOutcome::Stop) return item;
    // The model's own entry reflects whether anyone edited the container: it never is, so it is Changed or Unchanged.
    const kb::assets::AssetImportItemResult written = kb::assets::AssetImportService::ReimportFile(manager, id, sourcePath, options);
    if (written.status != kb::assets::AssetImportItemStatus::Updated && written.status != kb::assets::AssetImportItemStatus::Reused) {
        item.status = written.status;
        item.error = written.error;
        if (!item.backupId.empty()) {
            std::string restoreError;
            static_cast<void>(kb::assets::AssetImportBackups::Restore(manager, item.backupId, &restoreError));
            static_cast<void>(manager.DiscoverMountedAssets());
            item.backupId.clear();
        }
        return item;
    }
    item.status = written.status;
    item.assetHash = written.assetHash;
    item.sourceHash = written.sourceHash;
    if (!artifacts.empty() && !PublishStandalone(manager, artifacts, &error)) {
        // The container is already in place: put everything back so the project is as it was.
        if (!item.backupId.empty()) {
            std::string restoreError;
            static_cast<void>(kb::assets::AssetImportBackups::Restore(manager, item.backupId, &restoreError));
            static_cast<void>(manager.DiscoverMountedAssets());
            item.backupId.clear();
        }
        item.status = kb::assets::AssetImportItemStatus::Failed;
        item.error = error.empty() ? "Static mesh auxiliary asset publication failed." : std::move(error);
        return item;
    }
    item.producedAssets = PublishedArtifactIds(manager, artifacts);
    // The model itself may be the same while a texture or material was replaced.
    if (item.status == kb::assets::AssetImportItemStatus::Reused) {
        const bool replaced = std::ranges::any_of(item.changes, [&](const kb::assets::AssetImportChange& change) {
            const bool published = std::ranges::any_of(artifacts, [&](const kb::scene::SkeletalMeshImportArtifact& artifact) {
                return kb::assets::NormalizeAssetPath(artifact.virtualPath) == change.virtualPath.generic_string();
            });
            return published && (change.kind == kb::assets::AssetImportChangeKind::Added || change.kind == kb::assets::AssetImportChangeKind::Changed ||
                change.kind == kb::assets::AssetImportChangeKind::LocalOverride);
        });
        if (replaced) item.status = kb::assets::AssetImportItemStatus::Updated;
    }
    std::vector<std::pair<std::filesystem::path, std::string>> writtenEntries;
    writtenEntries.emplace_back(virtualPath, "RenderMesh");
    for (const kb::scene::SkeletalMeshImportArtifact& artifact : artifacts) writtenEntries.emplace_back(artifact.virtualPath, artifact.expectedAssetType);
    WriteRecord(manager, physical, sourcePath, options, previous, writtenEntries);
    static_cast<void>(manager.DiscoverMountedAssets());
    return item;
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
    const kb::assets::AssetMetadata* existingMesh = manager.Registry().FindByPath(plan->meshVirtualPath);
    const bool meshAlreadyExisted = existingMesh != nullptr;
    // Publishing over assets that exist is a reimport: what was edited since the last import is a local override.
    std::optional<kb::assets::AssetImportRecord> previousRecord;
    std::vector<GuardTarget> required;
    std::vector<std::filesystem::path> alwaysBackup;
    if (meshAlreadyExisted) {
        previousRecord = kb::assets::AssetImportRecordIO::Read(kb::assets::AssetImportRecordIO::PathFor(existingMesh->physicalPath));
        // The same source with the same options republishes what it published before: the recorded hash is then what the file should still be.
        const bool sameSource = previousRecord.has_value() && previousRecord->sourceHash == kb::assets::HashAssetFile(sourcePath) &&
            previousRecord->optionFlags == kb::assets::AssetImportOptionFlags(options) && previousRecord->transform == options.mesh.transform;
        const auto expectedHash = [&](const std::string& path) -> std::uint64_t {
            const kb::assets::AssetImportRecordEntry* entry = sameSource ? previousRecord->Find(path) : nullptr;
            return entry != nullptr ? entry->hash : 0U;
        };
        const std::string meshPath = kb::assets::NormalizeAssetPath(plan->meshVirtualPath);
        required.push_back({ meshPath, "SkeletalMesh", expectedHash(meshPath) });
        alwaysBackup.push_back(existingMesh->physicalPath);
        alwaysBackup.push_back(kb::assets::AssetImportRecordIO::PathFor(existingMesh->physicalPath));
        if (const kb::assets::AssetMetadata* skeleton = manager.Registry().FindByPath(plan->skeletonVirtualPath)) {
            const std::string skeletonPath = kb::assets::NormalizeAssetPath(plan->skeletonVirtualPath);
            required.push_back({ skeletonPath, "Skeleton", expectedHash(skeletonPath) });
            alwaysBackup.push_back(skeleton->physicalPath);
        }
        if (previousRecord.has_value()) {
            for (const kb::assets::AssetImportRecordEntry& entry : previousRecord->entries) {
                if (entry.assetType != "AnimationClip") continue;
                if (const auto clip = manager.Mounts().Resolve(entry.virtualPath)) alwaysBackup.push_back(*clip);
            }
        }
    }
    if (Guard(manager, options, previousRecord ? &*previousRecord : nullptr, prepared->artifacts, required, alwaysBackup, item) == GuardOutcome::Stop) {
        item.id = existingMesh != nullptr ? existingMesh->id : kb::assets::AssetId{};
        item.virtualPath = plan->meshVirtualPath;
        return item;
    }
    const auto published = Publisher::PublishWithArtifacts(manager, *plan, prepared->artifacts, &error);
    if (!published) {
        if (!item.backupId.empty()) {
            std::string ignored;
            static_cast<void>(kb::assets::AssetImportBackups::Restore(manager, item.backupId, &ignored));
            item.backupId.clear();
        }
        return fail(std::move(error), "asset publication failed.");
    }
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
    if (!animationsOnly) {
        std::vector<std::pair<std::filesystem::path, std::string>> writtenEntries;
        writtenEntries.emplace_back(metadata->virtualPath, "SkeletalMesh");
        if (const kb::assets::AssetMetadata* skeleton = manager.Registry().Find(published->skeletonAssetId)) writtenEntries.emplace_back(skeleton->virtualPath, "Skeleton");
        for (const kb::assets::AssetId clip : published->animationClipAssetIds) {
            if (const kb::assets::AssetMetadata* clipMetadata = manager.Registry().Find(clip)) writtenEntries.emplace_back(clipMetadata->virtualPath, "AnimationClip");
        }
        for (const kb::scene::SkeletalMeshImportArtifact& artifact : prepared->artifacts) writtenEntries.emplace_back(artifact.virtualPath, artifact.expectedAssetType);
        WriteRecord(manager, metadata->physicalPath, sourcePath, options, previousRecord, writtenEntries);
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
