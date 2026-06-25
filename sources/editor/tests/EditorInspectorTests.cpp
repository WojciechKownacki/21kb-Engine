#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "assets/EditorAssetBrowserState.hpp"
#include "console/EditorConsoleState.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/audio/AudioSettings.hpp"
#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneHistory.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "inspection/InspectorAudioTextBuilder.hpp"
#include "inspection/InspectorComponentCatalog.hpp"
#include "inspection/InspectorComponentLabelFormatter.hpp"
#include "inspection/InspectorMeshRendererMaterialSlotModel.hpp"
#include "inspection/InspectorMaterialTextureSlotFormatter.hpp"
#include "inspection/EditorValueFormatter.hpp"
#include "inspection/MaterialAssetFormatter.hpp"
#include "inspection/InspectorPanelState.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/scene/EcsRenderSceneSynchronizer.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderer.hpp"
#include "scene/EditorSceneAudioAssetActions.hpp"
#include "scene/EditorSceneMaterialAssetActions.hpp"
#include "scene/material/EditorMaterialReferenceFinder.hpp"
#include "scene/material_preview/EditorMaterialPreviewMeshFactory.hpp"
#include "scene/material_preview/EditorMaterialPreviewMeshLoader.hpp"
#include "scene/material_preview/EditorMaterialPreviewScene.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "scene/material/EditorMaterialAssetAuthoring.hpp"
#include "scene/material/MaterialEditorState.hpp"

#include <algorithm>
#include <array>
#include <bgfx/bgfx.h>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

class MaterialPreviewHeadlessSurface final : public kb::render::RenderSurface {
public:
    [[nodiscard]] std::uint32_t Width() const noexcept override {
        return 128U;
    }

    [[nodiscard]] std::uint32_t Height() const noexcept override {
        return 128U;
    }

    [[nodiscard]] void* NativeWindowHandle() const noexcept override {
        return nullptr;
    }

    [[nodiscard]] void* NativeDisplayHandle() const noexcept override {
        return nullptr;
    }
};

[[nodiscard]] float TriangleOutwardDot(const kb::render::RenderMeshAssetData& mesh, std::size_t indexOffset) {
    const auto& a = mesh.vertices[mesh.indices32[indexOffset]];
    const auto& b = mesh.vertices[mesh.indices32[indexOffset + 1U]];
    const auto& c = mesh.vertices[mesh.indices32[indexOffset + 2U]];
    const std::array<float, 3U> ab{ b.x - a.x, b.y - a.y, b.z - a.z };
    const std::array<float, 3U> ac{ c.x - a.x, c.y - a.y, c.z - a.z };
    const std::array<float, 3U> normal{
        (ab[1] * ac[2]) - (ab[2] * ac[1]),
        (ab[2] * ac[0]) - (ab[0] * ac[2]),
        (ab[0] * ac[1]) - (ab[1] * ac[0]),
    };
    const std::array<float, 3U> center{
        (a.x + b.x + c.x) / 3.0F,
        (a.y + b.y + c.y) / 3.0F,
        (a.z + b.z + c.z) / 3.0F,
    };
    return (normal[0] * center[0]) + (normal[1] * center[1]) + (normal[2] * center[2]);
}

void RunInspectorTextEditDirtyStateTest() {
    kb::editor::InspectorPanelState state;
    state.BeginTextEdit(kb::editor::InspectorPropertyId::MaterialMetallicFactor, "0.25");
    kb::editor::tests::Require(!state.IsTextEditDirty(), "Inspector text edit should start clean");
    state.AppendText('1');
    kb::editor::tests::Require(state.IsTextEditDirty(), "Inspector text edit should become dirty after buffer mutation");
    state.BackspaceText();
    kb::editor::tests::Require(!state.IsTextEditDirty(), "Inspector text edit should become clean when buffer returns to original value");
    state.ClearText();
    kb::editor::tests::Require(state.IsTextEditDirty(), "Inspector text edit clear should mark a non-empty original value dirty");
    state.EndTextEdit();
    kb::editor::tests::Require(!state.IsTextEditDirty(), "Inspector text edit should not remain dirty after ending edit");
}

void RunAudioComponentCatalogTest() {
    const std::vector<const kb::editor::InspectorComponentTile*> audioTiles = kb::editor::InspectorComponentCatalog::Search("audio");
    const auto hasTile = [&audioTiles](std::string_view id) {
        return std::ranges::any_of(audioTiles, [id](const kb::editor::InspectorComponentTile* tile) {
            return tile != nullptr && tile->id == id;
        });
    };

    kb::editor::tests::Require(hasTile("AudioSource"), "Add Component catalog should expose Audio Source");
    kb::editor::tests::Require(hasTile("AudioListener"), "Add Component catalog should expose Audio Listener");
    kb::editor::tests::Require(kb::editor::InspectorComponentCatalog::Find("AudioSource") != nullptr, "Audio Source component id should resolve");
    kb::editor::tests::Require(kb::editor::InspectorComponentCatalog::Find("AudioListener") != nullptr, "Audio Listener component id should resolve");
}

void RunAudioInspectorTextTest() {
    std::string text = "Entity";
    kb::scene::AudioSourceComponent source{
        .clipAssetId = 123,
        .volume = 0.5F,
        .pitch = 0.75F,
        .loop = true,
        .spatial = false,
        .autoplay = true,
        .attenuationModel = kb::audio::AudioAttenuationModel::Linear,
        .minDistance = 2.0F,
        .maxDistance = 25.0F,
    };
    kb::editor::InspectorAudioSourceTextBuilder{}.Append(text, source);
    kb::editor::InspectorAudioListenerTextBuilder{}.Append(text, kb::scene::AudioListenerComponent{ .primary = false, .enabled = true });

    kb::editor::tests::Require(text.find("Audio Source") != std::string::npos, "Inspector text should include Audio Source");
    kb::editor::tests::Require(text.find("Clip: 123") != std::string::npos, "Inspector text should include audio clip id");
    kb::editor::tests::Require(text.find("Attenuation: Linear") != std::string::npos, "Inspector text should include attenuation model");
    kb::editor::tests::Require(text.find("Audio Listener") != std::string::npos, "Inspector text should include Audio Listener");
    kb::editor::tests::Require(kb::editor::InspectorComponentLabelFormatter::AudioAttenuationModelName(kb::audio::AudioAttenuationModel::Exponential) == std::string_view{ "Exponential" }, "Audio attenuation label should resolve");
}

void RunMaterialTextureSlotDiagnosticTest() {
    kb::assets::AssetManager manager;
    const kb::assets::AssetId textureId{ 42U };
    static_cast<void>(manager.RegisterAsset(kb::assets::AssetMetadata{
        .id = textureId,
        .type = "ImportedAsset",
        .importCategory = "Texture",
        .name = "Albedo",
        .virtualPath = "/Game/Textures/Albedo.21kb",
        .runtimeLoadable = true,
    }));

    kb::editor::tests::Require(kb::editor::InspectorMaterialTextureSlotFormatter::DisplayName(manager, 0U) == "None", "Material texture formatter should show empty slots as None");
    kb::editor::tests::Require(kb::editor::InspectorMaterialTextureSlotFormatter::DisplayName(manager, textureId.value) == "Albedo", "Material texture formatter should resolve texture asset names");
    kb::editor::tests::Require(!kb::editor::InspectorMaterialTextureSlotFormatter::IsMissing(manager, textureId.value), "Material texture formatter reported a registered texture as missing");
    kb::editor::tests::Require(kb::editor::InspectorMaterialTextureSlotFormatter::IsMissing(manager, 999U), "Material texture formatter should diagnose unresolved texture asset ids");
    kb::editor::tests::Require(kb::editor::InspectorMaterialTextureSlotFormatter::DisplayName(manager, 999U).find("Missing texture asset 999") != std::string::npos, "Material texture formatter should display missing texture ids");
    kb::editor::tests::Require(kb::editor::InspectorMaterialTextureSlotFormatter::Diagnostic("Normal", 999U).find("Normal texture references missing asset 999") != std::string::npos, "Material texture formatter should create a slot-specific missing texture diagnostic");
}

void RunAudioAssetAssignmentTest() {
    const kb::assets::AssetMetadata audioMetadata{
        .type = "ImportedAsset",
        .importCategory = "Audio",
        .name = "Drip",
        .virtualPath = "/Game/Audio/Drip.wav",
    };
    const kb::assets::AssetMetadata meshMetadata{
        .type = "RenderMesh",
        .importCategory = "Mesh",
        .name = "Cube",
        .virtualPath = "/Game/Cube.21kb",
    };
    kb::editor::tests::Require(kb::editor::EditorSceneAudioAssetActions::IsAudioAsset(audioMetadata), "Audio asset action should accept imported audio assets");
    kb::editor::tests::Require(!kb::editor::EditorSceneAudioAssetActions::IsAudioAsset(meshMetadata), "Audio asset action should reject non-audio assets");

    kb::scene::Scene scene;
    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Emitter" });
    scene.Components().AudioSources().Set(entity, kb::scene::AudioSourceComponent{});
    kb::editor::tests::Require(kb::editor::EditorSceneAudioAssetActions::AssignAudioClip(scene, entity, kb::assets::AssetId{ 123 }), "Audio asset action should assign a clip to an Audio Source");

    const kb::scene::AudioSourceComponent* source = scene.Components().AudioSources().TryGet(entity);
    kb::editor::tests::Require(source != nullptr && source->clipAssetId == 123, "Audio Source clip asset id was not assigned");
}

void RunMaterialAssetAssignmentSavesInSceneTest() {
    const std::filesystem::path sceneFile = std::filesystem::temp_directory_path() / "21kb_editor_material_assignment_scene.21kbscene";
    std::error_code cleanupError;
    std::filesystem::remove(sceneFile, cleanupError);
    std::filesystem::remove(sceneFile.string() + ".meta", cleanupError);

    kb::scene::Scene source;
    const kb::scene::SceneEntity entity = source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Mesh" });
    source.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{ .meshAssetId = 77U });

    kb::editor::tests::Require(kb::editor::EditorSceneMaterialAssetActions::IsMaterialAsset(kb::assets::AssetMetadata{ .type = "RenderMaterial" }), "RenderMaterial should be accepted as a material asset");
    kb::editor::tests::Require(kb::editor::EditorSceneMaterialAssetActions::IsMaterialAsset(kb::assets::AssetMetadata{ .type = "RenderMaterialInstance" }), "RenderMaterialInstance should be accepted as a material asset");
    kb::editor::tests::Require(kb::editor::EditorSceneMaterialAssetActions::AssignMaterial(source, entity, kb::assets::AssetId{ 101U }), "Material asset action did not assign a Mesh Renderer material");
    kb::editor::tests::Require(kb::editor::EditorSceneMaterialAssetActions::AssignMaterialSlotOverride(source, entity, 1U, kb::assets::AssetId{ 202U }), "Material asset action did not assign a slot override");
    kb::editor::tests::Require(kb::scene::SceneDocumentService::Save(source, sceneFile, "MaterialAssignment"), "Material assignment scene could not be saved");

    kb::scene::Scene loaded;
    kb::editor::tests::Require(kb::scene::SceneDocumentService::LoadFileIntoScene(loaded, sceneFile), "Material assignment scene could not be loaded");
    const std::vector<kb::scene::SceneEntity> roots = loaded.Hierarchy().RootEntities();
    kb::editor::tests::Require(roots.size() == 1U, "Material assignment scene did not load one root entity");
    const kb::scene::MeshRendererComponent* renderer = loaded.Components().MeshRenderers().TryGet(roots.front());
    kb::editor::tests::Require(renderer != nullptr, "Material assignment scene did not preserve Mesh Renderer");
    kb::editor::tests::Require(renderer->materialAssetId == 101U, "Material assignment scene did not preserve materialAssetId");
    kb::editor::tests::Require(renderer->materialSlotOverrideCount == 2U && renderer->materialSlotAssetIds[1] == 202U, "Material assignment scene did not preserve material slot override");

    kb::editor::tests::Require(kb::editor::EditorSceneMaterialAssetActions::AssignMaterialSlotOverride(source, entity, 1U, {}), "Material asset action did not clear a slot override");
    const kb::scene::MeshRendererComponent* cleared = source.Components().MeshRenderers().TryGet(entity);
    kb::editor::tests::Require(cleared != nullptr && cleared->materialSlotOverrideCount == 0U, "Material asset action did not trim cleared trailing slot overrides");

    std::filesystem::remove(sceneFile, cleanupError);
    std::filesystem::remove(sceneFile.string() + ".meta", cleanupError);
}

void RunMeshRendererMaterialSlotModelTest() {
    kb::render::RenderMeshAssetData mesh;
    mesh.materialNames = { "Body", "Trim" };
    mesh.materialSlots = {
        kb::render::RenderMaterialSlotDesc{ .defaultMaterialAssetId = 201U },
        kb::render::RenderMaterialSlotDesc{ .defaultMaterialAssetId = 202U },
    };

    kb::scene::MeshRendererComponent renderer{ .meshAssetId = 77U, .materialAssetId = 100U };
    renderer.materialSlotOverrideCount = 2U;
    renderer.materialSlotAssetIds[1] = 303U;

    const auto materialName = [](std::uint64_t id) {
        switch (id) {
        case 201U:
            return std::string{ "BodyDefault" };
        case 202U:
            return std::string{ "TrimDefault" };
        case 303U:
            return std::string{ "TrimOverride" };
        default:
            return std::string{ "None" };
        }
    };

    const std::vector<kb::editor::InspectorMeshRendererMaterialSlotRow> rows =
        kb::editor::InspectorMeshRendererMaterialSlotModel::Build(renderer, mesh, materialName);
    kb::editor::tests::Require(rows.size() == 2U, "Mesh Renderer material slot model should expose mesh material slots");
    kb::editor::tests::Require(rows[0].label == "Material Override", "Mesh Renderer material slot model should show the primary override as a simple material field");
    kb::editor::tests::Require(rows[0].value == "None", "Mesh Renderer material slot model should show empty overrides as None");
    kb::editor::tests::Require(rows[1].hasOverride, "Mesh Renderer material slot model should mark explicit slot overrides");
    kb::editor::tests::Require(rows[1].label.find("Material Override 2") != std::string::npos, "Mesh Renderer material slot model should keep extra material overrides readable");
    kb::editor::tests::Require(rows[1].value == "TrimOverride", "Mesh Renderer material slot model should show only the assigned override material");

    kb::scene::MeshRendererComponent noSlotRenderer{ .meshAssetId = 88U, .materialAssetId = 400U };
    const std::vector<kb::editor::InspectorMeshRendererMaterialSlotRow> emptyRows =
        kb::editor::InspectorMeshRendererMaterialSlotModel::Build(noSlotRenderer, kb::render::RenderMeshAssetData{}, materialName);
    kb::editor::tests::Require(emptyRows.empty(), "Mesh Renderer material slot model should not invent slots when mesh has none");

    kb::scene::Scene scene;
    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "SlotDefaultMesh" });
    scene.Components().MeshRenderers().Set(entity, renderer);
    kb::editor::tests::Require(
        kb::editor::EditorSceneMaterialAssetActions::AssignMaterialSlotOverride(scene, entity, 1U, {}),
        "Mesh Renderer material slot clear should accept an empty material asset id");
    const kb::scene::MeshRendererComponent* cleared = scene.Components().MeshRenderers().TryGet(entity);
    kb::editor::tests::Require(cleared != nullptr && cleared->materialSlotOverrideCount == 0U, "Mesh Renderer material slot clear should trim cleared overrides");
    const std::vector<kb::editor::InspectorMeshRendererMaterialSlotRow> clearedRows =
        kb::editor::InspectorMeshRendererMaterialSlotModel::Build(*cleared, mesh, materialName);
    kb::editor::tests::Require(
        clearedRows.size() == 2U && !clearedRows[1].hasOverride && clearedRows[1].value == "None",
        "Mesh Renderer material slot clear should show the override as empty in the UI model");
}

void RunMaterialCreateAssignSaveReloadE2ETest() {
    const std::filesystem::path projectRoot = std::filesystem::temp_directory_path() / "21kb_editor_material_assign_e2e_project";
    const std::filesystem::path sceneFile = projectRoot / "Assets" / "Scenes" / "MaterialAssignment.21kbscene";
    const std::filesystem::path prefabFile = projectRoot / "Assets" / "Prefabs" / "MaterialAssignment.kbprefab";
    std::error_code cleanupError;
    std::filesystem::remove_all(projectRoot, cleanupError);
    std::filesystem::create_directories(sceneFile.parent_path(), cleanupError);
    std::filesystem::create_directories(prefabFile.parent_path(), cleanupError);
    kb::editor::tests::Require(!cleanupError, "Material assignment E2E test could not create project folders");

    kb::scene::Scene source;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::tests::Require(source.Assets().MountProject(projectRoot), "Material assignment E2E test could not mount project assets");

    kb::editor::EditorMaterialAssetAuthoring authoring{ source, browser, console };
    kb::editor::tests::Require(authoring.Create("/Game/Materials"), "Material assignment E2E test could not create material asset");
    const kb::assets::AssetMetadata* materialMetadata = source.Assets().Manager().Registry().FindByPath("/Game/Materials/NewMaterial.kbmat");
    kb::editor::tests::Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Material assignment E2E test did not discover created material metadata");
    const kb::assets::AssetId materialId = materialMetadata->id;

    const kb::scene::SceneObject mesh = source.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "MaterialAssignedMesh" });
    source.Components().MeshRenderers().Set(mesh.Entity(), kb::scene::MeshRendererComponent{ .meshAssetId = 9001U });
    kb::editor::tests::Require(kb::editor::EditorSceneMaterialAssetActions::AssignMaterial(source, mesh.Entity(), materialId), "Material assignment E2E test could not assign material to Mesh Renderer");
    kb::editor::tests::Require(kb::editor::EditorSceneMaterialAssetActions::AssignMaterialSlotOverride(source, mesh.Entity(), 1U, materialId), "Material assignment E2E test could not assign material slot override");
    const kb::scene::MeshRendererComponent* assigned = source.Components().MeshRenderers().TryGet(mesh.Entity());
    kb::editor::tests::Require(assigned != nullptr && assigned->materialAssetId == materialId.value, "Material assignment E2E test did not update source Mesh Renderer");
    kb::editor::tests::Require(
        assigned != nullptr && assigned->materialSlotOverrideCount == 2U && assigned->materialSlotAssetIds[1] == materialId.value,
        "Material assignment E2E test did not update source Mesh Renderer material slot override");

    kb::editor::tests::Require(kb::scene::SceneDocumentService::Save(source, sceneFile, "MaterialAssignment"), "Material assignment E2E test could not save scene");

    kb::scene::Scene reopenedScene;
    kb::editor::tests::Require(reopenedScene.Assets().MountProject(projectRoot), "Material assignment E2E scene reopen could not mount project assets");
    kb::editor::tests::Require(reopenedScene.Assets().Manager().RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()), "Material assignment E2E scene reopen could not register material loader");
    kb::editor::tests::Require(reopenedScene.Assets().Discover() >= 1U, "Material assignment E2E scene reopen did not discover project assets");
    const kb::assets::AssetMetadata* reopenedMaterial = reopenedScene.Assets().Manager().Registry().FindByPath("/Game/Materials/NewMaterial.kbmat");
    kb::editor::tests::Require(reopenedMaterial != nullptr && reopenedMaterial->id == materialId, "Material assignment E2E scene reopen did not preserve material asset id");
    const kb::assets::AssetHandle<kb::render::RenderMaterialAssetData> loadedMaterial = reopenedScene.Assets().Manager().Load<kb::render::RenderMaterialAssetData>(reopenedMaterial->id);
    kb::editor::tests::Require(loadedMaterial.IsLoaded(), "Material assignment E2E scene reopen could not load created material asset");
    kb::editor::tests::Require(kb::scene::SceneDocumentService::LoadFileIntoScene(reopenedScene, sceneFile), "Material assignment E2E test could not reload saved scene");
    const std::vector<kb::scene::SceneEntity> sceneRoots = reopenedScene.Hierarchy().RootEntities();
    kb::editor::tests::Require(sceneRoots.size() == 1U, "Material assignment E2E reloaded scene did not contain one mesh root");
    const kb::scene::MeshRendererComponent* sceneRenderer = reopenedScene.Components().MeshRenderers().TryGet(sceneRoots.front());
    kb::editor::tests::Require(sceneRenderer != nullptr && sceneRenderer->materialAssetId == materialId.value, "Material assignment E2E reloaded scene did not preserve material assignment");
    kb::editor::tests::Require(
        sceneRenderer != nullptr && sceneRenderer->materialSlotOverrideCount == 2U && sceneRenderer->materialSlotAssetIds[1] == materialId.value,
        "Material assignment E2E reloaded scene did not preserve material slot override assignment");

    const kb::scene::ScenePrefabHandle prefabHandle = source.Prefabs().CreateAsset(mesh, "MaterialAssignment", prefabFile);
    kb::editor::tests::Require(prefabHandle.IsValid(), "Material assignment E2E test could not save prefab asset");
    kb::scene::Scene reopenedPrefabScene;
    const kb::scene::ScenePrefabHandle loadedPrefab = reopenedPrefabScene.Prefabs().Load(prefabFile);
    kb::editor::tests::Require(loadedPrefab.IsValid(), "Material assignment E2E test could not reload saved prefab");
    const kb::scene::ScenePrefabInstance prefabInstance = reopenedPrefabScene.Prefabs().Instantiate(loadedPrefab);
    kb::editor::tests::Require(!prefabInstance.Empty() && prefabInstance.ObjectCount() == 1U, "Material assignment E2E reloaded prefab did not instantiate one mesh");
    const kb::scene::MeshRendererComponent* prefabRenderer = reopenedPrefabScene.Components().MeshRenderers().TryGet(prefabInstance.ObjectAt(0U).Entity());
    kb::editor::tests::Require(prefabRenderer != nullptr && prefabRenderer->materialAssetId == materialId.value, "Material assignment E2E reloaded prefab did not preserve material assignment");
    kb::editor::tests::Require(
        prefabRenderer != nullptr && prefabRenderer->materialSlotOverrideCount == 2U && prefabRenderer->materialSlotAssetIds[1] == materialId.value,
        "Material assignment E2E reloaded prefab did not preserve material slot override assignment");

    std::filesystem::remove_all(projectRoot, cleanupError);
}

void RunMaterialRenamePreservesMeshRendererAssignmentTest() {
    const std::filesystem::path projectRoot = std::filesystem::temp_directory_path() / "21kb_editor_material_rename_project";
    std::error_code cleanupError;
    std::filesystem::remove_all(projectRoot, cleanupError);

    kb::scene::Scene scene;
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::EditorConsoleState console;
    kb::editor::tests::Require(scene.Assets().MountProject(projectRoot), "Material rename test could not mount project assets");

    kb::editor::EditorMaterialAssetAuthoring authoring{ scene, browser, console };
    kb::editor::tests::Require(authoring.Create("/Game/Materials"), "Material rename test could not create material asset");
    const kb::assets::AssetMetadata* material = scene.Assets().Manager().Registry().FindByPath("/Game/Materials/NewMaterial.kbmat");
    kb::editor::tests::Require(material != nullptr, "Material rename test did not discover material asset");
    const kb::assets::AssetId materialId = material->id;

    const kb::scene::SceneObject mesh = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "RenamedMaterialMesh" });
    scene.Components().MeshRenderers().Set(mesh.Entity(), kb::scene::MeshRendererComponent{ .meshAssetId = 7001U });
    kb::editor::tests::Require(kb::editor::EditorSceneMaterialAssetActions::AssignMaterial(scene, mesh.Entity(), materialId), "Material rename test could not assign material");

    kb::editor::tests::Require(scene.Assets().Manager().RenameAsset(materialId, "RenamedMaterial"), "Material rename test could not rename material asset");
    const kb::assets::AssetMetadata* renamedById = scene.Assets().Manager().Registry().Find(materialId);
    const kb::assets::AssetMetadata* renamedByPath = scene.Assets().Manager().Registry().FindByPath("/Game/Materials/RenamedMaterial.kbmat");
    kb::editor::tests::Require(renamedById != nullptr, "Material rename should keep metadata addressable by original asset id");
    kb::editor::tests::Require(renamedByPath != nullptr && renamedByPath->id == materialId, "Material rename should keep the original asset id on the renamed path");
    const kb::scene::MeshRendererComponent* renderer = scene.Components().MeshRenderers().TryGet(mesh.Entity());
    kb::editor::tests::Require(renderer != nullptr && renderer->materialAssetId == materialId.value, "Material rename should not change Mesh Renderer material asset id");

    std::filesystem::remove_all(projectRoot, cleanupError);
}

void RunMaterialReferenceFinderReportsMeshRendererUsageTest() {
    kb::scene::Scene scene;
    const kb::assets::AssetId materialId{ 501U };
    const kb::assets::AssetId unusedMaterialId{ 777U };

    const kb::scene::SceneObject mainMaterialMesh = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "MainMaterialMesh" });
    scene.Components().MeshRenderers().Set(mainMaterialMesh.Entity(), kb::scene::MeshRendererComponent{
        .meshAssetId = 11U,
        .materialAssetId = materialId.value,
    });

    const kb::scene::SceneObject slotMaterialMesh = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "SlotMaterialMesh" });
    kb::scene::MeshRendererComponent slotRenderer{ .meshAssetId = 12U };
    slotRenderer.materialSlotAssetIds[2] = materialId.value;
    slotRenderer.materialSlotOverrideCount = 3U;
    scene.Components().MeshRenderers().Set(slotMaterialMesh.Entity(), slotRenderer);

    const std::vector<std::string> references = kb::editor::EditorMaterialReferenceFinder::FindSceneReferences(scene, materialId);
    kb::editor::tests::Require(references.size() == 2U, "Material reference finder should report main material and slot override references");
    kb::editor::tests::Require(std::ranges::any_of(references, [](const std::string& reference) {
        return reference.find("MainMaterialMesh") != std::string::npos && reference.find("Material") != std::string::npos;
    }), "Material reference finder missed main material reference");
    kb::editor::tests::Require(std::ranges::any_of(references, [](const std::string& reference) {
        return reference.find("SlotMaterialMesh") != std::string::npos && reference.find("Slot 2") != std::string::npos;
    }), "Material reference finder missed slot override reference");
    kb::editor::tests::Require(kb::editor::EditorMaterialReferenceFinder::FindSceneReferences(scene, unusedMaterialId).empty(), "Material reference finder should not report unrelated material ids");
}

void RunMaterialEditorStateIndependentFromInspectorSelectionTest() {
    kb::render::RenderMaterialAssetData material{};
    material.documentVersion = kb::render::kRenderMaterialAssetDocumentVersion;
    material.hasExplicitDocumentVersion = true;
    material.materialType = kb::render::kRenderMaterialAssetBuiltInPbrType;
    material.materialTypeVersion = kb::render::kRenderMaterialAssetBuiltInPbrTypeVersion;
    material.hasExplicitMaterialType = true;
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    const kb::assets::AssetId materialId{ 0x21414ULL };
    const kb::assets::AssetId meshId{ 0x21415ULL };

    kb::editor::MaterialEditorState materialEditor;
    materialEditor.Open(materialId, material);
    kb::editor::tests::Require(materialEditor.OpenAssetId() == materialId, "Material Editor state did not store the opened material asset id");
    kb::editor::tests::Require(materialEditor.WorkingCopy().has_value(), "Material Editor state did not capture a material working copy");
    kb::editor::tests::Require(materialEditor.CleanSnapshot().has_value(), "Material Editor state did not capture a clean snapshot");
    kb::editor::tests::Require(!materialEditor.Dirty(), "Material Editor state should open with a clean snapshot");
    kb::editor::tests::Require(!materialEditor.InfoPanelVisible(), "Material Editor info panel should be hidden by default");
    kb::editor::tests::Require(materialEditor.ToggleInfoPanel(), "Material Editor info panel toggle should report a state change");
    kb::editor::tests::Require(materialEditor.InfoPanelVisible(), "Material Editor info panel toggle should show schema details");

    kb::assets::AssetManager manager;
    static_cast<void>(manager.RegisterAsset(kb::assets::AssetMetadata{
        .id = meshId,
        .type = "RenderMesh",
        .name = "StateMesh",
        .virtualPath = "/Game/Meshes/StateMesh.gltf",
        .contentHash = 2U,
        .runtimeLoadable = true,
    }));
    kb::editor::EditorAssetBrowserState browser;
    kb::editor::tests::Require(browser.SelectAsset(meshId, manager), "Material Editor state test could not select a different Inspector asset");
    kb::editor::tests::Require(browser.InspectorAsset() == meshId, "Material Editor state test did not change Inspector asset selection");
    kb::editor::tests::Require(materialEditor.OpenAssetId() == materialId, "Material Editor state should not follow Inspector asset selection");
    kb::editor::tests::Require(materialEditor.SelectNode(1U), "Material Editor state should store selected graph node");
    kb::editor::tests::Require(materialEditor.SelectedNodeId() == 1U, "Material Editor state did not expose selected graph node");
    kb::editor::tests::Require(materialEditor.SelectParameter(kb::editor::InspectorPropertyId::MaterialRoughnessFactor), "Material Editor state should store selected parameter");

    material.desc.roughnessFactor = 0.375F;
    materialEditor.SetWorkingCopy(material);
    kb::editor::tests::Require(materialEditor.Dirty(), "Material Editor state should mark mutated working copy dirty");
    materialEditor.SetDiagnostics({ "Warning test: state diagnostic" }, false);
    kb::editor::tests::Require(!materialEditor.Diagnostics().empty() && !materialEditor.DiagnosticsHaveError(), "Material Editor state should store diagnostics snapshot");
    materialEditor.MarkSaved();
    kb::editor::tests::Require(!materialEditor.Dirty(), "Material Editor state should clear dirty after saving snapshot");
}

void RunEditorMaterialSlotOverrideSyncTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Mesh" });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{ .meshAssetId = 42U });
    kb::editor::tests::Require(kb::editor::EditorSceneMaterialAssetActions::AssignMaterialSlotOverride(scene, entity, 1U, kb::assets::AssetId{ 303U }), "Material asset action did not assign sync slot override");

    kb::render::RenderScene renderScene;
    kb::render::EcsRenderSceneSynchronizer{}.Sync(scene, renderScene);

    std::vector<kb::render::SceneRenderDrawGroup> groups;
    renderScene.BuildDrawGroups(groups);
    kb::editor::tests::Require(groups.size() == 1U && groups[0].instances.size() == 1U, "Editor material slot override sync did not produce one draw group");
    kb::editor::tests::Require(groups[0].instances[0].materialSlotOverrideCount == 2U, "Editor material slot override sync did not propagate override count");
    kb::editor::tests::Require(groups[0].instances[0].materialSlotAssetIds[1] == 303U, "Editor material slot override sync did not propagate override asset id");
}

void RunMaterialAssignmentUndoRedoTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Mesh" });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{ .meshAssetId = 42U });
    const auto currentMaterialId = [&scene]() -> std::uint64_t {
        const std::vector<kb::scene::SceneEntity> roots = scene.Hierarchy().RootEntities();
        kb::editor::tests::Require(roots.size() == 1U, "Material assignment undo test expected one root entity");
        const kb::scene::MeshRendererComponent* renderer = scene.Components().MeshRenderers().TryGet(roots.front());
        kb::editor::tests::Require(renderer != nullptr, "Material assignment undo test lost the Mesh Renderer component");
        return renderer->materialAssetId;
    };

    kb::editor::tests::Require(scene.History().Record("Assign Mesh Material"), "Material assignment undo test could not record scene history");
    kb::editor::tests::Require(kb::editor::EditorSceneMaterialAssetActions::AssignMaterial(scene, entity, kb::assets::AssetId{ 404U }), "Material assignment undo test could not assign material");
    kb::editor::tests::Require(currentMaterialId() == 404U, "Material assignment undo test did not assign the material id");

    kb::editor::tests::Require(scene.History().Undo(), "Material assignment undo test could not undo scene history");
    kb::editor::tests::Require(currentMaterialId() == 0U, "Material assignment undo did not restore the previous material id");

    kb::editor::tests::Require(scene.History().Redo(), "Material assignment undo test could not redo scene history");
    kb::editor::tests::Require(currentMaterialId() == 404U, "Material assignment redo did not restore the assigned material id");
}

void RunMaterialPreviewMeshFactoryTest() {
    const kb::render::RenderMeshAssetData sphere = kb::editor::EditorMaterialPreviewMeshFactory::BuildSphere();
    kb::editor::tests::Require(sphere.desc.vertexCount > 0U, "Material preview sphere did not generate vertices");
    kb::editor::tests::Require(sphere.desc.indexCount > 0U, "Material preview sphere did not generate indices");
    kb::editor::tests::Require(sphere.desc.materialSlotCount == 1U, "Material preview sphere should expose one material slot");
    kb::editor::tests::Require(sphere.bounds.radius > 0.0F, "Material preview sphere did not produce bounds");
    for (std::size_t index = 0U; index < sphere.indices32.size(); index += 3U) {
        kb::editor::tests::Require(TriangleOutwardDot(sphere, index) > 0.0F, "Material preview sphere triangle winding should face outward");
    }

    const kb::render::RenderMeshAssetData cube = kb::editor::EditorMaterialPreviewMeshFactory::BuildCube();
    kb::editor::tests::Require(cube.desc.vertexCount == 24U, "Material preview cube should generate one quad per face");
    kb::editor::tests::Require(cube.desc.indexCount == 36U, "Material preview cube should generate two triangles per face");
    kb::editor::tests::Require(cube.desc.materialSlotCount == 1U, "Material preview cube should expose one material slot");
}

void RunMaterialPreviewSceneBuildsRenderableMaterialTest() {
    const std::filesystem::path materialFile = std::filesystem::temp_directory_path() / "21kb_editor_material_preview_scene.kbmat";
    std::error_code cleanupError;
    std::filesystem::remove(materialFile, cleanupError);

    kb::render::RenderMaterialAssetData material{};
    material.desc.albedoTextureAssetId = 999U;
    kb::editor::tests::Require(kb::render::RenderMaterialAssetWriter::Save(materialFile, material), "Material preview test could not write material fixture");

    const kb::assets::AssetId materialId{ 5151U };
    kb::scene::Scene source;
    static_cast<void>(source.Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
        .id = materialId,
        .type = "RenderMaterial",
        .name = "PreviewMaterial",
        .virtualPath = "/Game/Materials/PreviewMaterial.kbmat",
        .physicalPath = materialFile,
        .contentHash = 1U,
        .runtimeLoadable = true,
    }));

    kb::editor::EditorMaterialPreviewScene preview;
    const kb::scene::Scene& previewScene = preview.SceneFor(source, materialId);
    const kb::editor::EditorMaterialPreviewTelemetry& telemetry = preview.Telemetry();
    kb::editor::tests::Require(telemetry.materialAssetId == materialId, "Material preview telemetry did not preserve material id");
    kb::editor::tests::Require(telemetry.materialMetadataFound, "Material preview telemetry did not find material metadata");
    kb::editor::tests::Require(telemetry.materialLoaded, "Material preview telemetry did not report loaded material");
    kb::editor::tests::Require(telemetry.previewSceneReady, "Material preview telemetry did not report a ready scene");
    kb::editor::tests::Require(telemetry.missingTextureCount == 1U, "Material preview telemetry did not diagnose the missing texture");

    kb::render::RenderScene renderScene;
    kb::render::EcsRenderSceneSynchronizer{}.Sync(previewScene, renderScene);
    std::vector<kb::render::SceneRenderDrawGroup> groups;
    renderScene.BuildDrawGroups(groups);
    kb::editor::tests::Require(groups.size() == 1U, "Material preview scene should produce one draw group");
    kb::editor::tests::Require(groups[0].meshAssetId == kb::editor::EditorMaterialPreviewMeshLoader::PreviewMeshAssetId().value, "Material preview scene did not use the preview mesh asset");
    kb::editor::tests::Require(groups[0].materialAssetId == materialId.value, "Material preview scene did not assign the inspected material");
    kb::editor::tests::Require(groups[0].instances.size() == 1U, "Material preview scene should render one mesh instance");
    kb::editor::tests::Require(renderScene.LightProxyCount() >= 2U, "Material preview scene should provide studio lighting for a visible preview sphere");

    kb::render::SceneRenderer renderer;
    renderer.SetDefaultLightingConfig(kb::render::SceneRenderLightingConfig{
        .environmentMode = kb::render::SceneRenderEnvironmentMode::Hemisphere,
        .environmentDiffuseIntensity = 0.55F,
        .environmentSpecularIntensity = 0.04F,
    });
    const kb::render::SceneRenderSubmitStats lightingStats = renderer.ValidateSceneResources(renderScene);
    kb::editor::tests::Require(lightingStats.sceneLightCount >= 2U, "Material preview renderer validation did not see preview lights");
    kb::editor::tests::Require(lightingStats.submittedForwardLightCount >= 1U, "Material preview renderer validation did not select a forward light");
    kb::editor::tests::Require(lightingStats.submittedEnvironmentLightingCount == 1U, "Material preview renderer validation should keep environment lighting active");
    kb::editor::tests::Require(lightingStats.environmentLightingMode == static_cast<std::uint32_t>(kb::render::SceneRenderEnvironmentMode::Hemisphere) + 1U, "Material preview renderer validation should use hemisphere environment lighting");

    MaterialPreviewHeadlessSurface surface;
    kb::render::DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    kb::render::Renderer submitRenderer;
    submitRenderer.ReserveRuntimeSceneResources(kb::render::Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 1U,
        .cachedMaterials = 1U,
        .frameReferencedMeshes = 1U,
        .frameReferencedMaterials = 1U,
        .scenePassSubmitStats = 2U,
        .renderSceneMeshProxies = 1U,
        .renderSceneLightProxies = 3U,
        .renderSceneDrawGroupKeys = 1U,
        .meshResourceSlots = 1U,
        .materialResourceSlots = 1U,
        .meshBindings = 1U,
        .materialBindings = 1U,
        .syncMeshProxies = 1U,
        .syncLightProxies = 3U,
        .syncTransformCacheEntries = 8U,
        .syncTransformResolvingEntries = 8U,
    });
    kb::editor::tests::Require(submitRenderer.Initialize(surface, &config), "Material preview headless renderer did not initialize");
    kb::editor::tests::Require(submitRenderer.BeginFrame(), "Material preview headless renderer did not begin a frame");
    const kb::render::RenderSceneSubmitDesc submitDesc{
        .target = kb::render::RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = kb::render::RenderViewportDesc{
                .id = kb::render::RenderViewportId{ 1U },
                .extent = kb::render::RenderExtent{ 128U, 128U },
                .viewportIndex = 0U,
            },
        },
        .lightingConfig = kb::render::SceneRenderLightingConfig{
            .environmentMode = kb::render::SceneRenderEnvironmentMode::Hemisphere,
            .environmentDiffuseIntensity = 0.55F,
            .environmentSpecularIntensity = 0.04F,
        },
        .meshPassMode = kb::render::SceneRenderMeshPassMode::OpaqueAndTransparent,
        .shadowPassEnabled = false,
        .postProcessEnabled = false,
        .selectionMaskEnabled = false,
        .selectionOutlineEnabled = false,
        .gpuDrivenRuntimeDispatchEnabled = false,
    };
    kb::editor::tests::Require(submitRenderer.SubmitScene(previewScene, submitDesc), "Material preview headless renderer rejected the preview scene");
    const kb::render::SceneRenderSubmitStats submitStats = submitRenderer.LastSceneSubmitStats();
    kb::editor::tests::Require(submitStats.visibleMeshCount >= 1U, "Material preview headless submit did not keep the sphere visible");
    kb::editor::tests::Require(submitStats.missingMeshBindingCount == 0U, "Material preview headless submit is missing the preview mesh binding");
    kb::editor::tests::Require(submitStats.missingMeshResourceCount == 0U, "Material preview headless submit is missing the preview mesh resource");
    kb::editor::tests::Require(submitStats.unsupportedMeshVertexFormatCount == 0U, "Material preview headless submit rejected the preview mesh vertex format");
    kb::editor::tests::Require(submitStats.missingMaterialBindingCount == 0U, "Material preview headless submit is missing the preview material binding");
    kb::editor::tests::Require(submitStats.missingMaterialResourceCount == 0U, "Material preview headless submit is missing the preview material resource");
    kb::editor::tests::Require(submitStats.submittedMeshCount >= 1U, "Material preview headless submit did not submit the sphere");
    kb::editor::tests::Require(submitStats.submittedDrawCallCount >= 1U, "Material preview headless submit did not issue a draw call");
    submitRenderer.EndFrame();
    submitRenderer.Shutdown();

    const std::uint64_t firstRevision = preview.Revision();
    static_cast<void>(source.Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
        .id = materialId,
        .type = "RenderMaterial",
        .name = "PreviewMaterial",
        .virtualPath = "/Game/Materials/PreviewMaterial.kbmat",
        .physicalPath = materialFile,
        .contentHash = 2U,
        .runtimeLoadable = true,
    }));
    static_cast<void>(preview.SceneFor(source, materialId));
    kb::editor::tests::Require(preview.Revision() > firstRevision, "Material preview scene did not rebuild after material content hash changed");

    std::filesystem::remove(materialFile, cleanupError);
}

void RunMaterialValueFormatterTest() {
    // KBMAT-0201: shared material/value formatters used by both the Inspector and the
    // dedicated Material Editor panel.
    using kb::editor::EditorValueFormatter;
    using kb::editor::MaterialAssetFormatter;
    kb::editor::tests::Require(EditorValueFormatter::FormatFloat(1.0F) == "1", "FormatFloat should trim trailing zeros");
    kb::editor::tests::Require(EditorValueFormatter::FormatFloat(0.25F) == "0.25", "FormatFloat should keep significant decimals");
    kb::editor::tests::Require(EditorValueFormatter::FormatFloat(-0.0F) == "0", "FormatFloat should normalize -0 to 0");
    kb::editor::tests::Require(EditorValueFormatter::FormatUInt64(123ULL) == "123", "FormatUInt64 should render a plain integer");
    kb::editor::tests::Require(MaterialAssetFormatter::AlphaModeName(kb::render::RenderMaterialAlphaMode::Opaque) == "Opaque", "AlphaModeName should render Opaque");
    kb::editor::tests::Require(MaterialAssetFormatter::AlphaModeName(kb::render::RenderMaterialAlphaMode::Mask) == "Mask", "AlphaModeName should render Mask");
    kb::editor::tests::Require(MaterialAssetFormatter::AlphaModeName(kb::render::RenderMaterialAlphaMode::Blend) == "Blend", "AlphaModeName should render Blend");
}

#if defined(_WIN32)
void RunMaterialEditorGraphLayoutAndHitTestTest() {
    const RECT content{0, 0, 440, 540};
    const kb::editor::MaterialEditorPanelLayout layout = kb::editor::MaterialEditorPanelRenderer::ResolveLayout(content);
    kb::editor::tests::Require(layout.graphCanvas.left == content.left, "Material Editor graph should own the full tab width");
    kb::editor::tests::Require(layout.graphCanvas.right == content.right, "Material Editor graph should own the full tab width");
    kb::editor::tests::Require(layout.graphCanvas.top == content.top + kb::editor::MaterialEditorPanelMetrics::HeaderHeight, "Material Editor graph should start directly below the toolbar");
    kb::editor::tests::Require(layout.graphCanvas.bottom == content.bottom, "Material Editor graph should fill the tab height");
    kb::editor::tests::Require(kb::editor::MaterialEditorPanelPointInRect(layout.graphCanvas, layout.previewFrame.left + 2, layout.previewFrame.top + 2), "Material preview should be an overlay inside the graph workspace");
    kb::editor::tests::Require(kb::editor::MaterialEditorPanelPointInRect(layout.graphCanvas, layout.assetBadge.left + 2, layout.assetBadge.top + 2), "Material identity should be an overlay inside the graph workspace");
    kb::editor::tests::Require(layout.infoButton.right <= layout.applyButton.left, "Material Editor Info command should sit directly before Apply To Selection");
    kb::editor::tests::Require(kb::editor::MaterialEditorPanelRenderer::CommandAt(content, layout.infoButton.left + 2, layout.infoButton.top + 2) == kb::editor::MaterialEditorPanelCommand::Info, "Material Editor should hit-test the Info command");
    kb::editor::tests::Require(kb::editor::MaterialEditorPanelRenderer::CommandAt(content, layout.applyButton.left + 2, layout.applyButton.top + 2) == kb::editor::MaterialEditorPanelCommand::ApplyToSelection, "Material Editor should hit-test the Apply To Selection command");
    kb::editor::tests::Require(kb::editor::MaterialEditorPanelRenderer::CommandAt(content, layout.saveButton.left + 2, layout.saveButton.top + 2) == kb::editor::MaterialEditorPanelCommand::Save, "Material Editor should hit-test the Save command");
    kb::editor::tests::Require(kb::editor::MaterialEditorPanelRenderer::CommandAt(content, layout.revertButton.left + 2, layout.revertButton.top + 2) == kb::editor::MaterialEditorPanelCommand::Revert, "Material Editor should hit-test the Revert command");
    kb::editor::tests::Require(kb::editor::MaterialEditorPanelRenderer::CommandAt(content, layout.validateButton.left + 2, layout.validateButton.top + 2) == kb::editor::MaterialEditorPanelCommand::Validate, "Material Editor should hit-test the Validate command");

    kb::render::RenderMaterialGraphDocument graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 240,
        .positionY = 180,
    });
    const std::optional<RECT> outputNode = kb::editor::MaterialEditorPanelRenderer::GraphNodeRect(content, graph, 1U);
    kb::editor::tests::Require(outputNode.has_value(), "Material Editor should resolve a graph node rect");
    kb::editor::tests::Require(outputNode->right > layout.graphCanvas.left + ((layout.graphCanvas.right - layout.graphCanvas.left) / 2), "Material Output should default to the right side of the graph workspace");
    kb::editor::tests::Require(kb::editor::MaterialEditorPanelRenderer::GraphNodeAt(content, graph, outputNode->left + 8, outputNode->top + 8) == 1U, "Material Editor should hit-test the material output node");
    kb::editor::tests::Require(!kb::editor::MaterialEditorPanelRenderer::GraphNodeAt(content, graph, layout.graphCanvas.left + 2, layout.graphCanvas.top + 2).has_value(), "Material Editor graph hit-test should ignore empty canvas space");
    const int outputPinY =
        outputNode->top
        + kb::editor::MaterialEditorPanelMetrics::GraphNodeHeaderHeight
        + kb::editor::MaterialEditorPanelMetrics::GraphNodeBodyTopPadding
        + (kb::editor::MaterialEditorPanelMetrics::GraphNodePinRowHeight / 2);
    kb::editor::tests::Require(
        kb::editor::MaterialEditorPanelRenderer::TextureSlotAt(content, outputNode->left + 12, outputPinY) == kb::editor::EditorMaterialTextureSlot::Albedo,
        "Material Editor should hit-test the Base Color output pin as a texture slot");
    kb::editor::tests::Require(
        kb::editor::MaterialEditorPanelRenderer::TextureSlotAt(content, outputNode->left + 12, outputPinY + (3 * kb::editor::MaterialEditorPanelMetrics::GraphNodePinRowHeight))
            == kb::editor::EditorMaterialTextureSlot::MetallicRoughness,
        "Material Editor should map Metallic/Roughness output pins to the metallic-roughness texture slot");
    kb::editor::tests::Require(!kb::editor::MaterialEditorPanelRenderer::TextureSlotAt(content, layout.graphCanvas.left + 24, layout.graphCanvas.top + 80).has_value(), "Material Editor graph workspace should ignore empty canvas texture drops");

    const kb::editor::MaterialEditorPanelDetailsRows details = kb::editor::MaterialEditorPanelRenderer::DetailsRows(kb::render::GetBuiltInPbrMaterialTypeSchema(), 1U);
    kb::editor::tests::Require(details.title.find("Selected Node #1") != std::string::npos, "Material Editor details should describe selected graph node context");
    kb::editor::tests::Require(std::ranges::any_of(details.parameterRows, [](const std::string& row) { return row.find("baseColor") != std::string::npos; }), "Material Editor details should expose schema-driven baseColor parameter");
    kb::editor::tests::Require(std::ranges::any_of(details.textureSlotRows, [](const std::string& row) { return row.find("Base Color") != std::string::npos && row.find("sRGB") != std::string::npos; }), "Material Editor details should expose schema-driven Base Color texture slot");
    kb::editor::tests::Require(std::ranges::none_of(details.parameterRows, [](const std::string& row) { return row.find("clearcoatFactor") != std::string::npos; }), "Material Editor details should not show unsupported advanced schema rows in MVP details");
}

void RunMaterialEditorParserDiagnosticRowsTest() {
    std::istringstream input{
        "version 1\n"
        "materialType builtin.pbr\n"
        "materialTypeVersion 1\n"
        "unknownMaterialField 7\n"
    };
    const kb::render::RenderMaterialAssetParseResult result = kb::render::RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    const kb::editor::MaterialEditorPanelDiagnosticRows rows = kb::editor::MaterialEditorPanelRenderer::DiagnosticRows(result);
    kb::editor::tests::Require(rows.hasError, "Material Editor diagnostic rows should preserve parser error severity");
    kb::editor::tests::Require(rows.rows.size() == 1U, "Material Editor diagnostic rows should expose parser diagnostics");
    kb::editor::tests::Require(rows.rows[0].find("unknown_field") != std::string::npos, "Material Editor diagnostic rows should include parser diagnostic code");
    kb::editor::tests::Require(rows.rows[0].find("line 4") != std::string::npos, "Material Editor diagnostic rows should include parser line numbers");
    kb::editor::tests::Require(rows.rows[0].find("unknownMaterialField") != std::string::npos, "Material Editor diagnostic rows should include parser field names");
}
#endif

} // namespace

namespace kb::editor::tests {

void RunEditorInspectorTests() {
    RunInspectorTextEditDirtyStateTest();
    RunAudioComponentCatalogTest();
    RunAudioInspectorTextTest();
    RunMaterialTextureSlotDiagnosticTest();
    RunAudioAssetAssignmentTest();
    RunMaterialAssetAssignmentSavesInSceneTest();
    RunMeshRendererMaterialSlotModelTest();
    RunMaterialCreateAssignSaveReloadE2ETest();
    RunMaterialRenamePreservesMeshRendererAssignmentTest();
    RunMaterialReferenceFinderReportsMeshRendererUsageTest();
    RunMaterialEditorStateIndependentFromInspectorSelectionTest();
    RunEditorMaterialSlotOverrideSyncTest();
    RunMaterialAssignmentUndoRedoTest();
    RunMaterialPreviewMeshFactoryTest();
    RunMaterialPreviewSceneBuildsRenderableMaterialTest();
    RunMaterialValueFormatterTest();
#if defined(_WIN32)
    RunMaterialEditorGraphLayoutAndHitTestTest();
    RunMaterialEditorParserDiagnosticRowsTest();
#endif
}

} // namespace kb::editor::tests
