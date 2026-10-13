#include "platform/win32/EditorNewSceneDialog.hpp"
#include "localization/EditorLocalization.hpp"

#if defined(_WIN32)
#include "platform/win32/EditorModalMessageLoop.hpp"
#include "platform/win32/EditorModalWindowScope.hpp"
#include "rendering/components/EditorDialogStyle.hpp"
#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"

#include <algorithm>
#include <array>
#include <utility>
#include <windowsx.h>

namespace kb::editor {
namespace {

constexpr wchar_t kNewSceneWindowClass[] = L"KBEditorNewSceneDialog";
constexpr int kDialogWidth = 540;
constexpr int kDialogHeight = 352;
constexpr int kPad = 14;
constexpr int kLabelWidth = 64;
constexpr UINT_PTR kNameId = 1001U;
constexpr UINT_PTR kFolderId = 1002U;
constexpr COLORREF kErrorColor = RGB(245, 191, 94);

enum class HoverTarget {
    None,
    Close,
    Create,
    Cancel,
    Basic,
    Empty,
    Startup,
};

[[nodiscard]] RECT Rect(int left, int top, int right, int bottom) noexcept {
    return RECT{left, top, right, bottom};
}

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

class NewSceneWindow final {
public:
    explicit NewSceneWindow(EditorNewSceneDialogDescriptor descriptor)
        : descriptor_(std::move(descriptor)) {}

    ~NewSceneWindow() {
        if (font_ != nullptr) DeleteObject(font_);
        if (editBrush_ != nullptr) DeleteObject(editBrush_);
    }

    NewSceneWindow(const NewSceneWindow&) = delete;
    NewSceneWindow& operator=(const NewSceneWindow&) = delete;

    [[nodiscard]] std::optional<EditorNewSceneChoice> Show(HWND owner) {
        owner_ = owner;
        if (!Create()) return std::nullopt;
        const RECT bounds = CenteredBounds();
        EditorModalLoopExit exit = EditorModalLoopExit::Completed;
        {
            const EditorModalWindowScope modal{window_};
            SetWindowPos(window_, HWND_TOP, bounds.left, bounds.top, bounds.right - bounds.left,
                bounds.bottom - bounds.top, SWP_SHOWWINDOW);
            SetForegroundWindow(window_);
            SetFocus(name_);
            SendMessageW(name_, EM_SETSEL, 0, -1);
            exit = RunEditorModalMessageLoop(window_, false, [this]() noexcept { return !running_; });
        }
        if (window_ != nullptr && IsWindow(window_) != 0) DestroyWindow(window_);
        if (owner_ != nullptr && IsWindow(owner_) != 0) SetForegroundWindow(owner_);
        return exit == EditorModalLoopExit::Completed && accepted_ ? std::optional{ choice_ } : std::nullopt;
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

    [[nodiscard]] RECT CardRect(kb::scene::SceneTemplate kind) const noexcept {
        const RECT client = Client();
        const int width = (client.right - 3 * kPad) / 2;
        const int left = kind == kb::scene::SceneTemplate::Basic ? kPad : 2 * kPad + width;
        return Rect(left, 62, left + width, 118);
    }

    [[nodiscard]] RECT FieldRect(int row) const noexcept {
        const RECT client = Client();
        const int top = 138 + row * 36;
        return Rect(kPad + kLabelWidth, top, client.right - kPad, top + 26);
    }

    [[nodiscard]] RECT StartupRect() const noexcept {
        return Rect(kPad + kLabelWidth, 212, kPad + kLabelWidth + 300, 232);
    }

    [[nodiscard]] RECT ErrorRect() const noexcept {
        const RECT client = Client();
        return Rect(kPad, 240, client.right - kPad, client.bottom - 48);
    }

    [[nodiscard]] RECT CreateRect() const noexcept {
        const RECT client = Client();
        return Rect(client.right - 100, client.bottom - 34, client.right - 14, client.bottom - 8);
    }

    [[nodiscard]] RECT CancelRect() const noexcept {
        const RECT create = CreateRect();
        return Rect(create.left - 94, create.top, create.left - 8, create.bottom);
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
        windowClass.lpfnWndProc = &NewSceneWindow::WindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        windowClass.lpszClassName = kNewSceneWindowClass;
        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW, kNewSceneWindowClass, L"", WS_POPUP, 0, 0, kDialogWidth,
            kDialogHeight, owner_, nullptr, windowClass.hInstance, this);
        return window_ != nullptr;
    }

    [[nodiscard]] HWND CreateField(int row, UINT_PTR id, const std::string& value) {
        const RECT field = FieldRect(row);
        const std::wstring initial = ScriptEditorTextEncoding::Widen(value);
        HWND edit = CreateWindowExW(0, L"EDIT", initial.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            field.left + 7, field.top + 4, field.right - field.left - 14, field.bottom - field.top - 8, window_,
            reinterpret_cast<HMENU>(id), GetModuleHandleW(nullptr), nullptr);
        if (edit == nullptr) return nullptr;
        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        SetWindowLongPtrW(edit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        editProc_ = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&NewSceneWindow::EditProc)));
        return editProc_ != nullptr ? edit : nullptr;
    }

    [[nodiscard]] bool CreateFields() {
        const EditorTheme theme = MakeEditorDarkTheme();
        editBrush_ = CreateSolidBrush(EditorDialogStyle::Color(theme.background));
        font_ = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        name_ = CreateField(0, kNameId, descriptor_.name);
        folder_ = CreateField(1, kFolderId, descriptor_.folder);
        return name_ != nullptr && folder_ != nullptr;
    }

    void Paint(HDC dc) const {
        const RECT client = Client();
        const EditorTheme theme = MakeEditorDarkTheme();
        const COLORREF primary = EditorDialogStyle::Color(theme.textPrimary);
        const COLORREF secondary = EditorDialogStyle::Color(theme.textSecondary);
        EditorDialogStyle::PaintSurface(dc, client, theme);
        EditorDialogStyle::PaintTitleBar(dc, theme, EditorDialogHeaderDescriptor{
            .bounds = Rect(client.left + 1, client.top + 1, client.right - 1, client.top + EditorDialogStyle::TitleBarHeight),
            .closeButton = CloseRect(),
            .title = EditorText("newScene.title", "New Scene"),
            .icon = HeroIconKind::Plus,
            .showIcon = true,
            .closeHovered = hovered_ == HoverTarget::Close,
        });
        EditorDialogStyle::PaintText(dc, Rect(kPad, 38, client.right - kPad, 58), EditorText("newScene.template", "Template"), primary, 11, FW_SEMIBOLD);
        for (const kb::scene::SceneTemplateInfo& info : kb::scene::SceneTemplates::All()) {
            const bool basic = info.kind == kb::scene::SceneTemplate::Basic;
            EditorDialogStyle::PaintListRow(dc, theme, EditorDialogListRowDescriptor{
                .bounds = CardRect(info.kind),
                .title = basic ? EditorText("newScene.template.basic", "Basic") : EditorText("newScene.template.empty", "Empty"),
                .subtitle = basic
                    ? EditorText("newScene.template.basicDescription", "Camera, sun, sky and a floor - ready to press Play.")
                    : EditorText("newScene.template.emptyDescription", "Nothing at all. Add objects from the Create menu."),
                .icon = basic ? HeroIconKind::Cube : HeroIconKind::DocumentText,
                .selected = choice_.sceneTemplate == info.kind,
                .hovered = hovered_ == (basic ? HoverTarget::Basic : HoverTarget::Empty),
            });
        }
        const std::array<std::string_view, 2> labels{ EditorText("newScene.name", "Name"), EditorText("newScene.folder", "Folder") };
        for (int row = 0; row < 2; ++row) {
            const RECT field = FieldRect(row);
            EditorDialogStyle::PaintText(dc, Rect(kPad, field.top, field.left - 6, field.bottom), labels[static_cast<std::size_t>(row)],
                secondary, 11);
            EditorDialogStyle::PaintField(dc, field, theme, {}, GetFocus() == (row == 0 ? name_ : folder_));
        }
        const RECT startup = StartupRect();
        EditorDialogStyle::PaintCheckbox(dc, Rect(startup.left, startup.top + 2, startup.left + 16, startup.top + 18), theme,
            choice_.setAsStartupScene);
        EditorDialogStyle::PaintText(dc, Rect(startup.left + 24, startup.top, startup.right, startup.bottom),
            EditorText("newScene.setAsStartup", "Set as startup scene (the game starts here)"), hovered_ == HoverTarget::Startup ? primary : secondary, 11);
        if (!error_.empty()) {
            EditorDialogStyle::PaintText(dc, ErrorRect(), error_, kErrorColor, 11, FW_NORMAL, DT_LEFT | DT_TOP | DT_WORDBREAK);
        }
        EditorDialogStyle::PaintFooter(dc, Rect(client.left + 1, client.bottom - 42, client.right - 1, client.bottom - 1), theme);
        EditorDialogStyle::PaintButton(dc, CancelRect(), theme, EditorText("common.cancel", "Cancel"), EditorDialogButtonTone::Neutral,
            hovered_ == HoverTarget::Cancel);
        EditorDialogStyle::PaintButton(dc, CreateRect(), theme, EditorText("common.create", "Create"), EditorDialogButtonTone::Primary,
            hovered_ == HoverTarget::Create);
    }

    [[nodiscard]] static std::string FieldText(HWND edit) {
        const int length = GetWindowTextLengthW(edit);
        std::wstring value(static_cast<std::size_t>(std::max(0, length)) + 1U, L'\0');
        if (length > 0) GetWindowTextW(edit, value.data(), length + 1);
        value.resize(static_cast<std::size_t>(std::max(0, length)));
        return ScriptEditorTextEncoding::Narrow(value);
    }

    // Create keeps the window open with the reason when the scene cannot be made.
    void TryCreate() {
        choice_.name = FieldText(name_);
        choice_.folder = FieldText(folder_);
        error_ = descriptor_.validate ? descriptor_.validate(choice_.name, choice_.folder) : std::string{};
        if (!error_.empty()) {
            InvalidateRect(window_, nullptr, FALSE);
            SetFocus(name_);
            return;
        }
        Finish(true);
    }

    void Finish(bool accepted) {
        if (!running_) return;
        accepted_ = accepted;
        running_ = false;
        DestroyWindow(window_);
    }

    void UpdateHover(int x, int y) {
        HoverTarget next = HoverTarget::None;
        if (Contains(CloseRect(), x, y)) next = HoverTarget::Close;
        else if (Contains(CreateRect(), x, y)) next = HoverTarget::Create;
        else if (Contains(CancelRect(), x, y)) next = HoverTarget::Cancel;
        else if (Contains(CardRect(kb::scene::SceneTemplate::Basic), x, y)) next = HoverTarget::Basic;
        else if (Contains(CardRect(kb::scene::SceneTemplate::Empty), x, y)) next = HoverTarget::Empty;
        else if (Contains(StartupRect(), x, y)) next = HoverTarget::Startup;
        if (next == hovered_) return;
        hovered_ = next;
        InvalidateRect(window_, nullptr, FALSE);
    }

    void Click(int x, int y) {
        if (Contains(CloseRect(), x, y) || Contains(CancelRect(), x, y)) Finish(false);
        else if (Contains(CreateRect(), x, y)) TryCreate();
        else if (Contains(CardRect(kb::scene::SceneTemplate::Basic), x, y)) Select(kb::scene::SceneTemplate::Basic);
        else if (Contains(CardRect(kb::scene::SceneTemplate::Empty), x, y)) Select(kb::scene::SceneTemplate::Empty);
        else if (Contains(StartupRect(), x, y)) {
            choice_.setAsStartupScene = !choice_.setAsStartupScene;
            InvalidateRect(window_, nullptr, FALSE);
        } else if (Contains(FieldRect(0), x, y)) SetFocus(name_);
        else if (Contains(FieldRect(1), x, y)) SetFocus(folder_);
    }

    void Select(kb::scene::SceneTemplate kind) {
        choice_.sceneTemplate = kind;
        InvalidateRect(window_, nullptr, FALSE);
    }

    static LRESULT CALLBACK EditProc(HWND edit, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<NewSceneWindow*>(GetWindowLongPtrW(edit, GWLP_USERDATA));
        if (self == nullptr || self->editProc_ == nullptr) return DefWindowProcW(edit, message, wparam, lparam);
        if (message == WM_KEYDOWN && wparam == VK_RETURN) {
            self->TryCreate();
            return 0;
        }
        if (message == WM_KEYDOWN && wparam == VK_ESCAPE) {
            self->Finish(false);
            return 0;
        }
        if (message == WM_KEYDOWN && wparam == VK_TAB) {
            SetFocus(edit == self->name_ ? self->folder_ : self->name_);
            return 0;
        }
        if (message == WM_CHAR && (wparam == L'\t' || wparam == L'\r' || wparam == 0x1B)) {
            return 0;
        }
        if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
            InvalidateRect(self->window_, nullptr, FALSE);
        }
        return CallWindowProcW(self->editProc_, edit, message, wparam, lparam);
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<NewSceneWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<NewSceneWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->window_ = window;
            return TRUE;
        }
        if (self == nullptr) return DefWindowProcW(window, message, wparam, lparam);
        switch (message) {
        case WM_CREATE:
            return self->CreateFields() ? 0 : -1;
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
        case WM_LBUTTONUP:
            self->Click(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            return 0;
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) self->Finish(false);
            else if (wparam == VK_RETURN) self->TryCreate();
            return 0;
        case WM_CLOSE:
            self->Finish(false);
            return 0;
        case WM_NCDESTROY:
            if (self->window_ == window) self->window_ = nullptr;
            self->name_ = nullptr;
            self->folder_ = nullptr;
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            break;
        default:
            break;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    EditorNewSceneDialogDescriptor descriptor_;
    HWND owner_ = nullptr;
    HWND window_ = nullptr;
    HWND name_ = nullptr;
    HWND folder_ = nullptr;
    WNDPROC editProc_ = nullptr;
    HBRUSH editBrush_ = nullptr;
    HFONT font_ = nullptr;
    EditorNewSceneChoice choice_{};
    std::string error_;
    HoverTarget hovered_ = HoverTarget::None;
    bool running_ = true;
    bool accepted_ = false;
};

} // namespace

std::optional<EditorNewSceneChoice> EditorNewSceneDialog::Show(HWND owner, EditorNewSceneDialogDescriptor descriptor) {
    return NewSceneWindow{std::move(descriptor)}.Show(owner);
}

} // namespace kb::editor

#endif
