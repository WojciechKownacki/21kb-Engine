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
#include "engine/scene/SceneRuntime.hpp"
#include "inspection/InspectorAudioTextBuilder.hpp"
#include "inspection/InspectorComponentCatalog.hpp"
#include "inspection/InspectorComponentLabelFormatter.hpp"
#include "inspection/InspectorMeshRendererMaterialSlotModel.hpp"
#include "inspection/InspectorMaterialTextureSlotFormatter.hpp"
#include "inspection/EditorValueFormatter.hpp"
#include "inspection/MaterialAssetFormatter.hpp"
#include "inspection/InspectorAddComponentBrowserModel.hpp"
#include "inspection/InspectorPanelState.hpp"
#include "inspection/InspectorPhysicsModel.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialFunctionAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialParameterCollection.hpp"
#include "kb/render/runtime/RuntimeMaterialResolver.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/scene/EcsRenderSceneSynchronizer.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderer.hpp"
#include "scene/EditorSceneAudioAssetActions.hpp"
#include "scene/EditorSceneMaterialAssetActions.hpp"
#include "scene/EditorSceneMeshAssetActions.hpp"
#include "scene/material/EditorMaterialReferenceFinder.hpp"
#include "scene/material_preview/EditorMaterialNodePreviewBuilder.hpp"
#include "scene/material_preview/EditorMaterialPreviewMeshFactory.hpp"
#include "scene/material_preview/EditorMaterialPreviewMeshLoader.hpp"
#include "scene/material_preview/EditorMaterialPreviewPrimitivePolicy.hpp"
#include "scene/material_preview/EditorMaterialPreviewScene.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "rendering/MaterialPreviewRenderPolicy.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "scene/material/EditorMaterialAssetAuthoring.hpp"
#include "scene/material/EditorMaterialAssetEditCommand.hpp"
#include "scene/material/MaterialEditorState.hpp"
#include "commands/EditorCommandStack.hpp"

#include <algorithm>
#include <array>
#include <bgfx/bgfx.h>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
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
    const auto vertexAt = [&mesh](std::uint32_t index) {
        if (!mesh.tangentVertices.empty()) {
            const auto& vertex = mesh.tangentVertices[index];
            return std::array<float, 3U>{ vertex.x, vertex.y, vertex.z };
        }
        const auto& vertex = mesh.vertices[index];
        return std::array<float, 3U>{ vertex.x, vertex.y, vertex.z };
    };
    const std::array<float, 3U> a = vertexAt(mesh.indices32[indexOffset]);
    const std::array<float, 3U> b = vertexAt(mesh.indices32[indexOffset + 1U]);
    const std::array<float, 3U> c = vertexAt(mesh.indices32[indexOffset + 2U]);
    const std::array<float, 3U> ab{ b[0] - a[0], b[1] - a[1], b[2] - a[2] };
    const std::array<float, 3U> ac{ c[0] - a[0], c[1] - a[1], c[2] - a[2] };
    const std::array<float, 3U> normal{
        (ab[1] * ac[2]) - (ab[2] * ac[1]),
        (ab[2] * ac[0]) - (ab[0] * ac[2]),
        (ab[0] * ac[1]) - (ab[1] * ac[0]),
    };
    const std::array<float, 3U> center{
        (a[0] + b[0] + c[0]) / 3.0F,
        (a[1] + b[1] + c[1]) / 3.0F,
        (a[2] + b[2] + c[2]) / 3.0F,
    };
    return (normal[0] * center[0]) + (normal[1] * center[1]) + (normal[2] * center[2]);
}

[[nodiscard]] bool SphereTangentsFollowUvDirection(const kb::render::RenderMeshAssetData& sphere) {
    std::uint32_t checked = 0U;
    for (const kb::render::RenderStaticMeshVertexP3N3T4UV2& vertex : sphere.tangentVertices) {
        const float radius = std::sqrt((vertex.x * vertex.x) + (vertex.z * vertex.z));
        if (radius < 0.2F) {
            continue;
        }

        const float tangentLength = std::sqrt((vertex.tx * vertex.tx) + (vertex.ty * vertex.ty) + (vertex.tz * vertex.tz));
        if (tangentLength <= 0.0001F) {
            return false;
        }
        const float tx = vertex.tx / tangentLength;
        const float ty = vertex.ty / tangentLength;
        const float tz = vertex.tz / tangentLength;
        const float expectedX = -vertex.z / radius;
        const float expectedZ = vertex.x / radius;
        const float alignment = (tx * expectedX) + (tz * expectedZ);
        if (alignment < 0.96F || std::abs(ty) > 0.08F || vertex.tw >= 0.0F) {
            return false;
        }
        ++checked;
    }
    return checked > 128U;
}

[[nodiscard]] kb::render::RenderMaterialGraphLink MakeInspectorMaterialGraphLink(
    kb::render::RenderMaterialGraphNodeKind fromKind,
    std::uint32_t fromNodeId,
    std::string fromPin,
    kb::render::RenderMaterialGraphNodeKind toKind,
    std::uint32_t toNodeId,
    std::string toPin) {
    kb::render::RenderMaterialGraphLink link{
        .fromNodeId = fromNodeId,
        .fromPinId = kb::render::RenderMaterialGraphStablePinId(fromKind, fromPin, true),
        .fromPin = std::move(fromPin),
        .toNodeId = toNodeId,
        .toPinId = kb::render::RenderMaterialGraphStablePinId(toKind, toPin, false),
        .toPin = std::move(toPin),
    };
    link.id = kb::render::MakeRenderMaterialGraphLinkId(link);
    return link;
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
    kb::editor::tests::Require(kb::editor::InspectorMaterialTextureSlotFormatter::IsMissing(manager, 999U), "KBMAT-UE-0014: Material texture formatter should diagnose unresolved texture asset ids");
    kb::editor::tests::Require(kb::editor::InspectorMaterialTextureSlotFormatter::DisplayName(manager, 999U).find("Missing texture asset 999") != std::string::npos, "KBMAT-UE-0014: Material texture formatter should display missing texture ids");
    kb::editor::tests::Require(kb::editor::InspectorMaterialTextureSlotFormatter::Diagnostic("Normal", 999U).find("Normal texture references missing asset 999") != std::string::npos, "KBMAT-UE-0014: Material texture formatter should create a slot-specific missing texture diagnostic");
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
    kb::editor::tests::Require(kb::editor::EditorSceneMaterialAssetActions::AssignMaterialSlotOverride(source, entity, 1U, kb::assets::AssetId{ 202U }), "Material asset action did not restore slot override before all-slots assignment");
    kb::editor::tests::Require(kb::editor::EditorSceneMaterialAssetActions::AssignMaterialToAllSlots(source, entity, kb::assets::AssetId{ 303U }), "KBMAT-UE-0009: Material all-slots action did not assign material");
    const kb::scene::MeshRendererComponent* allSlots = source.Components().MeshRenderers().TryGet(entity);
    kb::editor::tests::Require(allSlots != nullptr && allSlots->materialAssetId == 303U && allSlots->materialSlotOverrideCount == 0U && allSlots->materialSlotAssetIds[1] == 0U,
        "KBMAT-UE-0009: Material all-slots action should clear previous slot overrides");
    kb::scene::MeshRendererComponent staleOverrides{};
    staleOverrides.materialSlotOverrideCount = 5U;
    staleOverrides.materialSlotAssetIds[0] = 101U;
    staleOverrides.materialSlotAssetIds[2] = 202U;
    staleOverrides.materialSlotAssetIds[4] = 404U;
    kb::editor::EditorSceneMaterialAssetActions::CleanupMaterialSlotOverrides(staleOverrides, 2U);
    kb::editor::tests::Require(staleOverrides.materialSlotOverrideCount == 1U &&
            staleOverrides.materialSlotAssetIds[0] == 101U &&
            staleOverrides.materialSlotAssetIds[2] == 0U &&
            staleOverrides.materialSlotAssetIds[4] == 0U,
        "KBMAT-UE-0010: Material slot cleanup should remove overrides outside the mesh material slot count and trim trailing empty slots");

    std::filesystem::remove(sceneFile, cleanupError);
    std::filesystem::remove(sceneFile.string() + ".meta", cleanupError);
}

void RunMeshRendererMeshAssignmentActionTest() {
    kb::scene::Scene scene;
    const kb::assets::AssetId meshId{ 0x4D455348A5510001ULL };
    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Mesh Target" });
    kb::scene::MeshRendererComponent renderer{};
    renderer.materialAssetId = 0x4D41544C00000001ULL;
    renderer.materialSlotOverrideCount = 3U;
    renderer.materialSlotAssetIds[0] = 0x4D41544C00000002ULL;
    renderer.materialSlotAssetIds[2] = 0x4D41544C00000003ULL;
    scene.Components().MeshRenderers().Set(entity, renderer);

    kb::editor::tests::Require(kb::editor::EditorSceneMeshAssetActions::IsMeshAsset(kb::assets::AssetMetadata{ .type = "RenderMesh", .importCategory = "Model" }), "RenderMesh model asset should be accepted as mesh");
    kb::editor::tests::Require(kb::editor::EditorSceneMeshAssetActions::IsMeshAsset(kb::assets::AssetMetadata{ .type = "RenderMesh" }), "RenderMesh asset should be accepted as mesh without import category");
    kb::editor::tests::Require(kb::editor::EditorSceneMeshAssetActions::IsMeshAsset(kb::assets::AssetMetadata{ .importCategory = "Mesh" }), "Mesh import category should be accepted as mesh");
    kb::editor::tests::Require(!kb::editor::EditorSceneMeshAssetActions::IsMeshAsset(kb::assets::AssetMetadata{ .type = "RenderMaterial" }), "Non-mesh asset should be rejected as mesh");
    kb::editor::tests::Require(kb::editor::EditorSceneMeshAssetActions::AssignMesh(scene, entity, meshId), "Mesh Renderer mesh action did not assign a mesh asset");
    const kb::scene::MeshRendererComponent* assigned = scene.Components().MeshRenderers().TryGet(entity);
    kb::editor::tests::Require(assigned != nullptr && assigned->meshAssetId == meshId.value, "Mesh Renderer mesh action did not store meshAssetId");
    kb::editor::tests::Require(assigned->materialAssetId == renderer.materialAssetId, "Mesh Renderer mesh action cleared the primary material override");
    kb::editor::tests::Require(assigned->materialSlotOverrideCount == renderer.materialSlotOverrideCount, "Mesh Renderer mesh action changed material slot override count");
    kb::editor::tests::Require(assigned->materialSlotAssetIds[0] == renderer.materialSlotAssetIds[0] && assigned->materialSlotAssetIds[2] == renderer.materialSlotAssetIds[2],
        "Mesh Renderer mesh action cleared manual material slot overrides");
    kb::editor::tests::Require(kb::editor::EditorSceneMeshAssetActions::AssignMesh(scene, entity, {}), "Mesh Renderer mesh action did not clear meshAssetId");
    const kb::scene::MeshRendererComponent* cleared = scene.Components().MeshRenderers().TryGet(entity);
    kb::editor::tests::Require(cleared != nullptr && cleared->meshAssetId == 0U, "Mesh Renderer mesh action did not clear meshAssetId");
    kb::editor::tests::Require(cleared->materialAssetId == renderer.materialAssetId && cleared->materialSlotAssetIds[2] == renderer.materialSlotAssetIds[2],
        "Mesh Renderer mesh clear action should preserve manual material overrides");
}

void RunMeshRendererMaterialSlotModelTest() {
    kb::render::RenderMeshAssetData mesh;
    mesh.materialNames = { "Body", "Trim" };
    mesh.materialSlots = {
        kb::render::RenderMaterialSlotDesc{ .defaultMaterialAssetId = 201U },
        kb::render::RenderMaterialSlotDesc{ .defaultMaterialAssetId = 202U },
    };
    mesh.sections = {
        kb::render::RenderMeshSectionDesc{ .materialSlot = 0U },
        kb::render::RenderMeshSectionDesc{ .materialSlot = 1U },
        kb::render::RenderMeshSectionDesc{ .materialSlot = 0U },
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
    const auto materialStatus = [](std::uint64_t id) {
        switch (id) {
        case 201U:
            return std::string{ "Built-in PBR | runtime ready" };
        case 202U:
            return std::string{ "Graph-backed | type reference ok | diagnostics ok | artifact ready #9001" };
        case 303U:
            return std::string{ "Graph-backed | type reference ok | diagnostics error x1 | artifact pending" };
        default:
            return std::string{ "None" };
        }
    };

    const std::vector<kb::editor::InspectorMeshRendererMaterialSlotRow> rows =
        kb::editor::InspectorMeshRendererMaterialSlotModel::Build(renderer, mesh, materialName, materialStatus);
    kb::editor::tests::Require(rows.size() == 2U, "Mesh Renderer material slot model should expose mesh material slots");
    kb::editor::tests::Require(rows[0].slotName == "Body" && rows[0].importedSourceName == "Body", "KBMAT-UE-0011: Mesh Renderer material slot model should expose the slot name and imported source name");
    kb::editor::tests::Require(rows[0].defaultMaterialName == "BodyDefault", "KBMAT-UE-0011: Mesh Renderer material slot model should expose the default material name");
    kb::editor::tests::Require(rows[0].activeMaterialAssetId == 201U && rows[0].activeMaterialName == "BodyDefault", "KBMAT-GRAPH-0404: slot model should expose the active default material");
    kb::editor::tests::Require(rows[0].activeMaterialStatus == "Built-in PBR | runtime ready", "KBMAT-GRAPH-0404: slot model should expose built-in PBR material runtime status");
    kb::editor::tests::Require(rows[0].sectionsUsingSlot == "0, 2" && rows[0].sectionIndices.size() == 2U, "KBMAT-UE-0011: Mesh Renderer material slot model should expose sections using a slot");
    kb::editor::tests::Require(rows[0].label == "Slot 1 Override (Body)", "KBMAT-UE-0011: Mesh Renderer material slot model should label the primary slot override with its imported source name");
    kb::editor::tests::Require(rows[0].value == "None", "Mesh Renderer material slot model should show empty overrides as None");
    kb::editor::tests::Require(rows[1].hasOverride, "Mesh Renderer material slot model should mark explicit slot overrides");
    kb::editor::tests::Require(rows[1].slotName == "Trim" && rows[1].defaultMaterialName == "TrimDefault", "KBMAT-UE-0011: Mesh Renderer material slot model should expose extra slot names and defaults");
    kb::editor::tests::Require(rows[1].activeMaterialAssetId == 303U && rows[1].activeMaterialName == "TrimOverride", "KBMAT-GRAPH-0404: slot model should expose the active override material");
    kb::editor::tests::Require(
        rows[1].activeMaterialStatus.find("Graph-backed") != std::string::npos && rows[1].activeMaterialStatus.find("diagnostics error") != std::string::npos,
        "KBMAT-GRAPH-0404: slot model should expose graph-backed diagnostic status for active overrides");
    kb::editor::tests::Require(rows[1].sectionsUsingSlot == "1", "KBMAT-UE-0011: Mesh Renderer material slot model should list the section using an extra slot");
    kb::editor::tests::Require(rows[1].label.find("Slot 2 Override") != std::string::npos, "Mesh Renderer material slot model should keep extra material overrides readable");
    kb::editor::tests::Require(rows[1].value == "TrimOverride", "Mesh Renderer material slot model should show only the assigned override material");

    kb::scene::MeshRendererComponent noSlotRenderer{ .meshAssetId = 88U, .materialAssetId = 400U };
    const std::vector<kb::editor::InspectorMeshRendererMaterialSlotRow> emptyRows =
        kb::editor::InspectorMeshRendererMaterialSlotModel::Build(noSlotRenderer, kb::render::RenderMeshAssetData{}, materialName, materialStatus);
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
        kb::editor::InspectorMeshRendererMaterialSlotModel::Build(*cleared, mesh, materialName, materialStatus);
    kb::editor::tests::Require(
        clearedRows.size() == 2U && !clearedRows[1].hasOverride && clearedRows[1].value == "None",
        "Mesh Renderer material slot clear should show the override as empty in the UI model");
    kb::editor::tests::Require(
        clearedRows[1].activeMaterialAssetId == 202U && clearedRows[1].activeMaterialStatus.find("artifact ready") != std::string::npos,
        "KBMAT-GRAPH-0404: clearing an override should expose the default slot material status");
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
    kb::editor::tests::Require(materialEditor.MaterialDiffRows().empty(), "KBMAT-WORKFLOW-DIFF: Clean material editor snapshots should not report material diff rows");
    const auto findEditorParameter = [&materialEditor](std::string_view stableId) -> const kb::editor::MaterialEditorParameter* {
        const auto found = std::ranges::find_if(materialEditor.Parameters(), [stableId](const kb::editor::MaterialEditorParameter& parameter) {
            return parameter.stableId == stableId;
        });
        return found == materialEditor.Parameters().end() ? nullptr : &*found;
    };
    const kb::editor::MaterialEditorParameter* roughnessParameter = findEditorParameter("roughnessFactor");
    kb::editor::tests::Require(roughnessParameter != nullptr, "KBMAT-UE-0003: Material Editor parameter model should include roughnessFactor");
    kb::editor::tests::Require(roughnessParameter->type == kb::render::RenderMaterialParameterType::Scalar &&
            roughnessParameter->group == kb::editor::MaterialEditorParameterGroup::Core &&
            roughnessParameter->value.kind == kb::editor::MaterialEditorParameterValueKind::Scalar &&
            roughnessParameter->defaultValue.kind == kb::editor::MaterialEditorParameterValueKind::Scalar,
        "KBMAT-UE-0003: roughnessFactor parameter should expose type, group, value and default value");
    kb::editor::tests::Require(roughnessParameter->range.has_value() && roughnessParameter->range->min == 0.0F && roughnessParameter->range->max == 1.0F,
        "KBMAT-UE-0003: roughnessFactor parameter should expose schema range");
    kb::editor::tests::Require(roughnessParameter->enabled && roughnessParameter->overrideEnabled,
        "KBMAT-UE-0003: supported source material parameters should be enabled and overrideable in the working model");
    const kb::editor::MaterialEditorParameter* normalTextureParameter = findEditorParameter("normalTextureAssetId");
    kb::editor::tests::Require(normalTextureParameter != nullptr &&
            normalTextureParameter->type == kb::render::RenderMaterialParameterType::Texture &&
            normalTextureParameter->group == kb::editor::MaterialEditorParameterGroup::Texture &&
            normalTextureParameter->value.kind == kb::editor::MaterialEditorParameterValueKind::TextureAsset &&
            normalTextureParameter->expectedTextureColorSpace == kb::render::RenderMaterialTextureColorSpace::Linear &&
            normalTextureParameter->enabled,
        "KBMAT-UE-0003: Material Editor parameter model should expose supported texture slots");
    const kb::editor::MaterialEditorParameter* clearcoatParameter = findEditorParameter("clearcoatFactor");
    kb::editor::tests::Require(clearcoatParameter != nullptr && !clearcoatParameter->enabled,
        "KBMAT-UE-0003: unsupported advanced parameters should remain visible but disabled");
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
    {
        const std::vector<std::string> diffRows = materialEditor.MaterialDiffRows();
        kb::editor::tests::Require(std::ranges::any_of(diffRows, [](const std::string& row) {
            return row.find("Roughness") != std::string::npos && row.find("0.375") != std::string::npos;
        }), "KBMAT-WORKFLOW-DIFF: Material diff should expose changed scalar PBR fields");
    }
    materialEditor.SetDiagnostics({ "Warning test: state diagnostic" }, false);
    kb::editor::tests::Require(!materialEditor.Diagnostics().empty() && !materialEditor.DiagnosticsHaveError(), "Material Editor state should store diagnostics snapshot");
    materialEditor.RevertToCleanSnapshot();
    kb::editor::tests::Require(!materialEditor.Dirty(), "Material Editor state should clear dirty after reverting to the clean snapshot");
    kb::editor::tests::Require(materialEditor.WorkingCopy().has_value() && !kb::editor::tests::NearlyEqual(materialEditor.WorkingCopy()->desc.roughnessFactor, 0.375F),
        "Material Editor state should restore the clean working copy on revert");
    materialEditor.SetWorkingCopy(*materialEditor.CleanSnapshot());
    kb::editor::tests::Require(!materialEditor.Dirty(), "Material Editor state should not mark a clean-equivalent working copy dirty");
    material.desc.roughnessFactor = 0.5F;
    material.desc.normalTextureAssetId = 777U;
    materialEditor.SetWorkingCopy(material);
    roughnessParameter = findEditorParameter("roughnessFactor");
    normalTextureParameter = findEditorParameter("normalTextureAssetId");
    kb::editor::tests::Require(roughnessParameter != nullptr && kb::editor::tests::NearlyEqual(roughnessParameter->value.numbers[0], 0.5F),
        "KBMAT-UE-0003: Material Editor parameter model should refresh scalar values from the working copy");
    kb::editor::tests::Require(normalTextureParameter != nullptr && normalTextureParameter->value.assetId == 777U,
        "KBMAT-UE-0003: Material Editor parameter model should refresh texture asset ids from the working copy");
    {
        const std::vector<std::string> diffRows = materialEditor.MaterialDiffRows();
        kb::editor::tests::Require(std::ranges::any_of(diffRows, [](const std::string& row) {
            return row.find("Normal texture") != std::string::npos && row.find("777") != std::string::npos;
        }), "KBMAT-WORKFLOW-DIFF: Material diff should expose changed texture slots");
    }
    kb::render::RenderMaterialAssetData graphEdited = *materialEditor.WorkingCopy();
    graphEdited.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 9U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = -120,
        .positionY = 88,
    });
    graphEdited.graph.links.push_back(MakeInspectorMaterialGraphLink(
        kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        9U,
        "rgba",
        kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        1U,
        "baseColor"));
    materialEditor.SetWorkingCopy(std::move(graphEdited));
    {
        const std::vector<std::string> diffRows = materialEditor.MaterialDiffRows();
        kb::editor::tests::Require(std::ranges::any_of(diffRows, [](const std::string& row) {
            return row.find("Added node #9") != std::string::npos;
        }), "KBMAT-WORKFLOW-DIFF: Material diff should expose added graph nodes");
        kb::editor::tests::Require(std::ranges::any_of(diffRows, [](const std::string& row) {
            return row.find("Added link") != std::string::npos && row.find("9:rgba->1:baseColor") != std::string::npos;
        }), "KBMAT-WORKFLOW-DIFF: Material diff should expose added graph links");
    }
    materialEditor.MarkSaved();
    kb::editor::tests::Require(!materialEditor.Dirty(), "Material Editor state should clear dirty after saving snapshot");
    kb::editor::tests::Require(materialEditor.CleanSnapshot().has_value() && kb::editor::tests::NearlyEqual(materialEditor.CleanSnapshot()->desc.roughnessFactor, 0.5F),
        "Material Editor state should promote the saved working copy to the clean snapshot");
}

void RunMaterialEditorGraphBackedSchemaParameterModelTest() {
    kb::render::RenderMaterialGraphDocument graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ParameterColor,
        .positionX = -260,
        .positionY = 32,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "tintColor",
            .displayName = "Tint Color",
            .group = kb::render::RenderMaterialParameterGroup::Surface,
            .defaultValueHint = "0.25 0.5 0.75 1",
            .overrideSupported = false,
            .editorOrder = 10U,
        },
    });
    graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 3U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ParameterScalar,
        .positionX = -260,
        .positionY = 150,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "roughnessFactor",
            .displayName = "Roughness",
            .group = kb::render::RenderMaterialParameterGroup::Surface,
            .defaultValueHint = "0.42",
            .hasRange = true,
            .rangeMin = 0.0F,
            .rangeMax = 1.0F,
            .overrideSupported = true,
            .editorOrder = 20U,
        },
    });
    graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 4U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ParameterTexture,
        .positionX = -260,
        .positionY = 268,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "albedo",
            .displayName = "Albedo",
            .group = kb::render::RenderMaterialParameterGroup::Surface,
            .textureRole = "baseColor",
            .expectedTextureColorSpace = kb::render::RenderMaterialTextureColorSpace::Srgb,
            .overrideSupported = true,
            .editorOrder = 30U,
        },
    });
    graph.links.push_back(MakeInspectorMaterialGraphLink(
        kb::render::RenderMaterialGraphNodeKind::ParameterColor,
        2U,
        "rgba",
        kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        1U,
        "baseColor"));
    graph.links.push_back(MakeInspectorMaterialGraphLink(
        kb::render::RenderMaterialGraphNodeKind::ParameterScalar,
        3U,
        "value",
        kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        1U,
        "roughness"));

    const kb::render::RenderMaterialGraphMaterialTypeBuildResult typeResult =
        kb::render::BuildRenderMaterialGraphMaterialTypeDocument(
            graph,
            "graph.editor.tint",
            1U,
            kb::render::RenderMaterialGraphBuildContext{
                .assetId = 0x0302U,
                .sourcePath = "/Game/Materials/TintGraph.kbmaterialgraph",
            });
    kb::editor::tests::Require(typeResult.Succeeded() && typeResult.document.has_value(),
        "KBMAT-GRAPH-0302: Graph-backed Material Type schema fixture should generate");

    kb::render::RenderMaterialAssetData material{};
    material.materialType = typeResult.document->stableTypeId;
    material.materialTypeVersion = typeResult.document->version;
    material.hasExplicitMaterialType = true;
    material.hasExplicitMaterialTypeVersion = true;
    material.desc.roughnessFactor = 0.42F;
    material.desc.albedoTextureAssetId = 0xA11BEDU;
    material.graphParameterValues.push_back(kb::render::RenderMaterialGraphParameterValue{
        .stableId = "tintColor",
        .type = kb::render::RenderMaterialParameterType::Color,
        .numbers = { 0.8F, 0.7F, 0.6F, 1.0F },
    });
    material.graph = graph;
    material.graph.storageModel = "inline-kbmat";

    kb::editor::MaterialEditorState materialEditor;
    materialEditor.Open(kb::assets::AssetId{ 0x0302U }, material, typeResult.document->schema);

    const auto findEditorParameter = [&materialEditor](std::string_view stableId) -> const kb::editor::MaterialEditorParameter* {
        const auto found = std::ranges::find_if(materialEditor.Parameters(), [stableId](const kb::editor::MaterialEditorParameter& parameter) {
            return parameter.stableId == stableId;
        });
        return found == materialEditor.Parameters().end() ? nullptr : &*found;
    };
    const kb::editor::MaterialEditorParameter* tint = findEditorParameter("tintColor");
    kb::editor::tests::Require(tint != nullptr &&
            tint->type == kb::render::RenderMaterialParameterType::Color &&
            tint->group == kb::editor::MaterialEditorParameterGroup::Surface &&
            tint->value.kind == kb::editor::MaterialEditorParameterValueKind::Color &&
            tint->defaultValue.kind == kb::editor::MaterialEditorParameterValueKind::Color &&
            kb::editor::tests::NearlyEqual(tint->value.numbers[0], 0.8F) &&
            kb::editor::tests::NearlyEqual(tint->defaultValue.numbers[2], 0.75F) &&
            !tint->overrideEnabled &&
            tint->enabled,
        "KBMAT-GRAPH-0302: Material Editor should expose graph color parameters with schema defaults and override flags");
    const kb::editor::MaterialEditorParameter* roughness = findEditorParameter("roughnessFactor");
    kb::editor::tests::Require(roughness != nullptr &&
            roughness->group == kb::editor::MaterialEditorParameterGroup::Surface &&
            roughness->value.kind == kb::editor::MaterialEditorParameterValueKind::Scalar &&
            kb::editor::tests::NearlyEqual(roughness->value.numbers[0], 0.42F) &&
            kb::editor::tests::NearlyEqual(roughness->defaultValue.numbers[0], 0.42F) &&
            roughness->range.has_value() &&
            roughness->overrideEnabled,
        "KBMAT-GRAPH-0302: Material Editor should expose graph scalar parameters from active Material Type schema");
    const kb::editor::MaterialEditorParameter* albedo = findEditorParameter("albedoTextureAssetId");
    kb::editor::tests::Require(albedo != nullptr &&
            albedo->type == kb::render::RenderMaterialParameterType::Texture &&
            albedo->group == kb::editor::MaterialEditorParameterGroup::Texture &&
            albedo->value.assetId == 0xA11BEDU &&
            albedo->expectedTextureColorSpace == kb::render::RenderMaterialTextureColorSpace::Srgb &&
            albedo->overrideEnabled,
        "KBMAT-GRAPH-0302: Material Editor should expose graph texture role/color policy");

    const kb::editor::MaterialEditorPanelDetailsRows rows = kb::editor::MaterialEditorPanelRenderer::DetailsRows(materialEditor.Parameters(), 0U);
    kb::editor::tests::Require(std::ranges::any_of(rows.parameterRows, [](const std::string& row) {
            return row.find("Surface  Color  Tint Color") != std::string::npos &&
                row.find("default 0.25, 0.5, 0.75") != std::string::npos &&
                row.find("override disabled") != std::string::npos;
        }),
        "KBMAT-GRAPH-0302: Material Editor details should render graph parameter group, default and override flag");
    kb::editor::tests::Require(std::ranges::any_of(rows.textureSlotRows, [](const std::string& row) {
            return row.find("Texture  Albedo  sRGB") != std::string::npos &&
                row.find("albedoTextureAssetId") != std::string::npos &&
                row.find("override on") != std::string::npos;
        }),
        "KBMAT-GRAPH-0302: Material Editor details should render graph texture role/color policy and override flag");
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

void RunMaterialAssignmentPathRenderSyncTest() {
    kb::scene::Scene scene;
    const kb::assets::AssetId builtInPbrMaterialId{ 101U };
    const kb::assets::AssetId previousSlotMaterialId{ 202U };
    const kb::assets::AssetId graphBackedMaterialId{ 303U };
    const kb::assets::AssetId graphBackedSlotMaterialId{ 404U };
    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "AssignmentPathMesh" });
    const auto currentEntity = [&scene]() -> kb::scene::SceneEntity {
        const std::vector<kb::scene::SceneEntity> roots = scene.Hierarchy().RootEntities();
        kb::editor::tests::Require(roots.size() == 1U, "KBMAT-UE-0013: Assignment path test expected one root entity");
        return roots.front();
    };
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = 42U,
        .materialAssetId = builtInPbrMaterialId.value,
    });
    kb::editor::tests::Require(kb::editor::EditorSceneMaterialAssetActions::IsMaterialAsset(kb::assets::AssetMetadata{
            .id = graphBackedMaterialId,
            .type = "RenderMaterial",
            .virtualPath = "/Game/Materials/GraphBacked.kbmat",
        }),
        "KBMAT-GRAPH-0401: Graph-backed .kbmat must be accepted by the Mesh Renderer material assignment path");
    const kb::assets::AssetMetadata rawGraphMetadata{
        .id = kb::assets::AssetId{ 0x4752415048ULL },
        .type = "RenderMaterialGraph",
        .virtualPath = "/Game/Materials/RawGraph.kbmaterialgraph",
    };
    const kb::assets::AssetMetadata rawGraphAliasMetadata{
        .id = kb::assets::AssetId{ 0x4752415049ULL },
        .type = "MaterialGraph",
        .importCategory = "MaterialGraph",
        .virtualPath = "/Game/Materials/RawGraphAlias.kbmaterialgraph",
    };
    static_cast<void>(scene.Assets().Manager().RegisterAsset(rawGraphMetadata));
    static_cast<void>(scene.Assets().Manager().RegisterAsset(rawGraphAliasMetadata));
    kb::editor::tests::Require(!kb::editor::EditorSceneMaterialAssetActions::IsMaterialAsset(rawGraphMetadata) &&
            kb::editor::EditorSceneMaterialAssetActions::IsMaterialGraphAsset(rawGraphMetadata),
        "KBMAT-GRAPH-0402: Raw Material Graph assets must not be accepted as Mesh Renderer materials");
    kb::editor::tests::Require(!kb::editor::EditorSceneMaterialAssetActions::IsMaterialAsset(rawGraphAliasMetadata) &&
            kb::editor::EditorSceneMaterialAssetActions::IsMaterialGraphAsset(rawGraphAliasMetadata),
        "KBMAT-GRAPH-0402: Raw Material Graph aliases must not be accepted as Mesh Renderer materials");
    const std::string graphDropMessage{ kb::editor::EditorSceneMaterialAssetActions::MaterialGraphAssignmentRejectionMessage() };
    kb::editor::tests::Require(graphDropMessage.find("Create Material From Graph") != std::string::npos &&
            graphDropMessage.find(".kbmat") != std::string::npos,
        "KBMAT-GRAPH-0402: Raw Material Graph rejection should tell the user to create a .kbmat first");
    kb::editor::tests::Require(!kb::editor::EditorSceneMaterialAssetActions::IsMaterialAsset(kb::assets::AssetMetadata{ .type = "RenderTexture" }),
        "KBMAT-UE-0013: Material assignment path should reject non-material asset metadata");
    kb::editor::tests::Require(!kb::editor::EditorSceneMaterialAssetActions::AssignMaterial(scene, entity, rawGraphMetadata.id),
        "KBMAT-GRAPH-0506: Raw Material Graph asset id must not be saved as the primary Mesh Renderer material");
    kb::editor::tests::Require(!kb::editor::EditorSceneMaterialAssetActions::AssignMaterialSlotOverride(scene, entity, 1U, rawGraphMetadata.id),
        "KBMAT-GRAPH-0506: Raw Material Graph asset id must not be saved as a Mesh Renderer slot override");
    kb::editor::tests::Require(!kb::editor::EditorSceneMaterialAssetActions::AssignMaterial(scene, entity, rawGraphAliasMetadata.id),
        "KBMAT-GRAPH-0506: Raw MaterialGraph alias asset id must not be saved as the primary Mesh Renderer material");
    kb::editor::tests::Require(!kb::editor::EditorSceneMaterialAssetActions::AssignMaterialSlotOverride(scene, entity, 1U, rawGraphAliasMetadata.id),
        "KBMAT-GRAPH-0506: Raw MaterialGraph alias asset id must not be saved as a Mesh Renderer slot override");
    const kb::scene::MeshRendererComponent* rejectedGraphAssignment = scene.Components().MeshRenderers().TryGet(entity);
    kb::editor::tests::Require(rejectedGraphAssignment != nullptr &&
            rejectedGraphAssignment->materialAssetId == builtInPbrMaterialId.value &&
            rejectedGraphAssignment->materialSlotOverrideCount == 0U &&
            rejectedGraphAssignment->materialSlotAssetIds[1] == 0U,
        "KBMAT-GRAPH-0506: Raw Material Graph rejection should leave Mesh Renderer material state unchanged");

    kb::render::RenderScene renderScene;
    kb::render::EcsRenderSceneSynchronizer synchronizer;
    synchronizer.Sync(scene, renderScene);
    renderScene.ClearDirty();
    static_cast<void>(scene.Runtime().Update(0.016F));

    kb::editor::tests::Require(kb::editor::EditorSceneMaterialAssetActions::AssignMaterialSlotOverride(scene, entity, 1U, previousSlotMaterialId),
        "KBMAT-UE-0013: Could not seed a slot override before all-slots assignment");
    static_cast<void>(scene.Runtime().Update(0.016F));
    kb::editor::tests::Require(scene.History().Record("Drop Material To All Slots"), "KBMAT-UE-0013: Could not record all-slots material assignment");
    kb::editor::tests::Require(kb::editor::EditorSceneMaterialAssetActions::AssignMaterialToAllSlots(scene, entity, graphBackedMaterialId),
        "KBMAT-GRAPH-0401: Graph-backed .kbmat should assign as the primary Mesh Renderer material");
    const kb::scene::MeshRendererComponent* allSlots = scene.Components().MeshRenderers().TryGet(entity);
    kb::editor::tests::Require(allSlots != nullptr && allSlots->materialAssetId == graphBackedMaterialId.value && allSlots->materialSlotOverrideCount == 0U,
        "KBMAT-GRAPH-0401: Graph-backed all-slots assignment should clear previous slot overrides");
    synchronizer.SyncMeshRendererUpdates(scene, renderScene);
    const kb::render::MeshRenderProxy* allSlotsProxy = renderScene.FindMeshByEntity(entity.Id());
    kb::editor::tests::Require(allSlotsProxy != nullptr && kb::render::HasDirtyFlag(allSlotsProxy->dirty, kb::render::RenderProxyDirtyFlag::Material),
        "KBMAT-UE-0013: All-slots assignment should dirty the render material proxy");
    std::vector<kb::render::SceneRenderDrawGroup> groups;
    renderScene.BuildDrawGroups(groups);
    kb::editor::tests::Require(groups.size() == 1U && groups[0].materialAssetId == graphBackedMaterialId.value,
        "KBMAT-GRAPH-0401: All-slots assignment should rebuild draw groups with the graph-backed material");

    kb::editor::tests::Require(scene.History().Undo(), "KBMAT-UE-0013: Could not undo all-slots material assignment");
    const kb::scene::MeshRendererComponent* allSlotsUndo = scene.Components().MeshRenderers().TryGet(currentEntity());
    kb::editor::tests::Require(allSlotsUndo != nullptr && allSlotsUndo->materialAssetId == builtInPbrMaterialId.value &&
            allSlotsUndo->materialSlotOverrideCount == 2U &&
            allSlotsUndo->materialSlotAssetIds[1] == previousSlotMaterialId.value,
        "KBMAT-UE-0013: Undo should restore the previous primary material and slot override");
    kb::editor::tests::Require(scene.History().Redo(), "KBMAT-UE-0013: Could not redo all-slots material assignment");
    const kb::scene::MeshRendererComponent* allSlotsRedo = scene.Components().MeshRenderers().TryGet(currentEntity());
    kb::editor::tests::Require(allSlotsRedo != nullptr && allSlotsRedo->materialAssetId == graphBackedMaterialId.value && allSlotsRedo->materialSlotOverrideCount == 0U,
        "KBMAT-GRAPH-0401: Redo should reapply graph-backed all-slots assignment");

    synchronizer.Sync(scene, renderScene);
    renderScene.ClearDirty();
    static_cast<void>(scene.Runtime().Update(0.016F));
    const kb::scene::SceneEntity slotEntity = currentEntity();
    kb::editor::tests::Require(scene.History().Record("Drop Material To Slot"), "KBMAT-UE-0013: Could not record slot material assignment");
    kb::editor::tests::Require(kb::editor::EditorSceneMaterialAssetActions::AssignMaterialSlotOverride(scene, slotEntity, 1U, graphBackedSlotMaterialId),
        "KBMAT-GRAPH-0401: Graph-backed .kbmat should assign as a Mesh Renderer slot override");
    const kb::scene::MeshRendererComponent* slotAssigned = scene.Components().MeshRenderers().TryGet(slotEntity);
    kb::editor::tests::Require(slotAssigned != nullptr && slotAssigned->materialSlotOverrideCount == 2U && slotAssigned->materialSlotAssetIds[1] == graphBackedSlotMaterialId.value,
        "KBMAT-GRAPH-0401: Graph-backed slot assignment should store the slot override");
    synchronizer.SyncMeshRendererUpdates(scene, renderScene);
    const kb::render::MeshRenderProxy* slotProxy = renderScene.FindMeshByEntity(slotEntity.Id());
    kb::editor::tests::Require(slotProxy != nullptr && kb::render::HasDirtyFlag(slotProxy->dirty, kb::render::RenderProxyDirtyFlag::Material),
        "KBMAT-UE-0013: Slot assignment should dirty the render material proxy");
    groups.clear();
    renderScene.BuildDrawGroups(groups);
    kb::editor::tests::Require(groups.size() == 1U && groups[0].instances.size() == 1U &&
            groups[0].instances[0].materialSlotOverrideCount == 2U &&
            groups[0].instances[0].materialSlotAssetIds[1] == graphBackedSlotMaterialId.value,
        "KBMAT-GRAPH-0401: Slot assignment should rebuild draw groups with the graph-backed slot override");
}

void RunMaterialAssignmentUndoRedoTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Mesh" });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{ .meshAssetId = 42U });
    const auto currentEntity = [&scene]() -> kb::scene::SceneEntity {
        const std::vector<kb::scene::SceneEntity> roots = scene.Hierarchy().RootEntities();
        kb::editor::tests::Require(roots.size() == 1U, "Material assignment undo test expected one root entity");
        return roots.front();
    };
    const auto currentRenderer = [&scene, &currentEntity]() -> const kb::scene::MeshRendererComponent& {
        const kb::scene::MeshRendererComponent* renderer = scene.Components().MeshRenderers().TryGet(currentEntity());
        kb::editor::tests::Require(renderer != nullptr, "Material assignment undo test lost the Mesh Renderer component");
        return *renderer;
    };
    const auto currentMaterialId = [&currentRenderer]() -> std::uint64_t {
        return currentRenderer().materialAssetId;
    };

    kb::editor::tests::Require(scene.History().Record("Assign Mesh Material"), "Material assignment undo test could not record scene history");
    kb::editor::tests::Require(kb::editor::EditorSceneMaterialAssetActions::AssignMaterial(scene, entity, kb::assets::AssetId{ 404U }), "Material assignment undo test could not assign material");
    kb::editor::tests::Require(currentMaterialId() == 404U, "Material assignment undo test did not assign the material id");

    kb::editor::tests::Require(scene.History().Undo(), "Material assignment undo test could not undo scene history");
    kb::editor::tests::Require(currentMaterialId() == 0U, "Material assignment undo did not restore the previous material id");

    kb::editor::tests::Require(scene.History().Redo(), "Material assignment undo test could not redo scene history");
    kb::editor::tests::Require(currentMaterialId() == 404U, "Material assignment redo did not restore the assigned material id");

    kb::editor::tests::Require(scene.History().Record("Assign Mesh Material Slot"), "Material assignment undo test could not record slot scene history");
    kb::editor::tests::Require(kb::editor::EditorSceneMaterialAssetActions::AssignMaterialSlotOverride(scene, currentEntity(), 2U, kb::assets::AssetId{ 505U }), "Material assignment undo test could not assign material slot override");
    const kb::scene::MeshRendererComponent& slotAssigned = currentRenderer();
    kb::editor::tests::Require(slotAssigned.materialSlotOverrideCount == 3U && slotAssigned.materialSlotAssetIds[2] == 505U, "Material assignment undo test did not assign the slot material id");

    kb::editor::tests::Require(scene.History().Undo(), "Material assignment undo test could not undo slot scene history");
    const kb::scene::MeshRendererComponent& slotUndone = currentRenderer();
    kb::editor::tests::Require(slotUndone.materialAssetId == 404U, "Material assignment slot undo should preserve the primary material assignment");
    kb::editor::tests::Require(slotUndone.materialSlotOverrideCount == 0U && slotUndone.materialSlotAssetIds[2] == 0U, "Material assignment slot undo did not restore the previous slot override state");

    kb::editor::tests::Require(scene.History().Redo(), "Material assignment undo test could not redo slot scene history");
    const kb::scene::MeshRendererComponent& slotRedone = currentRenderer();
    kb::editor::tests::Require(slotRedone.materialSlotOverrideCount == 3U && slotRedone.materialSlotAssetIds[2] == 505U, "Material assignment slot redo did not restore the slot material id");

    kb::editor::tests::Require(scene.History().Record("Clear Mesh Material Slot"), "Material assignment undo test could not record slot clear scene history");
    kb::editor::tests::Require(kb::editor::EditorSceneMaterialAssetActions::AssignMaterialSlotOverride(scene, currentEntity(), 2U, {}), "Material assignment undo test could not clear material slot override");
    const kb::scene::MeshRendererComponent& slotCleared = currentRenderer();
    kb::editor::tests::Require(slotCleared.materialSlotOverrideCount == 0U && slotCleared.materialSlotAssetIds[2] == 0U, "Material assignment slot clear did not trim cleared trailing overrides");

    kb::editor::tests::Require(scene.History().Undo(), "Material assignment undo test could not undo slot clear scene history");
    const kb::scene::MeshRendererComponent& slotClearUndone = currentRenderer();
    kb::editor::tests::Require(slotClearUndone.materialSlotOverrideCount == 3U && slotClearUndone.materialSlotAssetIds[2] == 505U, "Material assignment slot clear undo did not restore the slot override");

    kb::editor::tests::Require(scene.History().Redo(), "Material assignment undo test could not redo slot clear scene history");
    const kb::scene::MeshRendererComponent& slotClearRedone = currentRenderer();
    kb::editor::tests::Require(slotClearRedone.materialAssetId == 404U, "Material assignment slot clear redo should preserve the primary material assignment");
    kb::editor::tests::Require(slotClearRedone.materialSlotOverrideCount == 0U && slotClearRedone.materialSlotAssetIds[2] == 0U, "Material assignment slot clear redo did not restore the cleared slot state");
}

void RunMaterialPreviewMeshFactoryTest() {
    const kb::render::RenderMeshAssetData sphere = kb::editor::EditorMaterialPreviewMeshFactory::BuildSphere();
    kb::editor::tests::Require(sphere.desc.vertexCount > 0U, "Material preview sphere did not generate vertices");
    kb::editor::tests::Require(sphere.desc.vertexFormat == kb::render::RenderVertexFormat::P3N3T4UV2 && !sphere.tangentVertices.empty(), "Material preview sphere must provide tangents for the PBR shader");
    kb::editor::tests::Require(SphereTangentsFollowUvDirection(sphere), "Material preview sphere tangents must follow UV direction for normal maps");
    kb::editor::tests::Require(sphere.desc.indexCount > 0U, "Material preview sphere did not generate indices");
    kb::editor::tests::Require(sphere.desc.materialSlotCount == 1U, "Material preview sphere should expose one material slot");
    kb::editor::tests::Require(sphere.bounds.radius > 0.0F, "Material preview sphere did not produce bounds");
    for (std::size_t index = 0U; index < sphere.indices32.size(); index += 3U) {
        kb::editor::tests::Require(TriangleOutwardDot(sphere, index) > 0.0F, "Material preview sphere triangle winding should face outward");
    }

    const kb::render::RenderMeshAssetData cube = kb::editor::EditorMaterialPreviewMeshFactory::BuildCube();
    kb::editor::tests::Require(cube.desc.vertexCount == 24U, "Material preview cube should generate one quad per face");
    kb::editor::tests::Require(cube.desc.vertexFormat == kb::render::RenderVertexFormat::P3N3T4UV2 && !cube.tangentVertices.empty(), "Material preview cube must provide tangents for the PBR shader");
    kb::editor::tests::Require(cube.desc.indexCount == 36U, "Material preview cube should generate two triangles per face");
    kb::editor::tests::Require(cube.desc.materialSlotCount == 1U, "Material preview cube should expose one material slot");

    const kb::render::RenderMeshAssetData cylinder = kb::editor::EditorMaterialPreviewMeshFactory::BuildCylinder();
    kb::editor::tests::Require(cylinder.desc.vertexCount > 96U, "KBMAT-PREVIEW-0001: Material preview cylinder should generate side and cap vertices");
    kb::editor::tests::Require(cylinder.desc.vertexFormat == kb::render::RenderVertexFormat::P3N3T4UV2 && !cylinder.tangentVertices.empty(), "KBMAT-PREVIEW-0001: Material preview cylinder must provide tangents for the PBR shader");
    kb::editor::tests::Require(cylinder.desc.indexCount == 576U, "KBMAT-PREVIEW-0001: Material preview cylinder should generate sides plus capped ends");
    kb::editor::tests::Require(cylinder.desc.materialSlotCount == 1U && cylinder.bounds.radius > 0.0F, "KBMAT-PREVIEW-0001: Material preview cylinder should expose a slot and bounds");

    const kb::render::RenderMeshAssetData plane = kb::editor::EditorMaterialPreviewMeshFactory::BuildPlane();
    kb::editor::tests::Require(plane.desc.vertexCount == 4U && plane.desc.indexCount == 6U, "KBMAT-UE-0008: Material preview plane should generate one quad");
    kb::editor::tests::Require(plane.desc.vertexFormat == kb::render::RenderVertexFormat::P3N3T4UV2 && !plane.tangentVertices.empty(), "Material preview plane must provide tangents for the PBR shader");
    kb::editor::tests::Require(plane.desc.materialSlotCount == 1U && plane.bounds.radius > 0.0F, "KBMAT-UE-0008: Material preview plane should expose a slot and bounds");

    const kb::editor::EditorMaterialPreviewPrimitivePolicy spherePolicy = kb::editor::EditorMaterialPreviewPrimitivePolicy::Sphere();
    const kb::editor::EditorMaterialPreviewPrimitivePolicy cylinderPolicy = kb::editor::EditorMaterialPreviewPrimitivePolicy::Cylinder();
    const kb::editor::EditorMaterialPreviewPrimitivePolicy cubePolicy = kb::editor::EditorMaterialPreviewPrimitivePolicy::Cube();
    const kb::editor::EditorMaterialPreviewPrimitivePolicy planePolicy = kb::editor::EditorMaterialPreviewPrimitivePolicy::Plane();
    const kb::editor::EditorMaterialPreviewPrimitivePolicy customPolicy = kb::editor::EditorMaterialPreviewPrimitivePolicy::CustomMesh(kb::assets::AssetId{ 0xC0570B1EC0570B1EULL });
    const kb::editor::EditorMaterialPreviewPrimitivePolicy fallbackPolicy = kb::editor::EditorMaterialPreviewPrimitivePolicy::CustomMesh({});
    kb::editor::tests::Require(spherePolicy.meshAssetId == kb::editor::EditorMaterialPreviewMeshLoader::PreviewMeshAssetId(), "KBMAT-UE-0008: Sphere policy should keep the legacy preview mesh id");
    kb::editor::tests::Require(cylinderPolicy.meshAssetId.IsValid() && cylinderPolicy.meshAssetId != spherePolicy.meshAssetId, "KBMAT-PREVIEW-0001: Cylinder policy should use a distinct generated mesh id");
    kb::editor::tests::Require(cubePolicy.meshAssetId.IsValid() && cubePolicy.meshAssetId != spherePolicy.meshAssetId, "KBMAT-UE-0008: Cube policy should use a distinct generated mesh id");
    kb::editor::tests::Require(planePolicy.meshAssetId.IsValid() && planePolicy.meshAssetId != spherePolicy.meshAssetId, "KBMAT-UE-0008: Plane policy should use a distinct generated mesh id");
    kb::editor::tests::Require(customPolicy.kind == kb::editor::EditorMaterialPreviewPrimitiveKind::CustomMesh && customPolicy.meshAssetId == customPolicy.customMeshAssetId, "KBMAT-UE-0008: Custom policy should preserve the selected mesh id");
    kb::editor::tests::Require(fallbackPolicy.kind == kb::editor::EditorMaterialPreviewPrimitiveKind::Fallback && fallbackPolicy.meshAssetId.IsValid(), "KBMAT-UE-0008: Invalid custom policy should fall back to a generated mesh");
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

    kb::render::RenderMaterialAssetData workingCopyMaterial = material;
    workingCopyMaterial.desc.baseColor[0] = 0.21F;
    workingCopyMaterial.desc.baseColor[1] = 0.42F;
    workingCopyMaterial.desc.baseColor[2] = 0.84F;
    workingCopyMaterial.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    kb::editor::EditorMaterialPreviewScene graphPreview;
    const kb::scene::Scene& graphPreviewScene = graphPreview.SceneFor(source, materialId, &workingCopyMaterial);
    const kb::render::ResolvedRuntimeMaterialAsset graphPreviewMaterial =
        kb::render::RuntimeMaterialResolver{}.ResolveAsset(graphPreviewScene.Assets().Manager(), materialId);
    kb::editor::tests::Require(graphPreviewMaterial.resolved, "KBMAT-GRAPH-0107: Graph preview should resolve the working-copy material through the runtime resolver");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(graphPreviewMaterial.material.desc.baseColor[0], 0.21F) &&
            kb::editor::tests::NearlyEqual(graphPreviewMaterial.material.desc.baseColor[1], 0.42F) &&
            kb::editor::tests::NearlyEqual(graphPreviewMaterial.material.desc.baseColor[2], 0.84F),
        "KBMAT-GRAPH-0107: Graph preview should use the Material Editor working copy instead of the saved material file");
    const std::uint64_t graphPreviewRevision = graphPreview.Revision();
    workingCopyMaterial.graph.nodes.front().positionX += 320;
    workingCopyMaterial.graph.nodes.front().positionY -= 160;
    workingCopyMaterial.graph.nodes.front().parameter.displayName = "Editor-only output label";
    workingCopyMaterial.graph.comments.push_back(kb::render::RenderMaterialGraphCommentBox{
        .id = 9U, .positionX = 0, .positionY = 0, .width = 640, .height = 320, .text = "Editor organization",
    });
    static_cast<void>(graphPreview.SceneFor(source, materialId, &workingCopyMaterial));
    kb::editor::tests::Require(graphPreview.Revision() == graphPreviewRevision,
        "P2.6: layout, labels and comments must not rebuild the production material preview scene");
    workingCopyMaterial.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 100,
        .positionY = 100,
    });
    static_cast<void>(graphPreview.SceneFor(source, materialId, &workingCopyMaterial));
    kb::editor::tests::Require(graphPreview.Revision() > graphPreviewRevision,
        "KBMAT-GRAPH-0107: Graph preview should rebuild when the working-copy graph changes");

    const kb::assets::AssetId functionId{ 0x515101U };
    const kb::assets::AssetId collectionId{ 0x515102U };
    const std::filesystem::path dependencyRoot =
        std::filesystem::temp_directory_path() / "21kb_editor_material_preview_dependencies";
    kb::assets::AssetMetadata functionMetadata{
        .id = functionId,
        .type = std::string{ kb::render::kRenderMaterialFunctionAssetType },
        .name = "PreviewFunction",
        .virtualPath = "/Game/Materials/PreviewFunction.kbmatfn",
        .physicalPath = dependencyRoot / "PreviewFunction.kbmatfn",
        .contentHash = 11U,
        .runtimeLoadable = true,
    };
    kb::assets::AssetMetadata collectionMetadata{
        .id = collectionId,
        .type = std::string{ kb::render::kRenderMaterialParameterCollectionAssetType },
        .name = "PreviewGlobals",
        .virtualPath = "/Game/Materials/PreviewGlobals.kbmatpc",
        .physicalPath = dependencyRoot / "PreviewGlobals.kbmatpc",
        .contentHash = 21U,
        .runtimeLoadable = true,
    };
    kb::editor::tests::Require(source.Assets().Manager().RegisterAsset(functionMetadata) &&
            source.Assets().Manager().RegisterAsset(collectionMetadata),
        "P1.15: preview dependency fixtures must register function and parameter-collection assets");
    workingCopyMaterial.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 20U,
        .kind = kb::render::RenderMaterialGraphNodeKind::MaterialFunctionCall,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = std::to_string(functionId.value),
        },
    });
    workingCopyMaterial.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 21U,
        .kind = kb::render::RenderMaterialGraphNodeKind::CollectionParameter,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{
            .stableId = "PreviewTint",
            .defaultValueHint = std::to_string(collectionId.value),
        },
    });
    kb::editor::EditorMaterialPreviewScene dependencyPreview;
    static_cast<void>(dependencyPreview.SceneFor(source, materialId, &workingCopyMaterial));
    const std::uint64_t dependencyRevision = dependencyPreview.Revision();
    std::ranges::reverse(workingCopyMaterial.graph.nodes);
    static_cast<void>(dependencyPreview.SceneFor(source, materialId, &workingCopyMaterial));
    kb::editor::tests::Require(dependencyPreview.Revision() == dependencyRevision,
        "P1.15/P2.6: dependency traversal and semantic preview hash must be invariant to graph node ordering");
    functionMetadata.contentHash = 12U;
    kb::editor::tests::Require(source.Assets().Manager().RegisterAsset(functionMetadata),
        "P1.15: function dependency content hash must be refreshable");
    static_cast<void>(dependencyPreview.SceneFor(source, materialId, &workingCopyMaterial));
    kb::editor::tests::Require(dependencyPreview.Revision() > dependencyRevision,
        "P1.15: changing a Material Function content hash must invalidate working-copy preview");
    const std::uint64_t functionDependencyRevision = dependencyPreview.Revision();
    collectionMetadata.contentHash = 22U;
    kb::editor::tests::Require(source.Assets().Manager().RegisterAsset(collectionMetadata),
        "P1.15: parameter-collection dependency content hash must be refreshable");
    static_cast<void>(dependencyPreview.SceneFor(source, materialId, &workingCopyMaterial));
    kb::editor::tests::Require(dependencyPreview.Revision() > functionDependencyRevision,
        "P1.15: changing a Material Parameter Collection content hash must invalidate working-copy preview");

    kb::render::RenderScene renderScene;
    kb::render::EcsRenderSceneSynchronizer{}.Sync(previewScene, renderScene);
    std::vector<kb::render::SceneRenderDrawGroup> groups;
    renderScene.BuildDrawGroups(groups);
    kb::editor::tests::Require(groups.size() == 1U, "Material preview scene should produce one draw group");
    kb::editor::tests::Require(groups[0].meshAssetId == kb::editor::EditorMaterialPreviewMeshLoader::PreviewMeshAssetId().value, "Material preview scene did not use the preview mesh asset");
    kb::editor::tests::Require(groups[0].materialAssetId == materialId.value, "Material preview scene did not assign the inspected material");
    kb::editor::tests::Require(groups[0].instances.size() == 1U, "Material preview scene should render one mesh instance");
    kb::editor::tests::Require(renderScene.LightProxyCount() == 0U, "Material preview scene should rely on the neutral preview render policy instead of baked scene lights");

    kb::editor::EditorMaterialPreviewScene cubePreview;
    const kb::editor::EditorMaterialPreviewPrimitivePolicy cubePolicy = kb::editor::EditorMaterialPreviewPrimitivePolicy::Cube();
    kb::editor::tests::Require(cubePreview.SetPrimitivePolicy(cubePolicy), "KBMAT-UE-0008: Material preview scene should accept cube primitive policy");
    const kb::scene::Scene& cubePreviewScene = cubePreview.SceneFor(source, materialId);
    kb::render::RenderScene cubeRenderScene;
    kb::render::EcsRenderSceneSynchronizer{}.Sync(cubePreviewScene, cubeRenderScene);
    std::vector<kb::render::SceneRenderDrawGroup> cubeGroups;
    cubeRenderScene.BuildDrawGroups(cubeGroups);
    kb::editor::tests::Require(cubeGroups.size() == 1U && cubeGroups[0].meshAssetId == cubePolicy.meshAssetId.value, "KBMAT-UE-0008: Material preview scene should use the selected primitive mesh id");

    kb::editor::EditorMaterialPreviewScene cylinderPreview;
    const kb::editor::EditorMaterialPreviewPrimitivePolicy cylinderPolicy = kb::editor::EditorMaterialPreviewPrimitivePolicy::Cylinder();
    kb::editor::tests::Require(cylinderPreview.SetPrimitivePolicy(cylinderPolicy), "KBMAT-PREVIEW-0001: Material preview scene should accept cylinder primitive policy");
    const kb::scene::Scene& cylinderPreviewScene = cylinderPreview.SceneFor(source, materialId);
    kb::render::RenderScene cylinderRenderScene;
    kb::render::EcsRenderSceneSynchronizer{}.Sync(cylinderPreviewScene, cylinderRenderScene);
    std::vector<kb::render::SceneRenderDrawGroup> cylinderGroups;
    cylinderRenderScene.BuildDrawGroups(cylinderGroups);
    kb::editor::tests::Require(cylinderGroups.size() == 1U && cylinderGroups[0].meshAssetId == cylinderPolicy.meshAssetId.value, "KBMAT-PREVIEW-0001: Material preview scene should use the cylinder mesh id");

    const kb::assets::AssetId customMeshId{ 0xC0570B1EC0570B1EULL };
    static_cast<void>(source.Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
        .id = customMeshId,
        .type = "RenderMesh",
        .name = "BrowserMesh",
        .virtualPath = "/Game/Meshes/BrowserMesh.21kb",
        .physicalPath = "__test_browser_mesh__",
        .runtimeLoadable = true,
    }));
    kb::editor::EditorMaterialPreviewScene customPreview;
    const kb::editor::EditorMaterialPreviewPrimitivePolicy customMeshPolicy = kb::editor::EditorMaterialPreviewPrimitivePolicy::CustomMesh(customMeshId);
    kb::editor::tests::Require(customPreview.SetPrimitivePolicy(customMeshPolicy), "KBMAT-PREVIEW-0002: Material preview scene should accept a mesh selected from the asset browser");
    const kb::scene::Scene& customPreviewScene = customPreview.SceneFor(source, materialId);
    kb::render::RenderScene customRenderScene;
    kb::render::EcsRenderSceneSynchronizer{}.Sync(customPreviewScene, customRenderScene);
    std::vector<kb::render::SceneRenderDrawGroup> customGroups;
    customRenderScene.BuildDrawGroups(customGroups);
    kb::editor::tests::Require(customGroups.size() == 1U && customGroups[0].meshAssetId == customMeshId.value, "KBMAT-PREVIEW-0002: Material preview scene should render the browser-selected mesh asset id");

    kb::editor::EditorMaterialPreviewScene settingsPreview;
    const kb::editor::EditorMaterialPreviewSceneSettings highContrastSettings =
        kb::editor::EditorMaterialPreviewSceneSettingsForPreset(kb::editor::EditorMaterialPreviewLightingPreset::HighContrast);
    const std::uint64_t settingsRevision = settingsPreview.Revision();
    kb::editor::tests::Require(settingsPreview.SetSceneSettings(highContrastSettings), "KBMAT-PREVIEW-0003: Material preview scene should accept scene lighting settings");
    static_cast<void>(settingsPreview.SceneFor(source, materialId));
    kb::editor::tests::Require(settingsPreview.Revision() > settingsRevision && settingsPreview.SceneSettings().lightingPreset == kb::editor::EditorMaterialPreviewLightingPreset::HighContrast,
        "KBMAT-PREVIEW-0003: Material preview scene settings should rebuild the runtime preview scene");
    const kb::render::SceneRenderLightingConfig highContrastLighting = kb::editor::MaterialPreviewRenderPolicy::NeutralPbrLightingConfig(highContrastSettings);
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(highContrastLighting.editorPreviewKeyLightIntensity, highContrastSettings.keyLightIntensity) &&
            kb::editor::tests::NearlyEqual(highContrastLighting.ambientIntensity, highContrastSettings.ambientIntensity),
        "KBMAT-PREVIEW-0003: Material preview lighting policy should use the authored scene settings");
    const kb::render::ScenePostProcessSettings highContrastPostProcess = kb::editor::MaterialPreviewRenderPolicy::StableExposurePostProcessSettings(highContrastSettings);
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(highContrastPostProcess.outputTransform.exposureStops, highContrastSettings.exposureStops),
        "KBMAT-PREVIEW-0003: Material preview exposure should use the authored scene settings");

    kb::render::SceneRenderer renderer;
    const kb::render::SceneRenderLightingConfig previewLighting = kb::editor::MaterialPreviewRenderPolicy::NeutralPbrLightingConfig();
    kb::editor::tests::Require(previewLighting.lightingPath == kb::render::SceneRenderLightingPath::Forward,
        "Material preview lighting should default to the Forward project lighting path");
    const kb::render::SceneRenderLightingConfig deferredPreviewLighting = kb::editor::MaterialPreviewRenderPolicy::NeutralPbrLightingConfig(
        kb::editor::EditorMaterialPreviewSceneSettings::Defaults(),
        kb::project::ProjectSceneLightingPath::Deferred);
    kb::editor::tests::Require(deferredPreviewLighting.lightingPath == kb::render::SceneRenderLightingPath::Deferred,
        "Material preview lighting should inherit Deferred from Project Settings");
    kb::editor::EditorMaterialPreviewSceneSettings normalDebugSettings = kb::editor::EditorMaterialPreviewSceneSettings::Defaults();
    normalDebugSettings.normalDebugView = true;
    const kb::render::SceneRenderLightingConfig normalDebugPreviewLighting =
        kb::editor::MaterialPreviewRenderPolicy::NeutralPbrLightingConfig(normalDebugSettings);
    kb::editor::tests::Require(normalDebugPreviewLighting.debugView == kb::render::SceneRenderDebugView::GBufferNormal,
        "Material preview normal debug mode should request the GBuffer normal view");
    const kb::render::SceneRenderLightingConfig forwardPlusPreviewLighting = kb::editor::MaterialPreviewRenderPolicy::NeutralPbrLightingConfig(
        kb::editor::EditorMaterialPreviewSceneSettings::Defaults(),
        kb::project::ProjectSceneLightingPath::ForwardPlus);
    kb::editor::tests::Require(forwardPlusPreviewLighting.lightingPath == kb::render::SceneRenderLightingPath::ClusteredForwardPlus &&
            forwardPlusPreviewLighting.maxForwardLights == kb::render::kMaxSceneForwardPlusLights,
        "Material preview lighting should inherit Forward+ from Project Settings with the expanded light budget");
    kb::editor::tests::Require(previewLighting.editorPreviewKeyLightEnabled, "KBMAT-0610: Material preview lighting must enable a neutral key light");
    kb::editor::tests::Require(previewLighting.editorPreviewKeyLightIntensity > 0.0F, "KBMAT-0610: Material preview key light must have positive intensity");
    kb::editor::tests::Require(previewLighting.environmentMode == kb::render::SceneRenderEnvironmentMode::Hemisphere, "KBMAT-0610: Material preview must use a neutral hemisphere/IBL fallback");
    kb::editor::tests::Require(previewLighting.environmentDiffuseIntensity > 0.0F, "KBMAT-0610: Material preview diffuse environment fallback must be active");
    kb::editor::tests::Require(previewLighting.environmentSpecularIntensity > 0.0F, "KBMAT-0610: Material preview specular environment fallback must be active");
    kb::editor::tests::Require(!previewLighting.shadowsEnabled, "KBMAT-0610: Material preview lighting should avoid unstable preview shadows");
    renderer.SetDefaultLightingConfig(previewLighting);
    const kb::render::SceneRenderSubmitStats lightingStats = renderer.ValidateSceneResources(renderScene);
    kb::editor::tests::Require(lightingStats.sceneLightCount == 0U, "KBMAT-0610: Material preview should not depend on scene-authored lights");
    kb::editor::tests::Require(lightingStats.submittedForwardLightCount == 1U, "KBMAT-0610: Material preview renderer validation did not submit the neutral key light");
    kb::editor::tests::Require(lightingStats.submittedEnvironmentLightingCount == 1U, "Material preview renderer validation should keep environment lighting active");
    kb::editor::tests::Require(lightingStats.environmentLightingMode == static_cast<std::uint32_t>(kb::render::SceneRenderEnvironmentMode::Hemisphere) + 1U, "Material preview renderer validation should use hemisphere environment lighting");
    const kb::render::ScenePostProcessSettings previewPostProcess = kb::editor::MaterialPreviewRenderPolicy::StableExposurePostProcessSettings();
    kb::editor::tests::Require(previewPostProcess.autoExposureMetering == kb::render::ScenePostProcessSettings::AutoExposureMeteringMode::Manual, "KBMAT-0610: Material preview exposure metering must be fixed");
    kb::editor::tests::Require(!previewPostProcess.outputTransform.autoExposure.enabled, "KBMAT-0610: Material preview auto exposure must be disabled");
    kb::editor::tests::Require(!previewPostProcess.outputTransform.autoExposure.temporalAdaptationEnabled, "KBMAT-0610: Material preview temporal exposure adaptation must be disabled");
    kb::editor::tests::Require(previewPostProcess.outputTransform.exposureStops == 0.0F, "KBMAT-0610: Material preview fixed exposure should be neutral");

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
        .lightingConfig = previewLighting,
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
    const kb::render::SceneRenderResourceMap* previewResourceMap = submitRenderer.SceneResourceMap();
    const kb::render::RenderResourceRegistry* previewResources = submitRenderer.SceneResources();
    kb::editor::tests::Require(previewResourceMap != nullptr && previewResources != nullptr, "KBMAT-1007: Material preview renderer did not expose runtime resources");
    const kb::render::RenderMaterialHandle previewMaterialHandle = previewResourceMap->ResolveMaterial(materialId.value);
    const kb::render::RenderMaterialResource* previewMaterialResource = previewResources->FindMaterial(previewMaterialHandle);
    kb::editor::tests::Require(previewMaterialHandle.IsValid() && previewMaterialResource != nullptr, "KBMAT-1007: Material preview renderer did not bind the inspected material resource");
    const kb::render::RenderMaterialResource previewMaterialSnapshot = *previewMaterialResource;
    submitRenderer.EndFrame();
    submitRenderer.Shutdown();

    kb::scene::Scene runtimeScene;
    kb::assets::AssetManager& runtimeManager = runtimeScene.Assets().Manager();
    static_cast<void>(runtimeManager.RegisterLoader(std::make_unique<kb::editor::EditorMaterialPreviewMeshLoader>()));
    static_cast<void>(runtimeManager.RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()));
    static_cast<void>(runtimeManager.RegisterAsset(kb::assets::AssetMetadata{
        .id = materialId,
        .type = "RenderMaterial",
        .name = "PreviewMaterial",
        .virtualPath = "/Game/Materials/PreviewMaterial.kbmat",
        .physicalPath = materialFile,
        .contentHash = 1U,
        .runtimeLoadable = true,
    }));
    static_cast<void>(runtimeManager.RegisterAsset(kb::assets::AssetMetadata{
        .id = kb::editor::EditorMaterialPreviewMeshLoader::PreviewMeshAssetId(),
        .type = "RenderMesh",
        .name = "Material Preview Sphere",
        .virtualPath = "/Editor/Preview/MaterialSphere",
        .physicalPath = "__editor_material_preview_sphere__",
        .runtimeLoadable = true,
    }));
    const kb::scene::SceneEntity runtimeMesh = runtimeScene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "KBMAT-1007 Runtime Mesh" });
    runtimeScene.Components().MeshRenderers().Set(runtimeMesh, kb::scene::MeshRendererComponent{
        .meshAssetId = kb::editor::EditorMaterialPreviewMeshLoader::PreviewMeshAssetId().value,
        .materialAssetId = materialId.value,
    });
    runtimeScene.Runtime().SynchronizeTransforms();

    kb::render::Renderer runtimeRenderer;
    runtimeRenderer.ReserveRuntimeSceneResources(kb::render::Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 1U,
        .cachedMaterials = 1U,
        .frameReferencedMeshes = 1U,
        .frameReferencedMaterials = 1U,
        .scenePassSubmitStats = 1U,
        .renderSceneMeshProxies = 1U,
        .renderSceneDrawGroupKeys = 1U,
        .meshResourceSlots = 1U,
        .materialResourceSlots = 1U,
        .meshBindings = 1U,
        .materialBindings = 1U,
        .syncMeshProxies = 1U,
        .syncTransformCacheEntries = 4U,
        .syncTransformResolvingEntries = 4U,
    });
    kb::editor::tests::Require(runtimeRenderer.Initialize(surface, &config), "KBMAT-1007: Runtime parity renderer did not initialize");
    kb::editor::tests::Require(runtimeRenderer.BeginFrame(), "KBMAT-1007: Runtime parity renderer did not begin a frame");
    kb::editor::tests::Require(runtimeRenderer.SubmitScene(runtimeScene, submitDesc), "KBMAT-1007: Runtime parity renderer rejected the same material params");
    const kb::render::SceneRenderSubmitStats runtimeSubmitStats = runtimeRenderer.LastSceneSubmitStats();
    kb::editor::tests::Require(runtimeSubmitStats.missingMaterialBindingCount == 0U && runtimeSubmitStats.missingMaterialResourceCount == 0U, "KBMAT-1007: Runtime parity submit is missing the material resource");
    const kb::render::SceneRenderResourceMap* runtimeResourceMap = runtimeRenderer.SceneResourceMap();
    const kb::render::RenderResourceRegistry* runtimeResources = runtimeRenderer.SceneResources();
    kb::editor::tests::Require(runtimeResourceMap != nullptr && runtimeResources != nullptr, "KBMAT-1007: Runtime parity renderer did not expose resources");
    const kb::render::RenderMaterialHandle runtimeMaterialHandle = runtimeResourceMap->ResolveMaterial(materialId.value);
    const kb::render::RenderMaterialResource* runtimeMaterialResource = runtimeResources->FindMaterial(runtimeMaterialHandle);
    kb::editor::tests::Require(runtimeMaterialHandle.IsValid() && runtimeMaterialResource != nullptr, "KBMAT-1007: Runtime parity renderer did not bind the material resource");
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(previewMaterialSnapshot.baseColor[0], runtimeMaterialResource->baseColor[0]) &&
            kb::editor::tests::NearlyEqual(previewMaterialSnapshot.baseColor[1], runtimeMaterialResource->baseColor[1]) &&
            kb::editor::tests::NearlyEqual(previewMaterialSnapshot.baseColor[2], runtimeMaterialResource->baseColor[2]) &&
            kb::editor::tests::NearlyEqual(previewMaterialSnapshot.baseColor[3], runtimeMaterialResource->baseColor[3]) &&
            kb::editor::tests::NearlyEqual(previewMaterialSnapshot.metallicFactor, runtimeMaterialResource->metallicFactor) &&
            kb::editor::tests::NearlyEqual(previewMaterialSnapshot.roughnessFactor, runtimeMaterialResource->roughnessFactor) &&
            kb::editor::tests::NearlyEqual(previewMaterialSnapshot.normalScale, runtimeMaterialResource->normalScale) &&
            kb::editor::tests::NearlyEqual(previewMaterialSnapshot.occlusionStrength, runtimeMaterialResource->occlusionStrength) &&
            kb::editor::tests::NearlyEqual(previewMaterialSnapshot.emissiveStrength, runtimeMaterialResource->emissiveStrength) &&
            previewMaterialSnapshot.albedoTextureAssetId == runtimeMaterialResource->albedoTextureAssetId &&
            previewMaterialSnapshot.alphaMode == runtimeMaterialResource->alphaMode,
        "KBMAT-1007: Material preview and runtime material resources diverged for the same material params");
    runtimeRenderer.EndFrame();
    runtimeRenderer.Shutdown();

    const std::uint64_t firstRevision = preview.Revision();
    kb::render::RenderMaterialAssetData updatedMaterial{};
    updatedMaterial.desc.baseColor[0] = 0.18F;
    updatedMaterial.desc.baseColor[1] = 0.72F;
    updatedMaterial.desc.baseColor[2] = 0.44F;
    kb::editor::tests::Require(kb::render::RenderMaterialAssetWriter::Save(materialFile, updatedMaterial), "Material preview hot reload test could not write changed material fixture");
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
    kb::editor::tests::Require(preview.Telemetry().missingTextureCount == 0U, "Material preview scene did not reload the changed material document");

    std::filesystem::remove(materialFile, cleanupError);
}

void RunMaterialNodePreviewBuilderTest() {
    kb::render::RenderMaterialAssetData material{};
    material.graph.nodes = {
        kb::render::RenderMaterialGraphNode{
            .id = 1U,
            .kind = kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        },
        kb::render::RenderMaterialGraphNode{
            .id = 2U,
            .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor,
            .parameter = kb::render::RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 0 0 1" },
        },
        kb::render::RenderMaterialGraphNode{
            .id = 3U,
            .kind = kb::render::RenderMaterialGraphNodeKind::Multiply,
        },
        kb::render::RenderMaterialGraphNode{
            .id = 6U,
            .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor,
            .parameter = kb::render::RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0 0 1 1" },
        },
        kb::render::RenderMaterialGraphNode{
            .id = 7U,
            .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor,
            .parameter = kb::render::RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 1 1 1" },
        },
    };
    material.graph.links.push_back(MakeInspectorMaterialGraphLink(
        kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        2U,
        "rgba",
        kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        1U,
        "baseColor"));
    material.graph.links.push_back(MakeInspectorMaterialGraphLink(
        kb::render::RenderMaterialGraphNodeKind::ConstantColor, 6U, "rgba",
        kb::render::RenderMaterialGraphNodeKind::Multiply, 3U, "a"));
    material.graph.links.push_back(MakeInspectorMaterialGraphLink(
        kb::render::RenderMaterialGraphNodeKind::ConstantColor, 7U, "rgba",
        kb::render::RenderMaterialGraphNodeKind::Multiply, 3U, "b"));

    const std::optional<kb::render::RenderMaterialAssetData> preview =
        kb::editor::EditorMaterialNodePreviewBuilder::Build(material, 3U);
    kb::editor::tests::Require(preview.has_value(), "KBMAT-PREVIEW-0004: Per-node preview should build a temporary graph-backed material");
    kb::editor::tests::Require(preview->graph.nodes.size() == 4U && preview->graph.links.size() == 3U &&
            std::ranges::none_of(preview->graph.nodes, [](const kb::render::RenderMaterialGraphNode& node) { return node.id == 2U; }) &&
            std::ranges::any_of(preview->graph.links, [](const kb::render::RenderMaterialGraphLink& link) {
                return link.fromNodeId == 3U && link.toPin == "baseColor";
            }) &&
            preview->graph.shadingModel == "unlit" && preview->graph.blendMode == "opaque",
        "P1.13: Per-node preview must contain only the selected node dependency closure and an isolated MaterialOutput");

    kb::assets::AssetMetadata metadata{
        .id = kb::assets::AssetId{ 0x51515151U },
        .type = "RenderMaterial",
        .name = "NodePreview",
        .virtualPath = "/Game/Materials/NodePreview.kbmat",
        .runtimeLoadable = true,
    };
    kb::assets::AssetManager manager;
    static_cast<void>(manager.RegisterAsset(metadata));
    const kb::render::ResolvedRuntimeMaterialDesc resolved =
        kb::render::RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, metadata, *preview);
    kb::editor::tests::Require(kb::editor::tests::NearlyEqual(resolved.desc.baseColor[0], 0.0F) &&
            kb::editor::tests::NearlyEqual(resolved.desc.baseColor[1], 0.0F) &&
            kb::editor::tests::NearlyEqual(resolved.desc.baseColor[2], 1.0F) &&
            resolved.graphDiagnostics.empty(),
        "KBMAT-PREVIEW-0004: Per-node preview should resolve through the runtime material graph path, not a CPU overlay");
    kb::editor::tests::Require(!kb::editor::EditorMaterialNodePreviewBuilder::Build(material, 1U).has_value(),
        "KBMAT-PREVIEW-0004: Per-node preview should not route MaterialOutput to itself");
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

    kb::render::RenderMaterialDesc material{};
    material.baseColor[0] = 0.25F;
    material.baseColor[1] = 0.5F;
    material.baseColor[2] = 0.75F;
    material.baseColor[3] = 1.0F;
    material.metallicFactor = 0.6F;
    material.roughnessFactor = 0.35F;
    material.normalScale = 0.8F;
    material.emissiveColor[0] = 0.1F;
    material.emissiveColor[1] = 0.2F;
    material.emissiveColor[2] = 0.3F;
    material.emissiveStrength = 2.0F;
    material.albedoTextureAssetId = 101U;
    material.normalTextureAssetId = 202U;
    material.metallicRoughnessTextureAssetId = 303U;
    const std::vector<kb::editor::MaterialDebugChannelRow> rows = MaterialAssetFormatter::DebugChannelRows(material, 5151U);
    kb::editor::tests::Require(rows.size() == 6U, "KBMAT-0611: material debug inspector must expose exactly the required channel rows");
    kb::editor::tests::Require(rows[0].label == "Material Id" && rows[0].value == "5151", "KBMAT-0611: material id debug channel is wrong");
    kb::editor::tests::Require(rows[1].label == "Base Color" && rows[1].value.find("texture #101") != std::string::npos, "KBMAT-0611: base color debug channel is missing texture source");
    kb::editor::tests::Require(rows[2].label == "Roughness" && rows[2].value.find("MR.g texture #303") != std::string::npos, "KBMAT-0611: roughness debug channel must report MR G source");
    kb::editor::tests::Require(rows[3].label == "Metallic" && rows[3].value.find("MR.b texture #303") != std::string::npos, "KBMAT-0611: metallic debug channel must report MR B source");
    kb::editor::tests::Require(rows[4].label == "Normal" && rows[4].value.find("texture #202") != std::string::npos, "KBMAT-0611: normal debug channel is missing texture source");
    kb::editor::tests::Require(rows[5].label == "Emissive" && rows[5].value.find("white fallback") != std::string::npos, "KBMAT-0611: emissive debug channel must report fallback source");
}

void RunMaterialEditorFinitePresentationParsingTest() {
    const kb::editor::MaterialEditorParameterValue scalar = kb::editor::MaterialEditorPanelConstantParameterValue(
        kb::render::RenderMaterialGraphNodeKind::ConstantScalar,
        "nan");
    const kb::editor::MaterialEditorParameterValue vector = kb::editor::MaterialEditorPanelConstantParameterValue(
        kb::render::RenderMaterialGraphNodeKind::ConstantVector,
        "1 2 inf");
    const kb::editor::MaterialEditorParameterValue color =
        kb::editor::MaterialEditorPanelColorValueFromHint("nan 0 0 1", true);
    const std::vector<kb::editor::MaterialEditorPanelColorRampStopModel> ramp =
        kb::editor::MaterialEditorPanelColorRampStops("0 0 0 0 1 1 1 inf");
    kb::editor::tests::Require(std::isfinite(scalar.numbers[0]) && scalar.numbers[0] == 0.0F &&
            std::ranges::all_of(vector.numbers, [](float value) { return std::isfinite(value) && value == 0.0F; }) &&
            std::ranges::all_of(color.numbers, [](float value) { return std::isfinite(value) && value == 1.0F; }) &&
            ramp.size() == 2U && ramp.front().position == 0.0F && ramp.back().position == 1.0F,
        "P2.9: node presentation parsers must reject NaN/Inf and use deterministic finite fallbacks");
}

void RunMaterialPreviewGpuGraphParityTest() {
    const auto makeLink = [](kb::render::RenderMaterialGraphNodeKind fromKind, std::uint32_t fromNode, std::string fromPin,
        kb::render::RenderMaterialGraphNodeKind toKind, std::uint32_t toNode, std::string toPin) {
        kb::render::RenderMaterialGraphLink link{
            .fromNodeId = fromNode,
            .fromPinId = kb::render::RenderMaterialGraphStablePinId(fromKind, fromPin, true),
            .fromPin = std::move(fromPin),
            .toNodeId = toNode,
            .toPinId = kb::render::RenderMaterialGraphStablePinId(toKind, toPin, false),
            .toPin = std::move(toPin),
        };
        link.id = kb::render::MakeRenderMaterialGraphLinkId(link);
        return link;
    };

    kb::scene::Scene scene;
    const kb::assets::AssetId materialId{ 0x1501U };
    static_cast<void>(scene.Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
        .id = materialId,
        .type = "RenderMaterial",
        .name = "GraphParity",
        .virtualPath = "/Game/Materials/GraphParity.kbmat",
        .contentHash = 1U,
        .runtimeLoadable = true,
    }));

    kb::render::RenderMaterialAssetData material{};
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{ .id = 2U, .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor, .positionX = -160, .positionY = 64 });
    material.graph.links.push_back(makeLink(kb::render::RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", kb::render::RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    const kb::editor::EditorMaterialPreviewTelemetry telemetry =
        kb::editor::EditorMaterialPreviewTelemetryBuilder::Build(scene.Assets().Manager(), materialId, &material, true);

    // The scene submit (MAT-07) keys its program off the graph source hash; the preview must derive the same key.
    const std::uint64_t sceneProgramKey = kb::render::CompileRenderMaterialGraphToShaderSource(
        material.graph, kb::render::RenderMaterialGraphBuildContext{ .assetId = materialId.value }).shader.sourceHash;

    kb::editor::tests::Require(telemetry.graphBacked &&
            telemetry.renderMode == kb::editor::MaterialPreviewRenderMode::GpuMaterialGraph &&
            telemetry.graphRuntimeState == kb::render::RenderMaterialGraphRuntimeState::UsingGpuGraph,
        "KBMAT-MAT15: A valid graph material preview must report the GPU graph path, not the CPU resolver");
    kb::editor::tests::Require(telemetry.graphProgramKey != 0U && telemetry.graphProgramKey == sceneProgramKey,
        "KBMAT-MAT15: Preview and scene must use the same shader program key for the same material");
    kb::editor::tests::Require(telemetry.compileDiagnostics.empty(),
        "KBMAT-MAT15: A valid graph preview must not surface compile diagnostics");

    kb::render::RenderMaterialAssetData qualityMaterial{};
    qualityMaterial.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    qualityMaterial.graph.shadingModel = "unlit";
    qualityMaterial.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 0 0 1" },
    });
    qualityMaterial.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 3U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0 0 1 1" },
    });
    qualityMaterial.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 4U,
        .kind = kb::render::RenderMaterialGraphNodeKind::QualitySwitch,
    });
    qualityMaterial.graph.links.push_back(makeLink(kb::render::RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", kb::render::RenderMaterialGraphNodeKind::QualitySwitch, 4U, "low"));
    qualityMaterial.graph.links.push_back(makeLink(kb::render::RenderMaterialGraphNodeKind::ConstantColor, 3U, "rgba", kb::render::RenderMaterialGraphNodeKind::QualitySwitch, 4U, "high"));
    qualityMaterial.graph.links.push_back(makeLink(kb::render::RenderMaterialGraphNodeKind::QualitySwitch, 4U, "result", kb::render::RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    const kb::render::RenderMaterialGraphBuildContext lowQualityContext{
        .assetId = materialId.value,
        .qualityLevel = kb::render::RenderMaterialGraphQualityLevel::Low,
    };
    const kb::render::RenderMaterialGraphBuildContext highQualityContext{
        .assetId = materialId.value,
        .qualityLevel = kb::render::RenderMaterialGraphQualityLevel::High,
    };
    const kb::editor::EditorMaterialPreviewTelemetry lowQualityTelemetry =
        kb::editor::EditorMaterialPreviewTelemetryBuilder::Build(scene.Assets().Manager(), materialId, &qualityMaterial, true, lowQualityContext);
    const kb::editor::EditorMaterialPreviewTelemetry highQualityTelemetry =
        kb::editor::EditorMaterialPreviewTelemetryBuilder::Build(scene.Assets().Manager(), materialId, &qualityMaterial, true, highQualityContext);
    kb::editor::tests::Require(lowQualityTelemetry.graphProgramKey != 0U && highQualityTelemetry.graphProgramKey != 0U,
        "KBMAT-MAT52: Quality preview variants must compile to visible graph program keys");
    kb::editor::tests::Require(lowQualityTelemetry.graphProgramKey != highQualityTelemetry.graphProgramKey,
        "KBMAT-MAT52: Quality preview must select a distinct graph shader variant for Low and High");

    // A broken graph (Float -> Color mismatch) must surface the error material + diagnostics, not a silent black state.
    kb::render::RenderMaterialAssetData broken{};
    broken.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    broken.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{ .id = 2U, .kind = kb::render::RenderMaterialGraphNodeKind::ConstantScalar, .positionX = -160, .positionY = 64 });
    broken.graph.links.push_back(makeLink(kb::render::RenderMaterialGraphNodeKind::ConstantScalar, 2U, "value", kb::render::RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const kb::editor::EditorMaterialPreviewTelemetry brokenTelemetry =
        kb::editor::EditorMaterialPreviewTelemetryBuilder::Build(scene.Assets().Manager(), materialId, &broken, true);
    kb::editor::tests::Require(brokenTelemetry.renderMode == kb::editor::MaterialPreviewRenderMode::ErrorMaterial &&
            brokenTelemetry.graphRuntimeState == kb::render::RenderMaterialGraphRuntimeState::UsingErrorMaterial &&
            !brokenTelemetry.compileDiagnostics.empty(),
        "KBMAT-MAT15: A broken graph preview must surface the error material and compile diagnostics, not hide the failure");
}

#if defined(_WIN32)
void RunMaterialEditorGraphLayoutAndHitTestTest() {
    const RECT content{0, 0, 760, 540};
    const kb::editor::MaterialEditorPanelLayout layout = kb::editor::MaterialEditorPanelRenderer::ResolveLayout(content);
    kb::editor::tests::Require(layout.graphCanvas.left == content.left, "Material Editor graph should own the full tab width");
    kb::editor::tests::Require(layout.graphCanvas.right == content.right, "Material Editor graph should own the full tab width");
    kb::editor::tests::Require(layout.graphCanvas.top == content.top + layout.headerHeight, "Material Editor graph should start directly below the responsive toolbar");
    kb::editor::tests::Require(layout.graphCanvas.bottom == content.bottom, "Material Editor graph should fill the tab height");
    kb::editor::tests::Require(kb::editor::MaterialEditorPanelPointInRect(layout.graphCanvas, layout.previewFrame.left + 2, layout.previewFrame.top + 2), "Material preview should be an overlay inside the graph workspace");
    kb::editor::tests::Require(layout.diagnosticsPanel.left >= layout.previewFrame.right, "Material diagnostics should not overlap the preview overlay");
    kb::editor::tests::Require(layout.infoButton.right <= layout.previewPrimitiveButton.left &&
            layout.previewPrimitiveButton.right <= layout.previewSceneButton.left &&
            layout.previewSceneButton.right <= layout.previewQualityButton.left &&
            layout.previewQualityButton.right <= layout.previewNormalButton.left &&
            layout.previewNormalButton.right <= layout.previewNodeButton.left,
        "KBMAT-PREVIEW-0003: Material Editor preview commands should preserve their order without overlap");
    kb::editor::tests::Require(layout.applyButton.right <= layout.saveButton.left &&
            layout.saveButton.right <= layout.revertButton.left &&
            layout.revertButton.right <= layout.validateButton.left,
        "Material Editor document commands should preserve their order without overlap after toolbar reflow");
    kb::editor::tests::Require(kb::editor::MaterialEditorPanelRenderer::CommandAt(content, layout.infoButton.left + 2, layout.infoButton.top + 2) == kb::editor::MaterialEditorPanelCommand::Info, "Material Editor should hit-test the Info command");
    kb::editor::tests::Require(kb::editor::MaterialEditorPanelRenderer::CommandAt(content, layout.previewPrimitiveButton.left + 2, layout.previewPrimitiveButton.top + 2) == kb::editor::MaterialEditorPanelCommand::PreviewPrimitive, "KBMAT-PREVIEW-0001: Material Editor should hit-test the preview primitive command");
    kb::editor::tests::Require(kb::editor::MaterialEditorPanelRenderer::CommandAt(content, layout.previewSceneButton.left + 2, layout.previewSceneButton.top + 2) == kb::editor::MaterialEditorPanelCommand::PreviewScene, "KBMAT-PREVIEW-0003: Material Editor should hit-test the preview scene settings command");
    kb::editor::tests::Require(kb::editor::MaterialEditorPanelRenderer::CommandAt(content, layout.previewQualityButton.left + 2, layout.previewQualityButton.top + 2) == kb::editor::MaterialEditorPanelCommand::PreviewQuality, "KBMAT-MAT52: Material Editor should hit-test the preview quality command");
    kb::editor::tests::Require(kb::editor::MaterialEditorPanelRenderer::CommandAt(content, layout.previewNormalButton.left + 2, layout.previewNormalButton.top + 2) == kb::editor::MaterialEditorPanelCommand::PreviewNormal, "KBMAT-NORMAL-0001: Material Editor should hit-test the GBuffer normal debug command");
    kb::editor::tests::Require(kb::editor::MaterialEditorPanelRenderer::CommandAt(content, layout.previewNodeButton.left + 2, layout.previewNodeButton.top + 2) == kb::editor::MaterialEditorPanelCommand::PreviewNode, "KBMAT-PREVIEW-0004: Material Editor should hit-test the per-node preview command");
    kb::editor::tests::Require(kb::editor::MaterialEditorPanelRenderer::CommandAt(content, layout.applyButton.left + 2, layout.applyButton.top + 2) == kb::editor::MaterialEditorPanelCommand::ApplyToSelection, "Material Editor should hit-test the Apply To Selection command");
    kb::editor::tests::Require(kb::editor::MaterialEditorPanelRenderer::CommandAt(content, layout.saveButton.left + 2, layout.saveButton.top + 2) == kb::editor::MaterialEditorPanelCommand::Save, "Material Editor should hit-test the Save command");
    kb::editor::tests::Require(kb::editor::MaterialEditorPanelRenderer::CommandAt(content, layout.revertButton.left + 2, layout.revertButton.top + 2) == kb::editor::MaterialEditorPanelCommand::Revert, "Material Editor should hit-test the Revert command");
    kb::editor::tests::Require(kb::editor::MaterialEditorPanelRenderer::CommandAt(content, layout.validateButton.left + 2, layout.validateButton.top + 2) == kb::editor::MaterialEditorPanelCommand::Validate, "Material Editor should hit-test the Validate command");
    kb::editor::tests::Require(kb::editor::MaterialEditorPanelRenderer::CommandAt(content, layout.previewFrame.left + 2, layout.previewFrame.bottom + 14) == kb::editor::MaterialEditorPanelCommand::None,
        "Material Editor should not keep dead asset badge/link hitboxes under the preview overlay");
    kb::editor::tests::Require(kb::editor::kMaterialEditorPanelToolbarCommands.size() == 10U, "KBMAT-PREVIEW-0003: Material Editor toolbar should expose real preview controls only");
    for (const kb::editor::MaterialEditorPanelCommand command : kb::editor::kMaterialEditorPanelToolbarCommands) {
        const std::string name{ kb::editor::MaterialEditorPanelCommandName(command) };
        kb::editor::tests::Require(!name.empty() && name != "None", "KBMAT-1002: every Material Editor toolbar button must expose a real command label");
        kb::editor::tests::Require(kb::editor::MaterialEditorPanelCommandHasBackendAction(command), "KBMAT-1002: every Material Editor toolbar button must route to a backend action");
        kb::editor::tests::Require(name != "Create Shader From Material", "KBMAT-0808: Create Shader From Material cannot exist before a real graph backend");
    }
    kb::editor::tests::Require(!kb::editor::MaterialEditorPanelCommandHasBackendAction(kb::editor::MaterialEditorPanelCommand::None), "KBMAT-1002: empty Material Editor hit-test space must not route to a backend action");

    const RECT narrowContent{ 0, 0, 434, 336 };
    const kb::editor::MaterialEditorPanelLayout narrowLayout = kb::editor::MaterialEditorPanelRenderer::ResolveLayout(narrowContent);
    kb::editor::tests::Require(narrowLayout.compactToolbar, "Material Editor should use the compact toolbar at the reported 434px production width");
    kb::editor::tests::Require(narrowLayout.headerHeight > kb::editor::MaterialEditorPanelMetrics::HeaderHeight, "Compact Material Editor toolbar should wrap instead of clipping commands");
    const std::array<RECT, 10U> narrowToolbarButtons{
        narrowLayout.infoButton,
        narrowLayout.previewPrimitiveButton,
        narrowLayout.previewSceneButton,
        narrowLayout.previewQualityButton,
        narrowLayout.previewNormalButton,
        narrowLayout.previewNodeButton,
        narrowLayout.applyButton,
        narrowLayout.saveButton,
        narrowLayout.revertButton,
        narrowLayout.validateButton,
    };
    for (const RECT& button : narrowToolbarButtons) {
        kb::editor::tests::Require(
            button.left >= narrowContent.left && button.right <= narrowContent.right &&
                button.top >= narrowContent.top && button.bottom <= narrowLayout.header.bottom,
            "Every compact Material Editor toolbar command must remain inside the visible header");
    }
    const RECT wideContent{ 0, 0, 1480, 900 };
    const kb::editor::MaterialEditorPanelLayout wideLayout = kb::editor::MaterialEditorPanelRenderer::ResolveLayout(wideContent);
    kb::editor::tests::Require(
        wideLayout.graphCanvas.left == wideContent.left && wideLayout.graphCanvas.right == wideContent.right &&
            wideLayout.previewFrame.left > wideLayout.graphCanvas.left,
        "Wide Material Editor should keep the graph full-width with a compact preview overlay");

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

    kb::editor::MaterialEditorState materialEditor;
    materialEditor.Open(kb::assets::AssetId{ 0x4D4154455249414CULL }, kb::render::RenderMaterialAssetData{});
    const kb::editor::MaterialEditorPanelDetailsRows details = kb::editor::MaterialEditorPanelRenderer::DetailsRows(materialEditor.Parameters(), 1U);
    kb::editor::tests::Require(details.title.find("Selected Node #1") != std::string::npos, "Material Editor details should describe selected graph node context");
    kb::editor::tests::Require(std::ranges::any_of(details.parameterRows, [](const std::string& row) { return row.find("baseColor") != std::string::npos && row.find("default") != std::string::npos; }), "Material Editor details should expose metadata-driven baseColor parameter with default value");
    kb::editor::tests::Require(std::ranges::any_of(details.parameterRows, [](const std::string& row) { return row.find("clearcoatFactor") != std::string::npos && row.find("disabled") != std::string::npos; }), "Material Editor details should expose unsupported advanced rows as disabled");
    kb::editor::tests::Require(std::ranges::any_of(details.textureSlotRows, [](const std::string& row) { return row.find("Base Color") != std::string::npos && row.find("sRGB") != std::string::npos; }), "Material Editor details should expose metadata-driven Base Color texture slot");
    kb::editor::tests::Require(std::ranges::any_of(details.textureSlotRows, [](const std::string& row) { return row.find("Normal") != std::string::npos && row.find("Linear") != std::string::npos; }), "Material Editor details should expose metadata-driven texture color policy");
}

void RunMaterialEditorDetailsCanonicalLayoutTest() {
    const RECT content{ 0, 0, 960, 720 };
    std::vector<kb::editor::MaterialEditorParameter> parameters;
    for (std::size_t index = 0U; index < 10U; ++index) {
        parameters.push_back(kb::editor::MaterialEditorParameter{
            .stableId = "scalar." + std::to_string(index),
            .type = kb::render::RenderMaterialParameterType::Scalar,
            .displayName = "Scalar " + std::to_string(index),
            .value = kb::editor::MaterialEditorParameterValue{
                .kind = kb::editor::MaterialEditorParameterValueKind::Scalar,
                .numbers = { static_cast<float>(index), 0.0F, 0.0F, 0.0F },
            },
        });
    }
    for (std::size_t index = 0U; index < 2U; ++index) {
        parameters.push_back(kb::editor::MaterialEditorParameter{
            .stableId = "texture." + std::to_string(index),
            .type = kb::render::RenderMaterialParameterType::Texture,
            .displayName = "Texture " + std::to_string(index),
            .value = kb::editor::MaterialEditorParameterValue{
                .kind = kb::editor::MaterialEditorParameterValueKind::TextureAsset,
                .assetId = 100U + index,
            },
        });
    }

    const std::vector<kb::editor::MaterialEditorGraphNodeProperty> nodeProperties{
        kb::editor::MaterialEditorGraphNodeProperty{
            .nodeId = 42U,
            .stableId = "node.name",
            .displayName = "Name",
            .kind = kb::editor::MaterialEditorGraphNodePropertyKind::Text,
            .value = kb::editor::MaterialEditorParameterValue{
                .kind = kb::editor::MaterialEditorParameterValueKind::Enum,
                .text = "Canonical",
            },
        },
        kb::editor::MaterialEditorGraphNodeProperty{
            .nodeId = 42U,
            .stableId = "uvSet",
            .displayName = "UV Set",
            .kind = kb::editor::MaterialEditorGraphNodePropertyKind::Enum,
            .type = kb::render::RenderMaterialParameterType::Enum,
            .value = kb::editor::MaterialEditorParameterValue{
                .kind = kb::editor::MaterialEditorParameterValueKind::Enum,
                .text = "1",
            },
            .options = {
                kb::editor::MaterialEditorGraphNodePropertyOption{ .value = "0", .label = "UV0" },
                kb::editor::MaterialEditorGraphNodePropertyOption{ .value = "1", .label = "UV1" },
                kb::editor::MaterialEditorGraphNodePropertyOption{ .value = "2", .label = "UV2" },
            },
            .dropdownOpen = true,
        },
    };

    kb::editor::MaterialEditorPanelDetailsRows materialRows =
        kb::editor::MaterialEditorPanelRenderer::DetailsRows(parameters, 42U, nodeProperties);
    materialRows.layerTreeRows = {
        kb::editor::MaterialEditorLayerTreeRow{ .nodeId = 42U, .index = 0U, .layerName = "Base" },
        kb::editor::MaterialEditorLayerTreeRow{ .nodeId = 42U, .index = 1U, .layerName = "Coat" },
    };
    materialRows.findResults = {
        kb::editor::MaterialEditorFindResult{ .label = "Node 42", .detail = "Canonical" },
        kb::editor::MaterialEditorFindResult{ .kind = kb::editor::MaterialEditorFindResultKind::Parameter, .label = "Scalar 0", .detail = "scalar.0" },
    };
    materialRows.materialDiffRows.assign(10U, "changed property");
    materialRows.debugChannelRows.assign(5U, kb::editor::MaterialDebugChannelRow{ .label = "Debug", .value = "Value" });
    materialRows.materialStats.available = true;
    materialRows.materialStats.passRows.push_back(kb::editor::MaterialEditorMaterialStatsPassRow{ .passName = "GBuffer", .graphProgram = true });
    materialRows.materialStats.warnings.push_back("stats warning");
    materialRows.shaderViewer.available = true;
    materialRows.shaderViewer.sources.push_back(kb::editor::MaterialEditorShaderSourceView{ .passName = "GBuffer", .backendName = "dx11", .stageName = "fragment" });
    materialRows.shaderViewer.reflectionRows.push_back(kb::editor::MaterialEditorShaderReflectionRow{ .category = "uniform", .name = "Tint", .stableId = "scalar.0" });

    kb::editor::MaterialEditorPanelDetailsRows instanceRows =
        kb::editor::MaterialEditorPanelRenderer::DetailsRows(parameters, 0U, {});
    instanceRows.title = "Material Instance Overrides";
    instanceRows.instanceParentRows = {
        kb::editor::MaterialEditorInstanceParentChainRow{ .assetId = kb::assets::AssetId{ 1U }, .label = "Instance", .current = true },
        kb::editor::MaterialEditorInstanceParentChainRow{ .assetId = kb::assets::AssetId{ 2U }, .label = "Parent" },
    };
    instanceRows.instanceOverrideGroupRows = {
        kb::editor::MaterialEditorInstanceOverrideGroupRow{ .group = kb::editor::MaterialEditorParameterGroup::Core, .activeOverrideCount = 2U, .totalParameterCount = 10U },
    };
    instanceRows.instanceStaticSwitchRows = {
        kb::editor::MaterialEditorInstanceStaticSwitchRow{ .nodeId = 7U, .stableId = "useCoat", .displayName = "Use Coat", .parentValue = "false", .value = "true", .overrideActive = true },
    };
    instanceRows.materialDiffRows.assign(12U, "instance override changed");

    const auto rectEqual = [](const RECT& lhs, const RECT& rhs) noexcept {
        return lhs.left == rhs.left && lhs.top == rhs.top && lhs.right == rhs.right && lhs.bottom == rhs.bottom;
    };
    const auto verifyRows = [&](const kb::editor::MaterialEditorPanelDetailsRows& rows, const char* label) {
        const auto require = [label](bool condition, std::string_view suffix) {
            const std::string message = std::string{ label } + " " + std::string{ suffix };
            kb::editor::tests::Require(condition, message.c_str());
        };
        const kb::editor::MaterialEditorDetailsLayout first =
            kb::editor::MaterialEditorPanelRenderer::ResolveDetailsLayout(content, rows, 0);
        require(first.visible && first.maxScroll > 0, "Details layout should be visible and scrollable");
        const std::array<int, 3U> offsets{ 0, first.maxScroll / 2, first.maxScroll };
        for (const int offset : offsets) {
            const kb::editor::MaterialEditorDetailsLayout layout =
                kb::editor::MaterialEditorPanelRenderer::ResolveDetailsLayout(content, rows, offset);
            const kb::editor::MaterialEditorDetailsHit searchHit = kb::editor::MaterialEditorPanelRenderer::DetailsHitAt(
                layout,
                rows,
                (layout.searchRect.left + layout.searchRect.right) / 2,
                (layout.searchRect.top + layout.searchRect.bottom) / 2);
            require(searchHit.kind == kb::editor::MaterialEditorDetailsHitKind::Search && rectEqual(searchHit.rect, layout.searchRect),
                "search render rect must equal hit rect");

            for (const kb::editor::MaterialEditorDetailsLayoutItem& item : layout.items) {
                if (item.clippedRect.right <= item.clippedRect.left || item.clippedRect.bottom <= item.clippedRect.top) {
                    continue;
                }
                const bool interactive =
                    item.kind == kb::editor::MaterialEditorDetailsItemKind::FindResultRow ||
                    item.kind == kb::editor::MaterialEditorDetailsItemKind::NodePropertyRow ||
                    item.kind == kb::editor::MaterialEditorDetailsItemKind::NodePropertyOptionRow ||
                    item.kind == kb::editor::MaterialEditorDetailsItemKind::ParameterRow ||
                    item.kind == kb::editor::MaterialEditorDetailsItemKind::TextureParameterRow;
                if (!interactive) {
                    continue;
                }
                const kb::editor::MaterialEditorDetailsHit hit = kb::editor::MaterialEditorPanelRenderer::DetailsHitAt(
                    layout,
                    rows,
                    (item.clippedRect.left + item.clippedRect.right) / 2,
                    (item.clippedRect.top + item.clippedRect.bottom) / 2);
                require(hit.kind != kb::editor::MaterialEditorDetailsHitKind::Backdrop && rectEqual(hit.rect, item.clippedRect),
                    "visible row render rect must equal hit rect");
                if (item.kind == kb::editor::MaterialEditorDetailsItemKind::NodePropertyRow ||
                    item.kind == kb::editor::MaterialEditorDetailsItemKind::NodePropertyOptionRow) {
                    require(hit.nodeProperty.stableId == rows.nodePropertyRows[item.index].stableId,
                        "node property hit must preserve stableId");
                } else if (item.kind == kb::editor::MaterialEditorDetailsItemKind::ParameterRow) {
                    require(hit.parameter.stableId == rows.parameterModels[item.index].stableId,
                        "parameter hit must preserve stableId");
                } else if (item.kind == kb::editor::MaterialEditorDetailsItemKind::TextureParameterRow) {
                    require(hit.parameter.stableId == rows.textureSlotModels[item.index].stableId,
                        "texture hit must preserve stableId");
                }
            }

            const kb::editor::MaterialEditorDetailsHit backdrop = kb::editor::MaterialEditorPanelRenderer::DetailsHitAt(
                layout,
                rows,
                layout.panel.left + 3,
                layout.viewport.top + 3);
            require(backdrop.kind == kb::editor::MaterialEditorDetailsHitKind::Backdrop,
                "opaque Details background must capture input instead of leaking to canvas");
        }
    };

    verifyRows(materialRows, "Material");
    verifyRows(instanceRows, "Material Instance");
}

void RunMaterialEditorOpaqueOverlayAndTexturePickerLayoutTest() {
    // Production-context coverage lives in EditorSelfTest; kb_editor_tests deliberately
    // does not link EditorSceneContext/MaterialEditorPanelRenderer.cpp.
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

void RunMaterialEditorGraphDiagnosticsRefreshTest() {
    kb::render::RenderMaterialAssetData material{};
    material.documentVersion = kb::render::kRenderMaterialAssetDocumentVersion;
    material.hasExplicitDocumentVersion = true;
    material.materialType = kb::render::kRenderMaterialAssetBuiltInPbrType;
    material.materialTypeVersion = kb::render::kRenderMaterialAssetBuiltInPbrTypeVersion;
    material.hasExplicitMaterialType = true;
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 120,
        .positionY = 120,
    });

    kb::editor::MaterialEditorState materialEditor;
    materialEditor.Open(kb::assets::AssetId{ 0x47444147ULL }, material);
    kb::editor::tests::Require(!materialEditor.DiagnosticsHaveError(), "KBMAT-GRAPH-0106: Disconnected BaseColor should use the black runtime fallback without a graph error");
    kb::editor::tests::Require(!std::ranges::any_of(materialEditor.Diagnostics(), [](const std::string& diagnostic) {
        return diagnostic.find("graph.disconnected_required_output") != std::string::npos;
    }), "KBMAT-GRAPH-0106: Material Editor diagnostics should not report disconnected BaseColor as an error");

    kb::editor::tests::Require(materialEditor.ConnectGraphPins(2U, "rgba", 1U, "baseColor"),
        "KBMAT-GRAPH-0106: Material Editor test should connect BaseColor");
    kb::editor::tests::Require(!std::ranges::any_of(materialEditor.Diagnostics(), [](const std::string& diagnostic) {
        return diagnostic.find("graph.disconnected_required_output") != std::string::npos;
    }), "KBMAT-GRAPH-0106: Material Editor graph diagnostics should refresh after fixing BaseColor");

    kb::render::RenderMaterialAssetData blendMaterial = *materialEditor.WorkingCopy();
    blendMaterial.desc.alphaMode = kb::render::RenderMaterialAlphaMode::Blend;
    materialEditor.SetWorkingCopy(std::move(blendMaterial));
    kb::editor::tests::Require(!materialEditor.DiagnosticsHaveError(), "KBMAT-GRAPH-0106: Unsupported blend mode should remain a warning in the diagnostics panel");
    kb::editor::tests::Require(std::ranges::any_of(materialEditor.Diagnostics(), [](const std::string& diagnostic) {
        return diagnostic.find("graph.unsupported_blend_mode") != std::string::npos;
    }), "KBMAT-GRAPH-0106: Material Editor diagnostics should include unsupported blend mode");

    kb::render::RenderMaterialAssetData invalidLinkMaterial = *materialEditor.WorkingCopy();
    invalidLinkMaterial.desc.alphaMode = kb::render::RenderMaterialAlphaMode::Opaque;
    invalidLinkMaterial.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 3U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 260,
        .positionY = 120,
    });
    std::erase_if(invalidLinkMaterial.graph.links, [](const kb::render::RenderMaterialGraphLink& link) {
        return link.toNodeId == 1U && link.toPin == "baseColor";
    });
    invalidLinkMaterial.graph.links.push_back(MakeInspectorMaterialGraphLink(
        kb::render::RenderMaterialGraphNodeKind::ConstantScalar,
        3U,
        "value",
        kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        1U,
        "baseColor"));
    materialEditor.SetWorkingCopy(invalidLinkMaterial);
    kb::editor::tests::Require(materialEditor.DiagnosticsHaveError(), "KBMAT-LIVE-0001: Invalid graph link should produce an editor diagnostic error");
    kb::editor::tests::Require(std::ranges::any_of(materialEditor.GraphDiagnosticMarkers(), [](const kb::editor::MaterialEditorGraphDiagnosticMarker& marker) {
        return marker.nodeId != 0U && marker.severity == kb::render::RenderMaterialGraphDiagnosticSeverity::Error &&
            marker.kind == kb::render::RenderMaterialGraphDiagnosticKind::TypeMismatch;
    }), "KBMAT-LIVE-0001: Invalid graph link should produce an error marker attached to a graph node");

    kb::render::RenderMaterialAssetData fixedMaterial = invalidLinkMaterial;
    std::erase_if(fixedMaterial.graph.links, [](const kb::render::RenderMaterialGraphLink& link) {
        return link.toNodeId == 1U && link.toPin == "baseColor";
    });
    fixedMaterial.graph.links.push_back(MakeInspectorMaterialGraphLink(
        kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        2U,
        "rgba",
        kb::render::RenderMaterialGraphNodeKind::MaterialOutput,
        1U,
        "baseColor"));
    materialEditor.SetWorkingCopy(std::move(fixedMaterial));
    kb::editor::tests::Require(!materialEditor.DiagnosticsHaveError() && materialEditor.GraphDiagnosticMarkers().empty(),
        "KBMAT-LIVE-0001: Fixing graph diagnostics should clear node error markers");
}

void RunMaterialEditorGraphNodeRenameTest() {
    kb::render::RenderMaterialAssetData material{};
    material.documentVersion = kb::render::kRenderMaterialAssetDocumentVersion;
    material.hasExplicitDocumentVersion = true;
    material.materialType = kb::render::kRenderMaterialAssetBuiltInPbrType;
    material.materialTypeVersion = kb::render::kRenderMaterialAssetBuiltInPbrTypeVersion;
    material.hasExplicitMaterialType = true;
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    material.graph.nodes.push_back(kb::render::RenderMaterialGraphNode{
        .id = 2U,
        .kind = kb::render::RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 120,
        .positionY = 120,
        .parameter = kb::render::RenderMaterialGraphParameterMetadata{ .displayName = "Old Tint", .defaultValueHint = "1 1 1 1" },
    });

    kb::editor::MaterialEditorState materialEditor;
    const kb::assets::AssetId materialId{ 0x52454E414D45ULL };
    materialEditor.Open(materialId, material);
    kb::editor::tests::Require(materialEditor.SelectNode(2U), "KBMAT-RENAME-0001: Material graph rename test should select the node");
    kb::editor::tests::Require(materialEditor.BeginGraphNodeRenameEdit(2U), "KBMAT-RENAME-0001: F2 rename should begin for a selected material graph node");
    kb::editor::tests::Require(materialEditor.IsGraphNodeRenameEditing(2U) && materialEditor.GraphNodeRenameEditBuffer() == "Old Tint",
        "KBMAT-RENAME-0001: Rename buffer should start from the current node display name");
    materialEditor.ClearGraphNodeRenameEditText();
    materialEditor.InsertGraphNodeRenameEditText("  Albedo Tint  ");
    const kb::render::RenderMaterialAssetData before = *materialEditor.WorkingCopy();
    kb::render::RenderMaterialAssetData after = before;
    for (kb::render::RenderMaterialGraphNode& node : after.graph.nodes) {
        if (node.id == 2U) {
            node.parameter.displayName = "Albedo Tint";
        }
    }

    kb::editor::EditorCommandStack stack;
    kb::editor::tests::Require(stack.Execute(kb::editor::EditorMaterialWorkingCopyEditCommand::Create(
        materialEditor,
        materialId,
        "Rename Material Graph Node",
        before,
        after,
        std::vector<std::uint32_t>{ 2U },
        std::vector<std::uint32_t>{ 2U },
        2U,
        2U)),
        "KBMAT-RENAME-0001: Material graph node rename should be recorded through the editor command stack");
    materialEditor.CancelGraphNodeRenameEdit();
    kb::editor::tests::Require(materialEditor.GraphNodeDisplayName(2U) == "Albedo Tint" && materialEditor.Dirty(),
        "KBMAT-RENAME-0001: Committed rename should update the visible graph node display name and dirty the working copy");
    const std::vector<kb::editor::MaterialEditorGraphNodeProperty> properties = materialEditor.GraphNodeProperties(2U);
    kb::editor::tests::Require(std::ranges::any_of(properties, [](const kb::editor::MaterialEditorGraphNodeProperty& property) {
        return property.stableId == "node.name" && property.value.text == "Albedo Tint";
    }), "KBMAT-RENAME-0001: Node details should expose the renamed node name property");

    kb::editor::tests::Require(stack.Undo(), "KBMAT-RENAME-0001: Material graph rename should undo");
    kb::editor::tests::Require(materialEditor.GraphNodeDisplayName(2U) == "Old Tint",
        "KBMAT-RENAME-0001: Undo should restore the previous material graph node name");
    kb::editor::tests::Require(stack.Redo(), "KBMAT-RENAME-0001: Material graph rename should redo");
    kb::editor::tests::Require(materialEditor.GraphNodeDisplayName(2U) == "Albedo Tint",
        "KBMAT-RENAME-0001: Redo should restore the renamed material graph node name");
}
#endif

void RunInspectorPhysicsModelTest() {
    using kb::editor::InspectorPhysicsModel;
    using kb::editor::PhysicsComponentKind;
    using kb::editor::PhysicsFieldKind;

    // Rigidbody: enum cycle, float apply/read, bool toggle, kind mismatch rejection.
    kb::scene::RigidbodyComponent rb;
    const std::vector<kb::editor::PhysicsField> rbFields = InspectorPhysicsModel::Fields(rb);
    kb::editor::tests::Require(rbFields.size() == 12U, "Rigidbody exposes 12 inspector fields");
    kb::editor::tests::Require(rbFields[0].kind == PhysicsFieldKind::Enum && rbFields[0].value == "Dynamic",
        "Rigidbody field 0 is the Body Type enum defaulting to Dynamic");
    kb::editor::tests::Require(InspectorPhysicsModel::CycleEnum(rb, 0) && rb.bodyType == kb::scene::RigidbodyBodyType::Kinematic,
        "Cycling Rigidbody body type Dynamic -> Kinematic");
    kb::editor::tests::Require(InspectorPhysicsModel::ApplyFloat(rb, 1, 7.5F) && rb.mass == 7.5F, "Applying the Rigidbody mass field writes mass");
    float readMass = 0.0F;
    kb::editor::tests::Require(InspectorPhysicsModel::ReadFloat(rb, 1, readMass) && readMass == 7.5F, "Reading the Rigidbody mass field returns mass");
    kb::editor::tests::Require(rb.useGravity && InspectorPhysicsModel::ToggleBool(rb, 3) && !rb.useGravity, "Toggling Rigidbody Use Gravity flips it");
    kb::editor::tests::Require(!InspectorPhysicsModel::ApplyFloat(rb, 3, 1.0F), "ApplyFloat rejects a Bool field index");
    kb::editor::tests::Require(!InspectorPhysicsModel::ToggleBool(rb, 1), "ToggleBool rejects a Float field index");

    // Collider: shape cycle, vec3 flattened into X/Y/Z floats, trigger toggle.
    kb::scene::ColliderComponent col;
    kb::editor::tests::Require(InspectorPhysicsModel::CycleEnum(col, 0) && col.shape == kb::scene::ColliderShape::Sphere, "Cycling Collider shape Box -> Sphere");
    kb::editor::tests::Require(InspectorPhysicsModel::ApplyFloat(col, 4, 3.0F) && col.boxSize.x == 3.0F, "Collider Box Size X maps to boxSize.x");
    kb::editor::tests::Require(!col.trigger && InspectorPhysicsModel::ToggleBool(col, 9) && col.trigger, "Toggling Collider Is Trigger flips it");

    // Character Controller: no enum field; floats + one bool.
    kb::scene::CharacterControllerComponent cc;
    kb::editor::tests::Require(!InspectorPhysicsModel::CycleEnum(cc, 0), "Character Controller has no enum field");
    kb::editor::tests::Require(InspectorPhysicsModel::ApplyFloat(cc, 5, 42.0F) && cc.slopeLimitDegrees == 42.0F, "Character Controller Slope Limit writes slopeLimitDegrees");
    kb::editor::tests::Require(cc.useGravity && InspectorPhysicsModel::ToggleBool(cc, 8) && !cc.useGravity, "Toggling Character Controller Use Gravity flips it");

    // Joint: type cycle, float, enable-limit toggle.
    kb::scene::JointComponent joint;
    kb::editor::tests::Require(InspectorPhysicsModel::CycleEnum(joint, 0) && joint.type == kb::scene::JointType::Hinge, "Cycling Joint type Fixed -> Hinge");
    kb::editor::tests::Require(InspectorPhysicsModel::ApplyFloat(joint, 10, -1.5F) && joint.minLimit == -1.5F, "Joint Min Limit writes minLimit");
    kb::editor::tests::Require(!joint.enableLimit && InspectorPhysicsModel::ToggleBool(joint, 12) && joint.enableLimit, "Toggling Joint Enable Limit flips it");

    kb::editor::tests::Require(InspectorPhysicsModel::KindOf(PhysicsComponentKind::Joint, 0) == PhysicsFieldKind::Enum, "KindOf reports the Joint type field as Enum");
    kb::editor::tests::Require(InspectorPhysicsModel::KindOf(PhysicsComponentKind::Rigidbody, 99) == PhysicsFieldKind::Float, "KindOf returns Float for an out-of-range index");
}

// Collapse state is a set of section ids, so every section — including the physics
// component sections added later — toggles independently. Guards the bug where
// physics section headers refused to collapse (a hand-maintained switch omitted them).
void RunInspectorSectionCollapseTest() {
    kb::editor::InspectorPanelState state;
    kb::editor::tests::Require(!state.IsCollapsed(kb::editor::InspectorSectionId::Collider), "Collider section starts expanded");
    state.ToggleCollapsed(kb::editor::InspectorSectionId::Collider);
    kb::editor::tests::Require(state.IsCollapsed(kb::editor::InspectorSectionId::Collider), "Toggling the Collider header collapses it");
    kb::editor::tests::Require(!state.IsCollapsed(kb::editor::InspectorSectionId::Rigidbody), "Collapsing Collider does not affect other sections");
    state.ToggleCollapsed(kb::editor::InspectorSectionId::Collider);
    kb::editor::tests::Require(!state.IsCollapsed(kb::editor::InspectorSectionId::Collider), "Toggling again expands the Collider section");
    for (const kb::editor::InspectorSectionId section : { kb::editor::InspectorSectionId::Rigidbody, kb::editor::InspectorSectionId::CharacterController, kb::editor::InspectorSectionId::Joint, kb::editor::InspectorSectionId::MeshRenderer, kb::editor::InspectorSectionId::Script }) {
        state.ToggleCollapsed(section);
        kb::editor::tests::Require(state.IsCollapsed(section), "Every section id collapses via the set-backed state");
    }
}

// Hover is index-aware so only the physics/script row actually under the cursor
// highlights — not every sibling row sharing the same property id. Guards the bug
// where hovering one Collider field highlighted all of them.
void RunInspectorHoverIndexTest() {
    kb::editor::InspectorPanelState state;
    static_cast<void>(state.SetHover(kb::editor::InspectorHitKind::FloatField, kb::editor::InspectorSectionId::Collider, kb::editor::InspectorPropertyId::ColliderField, 4));
    kb::editor::tests::Require(state.HoveredIndex() == 4, "SetHover records the row index");
    kb::editor::tests::Require(state.IsHovered(kb::editor::InspectorHitKind::FloatField, kb::editor::InspectorSectionId::Collider, kb::editor::InspectorPropertyId::ColliderField, 4),
        "The hovered Collider row (index 4) reports hovered");
    kb::editor::tests::Require(!state.IsHovered(kb::editor::InspectorHitKind::FloatField, kb::editor::InspectorSectionId::Collider, kb::editor::InspectorPropertyId::ColliderField, 5),
        "A sibling Collider row (index 5) sharing the property id is NOT hovered");
    kb::editor::tests::Require(state.IsHovered(kb::editor::InspectorHitKind::FloatField, kb::editor::InspectorSectionId::Collider, kb::editor::InspectorPropertyId::ColliderField, -1),
        "An index-blind query still matches the hovered property");
}

// The Unity-style Add Component menu model: two-level rows (categories ->
// components), search override, and the scroll/virtualization arithmetic.
void RunAddComponentBrowserModelTest() {
    using kb::editor::AddComponentRowKind;
    using kb::editor::InspectorAddComponentBrowserModel;

    const std::vector<kb::editor::AddComponentRow> categories = InspectorAddComponentBrowserModel::Rows("", "");
    kb::editor::tests::Require(!categories.empty() && categories.front().kind == AddComponentRowKind::Category, "Top level lists category rows");
    bool hasPhysics = false;
    for (const kb::editor::AddComponentRow& row : categories) {
        if (row.kind == AddComponentRowKind::Category && row.id == "Physics") {
            hasPhysics = true;
        }
    }
    kb::editor::tests::Require(hasPhysics, "The category list contains Physics");

    const std::vector<kb::editor::AddComponentRow> physics = InspectorAddComponentBrowserModel::Rows("Physics", "");
    kb::editor::tests::Require(physics.size() == 4U && physics.front().kind == AddComponentRowKind::Component, "A category lists its 4 components as Component rows");

    const std::vector<kb::editor::AddComponentRow> search = InspectorAddComponentBrowserModel::Rows("Physics", "collid");
    kb::editor::tests::Require(search.size() == 1U && search.front().id == "Collider", "A search query overrides the category and finds Collider");

    kb::editor::tests::Require(InspectorAddComponentBrowserModel::TotalHeight(10, 26) == 260, "TotalHeight is rows * rowHeight");
    kb::editor::tests::Require(InspectorAddComponentBrowserModel::MaxScroll(10, 26, 100) == 160, "MaxScroll is total minus the list height");
    kb::editor::tests::Require(InspectorAddComponentBrowserModel::MaxScroll(3, 26, 100) == 0, "MaxScroll is 0 when every row fits");

    const InspectorAddComponentBrowserModel::VisibleWindow window = InspectorAddComponentBrowserModel::Visible(20, 52, 26, 100);
    kb::editor::tests::Require(window.first == 2, "Virtualization skips the fully scrolled-past rows");
    kb::editor::tests::Require(window.first + window.count <= 20, "The visible window never runs past the row list");
    kb::editor::tests::Require(window.count >= 4, "The visible window covers the list height plus partial rows");
}

} // namespace

namespace kb::editor::tests {

void RunEditorInspectorTests() {
    RunInspectorPhysicsModelTest();
    RunInspectorSectionCollapseTest();
    RunInspectorHoverIndexTest();
    RunAddComponentBrowserModelTest();
    RunInspectorTextEditDirtyStateTest();
    RunAudioComponentCatalogTest();
    RunAudioInspectorTextTest();
    RunMaterialTextureSlotDiagnosticTest();
    RunAudioAssetAssignmentTest();
    RunMaterialAssetAssignmentSavesInSceneTest();
    RunMeshRendererMeshAssignmentActionTest();
    RunMeshRendererMaterialSlotModelTest();
    RunMaterialCreateAssignSaveReloadE2ETest();
    RunMaterialRenamePreservesMeshRendererAssignmentTest();
    RunMaterialReferenceFinderReportsMeshRendererUsageTest();
    RunMaterialEditorStateIndependentFromInspectorSelectionTest();
    RunMaterialEditorGraphBackedSchemaParameterModelTest();
    RunEditorMaterialSlotOverrideSyncTest();
    RunMaterialAssignmentPathRenderSyncTest();
    RunMaterialAssignmentUndoRedoTest();
    RunMaterialPreviewMeshFactoryTest();
    RunMaterialPreviewSceneBuildsRenderableMaterialTest();
    RunMaterialNodePreviewBuilderTest();
    RunMaterialPreviewGpuGraphParityTest();
    RunMaterialValueFormatterTest();
    RunMaterialEditorFinitePresentationParsingTest();
#if defined(_WIN32)
    RunMaterialEditorGraphLayoutAndHitTestTest();
    RunMaterialEditorDetailsCanonicalLayoutTest();
    RunMaterialEditorOpaqueOverlayAndTexturePickerLayoutTest();
    RunMaterialEditorParserDiagnosticRowsTest();
    RunMaterialEditorGraphDiagnosticsRefreshTest();
    RunMaterialEditorGraphNodeRenameTest();
#endif
}

} // namespace kb::editor::tests
