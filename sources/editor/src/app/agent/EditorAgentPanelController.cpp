#include "app/agent/EditorAgentPanelController.hpp"

#if defined(_WIN32)
#include "app/EditorTextInputShortcuts.hpp"
#include "localization/EditorLocalization.hpp"
#include "project/EditorProjectPaths.hpp"
#include "rendering/AgentPanelRenderer.hpp"
#include "scene/EditorSceneContext.hpp"

#include <shellapi.h>

#include <algorithm>
#include <string>

namespace kb::editor {
namespace {

constexpr UINT kRefreshCommand = 1U;
constexpr UINT kEditAgentsCommand = 2U;
constexpr UINT kFirstAgentCommand = 100U;

[[nodiscard]] std::wstring Widen(std::string_view text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

[[nodiscard]] bool ControlDown() noexcept {
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
}

} // namespace

EditorAgentWorkspace EditorAgentPanelController::Workspace() {
    wchar_t module[MAX_PATH]{};
    GetModuleFileNameW(nullptr, module, MAX_PATH);
    return EditorAgentWorkspace{
        .project = EditorProjectPaths::ProjectRoot(),
        .kbcli = std::filesystem::path{ module }.parent_path() / "kb_cli.exe",
    };
}

void EditorAgentPanelController::ShowAgentMenu(HWND owner, const RECT& anchor) {
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    const std::vector<EditorAgent>& agents = agent.Agents();
    for (std::size_t index = 0; index < agents.size(); ++index) {
        const UINT flags = MF_STRING | (static_cast<int>(index) == agent.CurrentIndex() ? MF_CHECKED : 0U);
        AppendMenuW(menu, flags, kFirstAgentCommand + static_cast<UINT>(index), Widen(agents[index].Label()).c_str());
    }
    if (!agents.empty()) {
        AppendMenuW(menu, MF_SEPARATOR, 0U, nullptr);
    }
    AppendMenuW(menu, MF_STRING, kRefreshCommand, Widen(EditorText("agent.refresh", "Refresh (find installed agents again)")).c_str());
    AppendMenuW(menu, MF_STRING, kEditAgentsCommand, Widen(EditorText("agent.editAgents", "Add your own agent...")).c_str());
    POINT point{ anchor.left, anchor.bottom };
    ClientToScreen(owner, &point);
    const int chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, point.x, point.y, 0, owner, nullptr);
    DestroyMenu(menu);
    if (chosen <= 0) {
        return;
    }
    const UINT command = static_cast<UINT>(chosen);
    if (command == kRefreshCommand) {
        agent.RefreshAgents();
    } else if (command == kEditAgentsCommand) {
        // The user's own agents live in a small JSON file with an example of its format.
        if (EnsureEditorAgentsFile()) {
            ShellExecuteW(owner, L"open", L"notepad.exe", EditorAgentsFile().wstring().c_str(), nullptr, SW_SHOWNORMAL);
            agent.Add(EditorAgentEntryKind::Info, "Opened " + EditorAgentsFile().string() +
                ". Add an agent to the \"agents\" list as in the example, save, then choose Refresh.");
        }
    } else if (command >= kFirstAgentCommand) {
        agent.SelectAgent(static_cast<int>(command - kFirstAgentCommand));
    }
}

bool EditorAgentPanelController::HandlePointerDown(HWND owner, const RECT& content, int x, int y) {
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    agent.Prepare();
    const AgentPanelHitKind hit = AgentPanelRenderer::HitTest(content, agent, x, y);
    agent.FocusInput(hit == AgentPanelHitKind::Input);
    switch (hit) {
    case AgentPanelHitKind::AgentMenu:
        ShowAgentMenu(owner, AgentPanelRenderer::Resolve(content, agent).agentMenu);
        return true;
    case AgentPanelHitKind::SignInHint: {
        const std::string command = AgentPanelRenderer::SignInCommand(agent);
        if (EditorTextInputShortcuts::CopyToClipboard(owner, command)) {
            agent.Add(EditorAgentEntryKind::Info, "Copied \"" + command + "\". Paste it into a terminal, sign in, then choose Refresh in the agent menu.");
        }
        return true;
    }
    case AgentPanelHitKind::ConnectTools:
        return agent.ConnectEngineTools(Workspace());
    case AgentPanelHitKind::NewConversation:
        if (!agent.Running()) {
            agent.NewConversation();
        }
        return true;
    case AgentPanelHitKind::Send:
        return SendOrStop();
    case AgentPanelHitKind::Input:
        return true;
    case AgentPanelHitKind::None:
    default:
        return false;
    }
}

bool EditorAgentPanelController::SendOrStop() {
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    agent.Prepare();
    if (agent.Running()) {
        agent.Stop();
        return true;
    }
    if (agent.Input().empty()) {
        return false;
    }
    // The agent works on the files: unsaved edits go to disk first so it sees what you see.
    if (sceneContext_.SceneDocumentDirty() && !sceneContext_.CurrentScenePath().empty() && sceneContext_.SaveOpenDocuments()) {
        agent.Add(EditorAgentEntryKind::Info, "Saved the scene so the agent works on your latest changes.");
    }
    return agent.Send(Workspace());
}

bool EditorAgentPanelController::HandleChar(WPARAM character) {
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    if (!agent.InputFocused()) {
        return false;
    }
    // Control characters (Enter, Backspace, Esc) arrive here too; WM_KEYDOWN acts on those.
    agent.TypeCharacter(static_cast<wchar_t>(character));
    return true;
}

bool EditorAgentPanelController::HandleKeyDown(HWND owner, WPARAM key) {
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    if (!agent.InputFocused()) {
        return false;
    }
    switch (EditorTextInputShortcuts::Resolve(key)) {
    case EditorTextInputShortcut::Paste:
        if (const std::optional<std::string> text = EditorTextInputShortcuts::PasteFromClipboard(owner); text.has_value()) {
            agent.InsertText(*text);
        }
        return true;
    case EditorTextInputShortcut::Copy:
        static_cast<void>(EditorTextInputShortcuts::CopyToClipboard(owner, agent.Input()));
        return true;
    case EditorTextInputShortcut::Cut:
        static_cast<void>(EditorTextInputShortcuts::CopyToClipboard(owner, agent.Input()));
        while (!agent.Input().empty()) agent.Backspace();
        return true;
    case EditorTextInputShortcut::SelectAll:
    case EditorTextInputShortcut::None:
        break;
    }
    switch (key) {
    case VK_RETURN:
        // Shift+Enter starts a new line; Enter sends.
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
            agent.InsertText("\n");
        } else {
            static_cast<void>(SendOrStop());
        }
        return true;
    case VK_BACK:
        agent.Backspace();
        return true;
    case VK_ESCAPE:
        agent.FocusInput(false);
        return true;
    default:
        // Plain keys belong to the request box; Ctrl shortcuts (Ctrl+S) still reach the editor.
        return !ControlDown();
    }
}

} // namespace kb::editor

#endif
