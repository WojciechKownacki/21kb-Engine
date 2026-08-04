#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "app/EditorPlayModeState.hpp"
#include "app/scene_viewport/EditorSceneViewportMeshPicker.hpp"
#include "app/scene_viewport/EditorTerrainStrokeTickPolicy.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "rendering/EditorTexturePreviewService.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarLayout.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarLabelFormat.hpp"
#include "scene/EditorViewportCameraState.hpp"
#include "scene/EditorViewportPreviewState.hpp"
#include "scene/AnimationPreviewContext.hpp"
#include "scene/EditorAnimationPreviewScene.hpp"

#include "engine/assets/AssetMetadata.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <vector>

namespace {

void RequireNear(float actual, float expected, float tolerance, const char* message) {
    kb::editor::tests::Require(std::fabs(actual - expected) <= tolerance, message);
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
    kb::editor::tests::Require(SceneViewportToolbarLabelFormat::Fps(std::span<char>{ fpsBuffer }, 144) == "FPS 144", "FPS label must format a positive frame rate as \"FPS 144\"");
    kb::editor::tests::Require(SceneViewportToolbarLabelFormat::Fps(std::span<char>{ fpsBuffer }, 0) == "FPS --", "FPS label must fall back to \"FPS --\" for a non-positive frame rate");

    std::array<char, 16> dcBuffer{};
    kb::editor::tests::Require(SceneViewportToolbarLabelFormat::DrawCalls(std::span<char>{ dcBuffer }, 1234U) == "DC 1234", "Draw-call label must format as \"DC 1234\"");
    kb::editor::tests::Require(SceneViewportToolbarLabelFormat::DrawCalls(std::span<char>{ dcBuffer }, 0U) == "DC 0", "Draw-call label must format zero as \"DC 0\"");

    std::array<char, 16> meshBuffer{};
    kb::editor::tests::Require(SceneViewportToolbarLabelFormat::Meshes(std::span<char>{ meshBuffer }, 57U) == "M 57", "Mesh label must format as \"M 57\"");

    std::array<char, 32> ecsBuffer{};
    kb::editor::tests::Require(SceneViewportToolbarLabelFormat::EcsMilliseconds(std::span<char>{ ecsBuffer }, true, 3.14159) == "ECS 3.14 ms", "ECS label must format a valid frame time to two decimals as \"ECS 3.14 ms\"");
    kb::editor::tests::Require(SceneViewportToolbarLabelFormat::EcsMilliseconds(std::span<char>{ ecsBuffer }, false, 0.0) == "ECS --", "ECS label must fall back to \"ECS --\" when no ECS frame is present");
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
    RunAnimationPreviewContextTracksSharedBindingTest();
    RunAnimationPreviewTransportTest();
    RunAnimationPreviewScenePresentationTest();
    RunTerrainToolbarAndStrokeTickPolicyTest();
    RunTexturePreviewLockBitsDecodeTest();
    RunToolbarHudLabelFormatTest();
    RunProfileCycleAndResolutionTest();
    RunFitCameraAndCustomTest();
    RunRenderProfileCycleTest();
    RunGridAndSnapStateTest();
    RunViewportCameraAxesTest();
    RunViewportCameraNavigationTest();
    RunViewportCameraOrbitTest();
    RunViewportCameraTrackDirectionTest();
    RunViewportMeshPickerNearestMeshRendererTest();
    RunViewportLightWireframePickerChoosesNestedInnerWireframeTest();
    RunViewportLightIconPickerSelectsLightIconsTest();
    RunViewportMeshPickerWinsInsideLightWireframeVolumeTest();
    RunRenderBackendSettingsTest();
    RunPlayModeStateTest();
}

} // namespace kb::editor::tests
