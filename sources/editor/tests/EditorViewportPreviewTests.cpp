#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "app/EditorPlayModeState.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "scene/EditorViewportCameraState.hpp"
#include "scene/EditorViewportPreviewState.hpp"

#include <cmath>
#include <string_view>

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
    kb::editor::tests::Require(phone.kind == kb::editor::EditorViewportProfileKind::PhonePortrait, "Viewport profile cycle did not select phone portrait");
    kb::editor::tests::Require(phone.devicePreview, "Phone portrait should be a device preview profile");
    kb::editor::tests::Require(phone.safeArea.top > 0U && phone.safeArea.bottom > 0U, "Phone portrait should expose safe area insets");
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

void RunViewportCameraAxesTest() {
    kb::editor::EditorViewportCameraState camera;
    const kb::editor::EditorViewportCameraAxes axes = camera.Axes();

    RequireNear(axes.position.x, 8.0F, 0.001F, "Viewport camera default x position is wrong");
    RequireNear(axes.position.y, 6.0F, 0.001F, "Viewport camera default y position is wrong");
    RequireNear(axes.position.z, -8.0F, 0.001F, "Viewport camera default z position is wrong");
    RequireNear(axes.forward.x, -0.612F, 0.001F, "Viewport camera default forward x is wrong");
    RequireNear(axes.forward.y, 0.5F, 0.001F, "Viewport camera default forward y is wrong");
    RequireNear(axes.forward.z, 0.612F, 0.001F, "Viewport camera default forward z is wrong");
    RequireNear(axes.up.y, 0.866F, 0.001F, "Viewport camera default up vector is wrong");
}

void RunViewportCameraNavigationTest() {
    kb::editor::EditorViewportCameraState camera;
    camera.BeginNavigation(kb::editor::EditorViewportCameraNavigationMode::Look, 100, 100);
    kb::editor::tests::Require(camera.IsNavigating(), "Viewport camera should enter navigation mode");
    kb::editor::tests::Require(camera.AllowsKeyboardFlight(), "RMB look mode should allow keyboard flight");

    static_cast<void>(camera.UpdatePointer(200, 50));
    kb::editor::tests::Require(camera.YawDegrees() < -45.0F, "Dragging look mode right should decrease yaw");
    kb::editor::tests::Require(camera.PitchDegrees() < 30.0F, "Dragging look mode up should decrease pitch");

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

    kb::editor::tests::Require(camera.YawDegrees() < -45.0F, "Alt+LMB orbit should rotate camera yaw");
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

} // namespace

namespace kb::editor::tests {

void RunEditorViewportPreviewTests() {
    RunProfileCycleAndResolutionTest();
    RunFitCameraAndCustomTest();
    RunViewportCameraAxesTest();
    RunViewportCameraNavigationTest();
    RunViewportCameraOrbitTest();
    RunViewportCameraTrackDirectionTest();
    RunRenderBackendSettingsTest();
    RunPlayModeStateTest();
}

} // namespace kb::editor::tests
