#include "rendering/script_editor/ScriptEditorWindow.hpp"
#include "app/EditorKeyState.hpp"

#if defined(_WIN32)
#include "rendering/script_editor/ScriptEditorDocument.hpp"
#include "rendering/script_editor/ScriptEditorInput.hpp"
#include "rendering/script_editor/ScriptEditorLayout.hpp"
#include "rendering/script_editor/ScriptEditorRenderer.hpp"
#include "rendering/script_editor/ScriptEditorTheme.hpp"
#include "rendering/script_editor/ScriptProblemService.hpp"
#include "rendering/script_editor/ScriptSourceFile.hpp"

#include <algorithm>
#include <string>

#include <windowsx.h>

namespace kb::editor {
namespace {

constexpr wchar_t kClassName[] = L"KbScriptCodeEditor";
constexpr UINT_PTR kAnalysisTimer = 2; // polls the background analysis while one is pending
constexpr UINT kAnalysisPollMs = 50;

struct ScriptEditorInstance {
    ScriptEditorDocument document;
    ScriptEditorViewport viewport;
    ScriptEditorMetrics metrics{ .charWidth = 8, .lineHeight = script_editor_theme::kLineHeight, .fontHeight = script_editor_theme::kFontHeight };
    std::string savedText;
    std::filesystem::path filePath;
    std::uint64_t generation = 0;
    ScriptProblemService problems;
    std::uint64_t seenRevision = 0;
    int hovered = -1; // the warning under the mouse
    POINT hoverPoint{};
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

// Monotonic across all editor windows: the host compares it frame-to-frame to
// detect a save and reload the affected script asset.
std::uint64_t& SaveSerialRef() noexcept {
    static std::uint64_t serial = 0;
    return serial;
}

void SaveInstance(ScriptEditorInstance& instance) {
    const std::string text = instance.document.ToText();
    if (ScriptSourceFile::Write(instance.filePath, text)) {
        instance.savedText = text;
        ++SaveSerialRef();
    }
}

[[nodiscard]] ScriptProblemOverlay OverlayOf(const ScriptEditorInstance& instance) {
    return ScriptProblemOverlay{ instance.problems.Problems(), instance.hovered, instance.hoverPoint };
}

void StartAnalysisPolling(HWND window) {
    SetTimer(window, kAnalysisTimer, kAnalysisPollMs, nullptr);
}

// Tells the analysis that the text was edited, if it was.
void NoteEdits(HWND window, ScriptEditorInstance& instance) {
    if (instance.document.Revision() != instance.seenRevision) {
        instance.seenRevision = instance.document.Revision();
        instance.problems.TextChanged(ScriptProblemService::Clock::now());
        StartAnalysisPolling(window);
    }
}

// New warnings: resize the text area for the list and keep the caret line out from under it.
void ShowProblems(HWND window, ScriptEditorInstance& instance) {
    instance.hovered = -1;
    instance.metrics.reservedBottom = ScriptEditorLayout::ProblemPanelHeight(instance.problems.Problems().size(), instance.metrics);
    const RECT client = ClientRect(window);
    const int visible = ScriptEditorLayout::VisibleLineCount(client, instance.metrics);
    instance.viewport.scrollLine = std::min(instance.viewport.scrollLine, ScriptEditorLayout::MaxScrollLine(instance.document, client, instance.metrics));
    if (instance.document.Caret().line >= instance.viewport.scrollLine + visible) {
        instance.viewport.scrollLine = instance.document.Caret().line - visible + 1;
    }
    InvalidateRect(window, nullptr, FALSE);
}

void PollAnalysis(HWND window, ScriptEditorInstance& instance) {
    if (instance.problems.Update(ScriptProblemService::Clock::now(), [&instance] { return instance.document.ToText(); })) {
        ShowProblems(window, instance);
    }
    if (instance.problems.Idle()) {
        KillTimer(window, kAnalysisTimer);
    }
}

[[nodiscard]] bool OverScrollbar(HWND window, ScriptEditorInstance& instance, int x, int y) {
    const RECT thumb = ScriptEditorLayout::ScrollbarThumb(instance.document, instance.viewport, ClientRect(window), instance.metrics);
    return thumb.bottom > thumb.top && x >= thumb.left && y >= thumb.top && y < thumb.bottom;
}

void DragScrollbar(HWND window, ScriptEditorInstance& instance, int y) {
    const RECT client = ClientRect(window);
    const RECT thumb = ScriptEditorLayout::ScrollbarThumb(instance.document, instance.viewport, client, instance.metrics);
    const int trackHeight = static_cast<int>(client.bottom - client.top) - instance.metrics.reservedBottom;
    const int travel = std::max(1, trackHeight - static_cast<int>(thumb.bottom - thumb.top));
    const int maxScroll = ScriptEditorLayout::MaxScrollLine(instance.document, client, instance.metrics);
    instance.viewport.scrollLine = std::clamp((y - instance.scrollbarGrabOffset) * maxScroll / travel, 0, maxScroll);
}

LRESULT CALLBACK EditorProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    ScriptEditorInstance* instance = InstanceOf(window);
    switch (message) {
    case WM_NCCREATE:
        // The window owns the instance until WM_NCDESTROY deletes it.
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new ScriptEditorInstance{}));
        return DefWindowProcW(window, message, wparam, lparam); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
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
            ScriptEditorRenderer::Paint(window, instance->document, instance->viewport, instance->metrics, OverlayOf(*instance), instance->caretVisible, GetFocus() == window);
        }
        return 0;
    case WM_PRINTCLIENT:
        if (instance != nullptr) {
            const RECT client = ClientRect(window);
            return ScriptEditorRenderer::PaintTo(
                       reinterpret_cast<HDC>(wparam),
                       client,
                       instance->document,
                       instance->viewport,
                       instance->metrics,
                       OverlayOf(*instance),
                       false,
                       false)
                ? 1
                : 0;
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
        if (instance != nullptr && wparam == kAnalysisTimer) {
            PollAnalysis(window, *instance);
        } else if (instance != nullptr) {
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
            const RECT client = ClientRect(window);
            if (y >= client.bottom - instance->metrics.reservedBottom && instance->metrics.reservedBottom > 0) { // the problem list
                const int row = ScriptEditorLayout::ProblemRowAt(instance->problems.Problems().size(), client, instance->metrics, y);
                if (row >= 0) {
                    const ScriptProblem& problem = instance->problems.Problems()[static_cast<std::size_t>(row)];
                    instance->document.SetCaret(ScriptCaret{ problem.line, problem.column }, false);
                    ScriptEditorLayout::EnsureCaretVisible(instance->document, instance->viewport, client, instance->metrics);
                    InvalidateRect(window, nullptr, FALSE);
                }
                return 0;
            }
            if (OverScrollbar(window, *instance, x, y)) {
                const RECT thumb = ScriptEditorLayout::ScrollbarThumb(instance->document, instance->viewport, ClientRect(window), instance->metrics);
                instance->scrollbarDragging = true;
                instance->scrollbarGrabOffset = y - thumb.top;
                SetCapture(window);
                return 0;
            }
            instance->document.SetCaret(ScriptEditorLayout::CaretFromPoint(instance->document, instance->viewport, instance->metrics, x, y), EditorKeyDown(VK_SHIFT));
            SetCapture(window);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        if (instance != nullptr && instance->hovered >= 0) {
            instance->hovered = -1;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (instance != nullptr && (wparam & MK_LBUTTON) == 0) {
            TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, window, 0 };
            TrackMouseEvent(&track);
            const int x = GET_X_LPARAM(lparam);
            const int y = GET_Y_LPARAM(lparam);
            const int hovered = ScriptEditorLayout::ProblemAt(instance->problems.Problems(), instance->document, instance->viewport, ClientRect(window), instance->metrics, x, y);
            if (hovered >= 0 || instance->hovered >= 0) { // the tooltip follows the mouse
                instance->hovered = hovered;
                instance->hoverPoint = POINT{ x, y };
                InvalidateRect(window, nullptr, FALSE);
            }
        } else if (instance != nullptr) {
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
            NoteEdits(window, *instance);
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
            NoteEdits(window, *instance);
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
        instance->metrics.reservedBottom = 0;
        instance->hovered = -1;
        instance->seenRevision = instance->document.Revision();
        instance->problems.Reset(ScriptProblemService::Clock::now());
        StartAnalysisPolling(window);
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

std::uint64_t ScriptEditorWindow::SaveSerial() noexcept {
    return SaveSerialRef();
}

} // namespace kb::editor

#endif
