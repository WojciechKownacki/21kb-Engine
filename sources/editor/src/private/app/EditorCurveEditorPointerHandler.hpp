#pragma once

#include "app/EditorWindowMessageContext.hpp"
#include "curves/CurveEditorInteraction.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <optional>

namespace kb::editor {

// Routes pointer and key messages to the shared curve view of the animation clip
// and particle editors. Returns true when the message was consumed.
class EditorCurveEditorPointerHandler {
public:
#if defined(_WIN32)
    explicit EditorCurveEditorPointerHandler(EditorWindowMessageContext context) noexcept : context_(context) {}

    [[nodiscard]] bool HandlePointer(HWND messageWindow, UINT message, WPARAM wparam, LPARAM lparam);
    [[nodiscard]] bool HandleKey(HWND messageWindow, WPARAM key);

private:
    // Binds the view under the point and returns its rectangle.
    [[nodiscard]] std::optional<RECT> BindViewAt(HWND messageWindow, int x, int y);
    void Apply(HWND messageWindow, const CurveEditorResult& result);
    void ShowTangentMenu(HWND messageWindow, int x, int y);

    EditorWindowMessageContext context_;
#endif
};

} // namespace kb::editor
