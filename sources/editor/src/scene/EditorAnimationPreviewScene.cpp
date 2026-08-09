#include "scene/EditorAnimationPreviewScene.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/AnimationAssets.hpp"
#include "engine/scene/AmbientRadianceComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/DrawD3DeformedGeometryComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneAnimators.hpp"
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
#include "diagnostics/EditorLagTrace.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>

namespace kb::editor {
namespace {

constexpr float kPreviewFloorExtent = 12.0F;
constexpr float kPreviewFloorHeight = -0.01F;

[[nodiscard]] bool RequiresLegacyFbxUpAxisCorrection(
    const kb::scene::SkeletalMeshBounds& bounds) noexcept {
    // Skeletal FBX assets produced before the explicit ufbx mirror-axis conversion were reflected
    // below Y=0: their complete AABB is below the preview floor and the character faces downward.
    // Limit this compatibility correction to that unambiguous legacy shape. Assets that cross the
    // ground plane, including intentionally off-centre rigs, retain their authored orientation.
    const float tolerance = std::max(0.001F, bounds.extents.y * 0.01F);
    return bounds.center.y < -tolerance &&
        bounds.center.y + bounds.extents.y <= tolerance;
}

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

template <typename T>
[[nodiscard]] kb::assets::AssetHandle<T> ShareOrLoadPreviewAsset(
    const kb::scene::Scene& source,
    kb::assets::AssetManager& previewAssets,
    kb::assets::AssetId id) {
    if (!id.IsValid()) return {};
    const kb::assets::AssetHandle<T> sourceAsset = source.Assets().Manager().AcquireLoaded<T>(id);
    if (sourceAsset.IsLoaded()) {
        // Runtime assets are exposed as immutable handles. PublishRuntimeAsset takes
        // mutable ownership only to erase the payload type; neither manager exposes a
        // mutable asset after publication, so both scenes can safely share the payload.
        std::shared_ptr<T> shared = std::const_pointer_cast<T>(sourceAsset.Shared());
        if (previewAssets.PublishRuntimeAsset(id, std::move(shared))) {
            return previewAssets.AcquireLoaded<T>(id);
        }
    }
    return previewAssets.Load<T>(id);
}

[[nodiscard]] float BoundsRadius(const kb::scene::SkeletalMeshBounds& bounds) noexcept {
    return std::max(0.25F, kb::math::Length(bounds.extents));
}

[[nodiscard]] kb::scene::Vec3 WorldPoint(
    const kb::scene::WorldTransform& transform, kb::scene::Vec3 point) noexcept {
    const kb::scene::Vec3 scaled{
        point.x * transform.scale.x,
        point.y * transform.scale.y,
        point.z * transform.scale.z,
    };
    return transform.position + kb::math::Rotate(transform.rotation, scaled);
}

[[nodiscard]] kb::scene::Vec3 WorldDirection(
    const kb::scene::WorldTransform& transform, kb::scene::Vec3 direction) noexcept {
    return kb::math::Normalize(kb::math::Rotate(transform.rotation, direction));
}

void AppendBoundsLines(
    AnimationPreviewOverlaySnapshot& output,
    const kb::scene::SkeletalMeshBounds& bounds,
    const kb::scene::WorldTransform& owner) {
    std::array<kb::scene::Vec3, 8U> corners{};
    for (std::uint32_t index = 0U; index < corners.size(); ++index) {
        corners[index] = WorldPoint(owner, kb::scene::Vec3{
            bounds.center.x + ((index & 1U) == 0U ? -bounds.extents.x : bounds.extents.x),
            bounds.center.y + ((index & 2U) == 0U ? -bounds.extents.y : bounds.extents.y),
            bounds.center.z + ((index & 4U) == 0U ? -bounds.extents.z : bounds.extents.z),
        });
    }
    constexpr std::array<std::array<std::uint32_t, 2U>, 12U> edges{{
        {{0U, 1U}}, {{0U, 2U}}, {{0U, 4U}}, {{1U, 3U}}, {{1U, 5U}}, {{2U, 3U}},
        {{2U, 6U}}, {{3U, 7U}}, {{4U, 5U}}, {{4U, 6U}}, {{5U, 7U}}, {{6U, 7U}},
    }};
    for (const auto& edge : edges) {
        output.lines.push_back(AnimationPreviewOverlayLine{
            .from = corners[edge[0]], .to = corners[edge[1]], .color = { 1.0F, 0.76F, 0.12F },
        });
    }
}

void AppendNormalLines(
    AnimationPreviewOverlaySnapshot& output,
    const kb::scene::SkeletalMeshAsset& mesh,
    const kb::scene::WorldTransform& owner,
    const kb::scene::AnimatorInstanceSkeletonView* skeletonView) {
    if (mesh.lods.empty() || mesh.lods.front().vertices.empty()) return;
    const kb::scene::SkeletalMeshLod& lod = mesh.lods.front();
    constexpr std::size_t kMaximumNormalLines = 128U;
    const std::size_t candidateCount = lod.indices.empty() ? lod.vertices.size() : lod.indices.size();
    const std::size_t stride = std::max<std::size_t>(
        1U, (candidateCount + kMaximumNormalLines - 1U) / kMaximumNormalLines);
    std::size_t emitted = 0U;

    const auto appendVertex = [&](const kb::scene::SkeletalMeshVertex& vertex,
                                  const kb::scene::SkeletalMeshSection* section) {
        kb::scene::Vec3 position = vertex.position;
        kb::scene::Vec3 normal = vertex.normal;
        if (section != nullptr && skeletonView != nullptr &&
            skeletonView->boneIds.size() == skeletonView->currentSkinMatrices.size()) {
            kb::scene::Vec3 skinnedPosition{};
            kb::scene::Vec3 skinnedNormal{};
            float accumulatedWeight = 0.0F;
            for (std::size_t influence = 0U; influence < vertex.jointWeights.size(); ++influence) {
                const float weight = vertex.jointWeights[influence];
                const std::uint16_t joint = vertex.jointIndices[influence];
                if (weight <= 0.0F || joint >= section->boneMap.size()) continue;
                const kb::scene::SkeletonBoneId boneId = section->boneMap[joint];
                const auto poseBone = std::find(skeletonView->boneIds.begin(), skeletonView->boneIds.end(), boneId);
                if (poseBone == skeletonView->boneIds.end()) continue;
                const std::size_t poseIndex = static_cast<std::size_t>(poseBone - skeletonView->boneIds.begin());
                const kb::math::Mat4& skin = skeletonView->currentSkinMatrices[poseIndex];
                const kb::math::Vec4 transformedPosition = skin * kb::math::Vec4{
                    vertex.position.x, vertex.position.y, vertex.position.z, 1.0F };
                const kb::math::Vec4 transformedNormal = skin * kb::math::Vec4{
                    vertex.normal.x, vertex.normal.y, vertex.normal.z, 0.0F };
                skinnedPosition = skinnedPosition + kb::scene::Vec3{
                    transformedPosition.x, transformedPosition.y, transformedPosition.z } * weight;
                skinnedNormal = skinnedNormal + kb::scene::Vec3{
                    transformedNormal.x, transformedNormal.y, transformedNormal.z } * weight;
                accumulatedWeight += weight;
            }
            if (accumulatedWeight > 0.000001F) {
                position = skinnedPosition * (1.0F / accumulatedWeight);
                normal = kb::math::Normalize(skinnedNormal);
            }
        }
        const kb::scene::Vec3 start = WorldPoint(owner, position);
        const kb::scene::Vec3 direction = WorldDirection(owner, normal);
        output.lines.push_back(AnimationPreviewOverlayLine{
            .from = start, .to = start + direction * 0.075F, .color = { 0.28F, 0.88F, 1.0F },
        });
        ++emitted;
    };

    if (lod.indices.empty() || lod.sections.empty()) {
        for (std::size_t vertexIndex = 0U;
             vertexIndex < lod.vertices.size() && emitted < kMaximumNormalLines;
             vertexIndex += stride) {
            appendVertex(lod.vertices[vertexIndex], nullptr);
        }
        return;
    }

    std::size_t candidateIndex = 0U;
    for (const kb::scene::SkeletalMeshSection& section : lod.sections) {
        const std::size_t sectionEnd = std::min<std::size_t>(
            lod.indices.size(), static_cast<std::size_t>(section.firstIndex) + section.indexCount);
        for (std::size_t indexOffset = section.firstIndex;
             indexOffset < sectionEnd && emitted < kMaximumNormalLines;
             ++indexOffset, ++candidateIndex) {
            if ((candidateIndex % stride) != 0U) continue;
            const std::uint32_t vertexIndex = lod.indices[indexOffset];
            if (vertexIndex < lod.vertices.size()) appendVertex(lod.vertices[vertexIndex], &section);
        }
    }
}

} // namespace

const kb::scene::Scene& EditorAnimationPreviewScene::SceneFor(
    const kb::scene::Scene& source, AnimationPreviewContext& context) {
    if (scene_ == nullptr || sourceSceneId_ != source.Id() || contextRevision_ != context.Revision()) {
        Rebuild(source, context);
    }
    SynchronizePlayback(context);
    SynchronizeCamera();
    return *scene_;
}

void EditorAnimationPreviewScene::Focus(float durationSeconds) noexcept {
    camera_.FocusOn(focusCenter_, focusRadius_, durationSeconds);
    SynchronizeCamera();
}

bool EditorAnimationPreviewScene::TickCamera(
    float deltaSeconds,
    const EditorViewportCameraFlightInput& flightInput) noexcept {
    bool changed = camera_.ApplyQueuedPointer();
    changed = camera_.ApplyKeyboardFlight(flightInput, deltaSeconds) || changed;
    changed = camera_.TickFocus(deltaSeconds) || changed;
    if (changed) {
        SynchronizeCamera();
    }
    return changed;
}

bool EditorAnimationPreviewScene::TickPlayback(AnimationPreviewContext& context, float deltaSeconds) noexcept {
    const bool advanced = context.Transport().Advance(deltaSeconds);
    SynchronizePlayback(context);
    return advanced;
}

AnimationPreviewOverlaySnapshot EditorAnimationPreviewScene::BuildOverlays(
    const AnimationPreviewContext& context) const {
    AnimationPreviewOverlaySnapshot output;
    if (scene_ == nullptr || !previewEntity_.IsValid()) return output;
    const AnimationPreviewOverlayState& settings = context.Overlays();
    if (!settings.BonesVisible() && !settings.BoneNamesVisible() && !settings.SocketsVisible() &&
        !settings.RootMotionVisible() && !settings.BoundsVisible() && !settings.LodVisible() &&
        !settings.NormalsVisible()) return output;

    const kb::scene::TransformComponent* ownerTransform = scene_->Transforms().TryGet(previewEntity_);
    if (ownerTransform == nullptr) return output;
    const kb::scene::WorldTransform owner = ownerTransform->WorldPayload();
    const auto skeletonView = scene_->Animators().InstanceSkeleton(previewEntity_);
    const kb::assets::AssetHandle<kb::scene::SkeletonAsset> skeleton = context.SkeletonAsset().IsValid()
        ? scene_->Assets().Manager().Load<kb::scene::SkeletonAsset>(context.SkeletonAsset())
        : kb::assets::AssetHandle<kb::scene::SkeletonAsset>{};
    const kb::assets::AssetHandle<kb::scene::SkeletalMeshAsset> mesh = context.SkeletalMeshAsset().IsValid()
        ? scene_->Assets().Manager().Load<kb::scene::SkeletalMeshAsset>(context.SkeletalMeshAsset())
        : kb::assets::AssetHandle<kb::scene::SkeletalMeshAsset>{};
    if (mesh.IsLoaded()) {
        output.lodCount = static_cast<std::uint32_t>(mesh->lods.size());
        if (settings.NormalsVisible()) {
            AppendNormalLines(output, *mesh, owner, skeletonView.has_value() ? &*skeletonView : nullptr);
        }
    }
    if (settings.LodVisible()) {
        output.labels.push_back(AnimationPreviewOverlayLabel{
            .position = focusCenter_,
            .text = "LODs: " + std::to_string(output.lodCount),
        });
    }
    if (skeletonView.has_value()) {
        output.poseEvaluationCount = skeletonView->evaluationCount;
        const std::span<const kb::scene::SkeletonBoneId> boneIds = skeletonView->boneIds;
        const std::span<const kb::scene::Vec3> positions = skeletonView->currentComponentPose.positions;
        if (skeleton.IsLoaded() && positions.size() == boneIds.size()) {
            for (std::size_t index = 0U; index < boneIds.size(); ++index) {
                const auto bone = std::find_if(skeleton->bones.begin(), skeleton->bones.end(),
                    [id = boneIds[index]](const kb::scene::SkeletonBone& value) { return value.id == id; });
                if (bone == skeleton->bones.end()) continue;
                const kb::scene::Vec3 position = WorldPoint(owner, positions[index]);
                if (settings.BoneNamesVisible()) {
                    output.labels.push_back(AnimationPreviewOverlayLabel{
                        .position = position, .text = bone->name,
                    });
                }
                if (settings.BonesVisible() && bone->parentIndex >= 0 &&
                    static_cast<std::size_t>(bone->parentIndex) < skeleton->bones.size()) {
                    const kb::scene::SkeletonBoneId parentId = skeleton->bones[static_cast<std::size_t>(bone->parentIndex)].id;
                    const auto parent = std::find(boneIds.begin(), boneIds.end(), parentId);
                    if (parent != boneIds.end()) {
                        output.lines.push_back(AnimationPreviewOverlayLine{
                            .from = WorldPoint(owner, positions[static_cast<std::size_t>(parent - boneIds.begin())]),
                            .to = position,
                            .color = { 0.96F, 0.35F, 0.12F },
                            .fromBoneId = parentId,
                            .boneId = boneIds[index],
                        });
                    }
                }
            }
            if (settings.RootMotionVisible() && !positions.empty()) {
                const kb::scene::Vec3 origin = WorldPoint(owner, {});
                const kb::scene::Vec3 root = WorldPoint(owner, positions.front());
                output.lines.push_back(AnimationPreviewOverlayLine{
                    .from = origin, .to = root, .color = { 0.94F, 0.16F, 0.78F },
                });
                output.labels.push_back(AnimationPreviewOverlayLabel{
                    .position = root, .text = "Root motion",
                });
            }
        }
        if (settings.SocketsVisible() && skeleton.IsLoaded()) {
            for (const kb::scene::SkeletonSocket& socket : skeleton->sockets) {
                const auto transform = scene_->Animators().SocketTransform(previewEntity_, socket.name);
                if (!transform.has_value()) continue;
                const kb::scene::Vec3 start = transform->worldSpace.position;
                const kb::scene::Vec3 x = kb::math::Rotate(transform->worldSpace.rotation, kb::scene::Vec3{ 0.12F, 0.0F, 0.0F });
                const kb::scene::Vec3 y = kb::math::Rotate(transform->worldSpace.rotation, kb::scene::Vec3{ 0.0F, 0.12F, 0.0F });
                const kb::scene::Vec3 z = kb::math::Rotate(transform->worldSpace.rotation, kb::scene::Vec3{ 0.0F, 0.0F, 0.12F });
                output.lines.push_back(AnimationPreviewOverlayLine{ .from = start, .to = start + x, .color = { 1.0F, 0.2F, 0.2F } });
                output.lines.push_back(AnimationPreviewOverlayLine{ .from = start, .to = start + y, .color = { 0.2F, 1.0F, 0.2F } });
                output.lines.push_back(AnimationPreviewOverlayLine{ .from = start, .to = start + z, .color = { 0.2F, 0.4F, 1.0F } });
                output.labels.push_back(AnimationPreviewOverlayLabel{
                    .position = start, .text = socket.name,
                });
            }
        }
    }
    if (settings.BoundsVisible() && mesh.IsLoaded()) {
        const std::optional<kb::scene::SkeletalMeshBounds> bounds = skeletonView.has_value()
            ? kb::scene::EvaluateSkeletalMeshAnimatedBounds(
                *mesh, 0U, skeletonView->boneIds, skeletonView->currentSkinMatrices)
            : std::optional<kb::scene::SkeletalMeshBounds>{ mesh->conservativeBounds };
        if (bounds.has_value()) AppendBoundsLines(output, *bounds, owner);
    }
    return output;
}

void EditorAnimationPreviewScene::Rebuild(
    const kb::scene::Scene& source, const AnimationPreviewContext& context) {
    const bool openingDifferentAsset = framedMeshAsset_ != context.SkeletalMeshAsset() ||
        framedSkeletonAsset_ != context.SkeletonAsset();
    scene_ = std::make_unique<kb::scene::Scene>(kb::scene::SceneMode::Runtime);
    for (const kb::assets::AssetMetadata& metadata : source.Assets().Manager().Registry().All()) {
        static_cast<void>(scene_->Assets().Manager().RegisterAsset(metadata));
    }
    RegisterPreviewFloorAsset(scene_->Assets().Manager());
    previewEntity_ = scene_->Entities().CreateEntity(
        kb::scene::SceneObjectDesc{ .name = "Animation Preview" });
    kb::assets::AssetManager& previewAssets = scene_->Assets().Manager();
    const kb::assets::AssetHandle<kb::scene::SkeletalMeshAsset> mesh =
        ShareOrLoadPreviewAsset<kb::scene::SkeletalMeshAsset>(source, previewAssets, context.SkeletalMeshAsset());
    const kb::assets::AssetHandle<kb::scene::SkeletonAsset> skeleton =
        ShareOrLoadPreviewAsset<kb::scene::SkeletonAsset>(source, previewAssets, context.SkeletonAsset());
    const bool correctLegacyFbxUpAxis = mesh.IsLoaded() &&
        RequiresLegacyFbxUpAxisCorrection(mesh->conservativeBounds);
    if (mesh.IsLoaded()) {
        diagnostics::EditorLagTrace::Marker(
            "skeletal-orientation",
            "assetId=" + std::to_string(context.SkeletalMeshAsset().value) +
            " legacyCorrection=" + (correctLegacyFbxUpAxis ? "1" : "0") +
            " centerY=" + std::to_string(mesh->conservativeBounds.center.y) +
            " extentY=" + std::to_string(mesh->conservativeBounds.extents.y));
    }
    const kb::scene::Quat previewRotation = correctLegacyFbxUpAxis
        ? kb::scene::Quat{ 0.0F, 0.0F, 1.0F, 0.0F }
        : kb::scene::Quat{};
    if (correctLegacyFbxUpAxis) {
        kb::scene::TransformComponent previewTransform = scene_->Transforms().Get(previewEntity_);
        previewTransform.localRotation = previewRotation;
        scene_->Transforms().Set(previewEntity_, previewTransform);
    }
    const std::uint64_t skeletonSignature = skeleton.IsLoaded()
        ? kb::scene::SkeletonCompatibilitySignature(*skeleton)
        : 0U;
    bool compatible = skeletonSignature != 0U;
    if (compatible && mesh.IsLoaded()) {
        compatible =
            mesh->skeletonAssetId == context.SkeletonAsset().value &&
            mesh->skeletonCompatibilitySignature == skeletonSignature;
    }
    if (mesh.IsLoaded()) {
        static_cast<void>(scene_->Components().MeshRenderers().Set(previewEntity_, kb::scene::MeshRendererComponent{
            .meshAssetId = context.SkeletalMeshAsset().value,
        }));
        static_cast<void>(scene_->Components().DeformedGeometries().Set(previewEntity_, kb::scene::DrawD3DeformedGeometryComponent{
            .skeletalMeshAssetId = context.SkeletalMeshAsset().value,
            // Asset-editor previews contain exactly one inspected character. Use its imported
            // conservative bounds instead of deriving animated bounds for frustum culling: a bad or
            // incomplete imported per-bone bound must never make the model disappear while its bones
            // remain visible, and there is no crowd-scale culling benefit in this viewport.
            .fixedBounds = true,
            .enabled = compatible,
        }));
    }
    if (compatible) {
        static_cast<void>(scene_->Components().SkeletonBindings().Set(previewEntity_, kb::scene::SkeletonBindingComponent{
            .skeletonAssetId = context.SkeletonAsset().value,
            .skeletonCompatibilitySignature = skeletonSignature,
            .enabled = true,
        }));
    }
    kb::assets::AssetId controllerId = context.ControllerAsset();
    if (compatible && controllerId.IsValid() && context.ControllerOverride() != nullptr) {
        compatible = scene_->Assets().Manager().PublishRuntimeAsset(
            controllerId, std::make_shared<kb::scene::AnimatorController>(*context.ControllerOverride()));
    }
    if (compatible && context.PoseMode() == AnimationPreviewPoseMode::Animated &&
        !controllerId.IsValid() && context.ClipAsset().IsValid()) {
        const kb::assets::AssetHandle<kb::scene::AnimationClip> clip =
            ShareOrLoadPreviewAsset<kb::scene::AnimationClip>(source, previewAssets, context.ClipAsset());
        compatible = clip.IsLoaded() &&
            clip->targetSkeletonAssetId == context.SkeletonAsset().value &&
            clip->targetSkeletonCompatibilitySignature == skeletonSignature;
        if (compatible) {
            const std::string clipId = kb::assets::ToString(context.ClipAsset());
            const std::filesystem::path controllerPath =
                std::filesystem::path{ "/__EditorPreview/AnimationClip_" + clipId + ".kbanimcontroller" };
            controllerId = kb::assets::MakeAssetId(controllerPath.generic_string());
            kb::assets::AssetManager& assets = scene_->Assets().Manager();
            const kb::assets::AssetMetadata* existing = assets.Registry().Find(controllerId);
            if (existing != nullptr &&
                (existing->type != kb::scene::kAnimatorControllerAssetType || existing->virtualPath != controllerPath)) {
                compatible = false;
            } else {
                if (existing == nullptr && !assets.RegisterAsset(kb::assets::AssetMetadata{
                        .id = controllerId,
                        .type = kb::scene::kAnimatorControllerAssetType,
                        .name = "Animation Clip Preview",
                        .virtualPath = controllerPath,
                        .runtimeLoadable = true,
                    })) {
                    compatible = false;
                }
                if (compatible) {
                    auto controller = std::make_shared<kb::scene::AnimatorController>();
                    controller->layers = {{
                        .name = "Preview",
                        .defaultState = "Clip",
                        .weight = 1.0F,
                        .mask = ~std::uint64_t{ 0U },
                        .states = {{ .name = "Clip", .clipReference = clipId }},
                    }};
                    compatible = assets.PublishRuntimeAsset(controllerId, std::move(controller));
                }
            }
        }
    }
    if (compatible && context.PoseMode() == AnimationPreviewPoseMode::Animated && controllerId.IsValid()) {
        static_cast<void>(scene_->Components().Animators().Set(previewEntity_, kb::scene::Animator{
            .controllerAssetId = controllerId.value,
            .enabled = true,
        }));
    }

    focusCenter_ = mesh.IsLoaded()
        ? kb::math::Rotate(previewRotation, mesh->conservativeBounds.center)
        : kb::scene::Vec3{};
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
        kb::scene::Vec3{ 0.0F, 0.0F, 1.0F }, kb::scene::Vec3{ 0.0F, 1.0F, 0.0F });
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

    if (openingDifferentAsset) {
        // Asset editors open on an orthographic-looking front view. The shared scene-camera default
        // is intentionally a three-quarter view, but it made imported rigs appear tilted even after
        // their legacy up-axis was corrected.
        // The canonical imported character convention presents the authored front when viewed
        // from +Z toward -Z. In this camera state yaw 180 places the camera on +Z; yaw 0 shows
        // the character's back and exposes the open spaces between Y Bot's rear armour panels.
        camera_.SetViewAngles(180.0F, 0.0F);
    }
    static_cast<void>(scene_->Runtime().Update(0.0F));
    if (!mesh.IsLoaded()) {
        if (const std::optional<kb::scene::AnimatorInstanceSkeletonView> pose =
                scene_->Animators().InstanceSkeleton(previewEntity_);
            pose.has_value() && !pose->currentComponentPose.positions.empty()) {
            kb::scene::Vec3 minimum = pose->currentComponentPose.positions.front();
            kb::scene::Vec3 maximum = minimum;
            for (const kb::scene::Vec3 position : pose->currentComponentPose.positions) {
                minimum.x = std::min(minimum.x, position.x);
                minimum.y = std::min(minimum.y, position.y);
                minimum.z = std::min(minimum.z, position.z);
                maximum.x = std::max(maximum.x, position.x);
                maximum.y = std::max(maximum.y, position.y);
                maximum.z = std::max(maximum.z, position.z);
            }
            focusCenter_ = (minimum + maximum) * 0.5F;
            focusRadius_ = std::max(0.1F, kb::math::Length(maximum - minimum) * 0.5F);
        }
    }
    Focus(0.0F);
    sourceSceneId_ = source.Id();
    framedMeshAsset_ = context.SkeletalMeshAsset();
    framedSkeletonAsset_ = context.SkeletonAsset();
    contextRevision_ = context.Revision();
    playbackRevision_ = 0U;
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

void EditorAnimationPreviewScene::SynchronizePlayback(AnimationPreviewContext& context) noexcept {
    if (scene_ == nullptr) return;
    if (scene_->Animators().Exists(previewEntity_)) {
        static_cast<void>(context.Transport().SetDurationSeconds(
            scene_->Animators().CurrentStateDuration(previewEntity_)));
    }
    if (playbackRevision_ == context.Transport().Revision()) return;
    if (scene_->Animators().Exists(previewEntity_)) {
        static_cast<void>(scene_->Animators().SeekNormalized(
            previewEntity_, context.Transport().NormalizedTime()));
        static_cast<void>(scene_->Runtime().Update(0.0F));
    }
    playbackRevision_ = context.Transport().Revision();
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
    framedMeshAsset_ = {};
    framedSkeletonAsset_ = {};
    contextRevision_ = 0U;
    playbackRevision_ = 0U;
    ++revision_;
}

} // namespace kb::editor
