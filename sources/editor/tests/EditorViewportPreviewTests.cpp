#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "app/EditorPlayModeState.hpp"
#include "app/scene_viewport/EditorViewportCameraNavigationInput.hpp"
#include "app/scene_viewport/EditorSceneViewportMeshPicker.hpp"
#include "app/scene_viewport/EditorTerrainStrokeTickPolicy.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/ParticleEffectComponent.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAnimators.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "rendering/EditorBgfxBackendSelector.hpp"
#include "rendering/EditorHostSurfaceLifecycle.hpp"
#include "rendering/SkeletalMeshEditorBonePicker.hpp"
#include "rendering/SkeletalMeshEditorSceneLabelBuilder.hpp"
#include "rendering/EditorTexturePreviewService.hpp"
#include "rendering/SceneViewportPresentationPolicy.hpp"
#include "rendering/SceneViewportSceneSyncPolicy.hpp"
#include "rendering/ParticleThumbnailTimeline.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarLayout.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarLabelFormat.hpp"
#include "rendering/SvgGraphicsPathBuilder.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarState.hpp"
#include "scene/EditorViewportCameraState.hpp"
#include "scene/EditorViewportPreviewState.hpp"
#include "scene/EditorPlayCameraResolver.hpp"
#include "scene/AnimationPreviewContext.hpp"
#include "scene/EditorAnimationPreviewScene.hpp"

#include "engine/assets/AssetMetadata.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void RequireNear(float actual, float expected, float tolerance, const char* message) {
    kb::editor::tests::Require(std::fabs(actual - expected) <= tolerance, message);
}

void RunSceneViewportPresentationPolicyTest() {
    using kb::editor::SceneViewportCameraSource;
    using kb::editor::SceneViewportPresentationPolicy;

    kb::editor::tests::Require(
        SceneViewportPresentationPolicy::CameraSource(false, false) == SceneViewportCameraSource::Editor &&
            SceneViewportPresentationPolicy::CameraSource(false, true) == SceneViewportCameraSource::Editor,
        "Stopped Scene View must use the editor fly camera");
    kb::editor::tests::Require(
        SceneViewportPresentationPolicy::CameraSource(true, true) == SceneViewportCameraSource::PrimaryScene,
        "Play mode Scene View must use an available primary scene camera");
    kb::editor::tests::Require(
        SceneViewportPresentationPolicy::CameraSource(true, false) == SceneViewportCameraSource::Editor &&
            SceneViewportPresentationPolicy::EditorOverlaysEnabled(true, false),
        "Play mode Scene View must retain the editor camera when no primary scene camera exists");
    kb::editor::tests::Require(
        SceneViewportPresentationPolicy::EditorOverlaysEnabled(false, false) &&
            SceneViewportPresentationPolicy::EditorOverlaysEnabled(false, true),
        "Stopped Scene View must retain authoring overlays");
    kb::editor::tests::Require(
        !SceneViewportPresentationPolicy::EditorOverlaysEnabled(true, true),
        "Play mode Scene View must not draw editor overlays over the game camera");
    kb::editor::tests::Require(
        SceneViewportPresentationPolicy::RequiresPresent(false, true),
        "Entering Play mode must request a Scene View present");
    kb::editor::tests::Require(
        SceneViewportPresentationPolicy::RequiresPresent(true, false),
        "Stopping Play mode must request a Scene View present");
    kb::editor::tests::Require(
        !SceneViewportPresentationPolicy::RequiresPresent(true, true) &&
            !SceneViewportPresentationPolicy::RequiresPresent(false, false),
        "An unchanged Scene View camera mode must not manufacture a present");
}

void RunSceneViewportSceneSyncPolicyTest() {
    using kb::editor::SceneViewportSceneSyncPolicy;

    const auto initial = SceneViewportSceneSyncPolicy::Resolve(0U, 1U, 1U, false, false, true);
    kb::editor::tests::Require(
        initial.fullSync && !initial.incrementalEntitySync && !initial.runtimeTransformSync,
        "The first viewport submission must establish one complete render scene");

    const auto cameraFrame = SceneViewportSceneSyncPolicy::Resolve(7U, 7U, 7U, false, false, true);
    kb::editor::tests::Require(
        !cameraFrame.fullSync && !cameraFrame.incrementalEntitySync && cameraFrame.runtimeTransformSync,
        "A Play camera frame must consume runtime transforms without rebuilding the scene");

    const auto editedEntity = SceneViewportSceneSyncPolicy::Resolve(7U, 8U, 7U, false, true, false);
    kb::editor::tests::Require(
        !editedEntity.fullSync && editedEntity.incrementalEntitySync && !editedEntity.runtimeTransformSync,
        "An authored entity edit must retain the incremental entity sync path");

    const auto structuralRuntimeChange = SceneViewportSceneSyncPolicy::Resolve(7U, 8U, 8U, true, false, true);
    kb::editor::tests::Require(
        structuralRuntimeChange.fullSync && !structuralRuntimeChange.runtimeTransformSync,
        "A runtime topology change must take precedence over affine-only synchronization");
}

void RunParticleThumbnailTimelineTest() {
    using kb::editor::ParticleThumbnailTimeline;

    const auto longLoop = ParticleThumbnailTimeline::Plan(6.0F, true);
    kb::editor::tests::Require(
        longLoop.simulationSteps == 360U &&
            longLoop.frameCount == 144U,
        "Particle thumbnail timeline did not preserve a full six-second lifecycle at 24 fps");
    kb::editor::tests::Require(
        ParticleThumbnailTimeline::CaptureStep(longLoop, 143U) >= 357U,
        "Particle thumbnail timeline stopped before the end of a looping lifecycle");
    kb::editor::tests::Require(
        ParticleThumbnailTimeline::PosterFrame(longLoop) == 4U &&
            ParticleThumbnailTimeline::CaptureStep(longLoop, 4U) <= 12U,
        "Particle thumbnail poster did not stay inside the bounded fast-start window");
    kb::editor::tests::Require(
        ParticleThumbnailTimeline::FrameAtSeconds(longLoop, 0.19) == 4U &&
            ParticleThumbnailTimeline::FrameAtSeconds(longLoop, 6.19) == 4U,
        "Particle thumbnail timeline did not loop by authored duration");

    const auto oneShot = ParticleThumbnailTimeline::Plan(1.1F, false);
    kb::editor::tests::Require(
        oneShot.simulationSteps == 66U && oneShot.frameCount == 27U &&
            ParticleThumbnailTimeline::CaptureStep(
                oneShot, oneShot.frameCount - 1U) ==
                oneShot.simulationSteps,
        "Particle thumbnail one-shot did not include its authored end frame");

    const auto shortFlash = ParticleThumbnailTimeline::Plan(0.08F, false);
    kb::editor::tests::Require(
        shortFlash.frameCount == 2U &&
            ParticleThumbnailTimeline::CaptureStep(shortFlash, 1U) == 5U &&
            ParticleThumbnailTimeline::PosterFrame(shortFlash) == 0U,
        "Particle thumbnail timeline did not keep a short one-shot bounded and complete");

    kb::scene::ParticleEffectAsset drainingEffect;
    drainingEffect.looping = false;
    drainingEffect.durationSeconds = 1.0F;
    drainingEffect.emitters.push_back(kb::scene::ParticleEmitterAsset{});
    drainingEffect.emitters.back().spawn.mode =
        kb::scene::ParticleSpawnMode::Continuous;
    drainingEffect.emitters.back().spawn.lifetimeMin = 0.25F;
    drainingEffect.emitters.back().spawn.lifetimeMax = 0.75F;
    const auto draining = ParticleThumbnailTimeline::Plan(drainingEffect);
    kb::editor::tests::Require(
        draining.simulationSteps == 105U && draining.frameCount == 42U &&
            std::fabs(draining.durationSeconds - 1.75F) <= 0.0001F,
        "Particle thumbnail one-shot omitted the final emitted particles' drain interval");

    const auto unbounded = ParticleThumbnailTimeline::Plan(0.0F, true);
    const auto extreme = ParticleThumbnailTimeline::Plan(1000000.0F, true);
    kb::editor::tests::Require(
        unbounded.usesBoundedPreviewWindow &&
            unbounded.simulationSteps == 300U &&
            std::fabs(unbounded.durationSeconds - 5.0F) <= 0.0001F &&
            extreme.usesBoundedPreviewWindow &&
            extreme.simulationSteps == 600U &&
            extreme.frameCount == 240U &&
            std::fabs(extreme.durationSeconds - 10.0F) <= 0.0001F,
        "Particle thumbnail timeline did not bound unending or pathological preview work");

    kb::scene::ParticleEffectAsset cascadingEffect;
    cascadingEffect.looping = false;
    cascadingEffect.durationSeconds = 1.0F;
    cascadingEffect.emitters.push_back(kb::scene::ParticleEmitterAsset{});
    cascadingEffect.emitters.back().spawn.mode =
        kb::scene::ParticleSpawnMode::Burst;
    cascadingEffect.emitters.back().spawn.bursts = {
        {.timeSeconds = 0.0F, .count = 1U},
    };
    cascadingEffect.emitters.back().spawn.lifetimeMin = 1.0F;
    cascadingEffect.emitters.back().spawn.lifetimeMax = 1.0F;
    cascadingEffect.eventBindings.push_back(
        kb::scene::ParticleEventBindingAsset{.maxDepth = 2U});
    const auto cascading = ParticleThumbnailTimeline::Plan(
        cascadingEffect);
    kb::editor::tests::Require(
        cascading.simulationSteps == 180U &&
            std::fabs(cascading.durationSeconds - 3.0F) <= 0.0001F,
        "Particle thumbnail one-shot omitted a bounded event-spawn chain");
}

void RunPlayCameraHierarchySelectionTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity first = scene.Entities().CreateEntity(
        kb::scene::SceneObjectDesc{ .name = "First Camera" });
    const kb::scene::SceneEntity container = scene.Entities().CreateEntity(
        kb::scene::SceneObjectDesc{ .name = "Container" });
    const kb::scene::SceneEntity last = scene.Entities().CreateEntity(
        kb::scene::SceneObjectDesc{
            .name = "Last Camera",
            .parent = scene.Entities().Object(container),
        });
    scene.Components().Cameras().Set(
        first, kb::scene::CameraComponent{ .primary = true, .priority = 100 });
    scene.Components().Cameras().Set(
        last, kb::scene::CameraComponent{ .primary = true, .priority = -100 });

    kb::editor::tests::Require(
        kb::editor::EditorPlayCameraResolver::Resolve(scene) == last,
        "Play camera must be the last active camera in visible Hierarchy order, independent of priority");

    kb::scene::CameraComponent inactive =
        *scene.Components().Cameras().TryGet(last);
    inactive.primary = false;
    scene.Components().Cameras().Set(last, inactive);
    kb::editor::tests::Require(
        kb::editor::EditorPlayCameraResolver::Resolve(scene) == first,
        "An inactive camera must not replace the preceding active Play camera");

    kb::scene::CameraComponent firstInactive =
        *scene.Components().Cameras().TryGet(first);
    firstInactive.primary = false;
    scene.Components().Cameras().Set(first, firstInactive);
    kb::editor::tests::Require(
        !kb::editor::EditorPlayCameraResolver::Resolve(scene).IsValid(),
        "A scene without an active camera must retain free Scene View during Play");
}

void RunProfileCycleAndResolutionTest() {
    kb::editor::EditorViewportPreviewState state;
    kb::editor::tests::Require(state.ProfileKind() == kb::editor::EditorViewportProfileKind::Free, "Viewport preview should start in Free profile");
    kb::editor::tests::Require(state.RenderWidthForPanel(800U) == 800U, "Free profile should use panel width");
    kb::editor::tests::Require(state.RenderHeightForPanel(600U) == 600U, "Free profile should use panel height");

    state.CycleProfile();
    kb::editor::tests::Require(state.ProfileKind() == kb::editor::EditorViewportProfileKind::Pc1080p, "Viewport profile cycle did not select PC 1080p");
    kb::editor::tests::Require(state.RenderWidthForPanel(800U) == 1920U, "PC 1080p profile width is wrong");
    kb::editor::tests::Require(state.RenderHeightForPanel(600U) == 1080U, "PC 1080p profile height is wrong");

    state.CycleProfile();
    state.CycleProfile();
    const kb::editor::EditorViewportProfile phone = state.Profile();
    kb::editor::tests::Require(phone.kind == kb::editor::EditorViewportProfileKind::PhoneLandscape, "Viewport profile cycle should skip the removed phone portrait profile");
    kb::editor::tests::Require(phone.devicePreview, "Phone landscape should remain a device preview profile");
    kb::editor::tests::Require(phone.safeArea.left > 0U && phone.safeArea.right > 0U, "Phone landscape should expose safe area insets");
}

void RunAnimationPreviewContextTracksSharedBindingTest() {
    kb::editor::AnimationPreviewContext preview;
    const std::uint64_t initialRevision = preview.Revision();
    preview.SetAssets({ 1U }, { 2U }, { 3U }, { 4U });
    kb::editor::tests::Require(
        preview.SkeletonAsset().value == 1U && preview.SkeletalMeshAsset().value == 2U &&
            preview.ClipAsset().value == 3U && preview.ControllerAsset().value == 4U &&
            preview.Revision() == initialRevision + 1U,
        "Animation preview context did not retain one shared asset binding");
    preview.SetPoseMode(kb::editor::AnimationPreviewPoseMode::Animated);
    kb::editor::tests::Require(
        preview.PoseMode() == kb::editor::AnimationPreviewPoseMode::Animated &&
            preview.Revision() == initialRevision + 2U,
        "Animation preview context did not invalidate after a pose-mode change");
    preview.Clear();
    kb::editor::tests::Require(
        !preview.SkeletonAsset().IsValid() && !preview.SkeletalMeshAsset().IsValid() &&
            !preview.ClipAsset().IsValid() && !preview.ControllerAsset().IsValid() &&
            preview.PoseMode() == kb::editor::AnimationPreviewPoseMode::Reference,
        "Animation preview context did not reset shared state atomically");
}

void RunAnimationPreviewTransportTest() {
    kb::editor::AnimationPreviewTransport transport;
    kb::editor::tests::Require(
        transport.SetDurationSeconds(2.0F) && transport.SetFrameRate(20.0F) &&
            transport.SetPlaying(true) && transport.Advance(0.5F) &&
            std::fabs(transport.NormalizedTime() - 0.25F) < 0.0001F,
        "Animation preview transport did not advance in clip-time units");
    kb::editor::tests::Require(
        transport.Step(1) && std::fabs(transport.NormalizedTime() - 0.275F) < 0.0001F,
        "Animation preview transport did not step by one configured frame");
    kb::editor::tests::Require(
        transport.SetLooping(false) && transport.Scrub(0.99F) && transport.Advance(1.0F) &&
            !transport.IsPlaying() && std::fabs(transport.NormalizedTime() - 1.0F) < 0.0001F,
        "Animation preview transport did not deterministically stop at a non-looping end");
    kb::editor::tests::Require(
        transport.SetLooping(true) && transport.SetPlaying(true) && transport.Scrub(0.95F) && transport.Advance(0.2F) &&
            std::fabs(transport.NormalizedTime() - 0.05F) < 0.0001F,
        "Animation preview transport did not wrap a looping playhead deterministically");
    transport.Reset();
    kb::editor::tests::Require(
        transport.SetDurationSeconds(2.0F) && transport.SetLoopRange(0.25F, 0.5F) &&
            transport.SetPlaying(true) && transport.Scrub(0.49F) && transport.Advance(0.1F) &&
            std::fabs(transport.NormalizedTime() - 0.29F) < 0.0001F,
        "Animation preview transport did not honor a bounded loop range");
}

void RunAnimationPreviewOverlayStateTest() {
    kb::editor::AnimationPreviewOverlayState overlays;
    const std::uint64_t revision = overlays.Revision();
    kb::editor::tests::Require(
        overlays.SetBonesVisible(true) && overlays.SetBoneNamesVisible(true) && overlays.SetSocketsVisible(true) &&
            overlays.SetRootMotionVisible(true) && overlays.SetBoundsVisible(true) && overlays.SetLodVisible(true) &&
            overlays.SetNormalsVisible(true) && overlays.Revision() == revision + 7U,
        "Animation preview overlays did not retain all runtime debug visibility switches");
    kb::editor::tests::Require(
        overlays.BonesVisible() && overlays.BoneNamesVisible() && overlays.SocketsVisible() &&
            overlays.RootMotionVisible() && overlays.BoundsVisible() && overlays.LodVisible() && overlays.NormalsVisible(),
        "Animation preview overlay visibility query lost enabled diagnostics");
}

void RunAnimationPreviewScenePresentationTest() {
    kb::scene::Scene source{ kb::scene::SceneMode::Runtime };
    kb::editor::AnimationPreviewContext context;
    kb::editor::EditorAnimationPreviewScene preview;
    const kb::scene::Scene& scene = preview.SceneFor(source, context);
    kb::editor::tests::Require(scene.Mode() == kb::scene::SceneMode::Runtime, "Animation preview must use a runtime scene");
    kb::editor::tests::Require(
        preview.PreviewEntity().IsValid() && preview.CameraEntity().IsValid() &&
            preview.FloorEntity().IsValid() && preview.EnvironmentEntity().IsValid(),
        "Animation preview did not create its presentation entities");
    kb::editor::tests::Require(
        scene.Components().Cameras().TryGet(preview.CameraEntity()) != nullptr &&
            scene.Components().MeshRenderers().TryGet(preview.FloorEntity()) != nullptr &&
            scene.Components().WorldBackdrops().TryGet(preview.EnvironmentEntity()) != nullptr &&
            scene.Components().AmbientRadiances().TryGet(preview.EnvironmentEntity()) != nullptr,
        "Animation preview presentation does not include camera, floor and environment");
    const kb::scene::TransformComponent floorTransform = scene.Transforms().Get(preview.FloorEntity());
    const kb::scene::Vec3 floorFront = kb::math::Rotate(
        floorTransform.localRotation, kb::scene::Vec3{ 0.0F, 0.0F, 1.0F });
    kb::editor::tests::Require(
        std::fabs(floorFront.x) < 0.0001F && floorFront.y > 0.9999F && std::fabs(floorFront.z) < 0.0001F,
        "Animation preview floor front face must point upward");
    const kb::scene::TransformComponent initialCamera = scene.Transforms().Get(preview.CameraEntity());
    preview.Camera().BeginNavigation(kb::editor::EditorViewportCameraNavigationMode::Orbit, 10, 10);
    static_cast<void>(preview.Camera().UpdatePointer(40, 24));
    preview.Camera().EndNavigation();
    const kb::scene::TransformComponent movedCamera = preview.SceneFor(source, context).Transforms().Get(preview.CameraEntity());
    kb::editor::tests::Require(
        std::fabs(initialCamera.localPosition.x - movedCamera.localPosition.x) > 0.0001F ||
            std::fabs(initialCamera.localPosition.y - movedCamera.localPosition.y) > 0.0001F ||
            std::fabs(initialCamera.localPosition.z - movedCamera.localPosition.z) > 0.0001F,
        "Animation preview camera navigation did not update the runtime camera transform");

    preview.Camera().BeginNavigation(kb::editor::EditorViewportCameraNavigationMode::Look, 40, 24);
    const kb::scene::Vec3 flightStart = preview.Camera().Position();
    kb::editor::EditorViewportCameraFlightInput flight{};
    flight.forward = true;
    kb::editor::tests::Require(
        preview.TickCamera(0.25F, flight) &&
            kb::math::Length(preview.Camera().Position() - flightStart) > 0.0001F,
        "Skeletal Mesh Editor RMB navigation must apply WASD camera flight");
    preview.Camera().EndNavigation();
}

void RunAnimationPreviewLodPolicyTest() {
    kb::editor::EditorAnimationPreviewScene preview;
    kb::scene::SkeletalMeshAsset mesh{};
    mesh.lods.resize(2U);
    mesh.lods[0].minScreenCoverage = 0.5F;
    mesh.lods[1].minScreenCoverage = 0.0F;

    kb::editor::tests::Require(
        preview.SetForcedLod(99U, static_cast<std::uint32_t>(mesh.lods.size())) &&
            preview.ForcedLod() == 1U && preview.ResolvePreviewLod(mesh) == 1U,
        "Skeletal Mesh preview LOD picker should clamp and resolve its session-only forced LOD");
    kb::editor::tests::Require(
        !preview.SetForcedLod(1U, static_cast<std::uint32_t>(mesh.lods.size())),
        "Skeletal Mesh preview LOD picker should not invalidate the viewport for an unchanged selection");
    kb::editor::tests::Require(
        preview.SetForcedLod(std::nullopt, static_cast<std::uint32_t>(mesh.lods.size())) &&
            !preview.ForcedLod().has_value(),
        "Skeletal Mesh preview LOD picker should return to automatic runtime selection");
    kb::editor::EditorAnimationPreviewScene emptyPreview;
    kb::editor::tests::Require(!emptyPreview.SetForcedLod(0U, 0U) && !emptyPreview.ForcedLod().has_value(),
        "Skeletal Mesh preview LOD picker should reject a forced LOD when the asset has no LODs");
}

void RunViewportCameraNavigationBindingPolicyTest() {
    using kb::editor::EditorViewportCameraNavigationMode;
    kb::editor::tests::Require(
        kb::editor::ResolveEditorViewportCameraNavigationMode(false, true, false, false) ==
            EditorViewportCameraNavigationMode::Look,
        "RMB must enter free-look navigation in every 3D viewport");
    kb::editor::tests::Require(
        kb::editor::ResolveEditorViewportCameraNavigationMode(false, true, false, true) ==
            EditorViewportCameraNavigationMode::Dolly,
        "Alt+RMB must retain the shared dolly binding");
}

void RunSkeletalMeshSceneLabelBuilderTest() {
    constexpr std::uint32_t kViewportWidth = 1280U;
    constexpr std::uint32_t kViewportHeight = 720U;
    constexpr float kReferenceCameraDistance = 5.0F;
    kb::editor::EditorViewportCameraState camera;
    const kb::editor::EditorViewportCameraAxes axes = camera.Axes();
    const kb::scene::Vec3 visiblePosition =
        axes.position + axes.forward * kReferenceCameraDistance;
    const std::array<kb::editor::AnimationPreviewOverlayLabel, 1U> visibleLabels{{
        { .position = visiblePosition, .text = "mixamorig:Hips" },
    }};
    std::vector<kb::editor::EditorSceneViewportTextLabel> labels;
    kb::editor::SkeletalMeshEditorSceneLabelBuilder::Append(
        labels, visibleLabels, camera, kViewportWidth, kViewportHeight,
        kReferenceCameraDistance);
    kb::editor::tests::Require(
        labels.size() == 1U && labels.front().text == "mixamorig:Hips",
        "A visible bone name must preserve its source text in the native font overlay");
    RequireNear(labels.front().x, 640.0F, 0.001F,
        "A centered bone must project to the horizontal viewport center");
    RequireNear(labels.front().y, 360.0F, 0.001F,
        "A centered bone must project to the vertical viewport center");
    RequireNear(labels.front().pixelHeight, 10.0F, 0.001F,
        "The reference camera distance must use the established font size 10");
    kb::editor::tests::Require(
        labels.front().color == std::array<std::uint8_t, 4U>{ 255U, 255U, 255U, 255U },
        "Bone names must use a white foreground");

    const auto pixelHeightAtDepth = [&](float depth) {
        const kb::scene::Vec3 position = axes.position + axes.forward * depth;
        const std::array<kb::editor::AnimationPreviewOverlayLabel, 1U> depthLabel{{
            { .position = position, .text = "Bone" },
        }};
        std::vector<kb::editor::EditorSceneViewportTextLabel> projected;
        kb::editor::SkeletalMeshEditorSceneLabelBuilder::Append(
            projected, depthLabel, camera, kViewportWidth, kViewportHeight,
            kReferenceCameraDistance);
        kb::editor::tests::Require(projected.size() == 1U,
            "A visible bone label must render at every valid camera depth");
        return projected.front().pixelHeight;
    };
    RequireNear(pixelHeightAtDepth(2.0F), 25.0F, 0.001F,
        "Bone labels should grow as the camera approaches");
    RequireNear(pixelHeightAtDepth(20.0F), 2.5F, 0.001F,
        "Bone labels should shrink as the camera recedes");

    const std::array<kb::editor::AnimationPreviewOverlayLabel, 1U> socketLabels{{
        {
            .position = visiblePosition,
            .text = "WeaponSocket",
            .kind = kb::editor::AnimationPreviewOverlayLabelKind::Socket,
        },
    }};
    std::vector<kb::editor::EditorSceneViewportTextLabel> socketText;
    kb::editor::SkeletalMeshEditorSceneLabelBuilder::Append(
        socketText, socketLabels, camera, kViewportWidth, kViewportHeight,
        kReferenceCameraDistance);
    kb::editor::tests::Require(
        socketText.size() == 1U &&
            socketText.front().color == std::array<std::uint8_t, 4U>{ 71U, 235U, 199U, 255U },
        "Socket labels should retain their semantic viewport color");

    const std::array<kb::editor::AnimationPreviewOverlayLabel, 1U> hiddenLabels{{
        { .position = axes.position - axes.forward * 2.0F, .text = "Behind camera" },
    }};
    std::vector<kb::editor::EditorSceneViewportTextLabel> hiddenText;
    kb::editor::SkeletalMeshEditorSceneLabelBuilder::Append(
        hiddenText, hiddenLabels, camera, kViewportWidth, kViewportHeight,
        kReferenceCameraDistance);
    kb::editor::tests::Require(hiddenText.empty(),
        "Labels behind the preview camera must be culled");
}

void RunAnimationPreviewLegacyFbxOrientationTest() {
    constexpr kb::assets::AssetId skeletonId{ 701U };
    constexpr kb::assets::AssetId meshId{ 702U };
    kb::scene::SkeletonAsset skeleton{};
    skeleton.bones = {
        { .id = 1U, .parentIndex = -1, .name = "Hips", .referencePose = { .position = { 0.0F, -1.0F, 0.0F } }, .inverseBind = {} },
        { .id = 2U, .parentIndex = 0, .name = "Head", .referencePose = { .position = { 0.0F, -0.6F, 0.0F } }, .inverseBind = {} },
    };
    skeleton.sockets = {{ .name = "HeadSocket", .boneId = 2U }};
    const std::uint64_t signature = kb::scene::SkeletonCompatibilitySignature(skeleton);
    kb::scene::SkeletalMeshAsset mesh{};
    mesh.skeletonAssetId = skeletonId.value;
    mesh.skeletonCompatibilitySignature = signature;
    mesh.conservativeBounds = { .center = { 0.0F, -0.8F, 0.0F }, .extents = { 0.5F, 0.8F, 0.25F } };
    mesh.fixedBounds = mesh.conservativeBounds;
    mesh.lods = {{
        .vertices = {
            { .position = { -0.5F, -1.6F, 0.0F }, .jointWeights = { 1.0F, 0.0F, 0.0F, 0.0F } },
            { .position = { 0.5F, -1.6F, 0.0F }, .jointWeights = { 1.0F, 0.0F, 0.0F, 0.0F } },
            { .position = { 0.0F, 0.0F, 0.0F }, .jointWeights = { 1.0F, 0.0F, 0.0F, 0.0F } },
        },
        .indices = { 0U, 1U, 2U },
        .sections = {{ .firstIndex = 0U, .indexCount = 3U, .boneMap = { 1U, 2U } }},
        .requiredBones = { 1U, 2U },
    }};

    kb::scene::Scene source{ kb::scene::SceneMode::Runtime };
    kb::assets::AssetManager& assets = source.Assets().Manager();
    kb::editor::tests::Require(
        assets.RegisterAsset({ .id = skeletonId, .type = kb::scene::kSkeletonAssetType,
            .name = "Legacy Skeleton", .virtualPath = "/Game/Legacy.kbskeleton" }) &&
        assets.RegisterAsset({ .id = meshId, .type = kb::scene::kSkeletalMeshAssetType,
            .name = "Legacy Mesh", .virtualPath = "/Game/Legacy.kbskeletalmesh" }) &&
        assets.PublishRuntimeAsset(skeletonId, std::make_shared<kb::scene::SkeletonAsset>(skeleton)) &&
        assets.PublishRuntimeAsset(meshId, std::make_shared<kb::scene::SkeletalMeshAsset>(mesh)),
        "Legacy FBX preview fixture could not publish its runtime assets");

    kb::editor::AnimationPreviewContext context;
    context.SetAssets(skeletonId, meshId, {}, {});
    static_cast<void>(context.Overlays().SetBonesVisible(true));
    static_cast<void>(context.Overlays().SetBoneNamesVisible(true));
    static_cast<void>(context.Overlays().SetSocketsVisible(true));
    static_cast<void>(context.Overlays().SetBoundsVisible(true));
    static_cast<void>(context.Overlays().SetLodVisible(true));
    static_cast<void>(context.Overlays().SetNormalsVisible(true));
    kb::editor::EditorAnimationPreviewScene preview;
    static_cast<void>(preview.SceneFor(source, context));
    const kb::editor::AnimationPreviewOverlaySnapshot overlays = preview.BuildOverlays(context);
    const auto headBoneLine = std::find_if(overlays.lines.begin(), overlays.lines.end(),
        [](const kb::editor::AnimationPreviewOverlayLine& line) { return line.boneId == 2U; });
    kb::editor::tests::Require(
        headBoneLine != overlays.lines.end() && headBoneLine->to.y > headBoneLine->from.y &&
            headBoneLine->fromBoneId == 1U,
        "Animation preview did not turn a legacy below-ground FBX skeleton upright");
    const auto hasLabel = [&overlays](std::string_view text) {
        return std::ranges::any_of(overlays.labels,
            [text](const kb::editor::AnimationPreviewOverlayLabel& label) { return label.text == text; });
    };
    const auto hasLineColor = [&overlays](kb::scene::Vec3 color) {
        return std::ranges::any_of(overlays.lines,
            [color](const kb::editor::AnimationPreviewOverlayLine& line) {
                return std::fabs(line.color.x - color.x) < 0.0001F &&
                    std::fabs(line.color.y - color.y) < 0.0001F &&
                    std::fabs(line.color.z - color.z) < 0.0001F;
            });
    };
    kb::editor::tests::Require(
        hasLabel("0: Hips") && hasLabel("1: Head") && hasLabel("HeadSocket") &&
            hasLabel("LOD 0 / 0 (Auto)"),
        "Bone names, sockets and LOD diagnostics must publish visible scene labels");
    kb::editor::tests::Require(
        hasLineColor({ 1.0F, 0.76F, 0.12F }) && hasLineColor({ 0.28F, 0.88F, 1.0F }) &&
            hasLineColor({ 1.0F, 0.2F, 0.2F }) && hasLineColor({ 0.2F, 1.0F, 0.2F }) &&
            hasLineColor({ 0.2F, 0.4F, 1.0F }),
        "Bounds, posed normals and socket axes must publish scene geometry");
    kb::editor::tests::Require(
        preview.Camera().YawDegrees() == 180.0F && preview.Camera().PitchDegrees() == 0.0F,
        "Skeletal Mesh Editor did not open a newly selected mesh facing the camera");
}

void RunSkeletalMeshEditorBonePickerTest() {
    kb::editor::EditorViewportCameraState camera;
    const kb::editor::EditorViewportCameraAxes axes = camera.Axes();
    const kb::scene::Vec3 center = axes.position + axes.forward * 5.0F;
    const kb::scene::Vec3 root = center - axes.right * 0.25F;
    const kb::scene::Vec3 child = center + axes.right * 0.25F;
    const std::array lines{
        kb::editor::AnimationPreviewOverlayLine{
            .from = root,
            .to = child,
            .fromBoneId = 11U,
            .boneId = 22U,
        },
    };
    constexpr kb::editor::SkeletalMeshEditorBonePickViewport viewport{
        .left = 0.0F,
        .top = 0.0F,
        .width = 1000.0F,
        .height = 1000.0F,
    };
    const auto project = [&](kb::scene::Vec3 point) {
        const kb::scene::Vec3 delta = point - axes.position;
        const auto dot = [](kb::scene::Vec3 left, kb::scene::Vec3 right) {
            return left.x * right.x + left.y * right.y + left.z * right.z;
        };
        const float depth = dot(delta, axes.forward);
        const float tangent = std::tan(camera.VerticalFovDegrees() * 0.00872664626F);
        return std::array{
            (dot(delta, axes.right) / (depth * tangent) * 0.5F + 0.5F) * viewport.width,
            (0.5F - dot(delta, axes.up) / (depth * tangent) * 0.5F) * viewport.height,
        };
    };
    const std::array<float, 2U> rootScreen = project(root);
    const std::array<float, 2U> childScreen = project(child);
    const float shaftX = (rootScreen[0] + childScreen[0]) * 0.5F;
    const float shaftY = (rootScreen[1] + childScreen[1]) * 0.5F;

    kb::editor::tests::Require(
        kb::editor::SkeletalMeshEditorBonePicker::Pick(
            viewport, camera, lines, rootScreen[0], rootScreen[1]) == 11U,
        "Skeletal Mesh viewport did not select the parent/root joint at a branch start");
    kb::editor::tests::Require(
        kb::editor::SkeletalMeshEditorBonePicker::Pick(
            viewport, camera, lines, childScreen[0], childScreen[1]) == 22U,
        "Skeletal Mesh viewport did not select the child joint at a bone end");
    kb::editor::tests::Require(
        kb::editor::SkeletalMeshEditorBonePicker::Pick(
            viewport, camera, lines, shaftX, shaftY + 10.0F) == 22U,
        "Skeletal Mesh viewport bone shaft hit target is narrower than the visible bone shape");
    kb::editor::tests::Require(
        !kb::editor::SkeletalMeshEditorBonePicker::Pick(
            viewport, camera, lines, shaftX, shaftY + 50.0F).has_value(),
        "Skeletal Mesh viewport selected a bone outside its visible hit target");
}

void RunAnimationPreviewExactScrubTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb-editor-animation-preview-scrub-tests";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    const std::filesystem::path skeletonPath = root / "Assets" / "Skeletal" / "Preview.kbskeleton";
    kb::scene::SkeletonAsset skeleton{};
    skeleton.bones = {{ .id = 7U, .parentIndex = -1, .name = "Root", .referencePose = {}, .inverseBind = {} }};
    kb::editor::tests::Require(kb::scene::SkeletonAssetIO::Save(skeletonPath, skeleton),
        "Animation preview scrub fixture could not save its skeleton");

    kb::scene::Scene source{ kb::scene::SceneMode::Runtime };
    kb::editor::tests::Require(source.Assets().MountProject(root) && source.Assets().Discover() == 1U,
        "Animation preview scrub fixture could not discover its skeleton");
    const kb::assets::AssetMetadata* skeletonMetadata =
        source.Assets().Manager().Registry().FindByPath("/Game/Skeletal/Preview.kbskeleton");
    kb::editor::tests::Require(skeletonMetadata != nullptr, "Animation preview scrub fixture lost skeleton metadata");
    if (skeletonMetadata == nullptr) return;
    const kb::assets::AssetId skeletonId = skeletonMetadata->id;

    const auto skeletonOnlySource = source.Assets().Manager().Load<kb::scene::SkeletonAsset>(skeletonId);
    kb::editor::tests::Require(skeletonOnlySource.IsLoaded(),
        "Skeleton-only preview fixture could not load its Skeleton");
    kb::editor::AnimationPreviewContext skeletonOnlyContext;
    skeletonOnlyContext.SetAssets(skeletonId, {}, {}, {});
    static_cast<void>(skeletonOnlyContext.Overlays().SetBonesVisible(true));
    kb::editor::EditorAnimationPreviewScene skeletonOnlyPreview;
    const kb::scene::Scene& skeletonOnlyScene = skeletonOnlyPreview.SceneFor(source, skeletonOnlyContext);
    const auto skeletonOnlyPose = skeletonOnlyScene.Animators().InstanceSkeleton(
        skeletonOnlyPreview.PreviewEntity());
    kb::editor::tests::Require(
        skeletonOnlyPose.has_value() &&
            skeletonOnlyPose->currentComponentPose.positions.size() == skeleton.bones.size() &&
            !skeletonOnlyScene.Components().MeshRenderers().Has(skeletonOnlyPreview.PreviewEntity()) &&
            !skeletonOnlyScene.Components().DeformedGeometries().Has(skeletonOnlyPreview.PreviewEntity()),
        "Skeleton-only preview should evaluate and display the rig without inventing mesh geometry");

    const std::uint64_t signature = kb::scene::SkeletonCompatibilitySignature(skeleton);
    kb::scene::SkeletalMeshAsset mesh{};
    mesh.skeletonAssetId = skeletonId.value;
    mesh.skeletonCompatibilitySignature = signature;
    mesh.conservativeBounds = { .center = {}, .extents = { 1.0F, 1.0F, 1.0F } };
    mesh.fixedBounds = mesh.conservativeBounds;
    kb::scene::SkeletalMeshLod lod{};
    lod.requiredBones = { 7U };
    lod.vertices = { {}, { .position = { 1.0F, 0.0F, 0.0F } }, { .position = { 0.0F, 1.0F, 0.0F } } };
    lod.indices = { 0U, 1U, 2U };
    lod.sections = {{ .firstIndex = 0U, .indexCount = 3U, .boneMap = { 7U } }};
    mesh.lods = { std::move(lod) };
    const std::filesystem::path meshPath = root / "Assets" / "Skeletal" / "Preview.kbskeletalmesh";
    kb::editor::tests::Require(kb::scene::SkeletalMeshAssetIO::Save(meshPath, mesh),
        "Animation preview scrub fixture could not save its mesh");

    kb::scene::AnimationClip clip{};
    clip.durationSeconds = 1.0F;
    clip.looping = true;
    clip.targetSkeletonAssetId = skeletonId.value;
    clip.targetSkeletonCompatibilitySignature = signature;
    clip.skeletalTracks = {{ .boneId = 7U, .bindingMask = 1U, .keyframes = {
        { .timeSeconds = 0.0F, .transform = {} },
        { .timeSeconds = 1.0F, .transform = { .position = { 10.0F, 0.0F, 0.0F } } },
    } }};
    const std::filesystem::path clipPath = root / "Assets" / "Animation" / "Preview.kbanim";
    kb::editor::tests::Require(kb::scene::AnimationAssetIO::SaveClip(clipPath, clip),
        "Animation preview scrub fixture could not save its clip");
    kb::scene::AnimatorController controller{};
    controller.layers = {{ .name = "Base", .defaultState = "Preview", .weight = 1.0F, .mask = 1U,
        .states = {{ .name = "Preview", .clipReference = "/Game/Animation/Preview.kbanim" }} }};
    const std::filesystem::path controllerPath = root / "Assets" / "Animation" / "Preview.kbanimcontroller";
    kb::editor::tests::Require(kb::scene::AnimationAssetIO::SaveController(controllerPath, controller) &&
            source.Assets().Discover() == 4U,
        "Animation preview scrub fixture could not discover mesh and animation assets");

    const kb::assets::AssetMetadata* meshMetadata =
        source.Assets().Manager().Registry().FindByPath("/Game/Skeletal/Preview.kbskeletalmesh");
    const kb::assets::AssetMetadata* clipMetadata =
        source.Assets().Manager().Registry().FindByPath("/Game/Animation/Preview.kbanim");
    const kb::assets::AssetMetadata* controllerMetadata =
        source.Assets().Manager().Registry().FindByPath("/Game/Animation/Preview.kbanimcontroller");
    kb::editor::tests::Require(meshMetadata != nullptr && clipMetadata != nullptr && controllerMetadata != nullptr,
        "Animation preview scrub fixture lost runtime asset metadata");
    if (meshMetadata == nullptr || clipMetadata == nullptr || controllerMetadata == nullptr) return;
    const auto sourceSkeleton = source.Assets().Manager().Load<kb::scene::SkeletonAsset>(skeletonId);
    const std::string skeletonLoadFailure = "Animation preview scrub fixture could not load its skeleton: " +
        source.Assets().Manager().LastError();
    kb::editor::tests::Require(sourceSkeleton.IsLoaded(), skeletonLoadFailure.c_str());
    const auto sourceMesh = source.Assets().Manager().Load<kb::scene::SkeletalMeshAsset>(meshMetadata->id);
    kb::editor::tests::Require(sourceMesh.IsLoaded(),
        "Animation preview scrub fixture could not load its skeletal mesh");
    kb::editor::tests::Require(source.Assets().Manager().Load<kb::scene::AnimationClip>(clipMetadata->id).IsLoaded(),
        "Animation preview scrub fixture could not load its clip");
    kb::editor::tests::Require(source.Assets().Manager().Load<kb::scene::AnimatorController>(controllerMetadata->id).IsLoaded(),
        "Animation preview scrub fixture could not load its controller");

    kb::editor::AnimationPreviewContext referenceContext;
    referenceContext.SetAssets(skeletonId, meshMetadata->id, {}, {});
    kb::editor::EditorAnimationPreviewScene referencePreview;
    const kb::scene::Scene& referenceScene = referencePreview.SceneFor(source, referenceContext);
    const auto sharedPreviewMesh = referenceScene.Assets().Manager().AcquireLoaded<kb::scene::SkeletalMeshAsset>(meshMetadata->id);
    kb::editor::tests::Require(
        sharedPreviewMesh.IsLoaded() && sharedPreviewMesh.Shared().get() == sourceMesh.Shared().get(),
        "Animation preview reparsed an already-loaded skeletal mesh instead of sharing its immutable payload");
    const auto referencePose = referenceScene.Animators().InstanceSkeleton(referencePreview.PreviewEntity());
    const kb::scene::DrawD3DeformedGeometryComponent* referenceGeometry =
        referenceScene.Components().DeformedGeometries().TryGet(referencePreview.PreviewEntity());
    kb::editor::tests::Require(
        !referenceScene.Animators().Exists(referencePreview.PreviewEntity()) && referencePose.has_value() &&
            referencePose->currentComponentPose.positions.size() == skeleton.bones.size() &&
            referenceGeometry != nullptr && referenceGeometry->enabled,
        "SkeletonBinding did not expose its reference pose to deformed rendering without an Animator");

    kb::editor::AnimationPreviewContext context;
    context.SetAssets(skeletonId, meshMetadata->id, clipMetadata->id, {});
    context.SetPoseMode(kb::editor::AnimationPreviewPoseMode::Animated);
    kb::editor::EditorAnimationPreviewScene preview;
    const kb::scene::Scene& initialPreviewScene = preview.SceneFor(source, context);
    kb::editor::tests::Require(initialPreviewScene.Animators().Exists(preview.PreviewEntity()),
        "Animation Clip preview did not create a runtime animator from the runtime clip");
    kb::editor::tests::Require(context.Transport().Scrub(0.5F), "Animation preview scrub rejected a normalized midpoint");
    const kb::scene::Scene& previewScene = preview.SceneFor(source, context);
    const auto pose = previewScene.Animators().InstanceSkeleton(preview.PreviewEntity());
    const float sampledPosition = pose.has_value() && !pose->currentComponentPose.positions.empty()
        ? pose->currentComponentPose.positions.front().x
        : -1000.0F;
    const std::string scrubFailure = "Animation preview scrub did not evaluate the exact midpoint skeletal pose (sample=" +
        std::to_string(sampledPosition) + ")";
    kb::editor::tests::Require(
        pose.has_value() && pose->currentComponentPose.positions.size() == 1U &&
            std::fabs(sampledPosition - 5.0F) < 0.0001F,
        scrubFailure.c_str());
    std::filesystem::remove_all(root, error);
}

void RunHostSurfaceLifecycleStateTest() {
    kb::editor::EditorHostSurfaceLifecycle lifecycle;
    constexpr kb::editor::EditorHostSurfaceLifecycle::HostKey firstHost = 1U;
    constexpr kb::editor::EditorHostSurfaceLifecycle::HostKey secondHost = 2U;
    lifecycle.TrackHost(firstHost);
    lifecycle.TrackHost(secondHost);
    kb::editor::tests::Require(lifecycle.Suspend(firstHost) && lifecycle.IsSuspended(firstHost) && !lifecycle.IsSuspended(secondHost),
        "Host lifecycle did not isolate a minimized host");
    kb::editor::tests::Require(!lifecycle.Suspend(firstHost) && lifecycle.Resume(firstHost) && !lifecycle.IsSuspended(firstHost),
        "Host lifecycle did not make minimize/resume transitions idempotent");
    kb::editor::tests::Require(lifecycle.Suspend(firstHost) && lifecycle.SuspendAll() &&
            lifecycle.IsSuspended(firstHost) && lifecycle.IsSuspended(secondHost),
        "Host lifecycle did not suspend every host on application deactivation");
    kb::editor::tests::Require(!lifecycle.Resume(firstHost) && lifecycle.ResumeAll() &&
            lifecycle.IsSuspended(firstHost) && !lifecycle.IsSuspended(secondHost) && lifecycle.Resume(firstHost),
        "Host lifecycle did not retain a minimized host across application reactivation");
}

void RunFitCameraAndCustomTest() {
    kb::editor::EditorViewportPreviewState state;
    state.CycleFitMode();
    kb::editor::tests::Require(state.FitMode() == kb::editor::EditorViewportFitMode::OneToOne, "Viewport fit cycle did not select 1:1");
    state.CycleFitMode();
    kb::editor::tests::Require(state.FitMode() == kb::editor::EditorViewportFitMode::Fill, "Viewport fit cycle did not select Fill");

    state.CycleCameraMode();
    kb::editor::tests::Require(state.CameraMode() == kb::editor::EditorViewportCameraMode::OverrideCamera, "Viewport camera cycle did not select override camera from default game camera");

    state.SetProfile(kb::editor::EditorViewportProfileKind::Custom);
    state.SetCustomResolution(1U, 50000U);
    kb::editor::tests::Require(state.RenderWidthForPanel(800U) == 16U, "Custom viewport width was not clamped");
    kb::editor::tests::Require(state.RenderHeightForPanel(600U) == 16384U, "Custom viewport height was not clamped");
}

void RunRenderProfileCycleTest() {
    kb::editor::EditorViewportPreviewState state;
    kb::editor::tests::Require(state.RenderProfile() == kb::editor::EditorViewportRenderProfile::Interactive, "Viewport render profile should default to Interactive");
    kb::editor::tests::Require(std::string_view(kb::editor::EditorViewportRenderProfileLabel(state.RenderProfile())) == "Interactive", "Interactive render profile label is wrong");

    state.CycleRenderProfile();
    kb::editor::tests::Require(state.RenderProfile() == kb::editor::EditorViewportRenderProfile::Lit, "Viewport render profile cycle did not select Lit");
    kb::editor::tests::Require(std::string_view(kb::editor::EditorViewportRenderProfileLabel(state.RenderProfile())) == "Lit", "Lit render profile label is wrong");

    state.CycleRenderProfile();
    kb::editor::tests::Require(state.RenderProfile() == kb::editor::EditorViewportRenderProfile::GamePreview, "Viewport render profile cycle did not select GamePreview");
    kb::editor::tests::Require(std::string_view(kb::editor::EditorViewportRenderProfileLabel(state.RenderProfile())) == "Game", "Game render profile label is wrong");

    state.CycleRenderProfile();
    kb::editor::tests::Require(state.RenderProfile() == kb::editor::EditorViewportRenderProfile::Interactive, "Viewport render profile cycle did not wrap to Interactive");
}

void RunTerrainToolbarAndStrokeTickPolicyTest() {
    const RECT content{ 10, 20, 1010, 720 };
    kb::editor::EditorViewportPreviewState viewport;
    const kb::editor::SceneViewportToolbarRects baseLayout =
        kb::editor::SceneViewportToolbarLayout::Resolve(content, viewport, false);
    const kb::editor::SceneViewportToolbarRects terrainLayout =
        kb::editor::SceneViewportToolbarLayout::Resolve(content, viewport, true);
    const kb::editor::TerrainViewportToolbarRects terrainTools =
        kb::editor::SceneViewportToolbarLayout::ResolveTerrainTools(content);

    kb::editor::tests::Require(
        baseLayout.renderArea.top == baseLayout.toolbar.bottom,
        "Scene render area must start below the fixed scene toolbar");
    kb::editor::tests::Require(
        terrainLayout.renderArea.top == terrainTools.panel.bottom + 8,
        "Terrain render area must reserve the fixed terrain toolbar row");
    kb::editor::tests::Require(
        terrainLayout.renderArea.top > baseLayout.renderArea.top,
        "Visible terrain tools must not overlap the bgfx viewport child window");

    float elapsed = 0.0F;
    kb::editor::tests::Require(
        !kb::editor::EditorTerrainStrokeTickPolicy::Advance(
            kb::editor::EditorTerrainStrokeTickPolicy::TickIntervalSeconds * 0.5F,
            elapsed),
        "Held sculpt must wait for its fixed update interval");
    kb::editor::tests::Require(
        kb::editor::EditorTerrainStrokeTickPolicy::Advance(
            kb::editor::EditorTerrainStrokeTickPolicy::TickIntervalSeconds * 0.5F,
            elapsed),
        "Held sculpt must tick without requiring mouse movement");
    kb::editor::tests::Require(
        kb::editor::EditorTerrainStrokeTickPolicy::Advance(1.0F, elapsed),
        "A delayed frame must still produce a held sculpt tick");
    kb::editor::tests::Require(
        elapsed < kb::editor::EditorTerrainStrokeTickPolicy::TickIntervalSeconds,
        "Held sculpt must discard an unbounded stalled-frame backlog");
}

void RunGridAndSnapStateTest() {
    kb::editor::EditorViewportPreviewState state;
    kb::editor::tests::Require(state.GridVisible(), "Viewport grid should be visible by default");
    kb::editor::tests::Require(!state.SnapEnabled(), "Viewport snap should be disabled by default");
    kb::editor::tests::Require(std::string_view(kb::editor::EditorViewportGridSpacingLabel(state.GridSpacing())) == "1m", "Default grid spacing label is wrong");
    kb::editor::tests::Require(std::string_view(kb::editor::EditorViewportSnapStepLabel(state.SnapStep())) == "1m", "Default snap step label is wrong");

    state.ToggleGridVisible();
    kb::editor::tests::Require(!state.GridVisible(), "Grid visibility toggle failed");
    state.CycleGridSpacing();
    kb::editor::tests::Require(std::string_view(kb::editor::EditorViewportGridSpacingLabel(state.GridSpacing())) == "2m", "Grid spacing cycle failed");
    kb::editor::tests::Require(kb::editor::EditorViewportGridSpacingOptionCount() == 6U, "Grid spacing dropdown option count is wrong");
    state.ToggleToolbarDropdown(kb::editor::EditorViewportToolbarDropdown::GridSpacing);
    kb::editor::tests::Require(state.ToolbarDropdown() == kb::editor::EditorViewportToolbarDropdown::GridSpacing, "Grid spacing dropdown did not open");
    state.SetGridSpacing(kb::editor::EditorViewportGridSpacingOption(1U));
    kb::editor::tests::Require(state.ToolbarDropdown() == kb::editor::EditorViewportToolbarDropdown::GridSpacing, "Choosing a grid spacing should keep the dropdown open");
    state.CloseToolbarDropdown();
    kb::editor::tests::Require(state.ToolbarDropdown() == kb::editor::EditorViewportToolbarDropdown::None, "Grid spacing dropdown did not close on explicit dismiss");
    kb::editor::tests::Require(std::string_view(kb::editor::EditorViewportGridSpacingLabel(state.GridSpacing())) == "0.5m", "Grid spacing option selection failed");

    state.ToggleSnapEnabled();
    state.SetSnapStep(0.5F);
    state.ToggleToolbarDropdown(kb::editor::EditorViewportToolbarDropdown::SnapStep);
    kb::editor::tests::Require(state.ToolbarDropdown() == kb::editor::EditorViewportToolbarDropdown::SnapStep, "Snap dropdown did not open");
    state.SetSnapStep(kb::editor::EditorViewportSnapStepOption(2U));
    kb::editor::tests::Require(state.ToolbarDropdown() == kb::editor::EditorViewportToolbarDropdown::SnapStep, "Choosing a snap step should keep the dropdown open");
    state.CloseToolbarDropdown();
    kb::editor::tests::Require(state.ToolbarDropdown() == kb::editor::EditorViewportToolbarDropdown::None, "Snap dropdown did not close on explicit dismiss");
    state.SetSnapStep(0.5F);
    const kb::scene::Vec3 snapped = state.SnapPosition(kb::scene::Vec3{ 1.24F, 1.26F, -1.26F });
    RequireNear(snapped.x, 1.0F, 0.001F, "SnapPosition did not snap x");
    RequireNear(snapped.y, 1.5F, 0.001F, "SnapPosition did not snap y");
    RequireNear(snapped.z, -1.5F, 0.001F, "SnapPosition did not snap z");

    const kb::scene::Vec3 axisSnapped = state.SnapPositionAxis(kb::scene::Vec3{ 1.24F, 1.26F, -1.26F }, 0);
    RequireNear(axisSnapped.x, 1.0F, 0.001F, "Axis snap did not snap the requested axis");
    RequireNear(axisSnapped.y, 1.26F, 0.001F, "Axis snap changed an unrelated axis");
}

void RunViewportCameraAxesTest() {
    kb::editor::EditorViewportCameraState camera;
    const kb::editor::EditorViewportCameraAxes axes = camera.Axes();

    RequireNear(axes.position.x, 8.0F, 0.001F, "Viewport camera default x position is wrong");
    RequireNear(axes.position.y, 6.0F, 0.001F, "Viewport camera default y position is wrong");
    RequireNear(axes.position.z, -8.0F, 0.001F, "Viewport camera default z position is wrong");
    RequireNear(axes.forward.x, -0.612F, 0.001F, "Viewport camera default forward x is wrong");
    RequireNear(axes.forward.y, -0.5F, 0.001F, "Viewport camera default forward y is wrong");
    RequireNear(axes.forward.z, 0.612F, 0.001F, "Viewport camera default forward z is wrong");
    RequireNear(axes.up.y, 0.866F, 0.001F, "Viewport camera default up vector is wrong");
}

void RunViewportCameraNavigationTest() {
    kb::editor::EditorViewportCameraState camera;
    camera.BeginNavigation(kb::editor::EditorViewportCameraNavigationMode::Look, 100, 100);
    kb::editor::tests::Require(camera.IsNavigating(), "Viewport camera should enter navigation mode");
    kb::editor::tests::Require(camera.AllowsKeyboardFlight(), "RMB look mode should allow keyboard flight");

    static_cast<void>(camera.UpdatePointer(200, 50));
    kb::editor::tests::Require(camera.YawDegrees() > -45.0F, "Dragging look mode right should increase yaw");
    kb::editor::tests::Require(camera.PitchDegrees() > -30.0F, "Dragging look mode up should increase pitch");

    const kb::scene::Vec3 beforeFlight = camera.Position();
    const bool moved = camera.ApplyKeyboardFlight(
        kb::editor::EditorViewportCameraFlightInput{ .forward = true, .right = true, .up = true },
        1.0F);
    kb::editor::tests::Require(moved, "Viewport camera flight input should move camera");
    const kb::scene::Vec3 afterFlight = camera.Position();
    kb::editor::tests::Require(
        afterFlight.x != beforeFlight.x || afterFlight.y != beforeFlight.y || afterFlight.z != beforeFlight.z,
        "Viewport camera position did not change after flight input");

    const float speedBeforeWheel = camera.Speed();
    static_cast<void>(camera.ApplyWheel(1.0F, true));
    kb::editor::tests::Require(camera.Speed() > speedBeforeWheel, "Mouse wheel in flight mode should increase camera speed");
    camera.EndNavigation();
    kb::editor::tests::Require(!camera.IsNavigating(), "Viewport camera should exit navigation mode");
}

void RunViewportCameraFlightSmoothingTest() {
    kb::editor::EditorViewportCameraState camera;
    camera.BeginNavigation(kb::editor::EditorViewportCameraNavigationMode::Look, 0, 0);
    constexpr float deltaSeconds = 1.0F / 60.0F;
    const kb::editor::EditorViewportCameraFlightInput forward{ .forward = true };

    const kb::scene::Vec3 start = camera.Position();
    kb::editor::tests::Require(camera.ApplyKeyboardFlight(forward, deltaSeconds), "Smoothed camera flight should start moving on the first frame");
    const float firstStep = kb::math::Length(camera.Position() - start);
    const kb::scene::Vec3 beforeSettledStep = camera.Position();
    for (int frame = 0; frame < 20; ++frame) {
        static_cast<void>(camera.ApplyKeyboardFlight(forward, deltaSeconds));
    }
    const float settledStep = kb::math::Length(camera.Position() - beforeSettledStep) / 20.0F;
    kb::editor::tests::Require(firstStep < settledStep, "Camera flight should ease into its target speed");

    const kb::scene::Vec3 beforeRelease = camera.Position();
    kb::editor::tests::Require(camera.ApplyKeyboardFlight({}, deltaSeconds), "Camera flight should ease out after releasing movement keys");
    kb::editor::tests::Require(kb::math::Length(camera.Position() - beforeRelease) > 0.0F, "Camera flight release should retain a small deceleration step");
    camera.EndNavigation();
    kb::editor::tests::Require(!camera.ApplyKeyboardFlight({}, deltaSeconds), "Ending camera navigation should clear smoothed flight velocity");
}

void RunViewportCameraOrbitTest() {
    kb::editor::EditorViewportCameraState camera;
    camera.BeginNavigation(kb::editor::EditorViewportCameraNavigationMode::Orbit, 20, 20);
    const kb::scene::Vec3 beforeOrbit = camera.Position();
    static_cast<void>(camera.UpdatePointer(120, 20));
    const kb::scene::Vec3 afterOrbit = camera.Position();

    kb::editor::tests::Require(camera.YawDegrees() > -45.0F, "Alt+LMB orbit should rotate camera yaw");
    kb::editor::tests::Require(
        afterOrbit.x != beforeOrbit.x || afterOrbit.z != beforeOrbit.z,
        "Alt+LMB orbit should move camera around the pivot");
}

void RunViewportCameraTrackDirectionTest() {
    kb::editor::EditorViewportCameraState camera;
    camera.BeginNavigation(kb::editor::EditorViewportCameraNavigationMode::Track, 100, 100);
    const kb::scene::Vec3 beforeTrack = camera.Position();

    static_cast<void>(camera.UpdatePointer(120, 80));
    const kb::scene::Vec3 afterTrack = camera.Position();

    kb::editor::tests::Require(afterTrack.x < beforeTrack.x, "Dragging track mode right should move camera left");
    kb::editor::tests::Require(afterTrack.y < beforeTrack.y, "Dragging track mode up should move camera down");
}

void RunViewportMeshPickerNearestMeshRendererTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity farEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Far Mesh" });
    const kb::scene::SceneEntity nearEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Near Mesh" });
    const kb::scene::SceneEntity offRay = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Off Ray Mesh" });

    scene.Components().MeshRenderers().Set(farEntity, kb::scene::MeshRendererComponent{ .meshAssetId = 101U });
    scene.Components().MeshRenderers().Set(nearEntity, kb::scene::MeshRendererComponent{ .meshAssetId = 202U });
    scene.Components().MeshRenderers().Set(offRay, kb::scene::MeshRendererComponent{ .meshAssetId = 303U });

    scene.Transforms().Set(farEntity, kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 0.0F, 0.0F, 5.0F } });
    scene.Transforms().Set(nearEntity, kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 0.0F, 0.0F, 0.0F } });
    scene.Transforms().Set(offRay, kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 5.0F, 0.0F, 0.0F } });

    const kb::editor::EditorSceneViewportPickResult pick = kb::editor::EditorSceneViewportMeshPicker::PickNearest(
        scene,
        kb::editor::EditorSceneViewportRay{
            .origin = kb::scene::Vec3{ 0.0F, 0.0F, -8.0F },
            .direction = kb::scene::Vec3{ 0.0F, 0.0F, 1.0F },
        });

    kb::editor::tests::Require(pick.IsValid(), "Viewport mesh picker should hit a Mesh Renderer under the ray");
    kb::editor::tests::Require(pick.entity == nearEntity, "Viewport mesh picker should choose the nearest Mesh Renderer under the ray");
}

void RunViewportMeshPickerUsesSynchronizedWorldTransformsTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity parent = scene.Entities().CreateEntity(
        kb::scene::SceneObjectDesc{ .name = "Moved Prefab Root" });
    const kb::scene::SceneEntity child = scene.Entities().CreateEntity(
        kb::scene::SceneObjectDesc{ .name = "Nested Mesh" });
    kb::editor::tests::Require(
        scene.Hierarchy().SetParent(child, parent),
        "Nested picker fixture should parent the mesh below the moved root");
    scene.Components().MeshRenderers().Set(
        child, kb::scene::MeshRendererComponent{ .meshAssetId = 404U });
    scene.Transforms().Set(
        parent,
        kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 6.0F, 0.0F, 0.0F },
        });
    scene.Transforms().Set(
        child,
        kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, 0.0F, 5.0F },
        });

    const kb::editor::EditorSceneViewportPickResult pick =
        kb::editor::EditorSceneViewportMeshPicker::PickNearest(
            scene,
            kb::editor::EditorSceneViewportRay{
                .origin = kb::scene::Vec3{ 6.0F, 0.0F, -8.0F },
                .direction = kb::scene::Vec3{ 0.0F, 0.0F, 1.0F },
            });

    kb::editor::tests::Require(
        pick.IsValid() && pick.entity == child,
        "Viewport picker should hit a nested mesh at its world position after its root moves");
}

void RunViewportMeshPickerUsesPublishedWorldBoundsTest() {
    // A character mesh is authored with its origin at the feet, so its visible body sits
    // entirely outside the picker's fallback unit box around the entity origin. The
    // renderer already publishes the real world bounds it culls with, and a click on the
    // drawn body must hit the entity.
    kb::scene::Scene scene;
    const kb::scene::SceneEntity beast = scene.Entities().CreateEntity(
        kb::scene::SceneObjectDesc{ .name = "Tall Character" });
    scene.Components().MeshRenderers().Set(
        beast, kb::scene::MeshRendererComponent{ .meshAssetId = 909U });
    scene.Transforms().Set(
        beast,
        kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, 0.0F, 0.0F },
        });

    kb::scene::SceneRenderVisibilityFrame frame;
    frame.frustumValid = true;
    frame.entries.push_back(kb::scene::SceneRenderVisibilityEntry{
        .entityId = beast.Id(),
        .worldBounds = kb::scene::SceneRenderBounds{
            .center = kb::math::Vec3{ 0.0F, 8.0F, 0.0F },
            .radius = 3.0F,
        },
        .visible = true,
    });
    kb::scene::SceneRenderFeedback::Publish(scene, frame);

    const kb::editor::EditorSceneViewportPickResult bodyPick =
        kb::editor::EditorSceneViewportMeshPicker::PickNearest(
            scene,
            kb::editor::EditorSceneViewportRay{
                .origin = kb::scene::Vec3{ 0.0F, 8.0F, -20.0F },
                .direction = kb::scene::Vec3{ 0.0F, 0.0F, 1.0F },
            });

    kb::editor::tests::Require(
        bodyPick.IsValid() && bodyPick.entity == beast,
        "Viewport picker should hit a mesh through its published world bounds, not only the unit box at its origin");

    const kb::editor::EditorSceneViewportPickResult emptyPick =
        kb::editor::EditorSceneViewportMeshPicker::PickNearest(
            scene,
            kb::editor::EditorSceneViewportRay{
                .origin = kb::scene::Vec3{ 0.0F, 40.0F, -20.0F },
                .direction = kb::scene::Vec3{ 0.0F, 0.0F, 1.0F },
            });

    kb::editor::tests::Require(
        !emptyPick.IsValid(),
        "Viewport picker must not report a hit for a ray that misses the published world bounds");
}

void RunViewportMeshPickerSkipsHiddenMeshesTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity hidden = scene.Entities().CreateEntity(
        kb::scene::SceneObjectDesc{ .name = "Hidden Mesh" });
    const kb::scene::SceneEntity visible = scene.Entities().CreateEntity(
        kb::scene::SceneObjectDesc{ .name = "Visible Mesh" });
    scene.Components().MeshRenderers().Set(
        hidden, kb::scene::MeshRendererComponent{ .meshAssetId = 405U });
    scene.Components().MeshRenderers().Set(
        visible, kb::scene::MeshRendererComponent{ .meshAssetId = 406U });
    scene.Components().Visibility().Set(
        hidden, kb::scene::VisibilityComponent{ .visible = false });
    scene.Transforms().Set(
        hidden,
        kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, 0.0F, 0.0F },
        });
    scene.Transforms().Set(
        visible,
        kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, 0.0F, 5.0F },
        });

    const kb::editor::EditorSceneViewportPickResult pick =
        kb::editor::EditorSceneViewportMeshPicker::PickNearest(
            scene,
            kb::editor::EditorSceneViewportRay{
                .origin = kb::scene::Vec3{ 0.0F, 0.0F, -8.0F },
                .direction = kb::scene::Vec3{ 0.0F, 0.0F, 1.0F },
            });

    kb::editor::tests::Require(
        pick.IsValid() && pick.entity == visible,
        "Viewport picker should ignore a hidden mesh in front of a visible mesh");
}

kb::editor::EditorSceneViewportRay BuildViewportRay(
    const kb::editor::EditorViewportCameraState& camera,
    const RECT& renderArea,
    float screenX,
    float screenY) {
    const kb::editor::EditorViewportCameraAxes axes = camera.Axes();
    const float width = kb::editor::EditorSceneViewportMath::RectWidth(renderArea);
    const float height = kb::editor::EditorSceneViewportMath::RectHeight(renderArea);
    const float aspect = width / height;
    const float tanHalfFov = std::tan(kb::editor::EditorSceneViewportMath::DegreesToRadians(camera.VerticalFovDegrees()) * 0.5F);
    const float ndcX = (screenX / width) * 2.0F - 1.0F;
    const float ndcY = 1.0F - (screenY / height) * 2.0F;
    const kb::scene::Vec3 direction = kb::editor::EditorSceneViewportMath::Normalize(kb::editor::EditorSceneViewportMath::Add(
        axes.forward,
        kb::editor::EditorSceneViewportMath::Add(
            kb::editor::EditorSceneViewportMath::Mul(axes.right, ndcX * tanHalfFov * aspect),
            kb::editor::EditorSceneViewportMath::Mul(axes.up, ndcY * tanHalfFov))));
    return kb::editor::EditorSceneViewportRay{
        .origin = axes.position,
        .direction = direction,
    };
}

void RunViewportLightWireframePickerChoosesNestedInnerWireframeTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity outerLight = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Outer Point Light" });
    const kb::scene::SceneEntity innerLight = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Inner Point Light" });

    scene.Components().Lights().Set(outerLight, kb::scene::LightComponent{
        .kind = kb::scene::LightKind::Point,
        .range = 3.0F,
    });
    scene.Components().Lights().Set(innerLight, kb::scene::LightComponent{
        .kind = kb::scene::LightKind::Point,
        .range = 1.0F,
    });

    const kb::scene::Vec3 position{0.0F, 0.0F, 0.0F};
    scene.Transforms().Set(outerLight, kb::scene::TransformComponent{ .localPosition = position, .worldPosition = position });
    scene.Transforms().Set(innerLight, kb::scene::TransformComponent{ .localPosition = position, .worldPosition = position });

    const kb::editor::EditorViewportCameraState camera;
    const RECT renderArea{0, 0, 960, 540};
    const kb::editor::EditorViewportCameraAxes axes = camera.Axes();
    float screenX = 0.0F;
    float screenY = 0.0F;
    const bool projected = kb::editor::EditorSceneViewportMath::WorldToScreen(
        camera,
        renderArea,
        kb::editor::EditorSceneViewportMath::Add(position, kb::editor::EditorSceneViewportMath::Mul(axes.right, 1.0F)),
        screenX,
        screenY);

    kb::editor::tests::Require(projected, "Point light wireframe test point should project into the viewport");
    const kb::editor::EditorSceneViewportPickResult pick = kb::editor::EditorSceneViewportMeshPicker::PickNearest(
        scene,
        camera,
        renderArea,
        screenX,
        screenY,
        BuildViewportRay(camera, renderArea, screenX, screenY));

    kb::editor::tests::Require(pick.IsValid(), "Viewport picker should hit a point light wireframe");
    kb::editor::tests::Require(pick.entity == innerLight, "Viewport picker should choose the nested inner light wireframe");

    float insideScreenX = 0.0F;
    float insideScreenY = 0.0F;
    const bool insideProjected = kb::editor::EditorSceneViewportMath::WorldToScreen(
        camera,
        renderArea,
        kb::editor::EditorSceneViewportMath::Add(position, kb::editor::EditorSceneViewportMath::Mul(axes.right, 0.5F)),
        insideScreenX,
        insideScreenY);
    kb::editor::tests::Require(insideProjected, "Nested point light interior test point should project into the viewport");
    const kb::editor::EditorSceneViewportPickResult insidePick = kb::editor::EditorSceneViewportMeshPicker::PickNearest(
        scene,
        camera,
        renderArea,
        insideScreenX,
        insideScreenY,
        BuildViewportRay(camera, renderArea, insideScreenX, insideScreenY));
    kb::editor::tests::Require(insidePick.IsValid(), "Viewport picker should hit a point light interior without pixel-perfect aiming");
    kb::editor::tests::Require(insidePick.entity == innerLight, "Viewport picker should prefer the nested inner light volume over the enclosing outer light");

    const float forgivingScreenX = screenX + 12.0F;
    const float forgivingScreenY = screenY;
    const kb::editor::EditorSceneViewportPickResult forgivingPick = kb::editor::EditorSceneViewportMeshPicker::PickNearest(
        scene,
        camera,
        renderArea,
        forgivingScreenX,
        forgivingScreenY,
        BuildViewportRay(camera, renderArea, forgivingScreenX, forgivingScreenY));
    kb::editor::tests::Require(forgivingPick.IsValid(), "Viewport picker should hit a point light wireframe with a forgiving pick radius");
    kb::editor::tests::Require(forgivingPick.entity == innerLight, "Viewport picker should keep the nested inner light selectable near its wireframe edge");
}

void RunViewportLightIconPickerSelectsLightIconsTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity pointLight = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Point Light Icon" });
    const kb::scene::SceneEntity directionalLight = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Directional Light Icon" });

    scene.Components().Lights().Set(pointLight, kb::scene::LightComponent{
        .kind = kb::scene::LightKind::Point,
        .range = 2.0F,
    });
    scene.Components().Lights().Set(directionalLight, kb::scene::LightComponent{
        .kind = kb::scene::LightKind::Directional,
    });

    const kb::scene::Vec3 pointPosition{-1.0F, 0.0F, 0.0F};
    const kb::scene::Vec3 directionalPosition{1.0F, 0.0F, 0.0F};
    scene.Transforms().Set(pointLight, kb::scene::TransformComponent{ .localPosition = pointPosition });
    scene.Transforms().Set(directionalLight, kb::scene::TransformComponent{ .localPosition = directionalPosition });

    const kb::editor::EditorViewportCameraState camera;
    const RECT renderArea{0, 0, 960, 540};
    float pointScreenX = 0.0F;
    float pointScreenY = 0.0F;
    float directionalScreenX = 0.0F;
    float directionalScreenY = 0.0F;
    kb::editor::tests::Require(
        kb::editor::EditorSceneViewportMath::WorldToScreen(camera, renderArea, pointPosition, pointScreenX, pointScreenY),
        "Point light icon test point should project into the viewport");
    kb::editor::tests::Require(
        kb::editor::EditorSceneViewportMath::WorldToScreen(camera, renderArea, directionalPosition, directionalScreenX, directionalScreenY),
        "Directional light icon test point should project into the viewport");

    const kb::editor::EditorSceneViewportPickResult pointPick = kb::editor::EditorSceneViewportMeshPicker::PickNearest(
        scene,
        camera,
        renderArea,
        pointScreenX,
        pointScreenY,
        BuildViewportRay(camera, renderArea, pointScreenX, pointScreenY));
    kb::editor::tests::Require(pointPick.IsValid(), "Viewport picker should hit a point light icon");
    kb::editor::tests::Require(pointPick.entity == pointLight, "Viewport picker should select the point light icon under the cursor");

    const kb::editor::EditorSceneViewportPickResult directionalPick = kb::editor::EditorSceneViewportMeshPicker::PickNearest(
        scene,
        camera,
        renderArea,
        directionalScreenX,
        directionalScreenY,
        BuildViewportRay(camera, renderArea, directionalScreenX, directionalScreenY));
    kb::editor::tests::Require(directionalPick.IsValid(), "Viewport picker should hit a directional light icon");
    kb::editor::tests::Require(directionalPick.entity == directionalLight, "Viewport picker should select the directional light icon under the cursor");
}

void RunViewportMeshPickerWinsInsideLightWireframeVolumeTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity light = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Enclosing Point Light" });
    const kb::scene::SceneEntity mesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Mesh Inside Light" });

    scene.Components().Lights().Set(light, kb::scene::LightComponent{
        .kind = kb::scene::LightKind::Point,
        .range = 4.0F,
    });
    scene.Components().MeshRenderers().Set(mesh, kb::scene::MeshRendererComponent{ .meshAssetId = 404U });

    const kb::scene::Vec3 lightPosition{0.0F, 0.0F, 0.0F};
    const kb::scene::Vec3 meshPosition{1.5F, 0.0F, 0.0F};
    scene.Transforms().Set(light, kb::scene::TransformComponent{ .localPosition = lightPosition });
    scene.Transforms().Set(mesh, kb::scene::TransformComponent{ .localPosition = meshPosition });

    const kb::editor::EditorViewportCameraState camera;
    const RECT renderArea{0, 0, 960, 540};
    float screenX = 0.0F;
    float screenY = 0.0F;
    kb::editor::tests::Require(
        kb::editor::EditorSceneViewportMath::WorldToScreen(camera, renderArea, meshPosition, screenX, screenY),
        "Mesh inside light wireframe test point should project into the viewport");

    const kb::editor::EditorSceneViewportPickResult pick = kb::editor::EditorSceneViewportMeshPicker::PickNearest(
        scene,
        camera,
        renderArea,
        screenX,
        screenY,
        BuildViewportRay(camera, renderArea, screenX, screenY));
    kb::editor::tests::Require(pick.IsValid(), "Viewport picker should hit a mesh inside a light wireframe");
    kb::editor::tests::Require(pick.entity == mesh, "Viewport picker should prefer a mesh hit over a light wireframe volume hit");
}

void RunRenderBackendSettingsTest() {
    kb::editor::EditorRenderBackendSettings settings;
    kb::editor::tests::Require(settings.Backend() == kb::editor::EditorRenderBackend::Auto, "Render backend should default to Auto");
    kb::editor::tests::Require(settings.Generation() == 0U, "Default render backend generation should be zero");
    kb::editor::tests::Require(kb::editor::EditorRenderBackendLabel(settings.Backend()) == std::string_view("Auto"), "Auto backend label is wrong");

    settings.CycleBackend();
    kb::editor::tests::Require(settings.Backend() == kb::editor::EditorRenderBackend::DirectX12, "Render backend cycle should select DX12");
    kb::editor::tests::Require(settings.Generation() == 1U, "Render backend cycle should bump generation");
    kb::editor::tests::Require(kb::editor::EditorRenderBackendLabel(settings.Backend()) == std::string_view("DX12"), "DX12 backend label is wrong");

    settings.CycleBackend();
    kb::editor::tests::Require(settings.Backend() == kb::editor::EditorRenderBackend::Vulkan, "Render backend cycle should select Vulkan");
    kb::editor::tests::Require(settings.Generation() == 2U, "Second render backend cycle should bump generation");
    kb::editor::tests::Require(kb::editor::EditorRenderBackendLabel(settings.Backend()) == std::string_view("Vulkan"), "Vulkan backend label is wrong");

    settings.SetBackend(kb::editor::EditorRenderBackend::Vulkan);
    kb::editor::tests::Require(settings.Generation() == 2U, "Setting identical render backend should not bump generation");

    settings.CycleBackend();
    kb::editor::tests::Require(settings.Backend() == kb::editor::EditorRenderBackend::Auto, "Render backend cycle should return to Auto");
    kb::editor::tests::Require(settings.Generation() == 3U, "Third render backend cycle should bump generation");
}

void RunViewportParticleIconPickerSelectsParticleEffectTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity particle = scene.Entities().CreateEntity(
        kb::scene::SceneObjectDesc{ .name = "Particle Icon" });
    scene.Components().ParticleEffects().Set(
        particle,
        kb::scene::ParticleEffectComponent{
            .effectAssetId = 99U,
            .enabled = true,
        });

    const kb::editor::EditorViewportCameraState camera;
    const kb::editor::EditorViewportCameraAxes axes = camera.Axes();
    const kb::scene::Vec3 position = axes.position + axes.forward * 5.0F;
    scene.Transforms().Set(
        particle,
        kb::scene::TransformComponent{ .localPosition = position });
    const RECT renderArea{0, 0, 960, 540};
    float screenX = 0.0F;
    float screenY = 0.0F;
    kb::editor::tests::Require(
        kb::editor::EditorSceneViewportMath::WorldToScreen(
            camera, renderArea, position, screenX, screenY),
        "Particle icon test point should project into the viewport");

    const kb::editor::EditorSceneViewportPickResult pick =
        kb::editor::EditorSceneViewportMeshPicker::PickNearest(
            scene,
            camera,
            renderArea,
            screenX + 12.0F,
            screenY,
            BuildViewportRay(camera, renderArea, screenX + 12.0F, screenY));
    kb::editor::tests::Require(
        pick.IsValid() && pick.entity == particle,
        "Viewport picker should select the visible particle icon with the same forgiving target as light icons");
}

void RunViewportPickerSelectsEntityWithoutRenderableComponentTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity projectile = scene.Entities().CreateEntity(
        kb::scene::SceneObjectDesc{ .name = "Projectile" });
    scene.Components().Rigidbodies().Set(projectile, kb::scene::RigidbodyComponent{});

    const kb::editor::EditorViewportCameraState camera;
    const kb::editor::EditorViewportCameraAxes axes = camera.Axes();
    const kb::scene::Vec3 position = axes.position + axes.forward * 5.0F;
    scene.Transforms().Set(
        projectile,
        kb::scene::TransformComponent{ .localPosition = position });

    const RECT renderArea{0, 0, 960, 540};
    float screenX = 0.0F;
    float screenY = 0.0F;
    kb::editor::tests::Require(
        kb::editor::EditorSceneViewportMath::WorldToScreen(
            camera, renderArea, position, screenX, screenY),
        "Non-renderable entity test point should project into the viewport");

    const kb::editor::EditorSceneViewportPickResult pick =
        kb::editor::EditorSceneViewportMeshPicker::PickNearest(
            scene,
            camera,
            renderArea,
            screenX + 8.0F,
            screenY,
            BuildViewportRay(camera, renderArea, screenX + 8.0F, screenY));
    kb::editor::tests::Require(
        pick.IsValid() && pick.entity == projectile,
        "Viewport picker should select an entity that has no mesh, light, or particle component");

    const kb::editor::EditorSceneViewportPickResult miss =
        kb::editor::EditorSceneViewportMeshPicker::PickNearest(
            scene,
            camera,
            renderArea,
            screenX + 40.0F,
            screenY,
            BuildViewportRay(camera, renderArea, screenX + 40.0F, screenY));
    kb::editor::tests::Require(
        !miss.IsValid(),
        "The unmarked entity origin target must stay bounded instead of claiming distant clicks");
}

void RunViewportMeshWinsOverEntityOriginPickTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity group = scene.Entities().CreateEntity(
        kb::scene::SceneObjectDesc{ .name = "Group" });
    const kb::scene::SceneEntity mesh = scene.Entities().CreateEntity(
        kb::scene::SceneObjectDesc{ .name = "Mesh At Group Origin" });
    scene.Components().MeshRenderers().Set(
        mesh, kb::scene::MeshRendererComponent{ .meshAssetId = 707U });

    const kb::editor::EditorViewportCameraState camera;
    const kb::editor::EditorViewportCameraAxes axes = camera.Axes();
    const kb::scene::Vec3 position = axes.position + axes.forward * 5.0F;
    scene.Transforms().Set(group, kb::scene::TransformComponent{ .localPosition = position });
    scene.Transforms().Set(mesh, kb::scene::TransformComponent{ .localPosition = position });

    const RECT renderArea{0, 0, 960, 540};
    float screenX = 0.0F;
    float screenY = 0.0F;
    kb::editor::tests::Require(
        kb::editor::EditorSceneViewportMath::WorldToScreen(
            camera, renderArea, position, screenX, screenY),
        "Shared origin test point should project into the viewport");

    const kb::editor::EditorSceneViewportPickResult pick =
        kb::editor::EditorSceneViewportMeshPicker::PickNearest(
            scene,
            camera,
            renderArea,
            screenX,
            screenY,
            BuildViewportRay(camera, renderArea, screenX, screenY));
    kb::editor::tests::Require(
        pick.IsValid() && pick.entity == mesh,
        "A mesh must keep the click against a grouping entity that shares its origin");
}

void RunViewportBoxPickerIncludesVisibleComponentOverlaysTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity light = scene.Entities().CreateEntity(
        kb::scene::SceneObjectDesc{ .name = "Box Selected Light" });
    const kb::scene::SceneEntity particle = scene.Entities().CreateEntity(
        kb::scene::SceneObjectDesc{ .name = "Box Selected Particle" });
    const kb::scene::SceneEntity projectile = scene.Entities().CreateEntity(
        kb::scene::SceneObjectDesc{ .name = "Box Selected Projectile" });
    scene.Components().Lights().Set(light, kb::scene::LightComponent{});
    scene.Components().Rigidbodies().Set(projectile, kb::scene::RigidbodyComponent{});
    scene.Components().ParticleEffects().Set(
        particle,
        kb::scene::ParticleEffectComponent{ .effectAssetId = 100U });

    const kb::editor::EditorViewportCameraState camera;
    const kb::editor::EditorViewportCameraAxes axes = camera.Axes();
    const kb::scene::Vec3 center = axes.position + axes.forward * 5.0F;
    scene.Transforms().Set(
        light,
        kb::scene::TransformComponent{
            .localPosition = center - axes.right * 0.5F,
        });
    scene.Transforms().Set(
        particle,
        kb::scene::TransformComponent{
            .localPosition = center + axes.right * 0.5F,
        });
    scene.Transforms().Set(
        projectile,
        kb::scene::TransformComponent{ .localPosition = center });

    const RECT renderArea{0, 0, 960, 540};
    const std::vector<kb::scene::SceneEntity> picked =
        kb::editor::EditorSceneViewportMeshPicker::PickInsideRect(
            scene, camera, renderArea, RECT{400, 220, 560, 320});
    kb::editor::tests::Require(
        std::ranges::find(picked, light) != picked.end(),
        "Viewport box picker should include a visible light overlay");
    kb::editor::tests::Require(
        std::ranges::find(picked, particle) != picked.end(),
        "Viewport box picker should include a visible particle overlay");
    kb::editor::tests::Require(
        std::ranges::find(picked, projectile) != picked.end(),
        "Viewport box picker should include an entity that has no renderable component");
}

void RunEditorBackendSelectionTest() {
    constexpr std::array supported{
        bgfx::RendererType::Direct3D12,
        bgfx::RendererType::Direct3D11,
        bgfx::RendererType::Vulkan,
    };
    kb::editor::EditorRenderBackendSettings settings;
#if defined(_WIN32)
    kb::editor::tests::Require(
        kb::editor::EditorBgfxBackendSelector::Resolve(
            supported.data(), static_cast<std::uint8_t>(supported.size()), &settings) ==
            bgfx::RendererType::Direct3D11,
        "Auto editor backend should prefer low-latency D3D11 on Windows");
#endif
    settings.SetBackend(kb::editor::EditorRenderBackend::DirectX12);
    kb::editor::tests::Require(
        kb::editor::EditorBgfxBackendSelector::Resolve(
            supported.data(), static_cast<std::uint8_t>(supported.size()), &settings) ==
            bgfx::RendererType::Direct3D12,
        "Explicit DX12 editor backend selection was not honored");
}

void RunPlayModeStateTest() {
    kb::editor::EditorPlayModeState playMode;
    kb::editor::tests::Require(playMode.Mode() == kb::editor::EditorPlayMode::Stopped, "Editor play mode should default to stopped mode");
    kb::editor::tests::Require(!playMode.IsPlaying(), "Editor play mode should default to stopped");
    kb::editor::tests::Require(playMode.Generation() == 0U, "Default editor play mode generation should be zero");

    playMode.Play();
    kb::editor::tests::Require(playMode.IsPlaying(), "Editor play mode play command should enter play mode");
    kb::editor::tests::Require(playMode.Generation() == 1U, "Editor play mode play command should bump generation");

    playMode.Play();
    kb::editor::tests::Require(playMode.Generation() == 1U, "Setting identical play mode should not bump generation");

    playMode.Pause();
    kb::editor::tests::Require(playMode.IsPaused(), "Editor play mode pause command should enter paused mode");
    kb::editor::tests::Require(playMode.Generation() == 2U, "Pausing play mode should bump generation");

    playMode.Resume();
    kb::editor::tests::Require(playMode.IsPlaying(), "Editor play mode resume command should enter play mode");
    kb::editor::tests::Require(playMode.Generation() == 3U, "Resuming play mode should bump generation");

    playMode.Stop();
    kb::editor::tests::Require(!playMode.IsPlaying(), "Editor play mode stop command should leave play mode");
    kb::editor::tests::Require(playMode.Mode() == kb::editor::EditorPlayMode::Stopped, "Editor play mode stop command should select stopped mode");
    kb::editor::tests::Require(playMode.Generation() == 4U, "Stopping play mode should bump generation");
}

// LIB-062: the scene-viewport toolbar's per-frame numeric HUD labels are
// now formatted through kb::library::TextFormatBuffer (via
// SceneViewportToolbarLabelFormat) instead of std::snprintf. Prove the
// formatter — the actual production code path that builds the on-screen
// FPS/draw-call/mesh/ECS strings each frame — produces the exact bytes,
// including the no-value fallbacks, entirely without heap allocation.
void RunToolbarHudLabelFormatTest() {
    using kb::editor::SceneViewportToolbarLabelFormat;

    std::array<char, 16> fpsBuffer{};
    kb::editor::tests::Require(SceneViewportToolbarLabelFormat::Fps(std::span<char>{ fpsBuffer }, 144, true) == "FPS 144", "FPS label must format a positive frame rate as \"FPS 144\"");
    kb::editor::tests::Require(SceneViewportToolbarLabelFormat::Fps(std::span<char>{ fpsBuffer }, 0, true) == "FPS --", "FPS label must fall back to \"FPS --\" for a non-positive frame rate");
    // A held reading must not be dressed as a live one. The editor draws on demand, so
    // "no frames right now" is the normal state, not a fault - but the counter has to say
    // it instead of leaving the last number standing as if it were current.
    kb::editor::tests::Require(SceneViewportToolbarLabelFormat::Fps(std::span<char>{ fpsBuffer }, 452, false) == "IDLE 452", "A held frame-rate reading must be labelled IDLE, not FPS");
    kb::editor::tests::Require(SceneViewportToolbarLabelFormat::Fps(std::span<char>{ fpsBuffer }, 0, false) == "FPS --", "With no measurement at all the counter must show \"FPS --\" whether idle or not");
}

// The scene-view FPS counter is fed by actual presents, and the editor presents only when
// something asks it to. That is the right design - burning the GPU on an untouched viewport
// would be worse - but it means the meter stops receiving samples the moment the user stops
// interacting. This proves the meter reports that state instead of silently freezing: the
// number is kept (it is still the honest cost of the last frame drawn) and marked not live,
// and the live -> idle crossing is announced exactly once so the toolbar can be repainted
// to show it.
void RunToolbarFpsCounterIdleReportingTest() {
    using kb::editor::SceneViewportToolbarState;

    SceneViewportToolbarState::Reset();
    const auto start = std::chrono::steady_clock::time_point{} + std::chrono::seconds{ 100 };

    kb::editor::tests::Require(!SceneViewportToolbarState::CurrentReading(start).live, "A counter that has never seen a frame must not claim a live reading");
    kb::editor::tests::Require(SceneViewportToolbarState::CurrentReading(start).fps == 0, "A counter that has never seen a frame must report no rate");
    kb::editor::tests::Require(!SceneViewportToolbarState::ConsumeIdleTransition(start), "A counter with no samples at all has no live reading to lose");

    // One 2 ms frame: 500 FPS, live.
    SceneViewportToolbarState::RecordFrameMilliseconds(2.0, start);
    kb::editor::tests::Require(SceneViewportToolbarState::CurrentReading(start).fps == 500, "A 2 ms frame must read as 500 FPS");
    kb::editor::tests::Require(SceneViewportToolbarState::CurrentReading(start).live, "The reading must be live immediately after a frame");
    kb::editor::tests::Require(!SceneViewportToolbarState::ConsumeIdleTransition(start), "A live counter must not announce an idle crossing");

    // Still live just inside the window, so ordinary 60 Hz interaction never blinks.
    const auto justInside = start + SceneViewportToolbarState::kLiveFor - std::chrono::milliseconds{ 1 };
    kb::editor::tests::Require(SceneViewportToolbarState::CurrentReading(justInside).live, "A reading must stay live for the whole live window");

    // Past the window the rate is kept but is no longer a current reading.
    const auto afterIdle = start + SceneViewportToolbarState::kLiveFor + std::chrono::milliseconds{ 1 };
    kb::editor::tests::Require(!SceneViewportToolbarState::CurrentReading(afterIdle).live, "A reading older than the live window must not be reported as live");
    kb::editor::tests::Require(SceneViewportToolbarState::CurrentReading(afterIdle).fps == 500, "Going idle must keep the last measured cost, not zero the counter");
    kb::editor::tests::Require(SceneViewportToolbarState::ConsumeIdleTransition(afterIdle), "The live -> idle crossing must be announced so the counter can be repainted");
    kb::editor::tests::Require(!SceneViewportToolbarState::ConsumeIdleTransition(afterIdle + std::chrono::seconds{ 5 }), "The idle crossing must be announced once, not on every poll, so an idle editor stays idle");

    // Drawing again re-arms both the reading and the crossing.
    const auto resumed = afterIdle + std::chrono::seconds{ 5 };
    SceneViewportToolbarState::RecordFrameMilliseconds(2.0, resumed);
    kb::editor::tests::Require(SceneViewportToolbarState::CurrentReading(resumed).live, "A new frame must make the reading live again");
    kb::editor::tests::Require(SceneViewportToolbarState::ConsumeIdleTransition(resumed + SceneViewportToolbarState::kLiveFor + std::chrono::milliseconds{ 1 }), "Each quiet spell must be announced, not only the first");
    SceneViewportToolbarState::Reset();
}

// The brand marks are vendored artwork whose subpaths lean on parts of the path grammar the
// hand-written glyphs never used: several coordinate pairs behind one command letter, and a
// relative moveto straight after a close, which must start from the point the close returned
// to. A pane silently landing in the wrong place - or not at all - is invisible in a build
// log and easy to miss at row height, so the shape is checked here rather than by eye.
void RunSvgPathMultiSubpathTest() {
    // Four separate 10x10 squares: the second reached by an absolute move, the third and
    // fourth by a relative move straight after a close.
    constexpr std::string_view fourSquares =
        "M0 0h10v10H0zM20 0h10v10H20zm-20 20h10v10H0zm20 0h10v10H20z";
    Gdiplus::GraphicsPath path(Gdiplus::FillModeAlternate);
    kb::editor::SvgGraphicsPathBuilder(fourSquares).Build(path);

    Gdiplus::RectF bounds{};
    kb::editor::tests::Require(path.GetBounds(&bounds) == Gdiplus::Ok,
        "A four-square path must produce measurable bounds");
    kb::editor::tests::Require(path.GetPointCount() > 0,
        "A four-square path must produce points");
    // All four squares span x 0..30 and y 0..30. If a relative moveto after a close starts
    // from the wrong point, or a later subpath is dropped, the box shrinks.
    kb::editor::tests::Require(bounds.X <= 0.5F && bounds.Y <= 0.5F,
        "The path must start at the origin, so no subpath drifted off");
    kb::editor::tests::Require(bounds.Width >= 29.0F && bounds.Height >= 29.0F,
        "Every one of the four squares must be present: a 30x30 box, not one square's worth");

    // The vendored Windows mark, which is four panes on a 128 viewBox reached the same way.
    constexpr std::string_view windowsMark =
        "M126 1.637l-67 9.834v49.831l67-.534zM1.647 66.709l.003 42.404 50.791 6.983-.04-49.057zm56.82.68l.094 49.465 67.376 9.509.016-58.863zM1.61 19.297l.047 42.383 50.791-.289-.023-49.016z";
    Gdiplus::GraphicsPath mark(Gdiplus::FillModeAlternate);
    kb::editor::SvgGraphicsPathBuilder(windowsMark).Build(mark);
    Gdiplus::RectF markBounds{};
    kb::editor::tests::Require(mark.GetBounds(&markBounds) == Gdiplus::Ok,
        "The Windows mark must produce measurable bounds");
    kb::editor::tests::Require(markBounds.Width >= 120.0F && markBounds.Height >= 120.0F,
        "All four panes of the Windows mark must be present, filling its 128 viewBox");

    // Bounds only prove the figures were added. What matters is what fills: a subpath can
    // be in the path and still not be painted, which is how three of these four panes went
    // missing on screen while every measurement above passed.
    kb::editor::tests::Require(mark.IsVisible(90.0F, 30.0F),
        "The top-right pane of the Windows mark must be filled");
    kb::editor::tests::Require(mark.IsVisible(27.0F, 37.0F),
        "The top-left pane of the Windows mark must be filled");
    kb::editor::tests::Require(mark.IsVisible(27.0F, 92.0F),
        "The bottom-left pane of the Windows mark must be filled");
    kb::editor::tests::Require(mark.IsVisible(92.0F, 97.0F),
        "The bottom-right pane of the Windows mark must be filled");

    // And under the non-zero rule, which is what a source declaring no fill-rule asks for
    // and what the editor actually draws this mark with.
    Gdiplus::GraphicsPath winding(Gdiplus::FillModeWinding);
    kb::editor::SvgGraphicsPathBuilder(windowsMark).Build(winding);
    kb::editor::tests::Require(winding.IsVisible(90.0F, 30.0F) && winding.IsVisible(27.0F, 37.0F) &&
            winding.IsVisible(27.0F, 92.0F) && winding.IsVisible(92.0F, 97.0F),
        "All four panes must fill under the non-zero rule too");
}

// Guards the LockBits fast path in EditorTexturePreviewService::DecodeGdiplus, which replaced a
// per-pixel Gdiplus::Bitmap::GetPixel() loop (4.2M COM calls / ~1.3s on a 2048^2 texture, run on the
// GDI paint thread = the measured "wmpaint" stall). LockBits + per-row memcpy must reproduce the exact
// same BGRA output: correct channel order (GDI+ 32bppARGB is B,G,R,A in memory = our 0xAARRGGBB uint32)
// and correct row stride (top-down orientation). A stride/order regression would either scramble
// channels or collapse rows to a constant; the four distinct corner colors below catch both.
void RunTexturePreviewLockBitsDecodeTest() {
    const int width = 2;
    const int height = 2;
    // 24bpp BGR, bottom-up, rows padded to a 4-byte boundary (2px * 3B = 6B -> 8B stride).
    const std::array<std::uint8_t, 16> pixelData{
        // bottom row (y=1): blue (0,1), white (1,1), 2 padding bytes
        255U, 0U, 0U, 255U, 255U, 255U, 0U, 0U,
        // top row (y=0): red (0,0), green (1,0), 2 padding bytes
        0U, 0U, 255U, 0U, 255U, 0U, 0U, 0U,
    };
    const std::uint32_t pixelBytes = static_cast<std::uint32_t>(pixelData.size());
    const std::uint32_t offsetBits = 14U + 40U;
    const std::uint32_t fileSize = offsetBits + pixelBytes;

    std::vector<std::uint8_t> bmp;
    bmp.reserve(fileSize);
    const auto put16 = [&bmp](std::uint16_t v) { bmp.push_back(static_cast<std::uint8_t>(v & 0xFFU)); bmp.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU)); };
    const auto put32 = [&bmp](std::uint32_t v) { for (int i = 0; i < 4; ++i) bmp.push_back(static_cast<std::uint8_t>((v >> (8U * static_cast<std::uint32_t>(i))) & 0xFFU)); };
    // BITMAPFILEHEADER
    bmp.push_back('B'); bmp.push_back('M');
    put32(fileSize); put16(0U); put16(0U); put32(offsetBits);
    // BITMAPINFOHEADER
    put32(40U); put32(static_cast<std::uint32_t>(width)); put32(static_cast<std::uint32_t>(height));
    put16(1U); put16(24U); put32(0U); put32(pixelBytes); put32(0U); put32(0U); put32(0U); put32(0U);
    bmp.insert(bmp.end(), pixelData.begin(), pixelData.end());

    // PreviewFor reads the engine's imported-asset container ("21KBAST\0" + 24-byte header + two
    // length-prefixed strings + raw payload), not a bare image file, so wrap the BMP accordingly.
    std::vector<std::uint8_t> container{ '2', '1', 'K', 'B', 'A', 'S', 'T', '\0' };
    container.resize(32U, 0U);           // 24-byte header region (skipped to offset 32)
    for (int i = 0; i < 8; ++i) container.push_back(0U);  // two 4-byte LE string lengths, both 0
    container.insert(container.end(), bmp.begin(), bmp.end());

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "kb_texture_preview_lockbits_test.21kbast";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(container.data()), static_cast<std::streamsize>(container.size()));
    }

    kb::assets::AssetMetadata metadata;
    metadata.type = "Texture";
    metadata.physicalPath = path;
    metadata.id.value = 0x1234ABCDU;
    metadata.contentHash = 0xFEEDFACEU;

    const kb::editor::EditorTexturePreviewImage* image = kb::editor::EditorTexturePreviewService::PreviewFor(metadata);
    kb::editor::tests::Require(image != nullptr, "LockBits decode must return a preview image for a valid BMP");
    kb::editor::tests::Require(image->width == width && image->height == height, "Decoded preview must preserve source dimensions");
    kb::editor::tests::Require(image->bgra.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height), "Decoded preview must have width*height pixels");

    // Exact channel-order + row-stride verification (top-down): index = y*width + x.
    kb::editor::tests::Require(image->bgra[0] == 0xFFFF0000U, "Top-left must decode to opaque red (channel order/stride)");
    kb::editor::tests::Require(image->bgra[1] == 0xFF00FF00U, "Top-right must decode to opaque green");
    kb::editor::tests::Require(image->bgra[2] == 0xFF0000FFU, "Bottom-left must decode to opaque blue (row stride)");
    kb::editor::tests::Require(image->bgra[3] == 0xFFFFFFFFU, "Bottom-right must decode to opaque white");

    // Negative control: a stride/constant-fill regression would collapse distinct corners to one value.
    kb::editor::tests::Require(image->bgra[0] != image->bgra[1], "Distinct source colors must stay distinct (no constant fill)");
    kb::editor::tests::Require(image->bgra[0] != image->bgra[2], "Top and bottom rows must differ (no row-stride collapse)");

    std::error_code removeError;
    std::filesystem::remove(path, removeError);
}

} // namespace

namespace kb::editor::tests {

void RunEditorViewportPreviewTests() {
    RunSceneViewportPresentationPolicyTest();
    RunSceneViewportSceneSyncPolicyTest();
    RunParticleThumbnailTimelineTest();
    RunPlayCameraHierarchySelectionTest();
    RunViewportCameraNavigationBindingPolicyTest();
    RunSkeletalMeshSceneLabelBuilderTest();
    RunAnimationPreviewContextTracksSharedBindingTest();
    RunAnimationPreviewTransportTest();
    RunAnimationPreviewOverlayStateTest();
    RunAnimationPreviewScenePresentationTest();
    RunAnimationPreviewLodPolicyTest();
    RunAnimationPreviewLegacyFbxOrientationTest();
    RunSkeletalMeshEditorBonePickerTest();
    RunAnimationPreviewExactScrubTest();
    RunHostSurfaceLifecycleStateTest();
    RunTerrainToolbarAndStrokeTickPolicyTest();
    RunTexturePreviewLockBitsDecodeTest();
    RunToolbarHudLabelFormatTest();
    RunToolbarFpsCounterIdleReportingTest();
    RunProfileCycleAndResolutionTest();
    RunFitCameraAndCustomTest();
    RunRenderProfileCycleTest();
    RunGridAndSnapStateTest();
    RunViewportCameraAxesTest();
    RunViewportCameraNavigationTest();
    RunViewportCameraFlightSmoothingTest();
    RunViewportCameraOrbitTest();
    RunViewportCameraTrackDirectionTest();
    RunViewportMeshPickerNearestMeshRendererTest();
    RunViewportMeshPickerUsesSynchronizedWorldTransformsTest();
    RunViewportMeshPickerSkipsHiddenMeshesTest();
    RunViewportMeshPickerUsesPublishedWorldBoundsTest();
    RunViewportLightWireframePickerChoosesNestedInnerWireframeTest();
    RunViewportLightIconPickerSelectsLightIconsTest();
    RunViewportParticleIconPickerSelectsParticleEffectTest();
    RunViewportPickerSelectsEntityWithoutRenderableComponentTest();
    RunViewportMeshWinsOverEntityOriginPickTest();
    RunViewportBoxPickerIncludesVisibleComponentOverlaysTest();
    RunViewportMeshPickerWinsInsideLightWireframeVolumeTest();
    RunRenderBackendSettingsTest();
    RunEditorBackendSelectionTest();
    RunPlayModeStateTest();
}

} // namespace kb::editor::tests
