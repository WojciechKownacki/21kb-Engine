#include "platform/win32/EditorChoiceDialog.hpp"

#if defined(_WIN32)
#include "platform/win32/EditorModalMessageLoop.hpp"
#include "platform/win32/EditorModalWindowScope.hpp"
#include "rendering/components/EditorDialogStyle.hpp"

#include <algorithm>
#include <utility>
#include <windowsx.h>

namespace kb::editor {
namespace {

constexpr wchar_t kChoiceWindowClass[] = L"KBEditorChoiceDialog";
constexpr int kDialogWidth = 480;
constexpr int kDialogHeight = 220;
constexpr int kButtonWidth = 104;
constexpr int kButtonGap = 8;

enum class HoverTarget {
    None,
    Close,
    Primary,
    Secondary,
    Cancel,
};

[[nodiscard]] RECT Rect(int left, int top, int right, int bottom) noexcept {
    return RECT{left, top, right, bottom};
}

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

class ChoiceWindow final {
public:
    explicit ChoiceWindow(EditorChoiceDialogDescriptor descriptor)
        : descriptor_(std::move(descriptor)) {}

    [[nodiscard]] EditorChoiceDialogResult Show(HWND owner) {
        owner_ = owner;
        if (!Create()) return EditorChoiceDialogResult::Cancel;
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
            SetFocus(window_);
            exit = RunEditorModalMessageLoop(window_, false, [this]() noexcept { return !running_; });
        }
        if (window_ != nullptr && IsWindow(window_) != 0) DestroyWindow(window_);
        if (owner_ != nullptr && IsWindow(owner_) != 0) SetForegroundWindow(owner_);
        return exit == EditorModalLoopExit::Completed ? result_ : EditorChoiceDialogResult::Cancel;
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

    [[nodiscard]] RECT PrimaryRect() const noexcept {
        const RECT client = Client();
        return Rect(client.right - kButtonWidth - 14, client.bottom - 34, client.right - 14, client.bottom - 8);
    }

    [[nodiscard]] RECT SecondaryRect() const noexcept {
        if (descriptor_.secondaryLabel.empty()) return RECT{};
        const RECT primary = PrimaryRect();
        return Rect(primary.left - kButtonWidth - kButtonGap, primary.top, primary.left - kButtonGap, primary.bottom);
    }

    [[nodiscard]] RECT CancelRect() const noexcept {
        if (descriptor_.cancelLabel.empty()) return RECT{};
        const RECT previous = descriptor_.secondaryLabel.empty() ? PrimaryRect() : SecondaryRect();
        return Rect(previous.left - kButtonWidth - kButtonGap, previous.top, previous.left - kButtonGap, previous.bottom);
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
        windowClass.lpfnWndProc = &ChoiceWindow::WindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        windowClass.lpszClassName = kChoiceWindowClass;
        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        window_ = CreateWindowExW(
            WS_EX_TOOLWINDOW,
            kChoiceWindowClass,
            L"",
            WS_POPUP,
            0, 0, kDialogWidth, kDialogHeight,
            owner_, nullptr, windowClass.hInstance, this);
        return window_ != nullptr;
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

        const RECT context = Rect(client.left + 1, EditorDialogStyle::TitleBarHeight, client.right - 1, 62);
        EditorDialogStyle::PaintToolbar(dc, context, theme);
        EditorDialogStyle::PaintText(
            dc,
            Rect(16, context.top, client.right - 16, context.bottom),
            "ACTION REQUIRED",
            EditorDialogStyle::Color(theme.accent),
            9,
            FW_SEMIBOLD);
        EditorDialogStyle::PaintText(
            dc,
            Rect(16, 76, client.right - 16, 122),
            descriptor_.message,
            EditorDialogStyle::Color(theme.textPrimary),
            12,
            FW_SEMIBOLD,
            DT_LEFT | DT_TOP | DT_WORDBREAK);
        EditorDialogStyle::PaintText(
            dc,
            Rect(16, 126, client.right - 16, client.bottom - 48),
            descriptor_.supportingText,
            EditorDialogStyle::Color(theme.textDisabled),
            10,
            FW_NORMAL,
            DT_LEFT | DT_TOP | DT_WORDBREAK);

        const RECT footer = Rect(client.left + 1, client.bottom - 42, client.right - 1, client.bottom - 1);
        EditorDialogStyle::PaintFooter(dc, footer, theme);
        if (!descriptor_.cancelLabel.empty()) {
            EditorDialogStyle::PaintButton(
                dc, CancelRect(), theme, descriptor_.cancelLabel, EditorDialogButtonTone::Neutral,
                hovered_ == HoverTarget::Cancel);
        }
        if (!descriptor_.secondaryLabel.empty()) {
            EditorDialogStyle::PaintButton(
                dc, SecondaryRect(), theme, descriptor_.secondaryLabel, EditorDialogButtonTone::Neutral,
                hovered_ == HoverTarget::Secondary);
        }
        EditorDialogStyle::PaintButton(
            dc, PrimaryRect(), theme, descriptor_.primaryLabel, descriptor_.primaryTone,
            hovered_ == HoverTarget::Primary);
    }

    void Finish(EditorChoiceDialogResult result) {
        if (!running_) return;
        result_ = result;
        running_ = false;
        DestroyWindow(window_);
    }

    void UpdateHover(int x, int y) {
        HoverTarget next = HoverTarget::None;
        if (Contains(CloseRect(), x, y)) next = HoverTarget::Close;
        else if (Contains(PrimaryRect(), x, y)) next = HoverTarget::Primary;
        else if (!descriptor_.secondaryLabel.empty() && Contains(SecondaryRect(), x, y)) next = HoverTarget::Secondary;
        else if (!descriptor_.cancelLabel.empty() && Contains(CancelRect(), x, y)) next = HoverTarget::Cancel;
        if (next == hovered_) return;
        hovered_ = next;
        InvalidateRect(window_, nullptr, FALSE);
    }

    [[nodiscard]] EditorChoiceDialogResult DismissResult() const noexcept {
        if (!descriptor_.cancelLabel.empty()) return EditorChoiceDialogResult::Cancel;
        if (!descriptor_.secondaryLabel.empty()) return EditorChoiceDialogResult::Secondary;
        return EditorChoiceDialogResult::Cancel;
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<ChoiceWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<ChoiceWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->window_ = window;
            return TRUE;
        }
        if (self == nullptr) return DefWindowProcW(window, message, wparam, lparam);
        switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            self->Paint(dc);
            EndPaint(window, &paint);
            return 0;
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
            if (Contains(self->CloseRect(), x, y)) self->Finish(self->DismissResult());
            else if (Contains(self->PrimaryRect(), x, y)) self->Finish(EditorChoiceDialogResult::Primary);
            else if (!self->descriptor_.secondaryLabel.empty() && Contains(self->SecondaryRect(), x, y)) {
                self->Finish(EditorChoiceDialogResult::Secondary);
            } else if (!self->descriptor_.cancelLabel.empty() && Contains(self->CancelRect(), x, y)) {
                self->Finish(EditorChoiceDialogResult::Cancel);
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) self->Finish(self->DismissResult());
            else if (wparam == VK_RETURN) self->Finish(EditorChoiceDialogResult::Primary);
            return 0;
        case WM_CLOSE:
            self->Finish(self->DismissResult());
            return 0;
        case WM_NCDESTROY:
            if (self->window_ == window) self->window_ = nullptr;
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            break;
        default:
            break;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    EditorChoiceDialogDescriptor descriptor_;
    HWND owner_ = nullptr;
    HWND window_ = nullptr;
    HoverTarget hovered_ = HoverTarget::None;
    EditorChoiceDialogResult result_ = EditorChoiceDialogResult::Cancel;
    bool running_ = true;
};

} // namespace

EditorChoiceDialogResult EditorChoiceDialog::Show(
    HWND owner,
    EditorChoiceDialogDescriptor descriptor) {
    return ChoiceWindow{std::move(descriptor)}.Show(owner);
}

} // namespace kb::editor

#endif
