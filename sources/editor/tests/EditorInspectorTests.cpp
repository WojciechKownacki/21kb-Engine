#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "engine/audio/AudioSettings.hpp"
#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "inspection/InspectorAudioTextBuilder.hpp"
#include "inspection/InspectorComponentCatalog.hpp"
#include "inspection/InspectorComponentLabelFormatter.hpp"
#include "scene/EditorSceneAudioAssetActions.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"

#include <algorithm>
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

} // namespace

namespace kb::editor::tests {

void RunEditorInspectorTests() {
    RunAudioComponentCatalogTest();
    RunAudioInspectorTextTest();
    RunAudioAssetAssignmentTest();
}

} // namespace kb::editor::tests
