#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "assets/EditorAssetBrowserState.hpp"
#include "console/EditorConsoleState.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderResources.hpp"
#include "scene/material/EditorMaterialAssetAuthoring.hpp"

#include <filesystem>
#include <optional>

namespace {

[[nodiscard]] std::filesystem::path TempRoot() {
    return std::filesystem::temp_directory_path() / "21kb_editor_material_asset_authoring_tests";
}

void CleanTempRoot() {
    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
    std::filesystem::create_directories(TempRoot() / "Project" / "Assets" / "Materials", error);
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

} // namespace

namespace kb::editor::tests {

void RunEditorMaterialAssetAuthoringTests() {
    RunCreateMaterialAssetThroughEditorAuthoringTest();
    RunEditMaterialAssetThroughEditorAuthoringTest();
}

} // namespace kb::editor::tests
