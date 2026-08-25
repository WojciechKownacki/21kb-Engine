#include "platform/win32/EditorTextEntryDialog.hpp"

#if defined(_WIN32)
#include "platform/win32/EditorModalMessageLoop.hpp"
#include "platform/win32/EditorModalWindowScope.hpp"
#include "rendering/components/EditorDialogStyle.hpp"
#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"

#include <algorithm>
#include <utility>
#include <windowsx.h>

namespace kb::editor {
namespace {

constexpr wchar_t kTextEntryWindowClass[] = L"KBEditorTextEntryDialog";
constexpr int kDialogWidth = 400;
constexpr int kDialogHeight = 190;
constexpr UINT_PTR kEditId = 1001U;

enum class HoverTarget {
    None,
    Close,
    Accept,
    Cancel,
};

[[nodiscard]] RECT Rect(int left, int top, int right, int bottom) noexcept {
    return RECT{left, top, right, bottom};
}

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

class TextEntryWindow final {
public:
    explicit TextEntryWindow(EditorTextEntryDialogDescriptor descriptor)
        : descriptor_(std::move(descriptor)) {}

    ~TextEntryWindow() {
        if (font_ != nullptr) DeleteObject(font_);
        if (editBrush_ != nullptr) DeleteObject(editBrush_);
    }

    [[nodiscard]] std::optional<std::string> Show(HWND owner) {
        owner_ = owner;
        if (!Create()) return std::nullopt;
        const RECT bounds = CenteredBounds();
        EditorModalLoopExit exit = EditorModalLoopExit::Completed;
        {
            const EditorModalWindowScope modal{window_};
            SetWindowPos(
                window_, HWND_TOP,
                bounds.left, bounds.top,
                bounds.right - bounds.left, bounds.bottom - bounds.top,
                SWP_SHOWWINDOW);
            SetForegroundWindow(window_);
            SetFocus(edit_);
            SendMessageW(edit_, EM_SETSEL, 0, -1);
            exit = RunEditorModalMessageLoop(window_, false, [this]() noexcept { return !running_; });
        }
        if (window_ != nullptr && IsWindow(window_) != 0) DestroyWindow(window_);
        if (owner_ != nullptr && IsWindow(owner_) != 0) SetForegroundWindow(owner_);
        return exit == EditorModalLoopExit::Completed && accepted_ ? result_ : std::nullopt;
    }

private:
    [[nodiscard]] RECT Client() const noexcept {
        RECT client{};
        GetClientRect(window_, &client);
        return client;
    }

    [[nodiscard]] RECT CloseRect() const noexcept {
        const RECT client = Client();
        return Rect(client.right - 32, 4, client.right - 10, 26);
    }

    [[nodiscard]] RECT FieldRect() const noexcept {
        const RECT client = Client();
        return Rect(14, 67, client.right - 14, 93);
    }

    [[nodiscard]] RECT AcceptRect() const noexcept {
        const RECT client = Client();
        return Rect(client.right - 100, client.bottom - 34, client.right - 14, client.bottom - 8);
    }

    [[nodiscard]] RECT CancelRect() const noexcept {
        const RECT accept = AcceptRect();
        return Rect(accept.left - 94, accept.top, accept.left - 8, accept.bottom);
    }

    [[nodiscard]] RECT CenteredBounds() const noexcept {
        RECT base{};
        if (owner_ == nullptr || IsWindow(owner_) == 0 || GetWindowRect(owner_, &base) == FALSE) {
            SystemParametersInfoW(SPI_GETWORKAREA, 0U, &base, 0U);
        }
        const int width = std::max(0L, base.right - base.left);
        const int height = std::max(0L, base.bottom - base.top);
        const int left = base.left + std::max(0, (width - kDialogWidth) / 2);
        const int top = base.top + std::max(0, (height - kDialogHeight) / 2);
        return Rect(left, top, left + kDialogWidth, top + kDialogHeight);
    }

    [[nodiscard]] bool Create() {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = &TextEntryWindow::WindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        windowClass.lpszClassName = kTextEntryWindowClass;
        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        window_ = CreateWindowExW(
            WS_EX_TOOLWINDOW,
            kTextEntryWindowClass,
            L"",
            WS_POPUP,
            0, 0, kDialogWidth, kDialogHeight,
            owner_, nullptr, windowClass.hInstance, this);
        return window_ != nullptr;
    }

    bool CreateEdit() {
        const EditorTheme theme = MakeEditorDarkTheme();
        editBrush_ = CreateSolidBrush(EditorDialogStyle::Color(theme.background));
        font_ = CreateFontW(
            -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        const RECT field = FieldRect();
        const std::wstring initial = ScriptEditorTextEncoding::Widen(descriptor_.value);
        edit_ = CreateWindowExW(
            0,
            L"EDIT",
            initial.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            field.left + 7,
            field.top + 3,
            field.right - field.left - 14,
            field.bottom - field.top - 6,
            window_,
            reinterpret_cast<HMENU>(kEditId),
            GetModuleHandleW(nullptr),
            nullptr);
        if (edit_ == nullptr) return false;
        SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        SetWindowLongPtrW(edit_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        editProc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            edit_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&TextEntryWindow::EditProc)));
        return editProc_ != nullptr;
    }

    void Paint(HDC dc) const {
        const RECT client = Client();
        const EditorTheme theme = MakeEditorDarkTheme();
        EditorDialogStyle::PaintSurface(dc, client, theme);
        EditorDialogStyle::PaintTitleBar(dc, theme, EditorDialogHeaderDescriptor{
            .bounds = Rect(client.left + 1, client.top + 1, client.right - 1, client.top + EditorDialogStyle::TitleBarHeight),
            .closeButton = CloseRect(),
            .title = descriptor_.title,
            .icon = descriptor_.icon,
            .showIcon = true,
            .closeHovered = hovered_ == HoverTarget::Close,
        });
        EditorDialogStyle::PaintText(
            dc,
            Rect(14, 39, client.right - 14, 63),
            descriptor_.label,
            EditorDialogStyle::Color(theme.textPrimary),
            11,
            FW_SEMIBOLD);
        EditorDialogStyle::PaintField(dc, FieldRect(), theme, {}, GetFocus() == edit_);
        EditorDialogStyle::PaintText(
            dc,
            Rect(14, 99, client.right - 14, client.bottom - 48),
            descriptor_.hint,
            EditorDialogStyle::Color(theme.textDisabled),
            10,
            FW_NORMAL,
            DT_LEFT | DT_TOP | DT_WORDBREAK);
        const RECT footer = Rect(client.left + 1, client.bottom - 42, client.right - 1, client.bottom - 1);
        EditorDialogStyle::PaintFooter(dc, footer, theme);
        EditorDialogStyle::PaintButton(
            dc, CancelRect(), theme, "Cancel", EditorDialogButtonTone::Neutral,
            hovered_ == HoverTarget::Cancel);
        EditorDialogStyle::PaintButton(
            dc, AcceptRect(), theme, descriptor_.acceptLabel, EditorDialogButtonTone::Primary,
            hovered_ == HoverTarget::Accept);
    }

    void Finish(bool accepted) {
        if (!running_) return;
        accepted_ = accepted;
        if (accepted && edit_ != nullptr) {
            const int length = GetWindowTextLengthW(edit_);
            std::wstring value(static_cast<std::size_t>(std::max(0, length)) + 1U, L'\0');
            if (length > 0) GetWindowTextW(edit_, value.data(), length + 1);
            value.resize(static_cast<std::size_t>(std::max(0, length)));
            result_ = ScriptEditorTextEncoding::Narrow(value);
        }
        running_ = false;
        DestroyWindow(window_);
    }

    void UpdateHover(int x, int y) {
        HoverTarget next = HoverTarget::None;
        if (Contains(CloseRect(), x, y)) next = HoverTarget::Close;
        else if (Contains(AcceptRect(), x, y)) next = HoverTarget::Accept;
        else if (Contains(CancelRect(), x, y)) next = HoverTarget::Cancel;
        if (next == hovered_) return;
        hovered_ = next;
        InvalidateRect(window_, nullptr, FALSE);
    }

    static LRESULT CALLBACK EditProc(HWND edit, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<TextEntryWindow*>(GetWindowLongPtrW(edit, GWLP_USERDATA));
        if (self == nullptr || self->editProc_ == nullptr) return DefWindowProcW(edit, message, wparam, lparam);
        if (message == WM_KEYDOWN && wparam == VK_RETURN) {
            self->Finish(true);
            return 0;
        }
        if (message == WM_KEYDOWN && wparam == VK_ESCAPE) {
            self->Finish(false);
            return 0;
        }
        if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
            const RECT field = self->FieldRect();
            InvalidateRect(self->window_, &field, FALSE);
        }
        return CallWindowProcW(self->editProc_, edit, message, wparam, lparam);
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<TextEntryWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<TextEntryWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->window_ = window;
            return TRUE;
        }
        if (self == nullptr) return DefWindowProcW(window, message, wparam, lparam);
        switch (message) {
        case WM_CREATE:
            return self->CreateEdit() ? 0 : -1;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            self->Paint(dc);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_CTLCOLOREDIT: {
            const EditorTheme theme = MakeEditorDarkTheme();
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetBkColor(dc, EditorDialogStyle::Color(theme.background));
            SetTextColor(dc, EditorDialogStyle::Color(theme.textPrimary));
            return reinterpret_cast<LRESULT>(self->editBrush_);
        }
        case WM_MOUSEMOVE: {
            self->UpdateHover(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0U};
            static_cast<void>(TrackMouseEvent(&track));
            return 0;
        }
        case WM_MOUSELEAVE:
            self->hovered_ = HoverTarget::None;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN:
            if (!Contains(self->CloseRect(), GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)) &&
                GET_Y_LPARAM(lparam) < EditorDialogStyle::TitleBarHeight) {
                ReleaseCapture();
                SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                return 0;
            }
            break;
        case WM_LBUTTONUP: {
            const int x = GET_X_LPARAM(lparam);
            const int y = GET_Y_LPARAM(lparam);
            if (Contains(self->CloseRect(), x, y) || Contains(self->CancelRect(), x, y)) self->Finish(false);
            else if (Contains(self->AcceptRect(), x, y)) self->Finish(true);
            else if (Contains(self->FieldRect(), x, y)) SetFocus(self->edit_);
            return 0;
        }
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) self->Finish(false);
            else if (wparam == VK_RETURN) self->Finish(true);
            return 0;
        case WM_CLOSE:
            self->Finish(false);
            return 0;
        case WM_NCDESTROY:
            if (self->window_ == window) self->window_ = nullptr;
            self->edit_ = nullptr;
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            break;
        default:
            break;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    EditorTextEntryDialogDescriptor descriptor_;
    HWND owner_ = nullptr;
    HWND window_ = nullptr;
    HWND edit_ = nullptr;
    WNDPROC editProc_ = nullptr;
    HBRUSH editBrush_ = nullptr;
    HFONT font_ = nullptr;
    std::optional<std::string> result_;
    HoverTarget hovered_ = HoverTarget::None;
    bool running_ = true;
    bool accepted_ = false;
};

} // namespace

std::optional<std::string> EditorTextEntryDialog::Show(
    HWND owner,
    EditorTextEntryDialogDescriptor descriptor) {
    return TextEntryWindow{std::move(descriptor)}.Show(owner);
}

} // namespace kb::editor

#endif
