#include "platform/win32/EditorRepositoryWindow.hpp"

#if defined(_WIN32)
#include "localization/EditorLocalization.hpp"
#include "platform/win32/EditorChoiceDialog.hpp"
#include "platform/win32/EditorTextEntryDialog.hpp"
#include "project/EditorProjectPaths.hpp"
#include "project/EditorProjectRepository.hpp"
#include "rendering/EditorPanelStyle.hpp"
#include "rendering/EditorToolbarRenderer.hpp"
#include "rendering/GdiBackBufferRenderer.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/components/EditorDialogStyle.hpp"
#include "rendering/gdi/EditorFontCache.hpp"
#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <windowsx.h>
#include <shellapi.h>

namespace kb::editor {
namespace {

constexpr wchar_t kWindowClass[] = L"KBEditorRepositoryWindow";
constexpr int kWidth = 360;
constexpr int kHeight = 212;
constexpr int kStatusHeight = 50;
constexpr int kInset = 14;
constexpr UINT kJobDoneMessage = WM_APP + 41U;
constexpr UINT_PTR kEditId = 1001U;
// A click on the label first closes the window through the lost activation; this keeps that same
// click from opening it again.
constexpr ULONGLONG kReopenGuardMs = 250ULL;

enum class Target { None, Primary, Secondary, Connect, Field };
enum class StatusKind { None, Busy, Success, Failure };

class RepositoryWindow;
std::unique_ptr<RepositoryWindow> g_window;
ULONGLONG g_closedAt = 0ULL;

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] std::wstring Wide(std::string_view text) {
    return ScriptEditorTextEncoding::Widen(text);
}

class RepositoryWindow final {
public:
    RepositoryWindow(HWND owner, POINT anchor, EditorShellInteractionState& shell)
        : owner_(owner), anchor_(anchor), shell_(shell), root_(EditorProjectPaths::ProjectRoot()) {}

    [[nodiscard]] HWND Handle() const noexcept {
        return window_;
    }

    [[nodiscard]] bool Create() {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
        windowClass.lpfnWndProc = &RepositoryWindow::WindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        windowClass.lpszClassName = kWindowClass;
        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
        state_ = EditorProjectRepository::Query(root_);
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"", WS_POPUP, anchor_.x, anchor_.y, kWidth, kHeight,
            owner_, nullptr, windowClass.hInstance, this);
        if (window_ == nullptr) {
            return false;
        }
        EditorDialogStyle::ApplyDropdownShape(window_);
        ShowWindow(window_, SW_SHOW);
        SetForegroundWindow(window_);
        SetFocus(window_);
        return true;
    }

private:
    [[nodiscard]] static RECT PrimaryRect() noexcept { return RECT{ kInset, 80, kWidth - kInset, 116 }; }
    [[nodiscard]] static RECT SecondaryRect() noexcept { return RECT{ kInset, 122, kWidth - kInset, 158 }; }
    [[nodiscard]] static RECT FieldRect() noexcept { return RECT{ kInset, 166, kWidth - kInset - 74, 198 }; }
    [[nodiscard]] static RECT ConnectRect() noexcept { return RECT{ kWidth - kInset - 68, 166, kWidth - kInset, 198 }; }

    [[nodiscard]] bool Connected() const noexcept {
        return !state_.remoteUrl.empty();
    }

    [[nodiscard]] bool PrimaryEnabled() const {
        return !busy_ && (!Connected() || !EditorProjectRepository::BrowserUrl(state_.remoteUrl).empty());
    }

    [[nodiscard]] bool SecondaryEnabled() const noexcept {
        return !busy_ && (Connected() || !state_.hasRepository);
    }

    void CreateEdit() {
        editBrush_ = CreateSolidBrush(EditorDialogStyle::Color(MakeEditorDarkTheme().chrome));
        const RECT field = FieldRect();
        const std::wstring initial = Wide(state_.remoteUrl);
        edit_ = CreateWindowExW(0, L"EDIT", initial.c_str(), WS_CHILD | ES_AUTOHSCROLL,
            field.left + 8, field.top + 7, field.right - field.left - 16, field.bottom - field.top - 14,
            window_, reinterpret_cast<HMENU>(kEditId), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(EditorFontCache::Acquire(13, FW_NORMAL)), TRUE);
        SetWindowLongPtrW(edit_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        editProc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(edit_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&RepositoryWindow::EditProc)));
        if (!initial.empty()) {
            ShowWindow(edit_, SW_SHOW);
        }
    }

    [[nodiscard]] std::string FieldText() const {
        const int length = GetWindowTextLengthW(edit_);
        std::wstring value(static_cast<std::size_t>(std::max(0, length)) + 1U, L'\0');
        if (length > 0) {
            GetWindowTextW(edit_, value.data(), length + 1);
        }
        value.resize(static_cast<std::size_t>(std::max(0, length)));
        return ScriptEditorTextEncoding::Narrow(value);
    }

    void PaintButton(HDC dc, const EditorTheme& theme, const RECT& rect, std::string_view label, HeroIconKind icon, bool primary, bool enabled, bool hovered, bool showIcon = true) const {
        COLORREF fill = primary ? EditorDialogStyle::Color(theme.accent) : EditorDialogStyle::Color(theme.toolbarButton);
        if (enabled && hovered) {
            fill = primary ? EditorDialogStyle::Blend(fill, RGB(255, 255, 255), 16) : RGB(87, 87, 87);
        }
        panel_style::DrawRoundedFrame(dc, rect, fill, fill, 3.0F, 1.0F);
        const COLORREF ink = !enabled ? EditorDialogStyle::Color(theme.textDisabled) : (primary ? RGB(255, 255, 255) : EditorDialogStyle::Color(theme.textPrimary));
        const int weight = primary ? FW_BOLD : FW_NORMAL;
        constexpr int iconSize = 16;
        constexpr int gap = 8;
        const int textWidth = EditorDialogStyle::TextWidth(dc, label, 13, weight);
        const int total = textWidth + (showIcon ? iconSize + gap : 0);
        int left = static_cast<int>(rect.left + rect.right) / 2 - total / 2;
        if (showIcon) {
            const int top = static_cast<int>(rect.top + rect.bottom) / 2 - iconSize / 2;
            HeroIconPainter::Draw(dc, RECT{ left, top, left + iconSize, top + iconSize }, icon, ink, 1);
            left += iconSize + gap;
        }
        EditorDialogStyle::PaintText(dc, RECT{ left, rect.top, static_cast<int>(rect.right) - 4, rect.bottom }, label, ink, 13, weight);
    }

    void Paint(HDC dc, const RECT& client) const {
        const EditorTheme theme = MakeEditorDarkTheme();
        GdiDrawing::FillRectColor(dc, client, EditorDialogStyle::Color(theme.header));

        const std::string title{ Connected() ? EditorText("repository.titleConnected", "Repository") : EditorText("repository.title", "Connect Repository") };
        std::string description;
        if (Connected()) {
            description = state_.remoteUrl;
            if (!state_.branch.empty()) {
                description = EditorLocalization::Format("repository.onBranch", "Branch {branch}", { { "branch", state_.branch } }) + "\n" + description;
            }
        } else if (state_.hasRepository) {
            description = std::string{ EditorText("repository.localOnly", "The history is on this computer. Connect it to a server to keep a copy there.") };
        } else {
            description = std::string{ EditorText("repository.none", "This project has no history of changes yet. Choose where it should be kept.") };
        }
        EditorDialogStyle::PaintText(dc, RECT{ kInset, 12, kWidth - kInset, 34 }, title, EditorDialogStyle::Color(theme.textPrimary), 15, FW_BOLD);
        EditorDialogStyle::PaintText(dc, RECT{ kInset, 36, kWidth - kInset, 76 }, description, EditorDialogStyle::Color(theme.textSecondary), 13, FW_NORMAL, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

        PaintButton(dc, theme, PrimaryRect(),
            Connected() ? EditorText("repository.openInBrowser", "Open in Browser") : EditorText("repository.connectGitHub", "Connect to GitHub"),
            Connected() ? HeroIconKind::ExternalLink : HeroIconKind::GitHub, true, PrimaryEnabled(), hovered_ == Target::Primary);
        PaintButton(dc, theme, SecondaryRect(),
            Connected() ? EditorText("menu.project.showInExplorer", "Show in Explorer")
                        : (state_.hasRepository ? EditorText("repository.localDone", "Local repository is ready") : EditorText("repository.createLocal", "Create Local Repository")),
            Connected() ? HeroIconKind::Folder : (state_.hasRepository ? HeroIconKind::Check : HeroIconKind::GitBranch), false, SecondaryEnabled(), hovered_ == Target::Secondary);

        const RECT field = FieldRect();
        const COLORREF input = EditorDialogStyle::Color(theme.chrome);
        const COLORREF outline = GetFocus() == edit_ ? EditorDialogStyle::Color(theme.accent) : EditorDialogStyle::Color(theme.toolbarButton);
        panel_style::DrawRoundedFrame(dc, field, input, outline, 3.0F, 1.0F);
        if (edit_ != nullptr && IsWindowVisible(edit_) == FALSE) {
            EditorDialogStyle::PaintText(dc, RECT{ field.left + 9, field.top, field.right - 8, field.bottom },
                EditorText("repository.addressPlaceholder", "Repository address"), EditorDialogStyle::Color(theme.textDisabled), 13);
        }
        PaintButton(dc, theme, ConnectRect(), EditorText("repository.connect", "Connect"), HeroIconKind::Plug, false, !busy_, hovered_ == Target::Connect, false);

        if (statusKind_ != StatusKind::None) {
            const COLORREF color = statusKind_ == StatusKind::Failure ? RGB(255, 138, 128)
                : (statusKind_ == StatusKind::Success ? RGB(123, 227, 160) : EditorDialogStyle::Color(theme.textSecondary));
            EditorDialogStyle::PaintText(dc, RECT{ kInset, kHeight - 4, kWidth - kInset, kHeight + kStatusHeight - 8 }, status_, color, 12, FW_NORMAL, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        }
    }

    void ShowStatus(StatusKind kind, std::string text) {
        const bool grew = statusKind_ == StatusKind::None && kind != StatusKind::None;
        statusKind_ = kind;
        status_ = std::move(text);
        if (grew) {
            SetWindowPos(window_, nullptr, 0, 0, kWidth, kHeight + kStatusHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            EditorDialogStyle::ApplyDropdownShape(window_);
        }
        InvalidateRect(window_, nullptr, FALSE);
    }

    void StartJob(std::string busyText, std::function<EditorRepositoryResult()> job) {
        busy_ = true;
        ShowStatus(StatusKind::Busy, std::move(busyText));
        std::thread([target = window_, job = std::move(job)]() {
            auto* result = new EditorRepositoryResult(job());
            if (PostMessageW(target, kJobDoneMessage, 0U, reinterpret_cast<LPARAM>(result)) == FALSE) {
                delete result;
            }
        }).detach();
    }

    void FinishJob(const EditorRepositoryResult& result) {
        busy_ = false;
        state_ = EditorProjectRepository::Query(root_);
        if (Connected() && edit_ != nullptr) {
            SetWindowTextW(edit_, Wide(state_.remoteUrl).c_str());
            ShowWindow(edit_, SW_SHOW);
        }
        ShowStatus(result.succeeded ? StatusKind::Success : StatusKind::Failure, result.message);
        EditorToolbarRenderer::InvalidateRepositoryState();
        InvalidateRect(owner_, nullptr, FALSE);
    }

    // A project inside another repository can have its own, but only on purpose: the outer one
    // stops seeing the project's files as its own.
    [[nodiscard]] bool ConfirmOwnRepository() {
        if (state_.hasRepository || state_.enclosingRoot.empty()) {
            return true;
        }
        const std::u8string outer = state_.enclosingRoot.u8string();
        return EditorChoiceDialog::Show(window_, EditorChoiceDialogDescriptor{
            .title = std::string{ EditorText("repository.title", "Connect Repository") },
            .message = EditorLocalization::Format("repository.nestedQuestion", "This project is inside another repository: {folder}. Give it its own?",
                { { "folder", std::string{ reinterpret_cast<const char*>(outer.data()), outer.size() } } }),
            .supportingText = std::string{ EditorText("repository.nestedSupporting", "The outer repository stops tracking this project's new changes.") },
            .primaryLabel = std::string{ EditorText("repository.nestedAccept", "Give It Its Own") },
            .cancelLabel = std::string{ EditorText("common.cancel", "Cancel") },
            .icon = HeroIconKind::GitBranch,
        }) == EditorChoiceDialogResult::Primary;
    }

    void ConnectToGitHub() {
        if (!EditorProjectRepository::GitHubCliAvailable()) {
            ShellExecuteW(window_, L"open", L"https://github.com/new", nullptr, nullptr, SW_SHOWNORMAL);
            ShowStatus(StatusKind::Failure, std::string{ EditorText("repository.noGitHubCli",
                "GitHub CLI is not installed, so the GitHub page was opened. Create a repository there and paste its address below.") });
            return;
        }
        if (!ConfirmOwnRepository()) {
            return;
        }
        const std::optional<std::string> name = EditorTextEntryDialog::Show(window_, {
            .title = std::string{ EditorText("repository.connectGitHub", "Connect to GitHub") },
            .label = std::string{ EditorText("repository.nameLabel", "Repository name") },
            .value = EditorProjectRepository::SuggestedRepositoryName(root_),
            .hint = std::string{ EditorText("repository.nameHint", "A new private repository is created on your GitHub account.") },
            .acceptLabel = std::string{ EditorText("repository.create", "Create") },
            .icon = HeroIconKind::GitHub,
        });
        SetForegroundWindow(window_);
        if (!name.has_value()) {
            return;
        }
        if (!EditorProjectRepository::IsValidRepositoryName(*name)) {
            ShowStatus(StatusKind::Failure, std::string{ EditorText("repository.badName", "A repository name may only use letters, digits, dots, dashes and underscores.") });
            return;
        }
        StartJob(std::string{ EditorText("repository.sendingGitHub", "Creating the repository and sending the project...") },
            [root = root_, repository = *name]() { return EditorProjectRepository::ConnectGitHub(root, repository); });
    }

    void CreateLocalRepository() {
        if (!ConfirmOwnRepository()) {
            return;
        }
        SetForegroundWindow(window_);
        StartJob(std::string{ EditorText("repository.creatingLocal", "Creating the repository...") },
            [root = root_]() { return EditorProjectRepository::CreateLocal(root); });
    }

    void ConnectToAddress() {
        const std::string url = FieldText();
        if (!ConfirmOwnRepository()) {
            return;
        }
        SetForegroundWindow(window_);
        StartJob(std::string{ EditorText("repository.connecting", "Connecting...") },
            [root = root_, url]() { return EditorProjectRepository::ConnectUrl(root, url); });
    }

    void Activate(Target target) {
        if (busy_) {
            return;
        }
        switch (target) {
        case Target::Primary:
            if (!PrimaryEnabled()) {
                return;
            }
            if (Connected()) {
                ShellExecuteW(window_, L"open", Wide(EditorProjectRepository::BrowserUrl(state_.remoteUrl)).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } else {
                ConnectToGitHub();
            }
            return;
        case Target::Secondary:
            if (!SecondaryEnabled()) {
                return;
            }
            if (Connected()) {
                ShellExecuteW(window_, L"open", root_.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } else {
                CreateLocalRepository();
            }
            return;
        case Target::Connect:
            ConnectToAddress();
            return;
        case Target::Field:
            ShowWindow(edit_, SW_SHOW);
            SetFocus(edit_);
            InvalidateRect(window_, nullptr, FALSE);
            return;
        case Target::None:
        default:
            return;
        }
    }

    [[nodiscard]] static Target HitTest(int x, int y) noexcept {
        if (Contains(PrimaryRect(), x, y)) return Target::Primary;
        if (Contains(SecondaryRect(), x, y)) return Target::Secondary;
        if (Contains(ConnectRect(), x, y)) return Target::Connect;
        if (Contains(FieldRect(), x, y)) return Target::Field;
        return Target::None;
    }

    void CloseWindow() {
        if (!busy_ && window_ != nullptr) {
            DestroyWindow(window_);
        }
    }

    static LRESULT CALLBACK EditProc(HWND edit, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<RepositoryWindow*>(GetWindowLongPtrW(edit, GWLP_USERDATA));
        if (self == nullptr || self->editProc_ == nullptr) {
            return DefWindowProcW(edit, message, wparam, lparam);
        }
        if (message == WM_KEYDOWN && wparam == VK_RETURN) {
            self->Activate(Target::Connect);
            return 0;
        }
        if (message == WM_KEYDOWN && wparam == VK_ESCAPE) {
            self->CloseWindow();
            return 0;
        }
        if (message == WM_CHAR && (wparam == VK_RETURN || wparam == VK_ESCAPE)) {
            return 0;
        }
        if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
            InvalidateRect(self->window_, nullptr, FALSE);
        }
        if (message == WM_KILLFOCUS && GetWindowTextLengthW(edit) == 0) {
            ShowWindow(edit, SW_HIDE);
        }
        return CallWindowProcW(self->editProc_, edit, message, wparam, lparam);
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<RepositoryWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<RepositoryWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->window_ = window;
            return TRUE;
        }
        if (self == nullptr) {
            return DefWindowProcW(window, message, wparam, lparam);
        }
        switch (message) {
        case WM_CREATE:
            self->CreateEdit();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            GdiBackBufferRenderer::Paint(
                window,
                [](const GdiBackBufferPaintContext& paint, void* context) {
                    static_cast<const RepositoryWindow*>(context)->Paint(paint.dc, paint.client);
                },
                self);
            return 0;
        case WM_CTLCOLOREDIT: {
            const EditorTheme theme = MakeEditorDarkTheme();
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetBkColor(dc, EditorDialogStyle::Color(theme.chrome));
            SetTextColor(dc, EditorDialogStyle::Color(theme.textPrimary));
            return reinterpret_cast<LRESULT>(self->editBrush_);
        }
        case WM_ACTIVATE:
            // Only a click in the editor puts it away: the browser and gh's console must not.
            if (LOWORD(wparam) == WA_INACTIVE && reinterpret_cast<HWND>(lparam) == self->owner_) {
                self->CloseWindow();
            }
            return 0;
        case WM_MOUSEMOVE: {
            const Target next = HitTest(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            if (next != self->hovered_) {
                self->hovered_ = next;
                InvalidateRect(window, nullptr, FALSE);
            }
            TRACKMOUSEEVENT track{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0U };
            static_cast<void>(TrackMouseEvent(&track));
            return 0;
        }
        case WM_MOUSELEAVE:
            self->hovered_ = Target::None;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN:
            SetFocus(window);
            return 0;
        case WM_LBUTTONUP:
            self->Activate(HitTest(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)));
            return 0;
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) {
                self->CloseWindow();
            }
            return 0;
        case kJobDoneMessage: {
            const std::unique_ptr<EditorRepositoryResult> result{ reinterpret_cast<EditorRepositoryResult*>(lparam) };
            self->FinishJob(*result);
            return 0;
        }
        case WM_NCDESTROY: {
            if (self->editBrush_ != nullptr) {
                DeleteObject(self->editBrush_);
            }
            self->shell_.SetRepositoryWindowOpen(false);
            const HWND owner = self->owner_;
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            g_closedAt = GetTickCount64();
            if (IsWindow(owner) != FALSE) {
                InvalidateRect(owner, nullptr, FALSE);
            }
            const LRESULT result = DefWindowProcW(window, message, wparam, lparam);
            g_window.reset();
            return result;
        }
        default:
            break;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    HWND owner_ = nullptr;
    POINT anchor_{};
    EditorShellInteractionState& shell_;
    std::filesystem::path root_;
    EditorRepositoryState state_{};
    HWND window_ = nullptr;
    HWND edit_ = nullptr;
    WNDPROC editProc_ = nullptr;
    HBRUSH editBrush_ = nullptr;
    Target hovered_ = Target::None;
    StatusKind statusKind_ = StatusKind::None;
    std::string status_;
    bool busy_ = false;
};

} // namespace

void EditorRepositoryWindow::Toggle(HWND owner, POINT anchor, EditorShellInteractionState& shellInteraction) {
    if (g_window != nullptr) {
        SetForegroundWindow(g_window->Handle());
        return;
    }
    if (GetTickCount64() - g_closedAt < kReopenGuardMs) {
        return;
    }
    g_window = std::make_unique<RepositoryWindow>(owner, anchor, shellInteraction);
    if (!g_window->Create()) {
        g_window.reset();
        return;
    }
    shellInteraction.SetRepositoryWindowOpen(true);
    InvalidateRect(owner, nullptr, FALSE);
}

} // namespace kb::editor

#endif
