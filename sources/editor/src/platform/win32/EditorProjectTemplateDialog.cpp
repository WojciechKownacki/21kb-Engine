#include "platform/win32/EditorProjectTemplateDialog.hpp"
#include "localization/EditorLocalization.hpp"

#if defined(_WIN32)
#include "platform/win32/EditorModalMessageLoop.hpp"
#include "platform/win32/EditorModalWindowScope.hpp"
#include "rendering/components/EditorDialogStyle.hpp"

#include <algorithm>
#include <span>
#include <windowsx.h>

namespace kb::editor {
namespace {

constexpr wchar_t kTemplateWindowClass[] = L"KBEditorProjectTemplateDialog";
constexpr int kDialogWidth = 520;
constexpr int kPad = 14;
constexpr int kListTop = 62;
constexpr int kRowHeight = 52;
constexpr int kRowStep = 58;
constexpr int kFooterSpace = 62;

// Hover targets besides the list rows (which are their index).
constexpr int kHoverNone = -1;
constexpr int kHoverClose = -2;
constexpr int kHoverOk = -3;
constexpr int kHoverCancel = -4;

[[nodiscard]] RECT Rect(int left, int top, int right, int bottom) noexcept {
    return RECT{left, top, right, bottom};
}

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

// The words are looked up one by one so that every text of the dialog is a key the localization check sees.
[[nodiscard]] std::string_view TemplateTitle(kb::scene::SceneTemplate kind) noexcept {
    switch (kind) {
    case kb::scene::SceneTemplate::Basic: return EditorText("newProject.template.basic3d", "Basic 3D");
    case kb::scene::SceneTemplate::Basic2D: return EditorText("newProject.template.basic2d", "Basic 2D");
    case kb::scene::SceneTemplate::FirstPerson: return EditorText("newProject.template.firstPerson", "First Person");
    case kb::scene::SceneTemplate::ThirdPerson: return EditorText("newProject.template.thirdPerson", "Third Person");
    case kb::scene::SceneTemplate::Empty: return EditorText("newScene.template.empty", "Empty");
    }
    return {};
}

[[nodiscard]] std::string_view TemplateDescription(kb::scene::SceneTemplate kind) noexcept {
    switch (kind) {
    case kb::scene::SceneTemplate::Basic:
        return EditorText("newScene.template.basicDescription", "Camera, sun, sky and a floor - ready to press Play.");
    case kb::scene::SceneTemplate::Basic2D:
        return EditorText("newProject.template.basic2dDescription", "Orthographic camera and a sprite floor with a collider, on the 2D physics.");
    case kb::scene::SceneTemplate::FirstPerson:
        return EditorText("newProject.template.firstPersonDescription", "A walking character seen through its own eyes, with mouse look.");
    case kb::scene::SceneTemplate::ThirdPerson:
        return EditorText("newProject.template.thirdPersonDescription", "A walking character with a camera that follows behind it.");
    case kb::scene::SceneTemplate::Empty:
        return EditorText("newScene.template.emptyDescription", "Nothing at all. Add objects from the Create menu.");
    }
    return {};
}

[[nodiscard]] HeroIconKind TemplateIcon(kb::scene::SceneTemplate kind) noexcept {
    switch (kind) {
    case kb::scene::SceneTemplate::Basic: return HeroIconKind::Cube;
    case kb::scene::SceneTemplate::Basic2D: return HeroIconKind::Layers;
    case kb::scene::SceneTemplate::FirstPerson: return HeroIconKind::Eye;
    case kb::scene::SceneTemplate::ThirdPerson: return HeroIconKind::Gamepad2;
    case kb::scene::SceneTemplate::Empty: return HeroIconKind::DocumentText;
    }
    return HeroIconKind::Cube;
}

class TemplateWindow final {
public:
    TemplateWindow() : templates_(kb::scene::SceneTemplates::All()) {}

    TemplateWindow(const TemplateWindow&) = delete;
    TemplateWindow& operator=(const TemplateWindow&) = delete;

    [[nodiscard]] std::optional<kb::scene::SceneTemplate> Show(HWND owner) {
        owner_ = owner;
        if (!Create()) return std::nullopt;
        const RECT bounds = CenteredBounds();
        EditorModalLoopExit exit = EditorModalLoopExit::Completed;
        {
            const EditorModalWindowScope modal{window_};
            SetWindowPos(window_, HWND_TOP, bounds.left, bounds.top, bounds.right - bounds.left,
                bounds.bottom - bounds.top, SWP_SHOWWINDOW);
            SetForegroundWindow(window_);
            exit = RunEditorModalMessageLoop(window_, false, [this]() noexcept { return !running_; });
        }
        if (window_ != nullptr && IsWindow(window_) != 0) DestroyWindow(window_);
        if (owner_ != nullptr && IsWindow(owner_) != 0) SetForegroundWindow(owner_);
        return exit == EditorModalLoopExit::Completed && accepted_ ? std::optional{ templates_[selected_].kind } : std::nullopt;
    }

private:
    [[nodiscard]] int DialogHeight() const noexcept {
        return kListTop + static_cast<int>(templates_.size()) * kRowStep + kFooterSpace;
    }

    [[nodiscard]] RECT Client() const noexcept {
        RECT client{};
        GetClientRect(window_, &client);
        return client;
    }

    [[nodiscard]] RECT CloseRect() const noexcept {
        const RECT client = Client();
        return Rect(client.right - 32, 4, client.right - 10, 26);
    }

    [[nodiscard]] RECT RowRect(std::size_t index) const noexcept {
        const int top = kListTop + static_cast<int>(index) * kRowStep;
        return Rect(kPad, top, Client().right - kPad, top + kRowHeight);
    }

    [[nodiscard]] RECT OkRect() const noexcept {
        const RECT client = Client();
        return Rect(client.right - 100, client.bottom - 34, client.right - 14, client.bottom - 8);
    }

    [[nodiscard]] RECT CancelRect() const noexcept {
        const RECT ok = OkRect();
        return Rect(ok.left - 94, ok.top, ok.left - 8, ok.bottom);
    }

    [[nodiscard]] RECT CenteredBounds() const noexcept {
        RECT base{};
        if (owner_ == nullptr || IsWindow(owner_) == 0 || GetWindowRect(owner_, &base) == FALSE) {
            SystemParametersInfoW(SPI_GETWORKAREA, 0U, &base, 0U);
        }
        const int width = std::max(0L, base.right - base.left);
        const int height = std::max(0L, base.bottom - base.top);
        const int left = base.left + std::max(0, (width - kDialogWidth) / 2);
        const int top = base.top + std::max(0, (height - DialogHeight()) / 2);
        return Rect(left, top, left + kDialogWidth, top + DialogHeight());
    }

    [[nodiscard]] bool Create() {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = &TemplateWindow::WindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        windowClass.lpszClassName = kTemplateWindowClass;
        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW, kTemplateWindowClass, L"", WS_POPUP, 0, 0, kDialogWidth,
            DialogHeight(), owner_, nullptr, windowClass.hInstance, this);
        return window_ != nullptr;
    }

    void Paint(HDC dc) const {
        const RECT client = Client();
        const EditorTheme theme = MakeEditorDarkTheme();
        EditorDialogStyle::PaintSurface(dc, client, theme);
        EditorDialogStyle::PaintTitleBar(dc, theme, EditorDialogHeaderDescriptor{
            .bounds = Rect(client.left + 1, client.top + 1, client.right - 1, client.top + EditorDialogStyle::TitleBarHeight),
            .closeButton = CloseRect(),
            .title = EditorText("project.newTitle", "New Project"),
            .icon = HeroIconKind::Plus,
            .showIcon = true,
            .closeHovered = hovered_ == kHoverClose,
        });
        EditorDialogStyle::PaintText(dc, Rect(kPad, 38, client.right - kPad, 58), EditorText("newScene.template", "Template"),
            EditorDialogStyle::Color(theme.textPrimary), 11, FW_SEMIBOLD);
        for (std::size_t index = 0U; index < templates_.size(); ++index) {
            const kb::scene::SceneTemplate kind = templates_[index].kind;
            EditorDialogStyle::PaintListRow(dc, theme, EditorDialogListRowDescriptor{
                .bounds = RowRect(index),
                .title = TemplateTitle(kind),
                .subtitle = TemplateDescription(kind),
                .icon = TemplateIcon(kind),
                .selected = selected_ == index,
                .hovered = hovered_ == static_cast<int>(index),
            });
        }
        EditorDialogStyle::PaintFooter(dc, Rect(client.left + 1, client.bottom - 42, client.right - 1, client.bottom - 1), theme);
        EditorDialogStyle::PaintButton(dc, CancelRect(), theme, EditorText("common.cancel", "Cancel"), EditorDialogButtonTone::Neutral,
            hovered_ == kHoverCancel);
        EditorDialogStyle::PaintButton(dc, OkRect(), theme, EditorText("common.ok", "OK"), EditorDialogButtonTone::Primary,
            hovered_ == kHoverOk);
    }

    [[nodiscard]] int TargetAt(int x, int y) const noexcept {
        if (Contains(CloseRect(), x, y)) return kHoverClose;
        if (Contains(OkRect(), x, y)) return kHoverOk;
        if (Contains(CancelRect(), x, y)) return kHoverCancel;
        for (std::size_t index = 0U; index < templates_.size(); ++index) {
            if (Contains(RowRect(index), x, y)) return static_cast<int>(index);
        }
        return kHoverNone;
    }

    void Finish(bool accepted) {
        if (!running_) return;
        accepted_ = accepted;
        running_ = false;
        DestroyWindow(window_);
    }

    void Click(int x, int y) {
        const int target = TargetAt(x, y);
        if (target == kHoverClose || target == kHoverCancel) {
            Finish(false);
        } else if (target == kHoverOk) {
            Finish(true);
        } else if (target >= 0) {
            Select(static_cast<std::size_t>(target));
        }
    }

    void Select(std::size_t index) {
        selected_ = index;
        InvalidateRect(window_, nullptr, FALSE);
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<TemplateWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<TemplateWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
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
            const int target = self->TargetAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            if (target != self->hovered_) {
                self->hovered_ = target;
                InvalidateRect(window, nullptr, FALSE);
            }
            TRACKMOUSEEVENT track{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0U };
            static_cast<void>(TrackMouseEvent(&track));
            return 0;
        }
        case WM_MOUSELEAVE:
            self->hovered_ = kHoverNone;
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
        case WM_LBUTTONUP:
            self->Click(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            return 0;
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) self->Finish(false);
            else if (wparam == VK_RETURN) self->Finish(true);
            else if (wparam == VK_UP && self->selected_ > 0U) self->Select(self->selected_ - 1U);
            else if (wparam == VK_DOWN && self->selected_ + 1U < self->templates_.size()) self->Select(self->selected_ + 1U);
            return 0;
        case WM_CLOSE:
            self->Finish(false);
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

    std::span<const kb::scene::SceneTemplateInfo> templates_;
    HWND owner_ = nullptr;
    HWND window_ = nullptr;
    std::size_t selected_ = 0U;
    int hovered_ = kHoverNone;
    bool running_ = true;
    bool accepted_ = false;
};

} // namespace

std::optional<kb::scene::SceneTemplate> EditorProjectTemplateDialog::Show(HWND owner) {
    return TemplateWindow{}.Show(owner);
}

} // namespace kb::editor

#endif
