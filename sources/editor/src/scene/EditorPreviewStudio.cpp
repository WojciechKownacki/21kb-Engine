#include "scene/EditorPreviewStudio.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/AmbientRadianceComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneLightingAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/WorldBackdropComponent.hpp"
#include "scene/material_preview/EditorMaterialPreviewMeshLoader.hpp"
#include "scene/material_preview/EditorMaterialPreviewPrimitivePolicy.hpp"

#include <memory>

namespace kb::editor {
namespace {

void RegisterPreviewFloorAsset(kb::assets::AssetManager& manager) {
    const kb::assets::AssetId planeAssetId =
        EditorMaterialPreviewPrimitivePolicy::GeneratedMeshAssetId(EditorMaterialPreviewPrimitiveKind::Plane);
    static_cast<void>(manager.RegisterLoader(std::make_unique<EditorMaterialPreviewMeshLoader>()));
    static_cast<void>(manager.RegisterAsset(kb::assets::AssetMetadata{
        .id = planeAssetId,
        .type = "RenderMesh",
        .name = "Preview Floor",
        .virtualPath = "/Editor/Preview/Floor",
        .physicalPath = "__editor_preview_floor__",
        .runtimeLoadable = true,
    }));
}

} // namespace

EditorPreviewStudio BuildEditorPreviewStudio(
    kb::scene::Scene& scene, const EditorViewportCameraState& camera, float floorHeight, float floorExtent) {
    RegisterPreviewFloorAsset(scene.Assets().Manager());
    EditorPreviewStudio studio;

    kb::scene::SceneObjectDesc cameraDesc{ .name = "Preview Camera" };
    cameraDesc.transform.localPosition = camera.Position();
    studio.camera = scene.Entities().CreateEntity(cameraDesc);
    static_cast<void>(scene.Components().Cameras().Set(studio.camera, kb::scene::CameraComponent{
        .verticalFovDegrees = camera.VerticalFovDegrees(),
        .nearClip = camera.NearClip(),
        .farClip = camera.FarClip(),
        .primary = true,
        .clearColor = kb::scene::Vec3{ 0.055F, 0.065F, 0.085F },
    }));

    kb::scene::SceneObjectDesc floorDesc{ .name = "Preview Floor" };
    floorDesc.transform.localPosition = kb::scene::Vec3{ 0.0F, floorHeight, 0.0F };
    floorDesc.transform.localRotation = kb::math::FromToRotation(
        kb::scene::Vec3{ 0.0F, 0.0F, 1.0F }, kb::scene::Vec3{ 0.0F, 1.0F, 0.0F });
    floorDesc.transform.localScale = kb::scene::Vec3{ floorExtent, floorExtent, 1.0F };
    studio.floor = scene.Entities().CreateEntity(floorDesc);
    static_cast<void>(scene.Components().MeshRenderers().Set(studio.floor, kb::scene::MeshRendererComponent{
        .meshAssetId = EditorMaterialPreviewPrimitivePolicy::GeneratedMeshAssetId(EditorMaterialPreviewPrimitiveKind::Plane).value,
    }));

    kb::scene::SceneObjectDesc keyLightDesc{ .name = "Preview Key Light" };
    keyLightDesc.transform.localPosition = kb::scene::Vec3{ 3.0F, 6.0F, -4.0F };
    const kb::scene::SceneEntity keyLight = scene.Entities().CreateEntity(keyLightDesc);
    static_cast<void>(scene.Components().Lights().Set(keyLight, kb::scene::LightComponent{
        .kind = kb::scene::LightKind::Directional,
        .color = kb::scene::Vec3{ 1.0F, 0.95F, 0.88F },
        .intensity = 2.0F,
        .castsShadow = true,
    }));

    kb::scene::SceneObjectDesc fillLightDesc{ .name = "Preview Fill Light" };
    fillLightDesc.transform.localPosition = kb::scene::Vec3{ -4.0F, 3.0F, 2.0F };
    const kb::scene::SceneEntity fillLight = scene.Entities().CreateEntity(fillLightDesc);
    static_cast<void>(scene.Components().Lights().Set(fillLight, kb::scene::LightComponent{
        .kind = kb::scene::LightKind::Point,
        .color = kb::scene::Vec3{ 0.54F, 0.68F, 1.0F },
        .intensity = 7.0F,
        .range = 12.0F,
        .castsShadow = false,
    }));

    studio.environment = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Preview Environment" });
    static_cast<void>(scene.Components().WorldBackdrops().Set(studio.environment, kb::scene::WorldBackdropComponent{
        .mode = kb::scene::WorldBackdropMode::VerticalGradient,
        .horizonColor = kb::scene::Vec3{ 0.08F, 0.10F, 0.14F },
        .zenithColor = kb::scene::Vec3{ 0.23F, 0.31F, 0.45F },
        .gradientExponent = 1.35F,
        .priority = 100,
        .enabled = true,
    }));
    static_cast<void>(scene.Components().AmbientRadiances().Set(studio.environment, kb::scene::AmbientRadianceComponent{
        .mode = kb::scene::AmbientRadianceMode::Gradient,
        .horizonColor = kb::scene::Vec3{ 0.10F, 0.12F, 0.16F },
        .zenithColor = kb::scene::Vec3{ 0.30F, 0.38F, 0.54F },
        .intensity = 0.75F,
        .diffuseIntensity = 1.0F,
        .specularIntensity = 0.35F,
        .priority = 100,
        .enabled = true,
    }));
    kb::scene::SceneLightingAccess::SetBasicLightingEnabled(scene, true);
    return studio;
}

void SynchronizeEditorPreviewCamera(
    kb::scene::Scene& scene, kb::scene::SceneEntity cameraEntity, const EditorViewportCameraState& camera) noexcept {
    if (!cameraEntity.IsValid() || !scene.Entities().IsAlive(cameraEntity)) {
        return;
    }
    kb::scene::TransformComponent transform = scene.Transforms().Get(cameraEntity);
    const EditorViewportCameraAxes axes = camera.Axes();
    transform.localPosition = axes.position;
    transform.localRotation = kb::math::LookRotation(axes.forward, axes.up);
    scene.Transforms().Set(cameraEntity, transform);
    scene.Runtime().SynchronizeTransforms();
}

} // namespace kb::editor
