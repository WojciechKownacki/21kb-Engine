#include "app/editor_settings/EditorSettingsPointerController.hpp"

#if defined(_WIN32)
#include "scene/EditorSceneContext.hpp"

#include <array>

namespace kb::editor {

bool EditorSettingsPointerController::HandlePointerDown(
    const RECT& content,
    int x,
    int y) {
    const EditorSettingsHit hit = EditorSettingsPanelRenderer::HitTest(content, x, y);
    if (hit.kind != EditorSettingsHitKind::Toggle &&
        hit.kind != EditorSettingsHitKind::Choice) {
        return false;
    }

    const EditorSavingPreferences current = sceneContext_.CaptureEditorSavingPreferences();
    EditorSavingPreferences candidate = current;
    if (hit.kind == EditorSettingsHitKind::Toggle && hit.row == 0) {
        candidate.autosaveEnabled = !candidate.autosaveEnabled;
    } else if (hit.kind == EditorSettingsHitKind::Choice && hit.row == 1) {
        constexpr std::array<std::uint32_t, 5> intervals{{5U, 10U, 15U, 30U, 60U}};
        if (!candidate.autosaveEnabled ||
            hit.option < 0 ||
            hit.option >= static_cast<int>(intervals.size())) {
            return false;
        }
        candidate.autosaveIntervalMinutes =
            intervals[static_cast<std::size_t>(hit.option)];
    }

    if (candidate == current) return false;
    return sceneContext_.CommitEditorSettings(candidate);
}

} // namespace kb::editor
#endif
