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
#include "inspection/EditorValueFormatter.hpp"
#include "inspection/MaterialAssetFormatter.hpp"
#include "inspection/InspectorPanelState.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/scene/EcsRenderSceneSynchronizer.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderer.hpp"
#include "scene/EditorSceneAudioAssetActions.hpp"
#include "scene/EditorSceneMaterialAssetActions.hpp"
#include "scene/material_preview/EditorMaterialPreviewMeshFactory.hpp"
#include "scene/material_preview/EditorMaterialPreviewMeshLoader.hpp"
#include "scene/material_preview/EditorMaterialPreviewScene.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"

#include <algorithm>
#include <array>
#include <bgfx/bgfx.h>
#include <cstddef>
#include <filesystem>
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
void RunMaterialEditorMvpLayoutKeepsParametersVisibleTest() {
    const RECT content{0, 0, 440, 540};
    const kb::editor::MaterialEditorPanelLayout layout = kb::editor::MaterialEditorPanelRenderer::ResolveLayout(content);
    kb::editor::tests::Require(layout.previewFrame.top > content.top, "Material Editor preview should be the first body element");
    kb::editor::tests::Require(layout.previewFrame.bottom < layout.parameterSectionTop, "Material Editor parameters should follow the preview");
    kb::editor::tests::Require(layout.mvpParameterBottom <= content.bottom - 12, "Material Editor MVP parameter rows should fit in the default panel height");
    kb::editor::tests::Require(layout.mvpParameterBottom < layout.textureSectionTop, "Material Editor texture slots should follow the MVP parameters");
    kb::editor::tests::Require(layout.textureSlotBottom <= content.bottom - 12, "Material Editor texture slot rows should fit in the default panel height");
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
    RunEditorMaterialSlotOverrideSyncTest();
    RunMaterialPreviewMeshFactoryTest();
    RunMaterialPreviewSceneBuildsRenderableMaterialTest();
    RunMaterialValueFormatterTest();
#if defined(_WIN32)
    RunMaterialEditorMvpLayoutKeepsParametersVisibleTest();
#endif
}

} // namespace kb::editor::tests
