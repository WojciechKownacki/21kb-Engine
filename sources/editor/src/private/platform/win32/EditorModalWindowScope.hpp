#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <vector>

namespace kb::editor {

// Makes a dialog modal against the whole editor instead of a single owner window.
//
// Disabling only the owner leaves every other top-level window live: a dialog opened from a panel hosted
// in a floating window keeps the main window clickable (and the other way round), so the user can delete
// the node the dialog is editing, switch materials, or stack a second modal loop on top of the first.
// Nested scopes behave correctly because each one restores exactly the windows it disabled.
class EditorModalWindowScope {
public:
    explicit EditorModalWindowScope(HWND modalWindow)
        : modalWindow_(modalWindow) {
        EnumThreadWindows(GetCurrentThreadId(), &EditorModalWindowScope::Collect, reinterpret_cast<LPARAM>(this));
        for (const HWND window : disabled_) {
            EnableWindow(window, FALSE);
        }
    }

    ~EditorModalWindowScope() {
        for (auto window = disabled_.rbegin(); window != disabled_.rend(); ++window) {
            if (IsWindow(*window) != 0) {
                EnableWindow(*window, TRUE);
            }
        }
    }

    EditorModalWindowScope(const EditorModalWindowScope&) = delete;
    EditorModalWindowScope& operator=(const EditorModalWindowScope&) = delete;
    EditorModalWindowScope(EditorModalWindowScope&&) = delete;
    EditorModalWindowScope& operator=(EditorModalWindowScope&&) = delete;

private:
    static BOOL CALLBACK Collect(HWND window, LPARAM parameter) {
        EditorModalWindowScope& scope = *reinterpret_cast<EditorModalWindowScope*>(parameter);
        if (window != scope.modalWindow_ && GetWindow(window, GW_OWNER) != scope.modalWindow_ &&
            IsWindowVisible(window) != 0 && IsWindowEnabled(window) != 0) {
            scope.disabled_.push_back(window);
        }
        return TRUE;
    }

    HWND modalWindow_ = nullptr;
    std::vector<HWND> disabled_;
};

} // namespace kb::editor

#endif
