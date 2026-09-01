#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorSceneContext;

class EditorBuildGameInputHandler final {
public:
    explicit EditorBuildGameInputHandler(EditorSceneContext& sceneContext) noexcept;
#if defined(_WIN32)
    [[nodiscard]] bool HandleCharacter(wchar_t character) const;
    [[nodiscard]] bool HandleKeyDown(HWND owner, WPARAM key) const;
#endif

private:
    EditorSceneContext& sceneContext_;
};

} // namespace kb::editor
