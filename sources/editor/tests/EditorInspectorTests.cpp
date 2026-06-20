#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/audio/AudioSettings.hpp"
#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "inspection/InspectorAudioTextBuilder.hpp"
#include "inspection/InspectorComponentCatalog.hpp"
#include "inspection/InspectorComponentLabelFormatter.hpp"
#include "inspection/InspectorMaterialTextureSlotFormatter.hpp"
#include "inspection/InspectorPanelState.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/scene/EcsRenderSceneSynchronizer.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "scene/EditorSceneAudioAssetActions.hpp"
#include "scene/EditorSceneMaterialAssetActions.hpp"
#include "scene/material_preview/EditorMaterialPreviewMeshFactory.hpp"
#include "scene/material_preview/EditorMaterialPreviewMeshLoader.hpp"
#include "scene/material_preview/EditorMaterialPreviewScene.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

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

void RunMaterialPreviewMeshFactoryTest() {
    const kb::render::RenderMeshAssetData sphere = kb::editor::EditorMaterialPreviewMeshFactory::BuildSphere();
    kb::editor::tests::Require(sphere.desc.vertexCount > 0U, "Material preview sphere did not generate vertices");
    kb::editor::tests::Require(sphere.desc.indexCount > 0U, "Material preview sphere did not generate indices");
    kb::editor::tests::Require(sphere.desc.materialSlotCount == 1U, "Material preview sphere should expose one material slot");
    kb::editor::tests::Require(sphere.bounds.radius > 0.0F, "Material preview sphere did not produce bounds");

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

} // namespace

namespace kb::editor::tests {

void RunEditorInspectorTests() {
    RunInspectorTextEditDirtyStateTest();
    RunAudioComponentCatalogTest();
    RunAudioInspectorTextTest();
    RunMaterialTextureSlotDiagnosticTest();
    RunAudioAssetAssignmentTest();
    RunMaterialAssetAssignmentSavesInSceneTest();
    RunEditorMaterialSlotOverrideSyncTest();
    RunMaterialPreviewMeshFactoryTest();
    RunMaterialPreviewSceneBuildsRenderableMaterialTest();
}

} // namespace kb::editor::tests
