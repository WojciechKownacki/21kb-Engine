#include "scene/material_preview/EditorMaterialPreviewScene.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneLightingAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "scene/material/EditorMaterialAssetGateway.hpp"
#include "scene/material_preview/EditorMaterialPreviewMeshLoader.hpp"

#include <memory>
#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] std::uint64_t MaterialContentHash(const kb::scene::Scene& scene, kb::assets::AssetId materialAssetId) noexcept {
    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(materialAssetId);
    return metadata == nullptr ? 0U : metadata->contentHash;
}

void RegisterPreviewLoaders(kb::assets::AssetManager& manager) {
    static_cast<void>(manager.RegisterLoader(std::make_unique<EditorMaterialPreviewMeshLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderTextureAssetLoader>()));
}

void CopyAssetRegistry(const kb::assets::AssetManager& source, kb::assets::AssetManager& target) {
    for (const kb::assets::AssetMetadata& metadata : source.Registry().All()) {
        static_cast<void>(target.RegisterAsset(metadata));
    }
}

void RegisterPreviewMesh(kb::assets::AssetManager& manager) {
    static_cast<void>(manager.RegisterAsset(kb::assets::AssetMetadata{
        .id = EditorMaterialPreviewMeshLoader::PreviewMeshAssetId(),
        .type = "RenderMesh",
        .name = "Material Preview Sphere",
        .virtualPath = "/Editor/Preview/MaterialSphere",
        .physicalPath = "__editor_material_preview_sphere__",
        .runtimeLoadable = true,
    }));
}

void AddPreviewMesh(kb::scene::Scene& scene, kb::assets::AssetId materialAssetId, bool materialLoaded) {
    const kb::scene::SceneEntity mesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{.name = "Material Preview Mesh"});
    scene.Components().MeshRenderers().Set(mesh, kb::scene::MeshRendererComponent{
        .meshAssetId = EditorMaterialPreviewMeshLoader::PreviewMeshAssetId().value,
        .materialAssetId = materialLoaded ? materialAssetId.value : 0U,
    });
}

void AddPreviewCamera(kb::scene::Scene& scene) {
    kb::scene::SceneObjectDesc cameraDesc{.name = "Material Preview Camera"};
    cameraDesc.transform.localPosition = kb::scene::Vec3{0.0F, 0.0F, -4.0F};
    const kb::scene::SceneEntity camera = scene.Entities().CreateEntity(cameraDesc);
    scene.Components().Cameras().Set(camera, kb::scene::CameraComponent{
        .verticalFovDegrees = 38.0F,
        .nearClip = 0.05F,
        .farClip = 50.0F,
        .primary = true,
    });
}

void AddPreviewLighting(kb::scene::Scene& scene) {
    kb::scene::SceneObjectDesc keyLightDesc{.name = "Material Preview Key Light"};
    keyLightDesc.transform.localPosition = kb::scene::Vec3{-2.0F, 2.0F, -3.0F};
    const kb::scene::SceneEntity keyLight = scene.Entities().CreateEntity(keyLightDesc);
    scene.Components().Lights().Set(keyLight, kb::scene::LightComponent{
        .kind = kb::scene::LightKind::Point,
        .color = kb::scene::Vec3{1.0F, 0.96F, 0.90F},
        .intensity = 4.0F,
        .range = 10.0F,
        .castsShadow = false,
    });

    kb::scene::SceneObjectDesc fillLightDesc{.name = "Material Preview Fill Light"};
    fillLightDesc.transform.localPosition = kb::scene::Vec3{2.0F, 1.0F, -2.0F};
    const kb::scene::SceneEntity fillLight = scene.Entities().CreateEntity(fillLightDesc);
    scene.Components().Lights().Set(fillLight, kb::scene::LightComponent{
        .kind = kb::scene::LightKind::Point,
        .color = kb::scene::Vec3{0.72F, 0.82F, 1.0F},
        .intensity = 1.2F,
        .range = 8.0F,
        .castsShadow = false,
    });

    kb::scene::SceneLightingAccess::SetBasicLightingEnabled(scene, true);
}

} // namespace

const kb::scene::Scene& EditorMaterialPreviewScene::SceneFor(const kb::scene::Scene& sourceScene, kb::assets::AssetId materialAssetId) {
    const std::uint64_t sourceRevision = sourceScene.Assets().Manager().Revision();
    const std::uint64_t contentHash = MaterialContentHash(sourceScene, materialAssetId);
    if (scene_ == nullptr || materialAssetId_.value != materialAssetId.value || sourceAssetRevision_ != sourceRevision || materialContentHash_ != contentHash) {
        Rebuild(sourceScene, materialAssetId);
    }
    return *scene_;
}

const EditorMaterialPreviewTelemetry& EditorMaterialPreviewScene::Telemetry() const noexcept {
    return telemetry_;
}

std::uint64_t EditorMaterialPreviewScene::Revision() const noexcept {
    return revision_;
}

void EditorMaterialPreviewScene::Clear() noexcept {
    scene_.reset();
    telemetry_ = {};
    materialAssetId_ = {};
    sourceAssetRevision_ = 0U;
    materialContentHash_ = 0U;
    ++revision_;
}

void EditorMaterialPreviewScene::Rebuild(const kb::scene::Scene& sourceScene, kb::assets::AssetId materialAssetId) {
    scene_ = std::make_unique<kb::scene::Scene>(kb::scene::SceneMode::Runtime);
    kb::assets::AssetManager& targetManager = scene_->Assets().Manager();
    RegisterPreviewLoaders(targetManager);
    CopyAssetRegistry(sourceScene.Assets().Manager(), targetManager);
    RegisterPreviewMesh(targetManager);

    const std::optional<kb::render::RenderMaterialAssetData> material = EditorMaterialAssetGateway::Read(sourceScene, materialAssetId);
    AddPreviewMesh(*scene_, materialAssetId, material.has_value());
    AddPreviewCamera(*scene_);
    AddPreviewLighting(*scene_);
    scene_->Runtime().SynchronizeTransforms();

    telemetry_ = EditorMaterialPreviewTelemetryBuilder::Build(sourceScene.Assets().Manager(), materialAssetId, material.has_value() ? &*material : nullptr, true);
    materialAssetId_ = materialAssetId;
    sourceAssetRevision_ = sourceScene.Assets().Manager().Revision();
    materialContentHash_ = MaterialContentHash(sourceScene, materialAssetId);
    ++revision_;
}

} // namespace kb::editor
