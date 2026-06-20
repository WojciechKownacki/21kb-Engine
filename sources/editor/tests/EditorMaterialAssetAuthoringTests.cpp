#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "assets/EditorAssetBrowserState.hpp"
#include "console/EditorConsoleState.hpp"
#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderResources.hpp"
#include "scene/material/EditorMaterialAssetAuthoring.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

[[nodiscard]] std::filesystem::path TempRoot() {
    return std::filesystem::temp_directory_path() / "21kb_editor_material_asset_authoring_tests";
}

void CleanTempRoot() {
    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
    std::filesystem::create_directories(TempRoot() / "Project" / "Assets" / "Materials", error);
}

void WriteTextFile(const std::filesystem::path& path, std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output << text;
}

[[nodiscard]] kb::assets::AssetMetadata Metadata(std::string name, std::string type, std::filesystem::path path) {
    return kb::assets::AssetMetadata{
        .type = std::move(type),
        .name = std::move(name),
        .virtualPath = std::move(path),
        .runtimeLoadable = true,
    };
}

[[nodiscard]] kb::assets::AssetId RequireAssetId(kb::assets::AssetManager& manager, const std::filesystem::path& path, const char* message) {
    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(path);
    kb::editor::tests::Require(metadata != nullptr, message);
    return metadata->id;
}

void RunCreateMaterialAssetThroughEditorAuthoringTest() {
    CleanTempRoot();

    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::tests::Require(scene.Assets().MountProject(TempRoot() / "Project"), "Material authoring test could not mount project assets");

    kb::editor::EditorMaterialAssetAuthoring authoring{ scene, browser, console };
    kb::editor::tests::Require(authoring.Create("/Game/Materials"), "Editor material authoring did not create a material asset");

    const std::filesystem::path materialPath = TempRoot() / "Project" / "Assets" / "Materials" / "NewMaterial.kbmat";
    kb::editor::tests::Require(std::filesystem::exists(materialPath), "Editor material authoring did not write NewMaterial.kbmat");

    const kb::assets::AssetManager& manager = scene.Assets().Manager();
    const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath("/Game/Materials/NewMaterial.kbmat");
    kb::editor::tests::Require(metadata != nullptr, "Editor material authoring did not register the created material");
    kb::editor::tests::Require(metadata->type == "RenderMaterial", "Editor material authoring registered the wrong asset type");
    kb::editor::tests::Require(browser.SelectedAsset() == metadata->id, "Editor material authoring did not select the created material asset");

    const std::optional<kb::render::RenderMaterialAssetData> loaded = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(loaded.has_value(), "Editor material authoring wrote a material file that could not be loaded");
    kb::editor::tests::Require(loaded->desc.baseColor[0] == 1.0F && loaded->desc.baseColor[1] == 1.0F && loaded->desc.baseColor[2] == 1.0F && loaded->desc.baseColor[3] == 1.0F, "Editor material authoring did not preserve default white base color");
    kb::editor::tests::Require(loaded->desc.metallicFactor == 0.0F, "Editor material authoring did not preserve default metallic factor");
    kb::editor::tests::Require(loaded->desc.roughnessFactor == 1.0F, "Editor material authoring did not preserve default roughness factor");
    kb::editor::tests::Require(loaded->desc.alphaMode == kb::render::RenderMaterialAlphaMode::Opaque, "Editor material authoring did not preserve default alpha mode");
    kb::editor::tests::Require(!loaded->desc.doubleSided, "Editor material authoring did not preserve default double-sided flag");
    kb::editor::tests::Require(console.Count(kb::editor::EditorConsoleLevel::Info) == 1U, "Editor material authoring should report one successful creation message");

    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
}

void RunEditMaterialAssetThroughEditorAuthoringTest() {
    CleanTempRoot();

    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::tests::Require(scene.Assets().MountProject(TempRoot() / "Project"), "Material edit test could not mount project assets");

    kb::editor::EditorMaterialAssetAuthoring authoring{ scene, browser, console };
    kb::editor::tests::Require(authoring.Create("/Game/Materials"), "Material edit test could not create a material asset");
    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().FindByPath("/Game/Materials/NewMaterial.kbmat");
    kb::editor::tests::Require(metadata != nullptr, "Material edit test did not discover created material metadata");
    const kb::assets::AssetHandle<kb::render::RenderMaterialAssetData> loadedBeforeEdit = scene.Assets().Manager().Load<kb::render::RenderMaterialAssetData>(metadata->id);
    kb::editor::tests::Require(loadedBeforeEdit.IsLoaded(), "Material edit test could not load created material into runtime cache");

    kb::editor::tests::Require(authoring.SetBaseColor(metadata->id, 0, 1.25F), "Material edit test could not set base color red");
    kb::editor::tests::Require(authoring.SetBaseColor(metadata->id, 1, 0.5F), "Material edit test could not set base color green");
    kb::editor::tests::Require(authoring.SetBaseColor(metadata->id, 2, -0.25F), "Material edit test could not set base color blue");
    kb::editor::tests::Require(authoring.SetBaseColor(metadata->id, 3, 0.75F), "Material edit test could not set base color alpha");
    kb::editor::tests::Require(authoring.SetMetallicFactor(metadata->id, 2.0F), "Material edit test could not set metallic factor");
    kb::editor::tests::Require(authoring.SetRoughnessFactor(metadata->id, -2.0F), "Material edit test could not set roughness factor");
    kb::editor::tests::Require(authoring.SetNormalScale(metadata->id, -3.0F), "Material edit test could not set normal scale");
    kb::editor::tests::Require(authoring.SetOcclusionStrength(metadata->id, 0.25F), "Material edit test could not set occlusion strength");
    kb::editor::tests::Require(authoring.SetEmissiveColor(metadata->id, 0, 0.1F), "Material edit test could not set emissive red");
    kb::editor::tests::Require(authoring.SetEmissiveColor(metadata->id, 1, 0.2F), "Material edit test could not set emissive green");
    kb::editor::tests::Require(authoring.SetEmissiveColor(metadata->id, 2, 0.3F), "Material edit test could not set emissive blue");
    kb::editor::tests::Require(authoring.SetEmissiveStrength(metadata->id, -1.0F), "Material edit test could not set emissive strength");
    kb::editor::tests::Require(authoring.SetAlphaCutoff(metadata->id, 0.45F), "Material edit test could not set alpha cutoff");
    kb::editor::tests::Require(authoring.SetAlphaMode(metadata->id, kb::render::RenderMaterialAlphaMode::Mask), "Material edit test could not set alpha mode");
    kb::editor::tests::Require(authoring.CycleAlphaMode(metadata->id), "Material edit test could not cycle alpha mode");
    kb::editor::tests::Require(authoring.ToggleDoubleSided(metadata->id), "Material edit test could not toggle double-sided flag");

    kb::editor::tests::Require(!scene.Assets().Manager().IsLoaded(metadata->id), "Material edit should unload stale runtime cache after save");
    const std::filesystem::path materialPath = TempRoot() / "Project" / "Assets" / "Materials" / "NewMaterial.kbmat";
    const std::optional<kb::render::RenderMaterialAssetData> edited = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(edited.has_value(), "Material edit test wrote an unreadable material file");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(edited->desc.baseColor[0], 1.0F) && kb::editor::tests::NearlyEqual(edited->desc.baseColor[1], 0.5F) && kb::editor::tests::NearlyEqual(edited->desc.baseColor[2], 0.0F) && kb::editor::tests::NearlyEqual(edited->desc.baseColor[3], 0.75F), "Material edit test did not clamp/persist base color");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(edited->desc.metallicFactor, 1.0F), "Material edit test did not clamp metallic factor");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(edited->desc.roughnessFactor, 0.0F), "Material edit test did not clamp roughness factor");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(edited->desc.normalScale, 0.0F), "Material edit test did not clamp normal scale");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(edited->desc.occlusionStrength, 0.25F), "Material edit test did not persist occlusion strength");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(edited->desc.emissiveColor[0], 0.1F) && kb::editor::tests::NearlyEqual(edited->desc.emissiveColor[1], 0.2F) && kb::editor::tests::NearlyEqual(edited->desc.emissiveColor[2], 0.3F), "Material edit test did not persist emissive color");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(edited->desc.emissiveStrength, 0.0F), "Material edit test did not clamp emissive strength");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(edited->desc.alphaCutoff, 0.45F), "Material edit test did not persist alpha cutoff");
    kb::editor::tests::Require(edited->desc.alphaMode == kb::render::RenderMaterialAlphaMode::Blend, "Material edit test did not persist cycled alpha mode");
    kb::editor::tests::Require(edited->desc.doubleSided, "Material edit test did not persist double-sided flag");

    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
}

void RunMaterialTextureSlotAuthoringTest() {
    CleanTempRoot();

    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::tests::Require(scene.Assets().MountProject(TempRoot() / "Project"), "Material texture slot test could not mount project assets");

    kb::editor::EditorMaterialAssetAuthoring authoring{ scene, browser, console };
    kb::editor::tests::Require(authoring.Create("/Game/Materials"), "Material texture slot test could not create a material asset");
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    const kb::assets::AssetMetadata* material = manager.Registry().FindByPath("/Game/Materials/NewMaterial.kbmat");
    kb::editor::tests::Require(material != nullptr, "Material texture slot test did not discover created material metadata");
    const kb::assets::AssetId materialId = material->id;

    const std::filesystem::path sourceRoot = TempRoot() / "Sources";
    WriteTextFile(sourceRoot / "Albedo.png", "albedo");
    WriteTextFile(sourceRoot / "Normal.png", "normal");
    WriteTextFile(sourceRoot / "MR.png", "metallic roughness");
    WriteTextFile(sourceRoot / "AO.png", "occlusion");
    WriteTextFile(sourceRoot / "Emissive.png", "emissive");
    const std::array<std::filesystem::path, 5U> sourceTextures{
        sourceRoot / "Albedo.png",
        sourceRoot / "Normal.png",
        sourceRoot / "MR.png",
        sourceRoot / "AO.png",
        sourceRoot / "Emissive.png",
    };
    const kb::assets::AssetImportResult imported = kb::assets::AssetImportService::ImportFiles(manager, sourceTextures, "/Game/Textures");
    kb::editor::tests::Require(imported.Succeeded(), "Material texture slot test could not import source textures");
    static_cast<void>(manager.RegisterAsset(Metadata("Cube", "RenderMesh", "/Game/Meshes/Cube.21kb")));

    const kb::assets::AssetId albedo = RequireAssetId(manager, imported.items[0].virtualPath, "Material texture slot test did not register albedo texture");
    const kb::assets::AssetId normal = RequireAssetId(manager, imported.items[1].virtualPath, "Material texture slot test did not register normal texture");
    const kb::assets::AssetId metallicRoughness = RequireAssetId(manager, imported.items[2].virtualPath, "Material texture slot test did not register metallic-roughness texture");
    const kb::assets::AssetId occlusion = RequireAssetId(manager, imported.items[3].virtualPath, "Material texture slot test did not register occlusion texture");
    const kb::assets::AssetId emissive = RequireAssetId(manager, imported.items[4].virtualPath, "Material texture slot test did not register emissive texture");
    const kb::assets::AssetId mesh = RequireAssetId(manager, "/Game/Meshes/Cube.21kb", "Material texture slot test did not register mesh asset");

    kb::editor::tests::Require(!authoring.SetTextureAsset(materialId, kb::editor::EditorMaterialTextureSlot::Albedo, mesh), "Material texture slot test accepted a non-texture asset");
    kb::editor::tests::Require(authoring.SetTextureAsset(materialId, kb::editor::EditorMaterialTextureSlot::Albedo, albedo), "Material texture slot test could not set albedo texture");
    kb::editor::tests::Require(authoring.SetTextureAsset(materialId, kb::editor::EditorMaterialTextureSlot::Normal, normal), "Material texture slot test could not set normal texture");
    kb::editor::tests::Require(authoring.SetTextureAsset(materialId, kb::editor::EditorMaterialTextureSlot::MetallicRoughness, metallicRoughness), "Material texture slot test could not set metallic-roughness texture");
    kb::editor::tests::Require(authoring.SetTextureAsset(materialId, kb::editor::EditorMaterialTextureSlot::Occlusion, occlusion), "Material texture slot test could not set occlusion texture");
    kb::editor::tests::Require(authoring.SetTextureAsset(materialId, kb::editor::EditorMaterialTextureSlot::Emissive, emissive), "Material texture slot test could not set emissive texture");

    const std::filesystem::path materialPath = TempRoot() / "Project" / "Assets" / "Materials" / "NewMaterial.kbmat";
    std::optional<kb::render::RenderMaterialAssetData> edited = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(edited.has_value(), "Material texture slot test wrote an unreadable material file");
    kb::editor::tests::Require(edited->desc.albedoTextureAssetId == albedo.value, "Material texture slot test did not persist albedo texture id");
    kb::editor::tests::Require(edited->desc.normalTextureAssetId == normal.value, "Material texture slot test did not persist normal texture id");
    kb::editor::tests::Require(edited->desc.metallicRoughnessTextureAssetId == metallicRoughness.value, "Material texture slot test did not persist metallic-roughness texture id");
    kb::editor::tests::Require(edited->desc.occlusionTextureAssetId == occlusion.value, "Material texture slot test did not persist occlusion texture id");
    kb::editor::tests::Require(edited->desc.emissiveTextureAssetId == emissive.value, "Material texture slot test did not persist emissive texture id");

    kb::editor::tests::Require(authoring.SetTextureAsset(materialId, kb::editor::EditorMaterialTextureSlot::Emissive, {}), "Material texture slot test could not clear emissive texture");
    edited = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(edited.has_value() && edited->desc.emissiveTextureAssetId == 0U, "Material texture slot test did not clear emissive texture id");

    kb::editor::tests::Require(authoring.CycleTextureAsset(materialId, kb::editor::EditorMaterialTextureSlot::Emissive), "Material texture slot test could not cycle emissive texture picker");
    edited = kb::render::RenderMaterialAssetLoader::LoadMaterial(materialPath);
    kb::editor::tests::Require(edited.has_value() && edited->desc.emissiveTextureAssetId != 0U, "Material texture slot picker did not assign an available texture");

    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
}

} // namespace

namespace kb::editor::tests {

void RunEditorMaterialAssetAuthoringTests() {
    RunCreateMaterialAssetThroughEditorAuthoringTest();
    RunEditMaterialAssetThroughEditorAuthoringTest();
    RunMaterialTextureSlotAuthoringTest();
}

} // namespace kb::editor::tests
