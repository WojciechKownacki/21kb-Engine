#include "rendering/script_editor/ScriptEditorWindow.hpp"

#if defined(_WIN32)
#include "rendering/script_editor/ScriptEditorDocument.hpp"
#include "rendering/script_editor/ScriptEditorInput.hpp"
#include "rendering/script_editor/ScriptEditorLayout.hpp"
#include "rendering/script_editor/ScriptEditorRenderer.hpp"
#include "rendering/script_editor/ScriptEditorTheme.hpp"
#include "rendering/script_editor/ScriptSourceFile.hpp"

#include <algorithm>
#include <string>

#include <windowsx.h>

namespace kb::editor {
namespace {

constexpr wchar_t kClassName[] = L"KbScriptCodeEditor";

struct ScriptEditorInstance {
    ScriptEditorDocument document;
    ScriptEditorViewport viewport;
    ScriptEditorMetrics metrics{ .charWidth = 8, .lineHeight = script_editor_theme::kLineHeight, .fontHeight = script_editor_theme::kFontHeight };
    std::string savedText;
    std::filesystem::path filePath;
    std::uint64_t generation = 0;
    bool caretVisible = true;
    bool scrollbarDragging = false;
    int scrollbarGrabOffset = 0;
    RECT bounds{};
};

[[nodiscard]] ScriptEditorInstance* InstanceOf(HWND window) noexcept {
    return window != nullptr ? reinterpret_cast<ScriptEditorInstance*>(GetWindowLongPtrW(window, GWLP_USERDATA)) : nullptr;
}

[[nodiscard]] RECT ClientRect(HWND window) noexcept {
    RECT client{};
    GetClientRect(window, &client);
    return client;
}

void SaveInstance(ScriptEditorInstance& instance) {
    const std::string text = instance.document.ToText();
    if (ScriptSourceFile::Write(instance.filePath, text)) {
        instance.savedText = text;
    }
}

[[nodiscard]] bool OverScrollbar(HWND window, ScriptEditorInstance& instance, int x, int y) {
    const RECT thumb = ScriptEditorLayout::ScrollbarThumb(instance.document, instance.viewport, ClientRect(window), instance.metrics);
    return thumb.bottom > thumb.top && x >= thumb.left && y >= thumb.top && y < thumb.bottom;
}

void DragScrollbar(HWND window, ScriptEditorInstance& instance, int y) {
    const RECT client = ClientRect(window);
    const RECT thumb = ScriptEditorLayout::ScrollbarThumb(instance.document, instance.viewport, client, instance.metrics);
    const int trackHeight = static_cast<int>(client.bottom - client.top);
    const int travel = std::max(1, trackHeight - static_cast<int>(thumb.bottom - thumb.top));
    const int maxScroll = ScriptEditorLayout::MaxScrollLine(instance.document, client, instance.metrics);
    instance.viewport.scrollLine = std::clamp((y - instance.scrollbarGrabOffset) * maxScroll / travel, 0, maxScroll);
}

LRESULT CALLBACK EditorProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    ScriptEditorInstance* instance = InstanceOf(window);
    switch (message) {
    case WM_NCCREATE:
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new ScriptEditorInstance{}));
        return DefWindowProcW(window, message, wparam, lparam);
    case WM_NCDESTROY:
        delete instance;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wparam, lparam);
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (instance != nullptr) {
            ScriptEditorRenderer::Paint(window, instance->document, instance->viewport, instance->metrics, instance->caretVisible, GetFocus() == window);
        }
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        if (instance != nullptr) {
            instance->caretVisible = true;
        }
        SetTimer(window, 1, GetCaretBlinkTime(), nullptr);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_TIMER:
        if (instance != nullptr) {
            instance->caretVisible = !instance->caretVisible;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT && instance != nullptr) {
            POINT cursor{};
            GetCursorPos(&cursor);
            ScreenToClient(window, &cursor);
            const bool overThumb = OverScrollbar(window, *instance, cursor.x, cursor.y);
            SetCursor(LoadCursorW(nullptr, overThumb ? MAKEINTRESOURCEW(32512) : MAKEINTRESOURCEW(32513)));
            return TRUE;
        }
        break;
    case WM_MOUSEWHEEL:
        if (instance != nullptr) {
            const int delta = GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
            const int maxScroll = ScriptEditorLayout::MaxScrollLine(instance->document, ClientRect(window), instance->metrics);
            instance->viewport.scrollLine = std::clamp(instance->viewport.scrollLine - delta * 3, 0, maxScroll);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (instance != nullptr) {
            SetFocus(window);
            const int x = GET_X_LPARAM(lparam);
            const int y = GET_Y_LPARAM(lparam);
            if (OverScrollbar(window, *instance, x, y)) {
                const RECT thumb = ScriptEditorLayout::ScrollbarThumb(instance->document, instance->viewport, ClientRect(window), instance->metrics);
                instance->scrollbarDragging = true;
                instance->scrollbarGrabOffset = y - thumb.top;
                SetCapture(window);
                return 0;
            }
            instance->document.SetCaret(ScriptEditorLayout::CaretFromPoint(instance->document, instance->viewport, instance->metrics, x, y), (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            SetCapture(window);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (instance != nullptr && (wparam & MK_LBUTTON) != 0) {
            if (instance->scrollbarDragging) {
                DragScrollbar(window, *instance, GET_Y_LPARAM(lparam));
            } else {
                instance->document.SetCaret(ScriptEditorLayout::CaretFromPoint(instance->document, instance->viewport, instance->metrics, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)), true);
            }
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (instance != nullptr) {
            instance->scrollbarDragging = false;
        }
        ReleaseCapture();
        return 0;
    case WM_CHAR:
        if (instance != nullptr && ScriptEditorInput::HandleChar(window, instance->document, instance->viewport, instance->metrics, static_cast<wchar_t>(wparam))) {
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if (instance != nullptr) {
            instance->caretVisible = true;
            const ScriptEditorInputResult result = ScriptEditorInput::HandleKeyDown(window, instance->document, instance->viewport, instance->metrics, wparam);
            if (result.saveRequested) {
                SaveInstance(*instance);
            }
            if (result.changed || result.saveRequested) {
                InvalidateRect(window, nullptr, FALSE);
            }
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void EnsureClass() {
    static bool registered = false;
    if (registered) {
        return;
    }
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    windowClass.lpfnWndProc = &EditorProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32513));
    windowClass.lpszClassName = kClassName;
    if (RegisterClassExW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
        registered = true;
    }
}

} // namespace

HWND ScriptEditorWindow::Ensure(HWND parent) {
    if (parent == nullptr) {
        return nullptr;
    }
    EnsureClass();
    return CreateWindowExW(
        0, kClassName, L"",
        WS_CHILD | WS_CLIPSIBLINGS,
        0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

void ScriptEditorWindow::Sync(HWND window, const RECT& bounds, const std::filesystem::path& filePath, std::uint64_t generation) {
    ScriptEditorInstance* instance = InstanceOf(window);
    if (instance == nullptr) {
        return;
    }

    if (bounds.left != instance->bounds.left || bounds.top != instance->bounds.top || bounds.right != instance->bounds.right || bounds.bottom != instance->bounds.bottom) {
        // SWP_NOCOPYBITS + a synchronous redraw avoid the smearing you get when a
        // child window over GDI is resized (Windows blits stale bits otherwise).
        SetWindowPos(window, nullptr, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
        instance->bounds = bounds;
    }

    if (instance->generation != generation) {
        instance->filePath = filePath;
        instance->savedText = ScriptSourceFile::Read(filePath);
        instance->document.Load(instance->savedText);
        instance->viewport = ScriptEditorViewport{};
        instance->generation = generation;
        InvalidateRect(window, nullptr, FALSE);
        SetFocus(window);
    }

    ShowWindow(window, SW_SHOW);
}

void ScriptEditorWindow::Hide(HWND window) noexcept {
    if (window != nullptr && IsWindow(window) != 0) {
        ShowWindow(window, SW_HIDE);
    }
}

bool ScriptEditorWindow::IsModified(HWND window) {
    const ScriptEditorInstance* instance = InstanceOf(window);
    return instance != nullptr && instance->document.IsModified(instance->savedText);
}

} // namespace kb::editor

#endif
