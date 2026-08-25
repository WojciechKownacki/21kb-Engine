#include "app/editor_settings/EditorSettingsPointerController.hpp"

#if defined(_WIN32)
#include "rendering/EditorRenderBackendSettings.hpp"
#include "scene/EditorSceneContext.hpp"

#include <array>

namespace kb::editor {
namespace {

void ApplyRendererChoice(EditorRenderBackendSettings& value, int row, int option) noexcept {
    constexpr std::array<std::uint8_t, 5> samples{{0U, 2U, 4U, 8U, 16U}};
    if (row == 0) value.SetBackend(static_cast<EditorRenderBackend>(option));
    else if (row == 2) value.SetAntiAliasingMode(static_cast<EditorAntiAliasingMode>(option));
    else if (row == 3) value.SetMsaaSamples(samples[static_cast<std::size_t>(option)]);
}

void ApplyWorkspaceChoice(EditorWorkspacePreferences& value, int category, int row, int option) noexcept {
    constexpr std::array<float, 5> steps{{0.1F, 0.5F, 1.0F, 5.0F, 10.0F}};
    constexpr std::array<float, 5> rotations{{0.0F, 5.0F, 15.0F, 30.0F, 45.0F}};
    constexpr std::array<std::uint32_t, 5> intervals{{5U, 10U, 15U, 30U, 60U}};
    constexpr std::array<float, 4> scales{{0.65F, 1.0F, 1.35F, 1.75F}};
    if (category == 1) {
        if (row == 1) value.gridSpacing = steps[static_cast<std::size_t>(option)];
        else if (row == 3) value.snapStep = steps[static_cast<std::size_t>(option)];
        else if (row == 4) value.rotationSnapDegrees = rotations[static_cast<std::size_t>(option)];
    } else if (category == 2 && row == 1) {
        value.autosaveIntervalMinutes = intervals[static_cast<std::size_t>(option)];
    } else if (category == 3) {
        if (row == 1) value.assetViewMode = static_cast<EditorAssetViewMode>(option);
        else if (row == 2) value.assetSortMode = static_cast<EditorAssetSortMode>(option);
        else if (row == 5) value.assetThumbnailScale = scales[static_cast<std::size_t>(option)];
    }
}

void ApplyRendererToggle(EditorRenderBackendSettings& value, int row) noexcept {
    if (row == 1) value.TogglePostProcessEnabled();
    else if (row == 4) value.ToggleBloomEnabled();
    else if (row == 5) value.ToggleShadowsEnabled();
    else if (row == 6) value.ToggleSelectionOutlineEnabled();
    else if (row == 7) value.ToggleGpuDrivenEnabled();
}

void ApplyWorkspaceToggle(EditorWorkspacePreferences& value, int category, int row) noexcept {
    if (category == 1) {
        if (row == 0) value.gridVisible = !value.gridVisible;
        else if (row == 2) value.snapEnabled = !value.snapEnabled;
    } else if (category == 2 && row == 0) {
        value.autosaveEnabled = !value.autosaveEnabled;
    } else if (category == 3) {
        if (row == 0) value.assetBrowserRecursive = !value.assetBrowserRecursive;
        else if (row == 3) value.assetShowFolders = !value.assetShowFolders;
        else if (row == 4) value.assetShowTemplates = !value.assetShowTemplates;
    }
}

} // namespace

bool EditorSettingsPointerController::HandlePointerDown(
    const RECT& content,
    int x,
    int y,
    EditorRenderBackendSettings& renderSettings) {
    const EditorSettingsHit hit = EditorSettingsPanelRenderer::HitTest(content, sceneContext_, x, y);
    if (hit.kind == EditorSettingsHitKind::Category) {
        return sceneContext_.EditorSettings().SelectCategory(hit.row);
    }
    if (hit.kind != EditorSettingsHitKind::Toggle && hit.kind != EditorSettingsHitKind::Choice) return false;
    const int category = sceneContext_.EditorSettings().SelectedCategory();
    const EditorWorkspacePreferences currentPreferences = sceneContext_.CaptureEditorWorkspacePreferences();
    EditorWorkspacePreferences candidatePreferences = currentPreferences;
    EditorRenderBackendSettings candidateRenderer = renderSettings;
    if (category == 0) {
        if (hit.kind == EditorSettingsHitKind::Toggle) ApplyRendererToggle(candidateRenderer, hit.row);
        else ApplyRendererChoice(candidateRenderer, hit.row, hit.option);
    } else {
        if (hit.kind == EditorSettingsHitKind::Toggle) ApplyWorkspaceToggle(candidatePreferences, category, hit.row);
        else ApplyWorkspaceChoice(candidatePreferences, category, hit.row, hit.option);
    }
    if (candidatePreferences == currentPreferences &&
        candidateRenderer.Generation() == renderSettings.Generation()) {
        return false;
    }
    if (!sceneContext_.CommitEditorSettings(candidatePreferences, candidateRenderer)) return false;
    renderSettings = candidateRenderer;
    sceneContext_.MarkSceneRenderDirty();
    return true;
}

} // namespace kb::editor
#endif
