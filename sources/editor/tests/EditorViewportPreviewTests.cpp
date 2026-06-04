#include "EditorTestSupport.hpp"
#include "EditorTestSuites.hpp"

#include "app/EditorPlayModeState.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "scene/EditorViewportPreviewState.hpp"

#include <string_view>

namespace {

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
    RunRenderBackendSettingsTest();
    RunPlayModeStateTest();
}

} // namespace kb::editor::tests
