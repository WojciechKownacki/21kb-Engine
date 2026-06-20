#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/audio/AudioSettings.hpp"
#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "inspection/InspectorAudioTextBuilder.hpp"
#include "inspection/InspectorComponentCatalog.hpp"
#include "inspection/InspectorComponentLabelFormatter.hpp"
#include "inspection/InspectorMaterialTextureSlotFormatter.hpp"
#include "kb/render/scene/EcsRenderSceneSynchronizer.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "scene/EditorSceneAudioAssetActions.hpp"
#include "scene/EditorSceneMaterialAssetActions.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

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

} // namespace

namespace kb::editor::tests {

void RunEditorInspectorTests() {
    RunAudioComponentCatalogTest();
    RunAudioInspectorTextTest();
    RunMaterialTextureSlotDiagnosticTest();
    RunAudioAssetAssignmentTest();
    RunMaterialAssetAssignmentSavesInSceneTest();
    RunEditorMaterialSlotOverrideSyncTest();
}

} // namespace kb::editor::tests
