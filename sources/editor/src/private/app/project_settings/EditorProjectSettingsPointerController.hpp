#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorSceneContext;

#if defined(_WIN32)
// Routes clicks inside the Project Settings panel content to the project-wide
// input settings (mapping context selector + enabled toggle).
class EditorProjectSettingsPointerController {
public:
    explicit EditorProjectSettingsPointerController(EditorSceneContext& sceneContext) noexcept;

    // Returns true when the click mutated a setting and the panel should repaint.
    [[nodiscard]] bool HandlePointerDown(const RECT& content, int x, int y);

private:
    EditorSceneContext& sceneContext_;
};
#endif

} // namespace kb::editor
