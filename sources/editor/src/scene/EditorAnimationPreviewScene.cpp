#include "scene/EditorAnimationPreviewScene.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/AmbientRadianceComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/DrawD3DeformedGeometryComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneLightingAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SkeletonAsset.hpp"
#include "engine/scene/SkeletonBindingComponent.hpp"
#include "engine/scene/SkeletalMeshAsset.hpp"
#include "engine/scene/WorldBackdropComponent.hpp"
#include "scene/material_preview/EditorMaterialPreviewMeshLoader.hpp"
#include "scene/material_preview/EditorMaterialPreviewPrimitivePolicy.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

namespace kb::editor {
namespace {

constexpr float kPreviewFloorExtent = 12.0F;
constexpr float kPreviewFloorHeight = -0.01F;

void RegisterPreviewFloorAsset(kb::assets::AssetManager& manager) {
    const kb::assets::AssetId planeAssetId =
        EditorMaterialPreviewPrimitivePolicy::GeneratedMeshAssetId(EditorMaterialPreviewPrimitiveKind::Plane);
    static_cast<void>(manager.RegisterLoader(std::make_unique<EditorMaterialPreviewMeshLoader>()));
    static_cast<void>(manager.RegisterAsset(kb::assets::AssetMetadata{
        .id = planeAssetId,
        .type = "RenderMesh",
        .name = "Animation Preview Floor",
        .virtualPath = "/Editor/Preview/AnimationFloor",
        .physicalPath = "__editor_animation_preview_floor__",
        .runtimeLoadable = true,
    }));
}

[[nodiscard]] float BoundsRadius(const kb::scene::SkeletalMeshBounds& bounds) noexcept {
    return std::max(0.25F, kb::math::Length(bounds.extents));
}

} // namespace

const kb::scene::Scene& EditorAnimationPreviewScene::SceneFor(
    const kb::scene::Scene& source, const AnimationPreviewContext& context) {
    if (scene_ == nullptr || sourceSceneId_ != source.Id() || contextRevision_ != context.Revision()) {
        Rebuild(source, context);
    }
    SynchronizeCamera();
    return *scene_;
}

void EditorAnimationPreviewScene::Focus(float durationSeconds) noexcept {
    camera_.FocusOn(focusCenter_, focusRadius_, durationSeconds);
    SynchronizeCamera();
}

bool EditorAnimationPreviewScene::TickCamera(float deltaSeconds) noexcept {
    const bool animating = camera_.TickFocus(deltaSeconds);
    SynchronizeCamera();
    return animating;
}

void EditorAnimationPreviewScene::Rebuild(
    const kb::scene::Scene& source, const AnimationPreviewContext& context) {
    scene_ = std::make_unique<kb::scene::Scene>(kb::scene::SceneMode::Runtime);
    for (const kb::assets::AssetMetadata& metadata : source.Assets().Manager().Registry().All()) {
        static_cast<void>(scene_->Assets().Manager().RegisterAsset(metadata));
    }
    RegisterPreviewFloorAsset(scene_->Assets().Manager());
    previewEntity_ = scene_->Entities().CreateEntity(
        kb::scene::SceneObjectDesc{ .name = "Animation Preview" });
    const kb::assets::AssetHandle<kb::scene::SkeletalMeshAsset> mesh =
        context.SkeletalMeshAsset().IsValid()
        ? scene_->Assets().Manager().Load<kb::scene::SkeletalMeshAsset>(context.SkeletalMeshAsset())
        : kb::assets::AssetHandle<kb::scene::SkeletalMeshAsset>{};
    bool compatible = mesh.IsLoaded();
    std::uint64_t skeletonSignature = 0U;
    if (compatible && context.SkeletonAsset().IsValid()) {
        const kb::assets::AssetHandle<kb::scene::SkeletonAsset> skeleton =
            scene_->Assets().Manager().Load<kb::scene::SkeletonAsset>(context.SkeletonAsset());
        skeletonSignature = skeleton.IsLoaded() ? kb::scene::SkeletonCompatibilitySignature(*skeleton) : 0U;
        compatible = skeletonSignature != 0U &&
            mesh->skeletonAssetId == context.SkeletonAsset().value &&
            mesh->skeletonCompatibilitySignature == skeletonSignature;
    }
    static_cast<void>(scene_->Components().MeshRenderers().Set(previewEntity_, kb::scene::MeshRendererComponent{
        .meshAssetId = context.SkeletalMeshAsset().value,
    }));
    static_cast<void>(scene_->Components().DeformedGeometries().Set(previewEntity_, kb::scene::DrawD3DeformedGeometryComponent{
        .skeletalMeshAssetId = context.SkeletalMeshAsset().value,
        .enabled = compatible,
    }));
    if (compatible && context.SkeletonAsset().IsValid()) {
        static_cast<void>(scene_->Components().SkeletonBindings().Set(previewEntity_, kb::scene::SkeletonBindingComponent{
            .skeletonAssetId = context.SkeletonAsset().value,
            .skeletonCompatibilitySignature = skeletonSignature,
            .enabled = true,
        }));
    }
    if (compatible && context.PoseMode() == AnimationPreviewPoseMode::Animated &&
        context.ControllerAsset().IsValid()) {
        static_cast<void>(scene_->Components().Animators().Set(previewEntity_, kb::scene::Animator{
            .controllerAssetId = context.ControllerAsset().value,
            .enabled = true,
        }));
    }

    focusCenter_ = mesh.IsLoaded() ? mesh->conservativeBounds.center : kb::scene::Vec3{};
    focusRadius_ = mesh.IsLoaded() ? BoundsRadius(mesh->conservativeBounds) : 1.0F;

    kb::scene::SceneObjectDesc cameraDesc{ .name = "Animation Preview Camera" };
    cameraDesc.transform.localPosition = camera_.Position();
    cameraEntity_ = scene_->Entities().CreateEntity(cameraDesc);
    static_cast<void>(scene_->Components().Cameras().Set(cameraEntity_, kb::scene::CameraComponent{
        .verticalFovDegrees = camera_.VerticalFovDegrees(),
        .nearClip = camera_.NearClip(),
        .farClip = camera_.FarClip(),
        .primary = true,
        .clearColor = kb::scene::Vec3{ 0.055F, 0.065F, 0.085F },
    }));

    kb::scene::SceneObjectDesc floorDesc{ .name = "Animation Preview Floor" };
    floorDesc.transform.localPosition = kb::scene::Vec3{ 0.0F, kPreviewFloorHeight, 0.0F };
    floorDesc.transform.localRotation = kb::math::FromToRotation(
        kb::scene::Vec3{ 0.0F, 0.0F, -1.0F }, kb::scene::Vec3{ 0.0F, 1.0F, 0.0F });
    floorDesc.transform.localScale = kb::scene::Vec3{ kPreviewFloorExtent, kPreviewFloorExtent, 1.0F };
    floorEntity_ = scene_->Entities().CreateEntity(floorDesc);
    static_cast<void>(scene_->Components().MeshRenderers().Set(floorEntity_, kb::scene::MeshRendererComponent{
        .meshAssetId = EditorMaterialPreviewPrimitivePolicy::GeneratedMeshAssetId(EditorMaterialPreviewPrimitiveKind::Plane).value,
    }));

    kb::scene::SceneObjectDesc keyLightDesc{ .name = "Animation Preview Key Light" };
    keyLightDesc.transform.localPosition = kb::scene::Vec3{ 3.0F, 6.0F, -4.0F };
    const kb::scene::SceneEntity keyLight = scene_->Entities().CreateEntity(keyLightDesc);
    static_cast<void>(scene_->Components().Lights().Set(keyLight, kb::scene::LightComponent{
        .kind = kb::scene::LightKind::Directional,
        .color = kb::scene::Vec3{ 1.0F, 0.95F, 0.88F },
        .intensity = 2.0F,
        .castsShadow = true,
    }));

    kb::scene::SceneObjectDesc fillLightDesc{ .name = "Animation Preview Fill Light" };
    fillLightDesc.transform.localPosition = kb::scene::Vec3{ -4.0F, 3.0F, 2.0F };
    const kb::scene::SceneEntity fillLight = scene_->Entities().CreateEntity(fillLightDesc);
    static_cast<void>(scene_->Components().Lights().Set(fillLight, kb::scene::LightComponent{
        .kind = kb::scene::LightKind::Point,
        .color = kb::scene::Vec3{ 0.54F, 0.68F, 1.0F },
        .intensity = 7.0F,
        .range = 12.0F,
        .castsShadow = false,
    }));

    environmentEntity_ = scene_->Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Animation Preview Environment" });
    static_cast<void>(scene_->Components().WorldBackdrops().Set(environmentEntity_, kb::scene::WorldBackdropComponent{
        .mode = kb::scene::WorldBackdropMode::VerticalGradient,
        .horizonColor = kb::scene::Vec3{ 0.08F, 0.10F, 0.14F },
        .zenithColor = kb::scene::Vec3{ 0.23F, 0.31F, 0.45F },
        .gradientExponent = 1.35F,
        .priority = 100,
        .enabled = true,
    }));
    static_cast<void>(scene_->Components().AmbientRadiances().Set(environmentEntity_, kb::scene::AmbientRadianceComponent{
        .mode = kb::scene::AmbientRadianceMode::Gradient,
        .horizonColor = kb::scene::Vec3{ 0.10F, 0.12F, 0.16F },
        .zenithColor = kb::scene::Vec3{ 0.30F, 0.38F, 0.54F },
        .intensity = 0.75F,
        .diffuseIntensity = 1.0F,
        .specularIntensity = 0.35F,
        .priority = 100,
        .enabled = true,
    }));
    kb::scene::SceneLightingAccess::SetBasicLightingEnabled(*scene_, true);

    Focus(0.0F);
    static_cast<void>(scene_->Runtime().Update(0.0F));
    sourceSceneId_ = source.Id();
    contextRevision_ = context.Revision();
    ++revision_;
}

void EditorAnimationPreviewScene::SynchronizeCamera() noexcept {
    if (scene_ == nullptr || !cameraEntity_.IsValid() || !scene_->Entities().IsAlive(cameraEntity_)) {
        return;
    }
    kb::scene::TransformComponent transform = scene_->Transforms().Get(cameraEntity_);
    const EditorViewportCameraAxes axes = camera_.Axes();
    transform.localPosition = axes.position;
    transform.localRotation = kb::math::LookRotation(axes.forward, axes.up);
    scene_->Transforms().Set(cameraEntity_, transform);
    scene_->Runtime().SynchronizeTransforms();
}

void EditorAnimationPreviewScene::Clear() noexcept {
    scene_.reset();
    previewEntity_ = {};
    cameraEntity_ = {};
    floorEntity_ = {};
    environmentEntity_ = {};
    focusCenter_ = {};
    focusRadius_ = 1.0F;
    sourceSceneId_ = 0U;
    contextRevision_ = 0U;
    ++revision_;
}

} // namespace kb::editor
