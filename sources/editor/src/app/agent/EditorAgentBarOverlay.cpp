#include "app/agent/EditorAgentBarOverlay.hpp"

#if defined(_WIN32)
#include "agent/EditorAgentCatalog.hpp"
#include "app/agent/EditorAgentPanelController.hpp"
#include "rendering/AgentBarRenderer.hpp"
#include "scene/EditorSceneContext.hpp"

#include <shellapi.h>
#include <windowsx.h>

namespace kb::editor {
namespace {

constexpr wchar_t kWindowClass[] = L"KBEditorAgentBubble";

// The layout the window was placed by: its width less the shadow margins is the bubble's.
[[nodiscard]] AgentBarLayout CurrentLayout(const RECT& sceneContent, const EditorAgentPanelState& state) {
    const RECT window = AgentBarRenderer::WindowRect(sceneContent, state);
    const int contentHeight = static_cast<int>(sceneContent.bottom - sceneContent.top);
    return AgentBarRenderer::Resolve(static_cast<int>(window.right - window.left) - 48, contentHeight - 72, state);
}

} // namespace

EditorAgentBarOverlay::~EditorAgentBarOverlay() {
    if (window_ != nullptr && IsWindow(window_) != 0) {
        DestroyWindow(window_);
    }
    if (memoryDc_ != nullptr) {
        SelectObject(memoryDc_, previousBitmap_);
        DeleteDC(memoryDc_);
    }
    if (bitmap_ != nullptr) {
        DeleteObject(bitmap_);
    }
}

bool EditorAgentBarOverlay::Ensure(HWND owner) {
    if (window_ != nullptr && IsWindow(window_) != 0) {
        if (owner_ != owner) {
            SetWindowLongPtrW(window_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
            owner_ = owner;
        }
        return true;
    }
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &EditorAgentBarOverlay::WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    window_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kWindowClass, L"", WS_POPUP,
        0, 0, 1, 1, owner, nullptr, windowClass.hInstance, this);
    owner_ = window_ != nullptr ? owner : nullptr;
    shown_ = false;
    return window_ != nullptr;
}

void EditorAgentBarOverlay::Show(HWND owner, const RECT& sceneContent, const EditorTheme& theme, EditorSceneContext& sceneContext) {
    static_cast<void>(theme);
    if (owner == nullptr || !Ensure(owner)) {
        Hide();
        return;
    }
    sceneContext_ = &sceneContext;
    sceneContent_ = sceneContent;
    // The bubble names the agent it will ask, so it looks for agents as soon as it is shown.
    sceneContext.AgentPanel().Prepare();
    Render();
}

void EditorAgentBarOverlay::Render() {
    if (window_ == nullptr || sceneContext_ == nullptr || owner_ == nullptr) {
        return;
    }
    const EditorAgentPanelState& state = sceneContext_->AgentPanel();
    const RECT local = AgentBarRenderer::WindowRect(sceneContent_, state);
    if (local.right <= local.left) {
        Hide();
        return;
    }
    // The editor repaints often (the pointer, the camera); the bubble is drawn again only when
    // something it shows has changed.
    const EditorAgentConversation& active = state.Active();
    std::string key;
    key.reserve(256U + active.draft.size());
    for (const long value : { static_cast<long>(local.left), static_cast<long>(local.top), static_cast<long>(local.right), static_cast<long>(local.bottom),
             static_cast<long>(state.ActiveConversation()), static_cast<long>(state.ConversationCount()), static_cast<long>(active.entries.size()),
             static_cast<long>(active.entries.empty() ? 0U : active.entries.back().text.size()), static_cast<long>(active.scrollFromBottom),
             static_cast<long>(active.usage.contextTokens), static_cast<long>(state.CurrentIndex()), static_cast<long>(state.MenuAgent()),
             static_cast<long>(state.InputFocused()) | static_cast<long>(state.Running()) << 1 | static_cast<long>(state.AnyRunning()) << 2 |
                 static_cast<long>(state.BarExpanded()) << 3 | static_cast<long>(state.AnswerDismissed()) << 4 | static_cast<long>(state.ModelMenuOpen()) << 5,
             static_cast<long>(hoverHit_.kind), static_cast<long>(hoverHit_.index) }) {
        key += std::to_string(value);
        key += ',';
    }
    key += state.Model() + ',' + state.Effort() + ',' + std::to_string(state.Agents().size()) + ',';
    for (int index = 0; index < state.ConversationCount(); ++index) {
        key += state.Conversation(index).title + (state.Conversation(index).running ? "*," : ",");
    }
    key += active.draft;
    if (shown_ && key == renderedKey_) {
        return;
    }
    renderedKey_ = std::move(key);

    layout_ = CurrentLayout(sceneContent_, state);
    const AgentBarLayout& layout = layout_;
    const SIZE size{ layout.windowWidth, layout.windowHeight };
    if (bitmap_ == nullptr || size.cx != bitmapSize_.cx || size.cy != bitmapSize_.cy) {
        if (memoryDc_ == nullptr) {
            memoryDc_ = CreateCompatibleDC(nullptr);
        }
        if (bitmap_ != nullptr) {
            SelectObject(memoryDc_, previousBitmap_);
            DeleteObject(bitmap_);
        }
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = size.cx;
        info.bmiHeader.biHeight = -size.cy;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        bitmap_ = CreateDIBSection(memoryDc_, &info, DIB_RGB_COLORS, &pixels_, nullptr, 0);
        if (bitmap_ == nullptr || pixels_ == nullptr) {
            bitmap_ = nullptr;
            return;
        }
        previousBitmap_ = SelectObject(memoryDc_, bitmap_);
        bitmapSize_ = size;
    }
    AgentBarRenderer::Paint(pixels_, layout, state, hover_);

    POINT origin{ local.left, local.top };
    ClientToScreen(owner_, &origin);
    POINT source{ 0, 0 };
    SIZE windowSize = size;
    BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(window_, nullptr, &origin, &windowSize, memoryDc_, &source, 0, &blend, ULW_ALPHA);
    if (!shown_) {
        SetWindowPos(window_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        shown_ = true;
    }
}

void EditorAgentBarOverlay::Hide() noexcept {
    if (window_ != nullptr && IsWindow(window_) != 0 && shown_) {
        ShowWindow(window_, SW_HIDE);
    }
    shown_ = false;
    renderedKey_.clear();
}

void EditorAgentBarOverlay::HandlePointerDown(int x, int y) {
    if (sceneContext_ == nullptr) {
        return;
    }
    EditorAgentPanelState& agent = sceneContext_->AgentPanel();
    agent.Prepare();
    // What the user clicked is what is on screen.
    const AgentBarLayout layout = layout_;
    const AgentBarHit hit = AgentBarRenderer::HitTest(layout, agent, x, y);
    if (hit.kind == AgentBarHitKind::None) {
        return;
    }
    EditorAgentPanelController controller{ *sceneContext_ };
    // Any click on the bubble keeps typing going into it.
    agent.FocusInput(true);
    // A click anywhere but the picker closes it; the picker's own rows decide for themselves.
    const bool inMenu = hit.kind == AgentBarHitKind::MenuAgent || hit.kind == AgentBarHitKind::MenuModel ||
        hit.kind == AgentBarHitKind::MenuEffort || hit.kind == AgentBarHitKind::MenuPanel;
    if (!inMenu && hit.kind != AgentBarHitKind::Model) {
        agent.SetModelMenuOpen(false);
    }
    switch (hit.kind) {
    case AgentBarHitKind::Model:
        agent.SetModelMenuOpen(!agent.ModelMenuOpen());
        break;
    case AgentBarHitKind::MenuAgent: {
        const EditorAgent& chosen = agent.Agents()[static_cast<std::size_t>(hit.index)];
        // An agent without its own models is chosen at once; otherwise its models are listed.
        if (chosen.models.empty() && chosen.efforts.empty()) {
            agent.SelectAgent(hit.index);
            agent.SetModelMenuOpen(false);
        } else {
            agent.SetMenuAgent(hit.index);
        }
        break;
    }
    case AgentBarHitKind::MenuModel: {
        const int listed = agent.MenuAgent();
        if (listed >= 0 && listed < static_cast<int>(agent.Agents().size())) {
            const std::vector<std::string> models = agent.Agents()[static_cast<std::size_t>(listed)].models;
            agent.SelectAgent(listed);
            agent.SelectModel(hit.index == 0 ? std::string{} : models[static_cast<std::size_t>(hit.index - 1)]);
        }
        agent.SetModelMenuOpen(false);
        break;
    }
    case AgentBarHitKind::MenuEffort: {
        const int listed = agent.MenuAgent();
        if (listed >= 0 && listed < static_cast<int>(agent.Agents().size())) {
            const std::vector<std::string> efforts = agent.Agents()[static_cast<std::size_t>(listed)].efforts;
            agent.SelectAgent(listed);
            agent.SetMenuAgent(listed);
            agent.SelectEffort(hit.index == 0 ? std::string{} : efforts[static_cast<std::size_t>(hit.index - 1)]);
        }
        break;
    }
    case AgentBarHitKind::MenuRefresh:
        agent.RefreshAgents();
        break;
    case AgentBarHitKind::MenuAddAgent:
        // The user's own agents live in a small JSON file with an example of its format.
        if (EnsureEditorAgentsFile()) {
            ShellExecuteW(owner_, L"open", L"notepad.exe", EditorAgentsFile().wstring().c_str(), nullptr, SW_SHOWNORMAL);
        }
        break;
    case AgentBarHitKind::MenuPanel:
        break;
    case AgentBarHitKind::Expand:
        agent.SetBarExpanded(!agent.BarExpanded());
        break;
    case AgentBarHitKind::Send:
        static_cast<void>(controller.SendOrStop());
        break;
    case AgentBarHitKind::Answer:
        agent.SetBarExpanded(true);
        break;
    case AgentBarHitKind::AnswerClose:
        agent.DismissAnswer();
        break;
    case AgentBarHitKind::Tab:
        agent.SelectConversation(hit.index);
        break;
    case AgentBarHitKind::CloseTab:
        agent.CloseConversation(hit.index);
        break;
    case AgentBarHitKind::NewTab:
        agent.NewConversation();
        break;
    case AgentBarHitKind::Dock:
        agent.RequestDock(EditorAgentPanelState::DockRequest::Dock);
        break;
    case AgentBarHitKind::Input:
    case AgentBarHitKind::Conversation:
    case AgentBarHitKind::None:
        break;
    }
    Render();
    if (owner_ != nullptr) {
        InvalidateRect(owner_, nullptr, FALSE);
    }
}

LRESULT CALLBACK EditorAgentBarOverlay::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* overlay = reinterpret_cast<EditorAgentBarOverlay*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_NCCREATE: {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_MOUSEACTIVATE:
        // The editor window keeps activation and the keyboard; see the class comment.
        return MA_NOACTIVATE;
    case WM_LBUTTONDOWN:
        if (overlay != nullptr) {
            overlay->HandlePointerDown(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        }
        return 0;
    case WM_MOUSEMOVE:
        if (overlay != nullptr && overlay->sceneContext_ != nullptr) {
            overlay->hover_ = POINT{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            if (!overlay->tracking_) {
                TRACKMOUSEEVENT track{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0U };
                overlay->tracking_ = TrackMouseEvent(&track) != 0;
            }
            // Drawn again only when the pointer moves onto another row or button.
            const AgentBarHit hit = AgentBarRenderer::HitTest(overlay->layout_, overlay->sceneContext_->AgentPanel(), overlay->hover_.x, overlay->hover_.y);
            if (hit.kind != overlay->hoverHit_.kind || hit.index != overlay->hoverHit_.index) {
                overlay->hoverHit_ = hit;
                overlay->Render();
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        if (overlay != nullptr) {
            overlay->tracking_ = false;
            overlay->hover_ = POINT{ -1, -1 };
            overlay->hoverHit_ = {};
            overlay->Render();
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (overlay != nullptr && overlay->sceneContext_ != nullptr) {
            EditorAgentPanelState& agent = overlay->sceneContext_->AgentPanel();
            if (overlay->layout_.expanded) {
                agent.ScrollBy(GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA * 60, AgentBarRenderer::MaximumScroll(overlay->layout_, agent));
                overlay->Render();
            }
        }
        return 0;
    case WM_SETCURSOR:
        if (overlay != nullptr && overlay->sceneContext_ != nullptr) {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(window, &point);
            const AgentBarHit hit = AgentBarRenderer::HitTest(overlay->layout_, overlay->sceneContext_->AgentPanel(), point.x, point.y);
            const bool text = hit.kind == AgentBarHitKind::Input;
            const bool button = hit.kind != AgentBarHitKind::None && hit.kind != AgentBarHitKind::Input && hit.kind != AgentBarHitKind::Conversation;
            SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(text ? 32513 : button ? 32649 : 32512)));
            return TRUE;
        }
        break;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR:
        if (overlay != nullptr && overlay->owner_ != nullptr && IsWindow(overlay->owner_) != 0) {
            return SendMessageW(overlay->owner_, message, wparam, lparam);
        }
        break;
    case WM_NCDESTROY:
        if (overlay != nullptr && overlay->window_ == window) {
            overlay->window_ = nullptr;
            overlay->owner_ = nullptr;
            overlay->shown_ = false;
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace kb::editor

#endif
