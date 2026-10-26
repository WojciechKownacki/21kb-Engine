// Reimporting a changed model: the asset is replaced in place, and the textures and materials the import wrote are not overwritten when they
// were edited since, unless the author says so; whatever is overwritten can be put back; a dry run writes nothing. Every fixture is written
// by the test.

#include "RendererTestSupport.hpp"

#include "engine/assets/AssetImportRecord.hpp"
#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/ImportedAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderMeshImportArtifacts.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace kb::render::tests {
namespace {

using kb::assets::AssetImportChangeKind;
using kb::assets::AssetImportConflictPolicy;
using kb::assets::AssetImportItemStatus;

void RequireThat(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << '\n';
        Require(false, "mesh reimport test failed");
    }
}

class TempRoot final {
public:
    TempRoot() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / ("21kb_mesh_reimport_" + std::to_string(std::random_device{}()) + "_" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }
    ~TempRoot() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    TempRoot(const TempRoot&) = delete;
    TempRoot& operator=(const TempRoot&) = delete;
    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

void WriteBytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    RequireThat(output.good(), "Fixture could not be written: " + path.string());
}

void WriteText(const std::filesystem::path& path, std::string_view text) {
    WriteBytes(path, std::span<const std::uint8_t>{ reinterpret_cast<const std::uint8_t*>(text.data()), text.size() });
}

// One triangle (0,0,0) (scale,0,0) (0,scale,0) as little-endian floats.
[[nodiscard]] std::vector<std::uint8_t> TriangleBuffer(float scale) {
    std::vector<std::uint8_t> bytes;
    for (const float value : { 0.0F, 0.0F, 0.0F, scale, 0.0F, 0.0F, 0.0F, scale, 0.0F }) {
        const auto bits = std::bit_cast<std::uint32_t>(value);
        for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) bytes.push_back(static_cast<std::uint8_t>(bits >> shift));
    }
    return bytes;
}

// A 1 by 1 PNG.
[[nodiscard]] std::vector<std::uint8_t> OnePixelPng() {
    return { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06,
        0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xFF, 0xFF, 0x3F, 0x00, 0x05, 0xFE, 0x02,
        0xFE, 0xA7, 0x35, 0x81, 0x84, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };
}

[[nodiscard]] std::string Document(float roughness) {
    return std::string{ R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],)" } +
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],)" +
        R"("materials":[{"name":"Skin","pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicFactor":0.25,"roughnessFactor":)" + std::to_string(roughness) + "}}]," +
        R"("textures":[{"source":0}],"images":[{"uri":"Skin.png"}],)" +
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],)" +
        R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36}],"buffers":[{"byteLength":36,"uri":"Triangle.bin"}]})";
}

struct Project {
    kb::assets::AssetManager manager;
    std::filesystem::path assets;
    std::filesystem::path source;

    explicit Project(const std::filesystem::path& root) : assets(root / "Project" / "Assets"), source(root / "Source" / "Triangle.gltf") {
        std::filesystem::create_directories(assets);
        Require(manager.RegisterLoader(std::make_unique<kb::assets::ImportedAssetLoader>()), "imported asset loader");
        Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "mesh loader");
        Require(manager.Mounts().Mount("Game", assets), "project mount");
        WriteBytes(source.parent_path() / "Skin.png", OnePixelPng());
        WriteSource(1.0F, 0.75F);
    }

    void WriteSource(float scale, float roughness) {
        WriteBytes(source.parent_path() / "Triangle.bin", TriangleBuffer(scale));
        WriteText(source, Document(roughness));
    }

    [[nodiscard]] std::filesystem::path Physical(std::string_view virtualPath) const { return *manager.Mounts().Resolve(std::filesystem::path{ virtualPath }); }
    [[nodiscard]] float MaxX(kb::assets::AssetId id) {
        const kb::assets::AssetHandle<RenderMeshAssetData> mesh = manager.Load<RenderMeshAssetData>(id);
        RequireThat(mesh.IsLoaded(), "the mesh did not load");
        float reach = 0.0F;
        for (const RenderStaticMeshVertexP3N3UV2& vertex : mesh->vertices) reach = std::max(reach, std::fabs(vertex.x));
        for (const RenderStaticMeshVertexP3N3T4UV2& vertex : mesh->tangentVertices) reach = std::max(reach, std::fabs(vertex.x));
        return reach;
    }
};

[[nodiscard]] float Roughness(const std::filesystem::path& material) {
    const auto loaded = RenderMaterialAssetLoader::LoadMaterial(material);
    RequireThat(loaded.has_value(), "the material did not load");
    return loaded->desc.roughnessFactor;
}

[[nodiscard]] const kb::assets::AssetImportChange* ChangeOf(const kb::assets::AssetImportItemResult& item, std::string_view virtualPath) {
    for (const kb::assets::AssetImportChange& change : item.changes) {
        if (change.virtualPath.generic_string() == virtualPath) return &change;
    }
    return nullptr;
}

void ReimportReplacesInPlaceAndKeepsLocalEditsUntilTold() {
    const TempRoot root;
    Project project{ root.Path() };
    kb::assets::AssetImportOptions options{};
    options.mesh.importTextures = true;
    options.mesh.importMaterials = true;

    // The first import writes the model, a texture and a material, and records their hashes.
    const std::array<std::filesystem::path, 1U> sources{ project.source };
    const kb::assets::AssetImportResult first = RenderMeshImportArtifacts::ImportStaticFiles(project.manager, sources, "/Game/Meshes", options);
    RequireThat(first.Succeeded() && first.items.front().status == AssetImportItemStatus::Created && first.items.front().producedAssets.size() == 2U, "the first import");
    const kb::assets::AssetImportItemResult imported = first.items.front();
    const std::filesystem::path meshPath = imported.assetPhysicalPath;
    const std::optional<kb::assets::AssetImportRecord> record = kb::assets::AssetImportRecordIO::Read(kb::assets::AssetImportRecordIO::PathFor(meshPath));
    RequireThat(record.has_value() && record->entries.size() == 2U && record->sourceName == "Triangle.gltf", "the import left a record");
    std::filesystem::path materialPath;
    std::string materialVirtual;
    for (const kb::assets::AssetId produced : imported.producedAssets) {
        const kb::assets::AssetMetadata* metadata = project.manager.Registry().Find(produced);
        RequireThat(metadata != nullptr, "a produced asset is registered");
        if (metadata->type == "RenderMaterial") {
            materialPath = metadata->physicalPath;
            materialVirtual = metadata->virtualPath.generic_string();
        }
    }
    RequireThat(!materialPath.empty() && std::fabs(Roughness(materialPath) - 0.75F) < 0.001F && std::fabs(project.MaxX(imported.id) - 1.0F) < 0.001F, "the first import is as authored");

    // Nothing changed: the same source gives the same container and the same material.
    kb::assets::AssetImportItemResult same = RenderMeshImportArtifacts::ReimportStatic(project.manager, imported.id, project.source, options);
    RequireThat(same.status == AssetImportItemStatus::Reused && same.backupId.empty() && same.conflicts.empty() && same.id == imported.id, "an unchanged reimport changes nothing: " + same.error);
    RequireThat(ChangeOf(same, materialVirtual) != nullptr && ChangeOf(same, materialVirtual)->kind == AssetImportChangeKind::Unchanged, "the material is unchanged");

    // A changed model, an untouched material: replaced in place, the id and the path are the same, a backup was kept.
    project.WriteSource(2.0F, 0.75F);
    kb::assets::AssetImportItemResult updated = RenderMeshImportArtifacts::ReimportStatic(project.manager, imported.id, project.source, options);
    RequireThat(updated.status == AssetImportItemStatus::Updated && updated.id == imported.id && updated.assetPhysicalPath == meshPath && !updated.backupId.empty() && updated.conflicts.empty(),
        "a changed model is replaced in place: " + updated.error);
    RequireThat(std::fabs(project.MaxX(imported.id) - 2.0F) < 0.001F, "the asset now holds the new geometry");
    const std::uint64_t updatedContainerHash = kb::assets::HashAssetFile(meshPath);

    // An edit of the material, then a source whose material changed too: the edit is reported and nothing is touched.
    {
        auto edited = RenderMaterialAssetLoader::LoadMaterial(materialPath);
        RequireThat(edited.has_value(), "load the material to edit it");
        edited->desc.roughnessFactor = 0.5F;
        RequireThat(RenderMaterialAssetWriter::Save(materialPath, *edited), "save the local edit");
    }
    const std::uint64_t editedHash = kb::assets::HashAssetFile(materialPath);
    project.WriteSource(3.0F, 0.1F);
    const std::size_t backupsBefore = kb::assets::AssetImportBackups::List(project.manager).size();
    kb::assets::AssetImportItemResult dry = [&] {
        kb::assets::AssetImportOptions plan = options;
        plan.dryRun = true;
        return RenderMeshImportArtifacts::ReimportStatic(project.manager, imported.id, project.source, plan);
    }();
    RequireThat(dry.error.empty() && ChangeOf(dry, materialVirtual) != nullptr && ChangeOf(dry, materialVirtual)->kind == AssetImportChangeKind::LocalOverride && dry.conflicts.size() == 1U,
        "a dry run names the edited material: " + dry.error);
    RequireThat(kb::assets::HashAssetFile(meshPath) == updatedContainerHash && kb::assets::HashAssetFile(materialPath) == editedHash &&
            kb::assets::AssetImportBackups::List(project.manager).size() == backupsBefore && std::fabs(project.MaxX(imported.id) - 2.0F) < 0.001F,
        "a dry run writes nothing");
    kb::assets::AssetImportItemResult refused = RenderMeshImportArtifacts::ReimportStatic(project.manager, imported.id, project.source, options);
    RequireThat(refused.status == AssetImportItemStatus::Failed && refused.conflicts.size() == 1U && refused.conflicts.front().virtualPath.generic_string() == materialVirtual &&
            refused.error.find("edited since the last import") != std::string::npos, "the default refuses over an edited material: " + refused.error);
    RequireThat(kb::assets::HashAssetFile(meshPath) == updatedContainerHash && kb::assets::HashAssetFile(materialPath) == editedHash &&
            kb::assets::AssetImportBackups::List(project.manager).size() == backupsBefore, "a refusal changes nothing and keeps no backup");

    // Keep local: the model follows the source, the edited material stays.
    options.conflictPolicy = AssetImportConflictPolicy::KeepLocal;
    kb::assets::AssetImportItemResult kept = RenderMeshImportArtifacts::ReimportStatic(project.manager, imported.id, project.source, options);
    RequireThat(kept.status == AssetImportItemStatus::Updated && kept.conflicts.size() == 1U && !kept.backupId.empty() && kb::assets::HashAssetFile(materialPath) == editedHash &&
            std::fabs(project.MaxX(imported.id) - 3.0F) < 0.001F, "keep-local updates the model and leaves the edit: " + kept.error);
    RequireThat(std::fabs(Roughness(materialPath) - 0.5F) < 0.001F, "the edited roughness is still there");
    // The kept edit is still an edit next time.
    project.WriteSource(4.0F, 0.1F);
    options.conflictPolicy = AssetImportConflictPolicy::Report;
    RequireThat(RenderMeshImportArtifacts::ReimportStatic(project.manager, imported.id, project.source, options).status == AssetImportItemStatus::Failed, "a kept edit is still reported by the next reimport");

    // Overwrite: the material becomes the imported one, and the backup puts everything back.
    options.conflictPolicy = AssetImportConflictPolicy::Overwrite;
    const std::uint64_t containerBefore = kb::assets::HashAssetFile(meshPath);
    kb::assets::AssetImportItemResult overwritten = RenderMeshImportArtifacts::ReimportStatic(project.manager, imported.id, project.source, options);
    RequireThat(overwritten.status == AssetImportItemStatus::Updated && !overwritten.backupId.empty() && std::fabs(Roughness(materialPath) - 0.1F) < 0.001F &&
            std::fabs(project.MaxX(imported.id) - 4.0F) < 0.001F, "overwrite replaces the edit: " + overwritten.error);
    std::string restoreError;
    RequireThat(kb::assets::AssetImportBackups::Restore(project.manager, overwritten.backupId, &restoreError), "undo the reimport: " + restoreError);
    static_cast<void>(project.manager.DiscoverMountedAssets());
    static_cast<void>(project.manager.Unload(imported.id));
    RequireThat(kb::assets::HashAssetFile(meshPath) == containerBefore && kb::assets::HashAssetFile(materialPath) == editedHash && std::fabs(Roughness(materialPath) - 0.5F) < 0.001F &&
            std::fabs(project.MaxX(imported.id) - 3.0F) < 0.001F, "undoing the reimport brings back the model and the edited material");
    RequireThat(!std::filesystem::exists(*kb::assets::AssetImportBackups::Root(project.manager) / overwritten.backupId), "a restored backup is removed");
    std::string missing;
    RequireThat(!kb::assets::AssetImportBackups::Restore(project.manager, overwritten.backupId, &missing) && !missing.empty(), "a backup can be restored once");
    RequireThat(!kb::assets::AssetImportBackups::Restore(project.manager, "../x", &missing), "a backup id is not a path");
}

void ImportingOverEditedAssetsIsAReimport() {
    // The plain import of the same source into the same folder republishes the textures and materials: an edit of one is reported there too.
    const TempRoot root;
    Project project{ root.Path() };
    kb::assets::AssetImportOptions options{};
    options.mesh.importTextures = true;
    options.mesh.importMaterials = true;
    const std::array<std::filesystem::path, 1U> sources{ project.source };
    const kb::assets::AssetImportResult first = RenderMeshImportArtifacts::ImportStaticFiles(project.manager, sources, "/Game/Meshes", options);
    RequireThat(first.Succeeded(), "the first import");
    std::filesystem::path materialPath;
    for (const kb::assets::AssetId produced : first.items.front().producedAssets) {
        const kb::assets::AssetMetadata* metadata = project.manager.Registry().Find(produced);
        if (metadata->type == "RenderMaterial") materialPath = metadata->physicalPath;
    }
    auto edited = RenderMaterialAssetLoader::LoadMaterial(materialPath);
    RequireThat(edited.has_value(), "load the material");
    edited->desc.roughnessFactor = 0.4F;
    RequireThat(RenderMaterialAssetWriter::Save(materialPath, *edited), "edit the material");
    const std::uint64_t editedHash = kb::assets::HashAssetFile(materialPath);
    // Same source again: the container is reused, and the edited material is not silently replaced by the imported one.
    const kb::assets::AssetImportResult again = RenderMeshImportArtifacts::ImportStaticFiles(project.manager, sources, "/Game/Meshes", options);
    RequireThat(!again.Succeeded() && again.items.front().conflicts.size() == 1U && kb::assets::HashAssetFile(materialPath) == editedHash,
        "importing again does not overwrite an edited material without being told");
    options.conflictPolicy = AssetImportConflictPolicy::KeepLocal;
    RequireThat(RenderMeshImportArtifacts::ImportStaticFiles(project.manager, sources, "/Game/Meshes", options).Succeeded() && kb::assets::HashAssetFile(materialPath) == editedHash, "keep-local keeps it");
    options.dryRun = true;
    RequireThat(!RenderMeshImportArtifacts::ImportStaticFiles(project.manager, sources, "/Game/Meshes", options).Succeeded(), "a new static import cannot be a dry run");
}

void RecordsAndBackupsAreStrict() {
    kb::assets::AssetImportRecord record;
    record.sourceName = "Rock.gltf";
    record.sourceHash = 123U;
    record.optionFlags = 6U;
    record.transform.scale = 2.0F;
    record.Set("/Game/Meshes/Rock.21kb", "RenderMesh", 77U);
    record.Set("/Game/Meshes/Materials/Rock Mat.kbmat", "RenderMaterial", 88U);
    const std::string text = kb::assets::AssetImportRecordIO::Encode(record);
    const auto parsed = kb::assets::AssetImportRecordIO::Parse(text);
    RequireThat(parsed.has_value() && *parsed == record, "a record round-trips, spaces in a path included");
    for (const std::string bad : { std::string{}, std::string{ "21kb-import-record 2\n" }, text + "mystery 1\n", text + "asset x - /Game/A\n", text + "asset 5 - /Game/Meshes/Rock.21kb\n",
             std::string{ "21kb-import-record 1\nsource 1 a\n" } }) {
        std::string error;
        RequireThat(!kb::assets::AssetImportRecordIO::Parse(bad, &error).has_value() && !error.empty(), "a bad record is refused");
    }
}

} // namespace

void RunMeshReimportTests() {
    RecordsAndBackupsAreStrict();
    ReimportReplacesInPlaceAndKeepsLocalEditsUntilTold();
    ImportingOverEditedAssetsIsAReimport();
}

} // namespace kb::render::tests
