#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <optional>

namespace kb::editor {

class EditorSceneContext;
class EditorRenderBackendSettings;

#if defined(_WIN32)
// Routes clicks and hover inside the Project Settings panel content to the
// project-wide input settings (mapping context selector + enabled toggle).
class EditorProjectSettingsPointerController {
public:
    explicit EditorProjectSettingsPointerController(EditorSceneContext& sceneContext) noexcept;

    // Returns true when the click mutated a setting and the panel should repaint.
    [[nodiscard]] bool HandlePointerDown(const RECT& content, int x, int y);
    // `owner` hosts the menu the Add scene button opens; without one that button does nothing.
    [[nodiscard]] bool HandlePointerDown(const RECT& content, int x, int y, EditorRenderBackendSettings& renderBackendSettings, HWND owner = nullptr);

    // Ends the typing of a number into a setting: the typed value is applied (clamped to the setting's bounds,
    // and dropped when it is not a number) and the cell goes back to showing the setting. False when nothing was
    // being typed.
    static bool CommitNumberEdit(EditorSceneContext& sceneContext);

    // Hover tracking for the open dropdown list. Returns true when the hovered
    // option changed (so the caller can repaint).
    [[nodiscard]] bool UpdateHover(const RECT& content, int x, int y);
    [[nodiscard]] bool UpdateHoverOrClear(const std::optional<RECT>& content, int x, int y);
    [[nodiscard]] bool Contains(const std::optional<RECT>& content, int x, int y) const noexcept;

private:
    EditorSceneContext& sceneContext_;
};
#endif

} // namespace kb::editor
