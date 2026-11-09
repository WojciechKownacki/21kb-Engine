#include "app/agent/EditorAgentPanelController.hpp"

#if defined(_WIN32)
#include "app/EditorTextInputShortcuts.hpp"
#include "localization/EditorLocalization.hpp"
#include "project/EditorProjectPaths.hpp"
#include "rendering/AgentBarRenderer.hpp"
#include "rendering/AgentPanelRenderer.hpp"
#include "scene/EditorSceneContext.hpp"

#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <string>

namespace kb::editor {
namespace {

constexpr UINT kRefreshCommand = 1U;
constexpr UINT kEditAgentsCommand = 2U;
constexpr UINT kFirstAgentCommand = 100U;
constexpr UINT kFirstChoiceCommand = 1000U;
constexpr UINT kFirstEffortCommand = 2000U;
constexpr UINT kFirstModelCommand = 3000U;
constexpr UINT kModelsPerAgent = 64U;

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
    const AgentPanelHit hit = AgentPanelRenderer::HitTest(content, agent, x, y);
    agent.FocusInput(hit.kind == AgentPanelHitKind::Input || hit.kind == AgentPanelHitKind::Suggestion ||
        hit.kind == AgentPanelHitKind::NewTab);
    const AgentPanelLayout layout = AgentPanelRenderer::Resolve(content, agent);
    switch (hit.kind) {
    case AgentPanelHitKind::Tab:
        agent.SelectConversation(hit.index);
        return true;
    case AgentPanelHitKind::TabClose:
        agent.CloseConversation(hit.index);
        return true;
    case AgentPanelHitKind::NewTab:
        agent.NewConversation();
        return true;
    case AgentPanelHitKind::Undock:
        agent.RequestDock(EditorAgentPanelState::DockRequest::Undock);
        return true;
    case AgentPanelHitKind::AgentMenu:
        ShowAgentMenu(owner, layout.agentMenu);
        return true;
    case AgentPanelHitKind::ModelMenu:
        ShowChoiceMenu(owner, layout.modelMenu, true);
        return true;
    case AgentPanelHitKind::EffortMenu:
        ShowChoiceMenu(owner, layout.effortMenu, false);
        return true;
    case AgentPanelHitKind::SignInHint: {
        const std::string command = AgentPanelRenderer::SignInCommand(agent);
        if (EditorTextInputShortcuts::CopyToClipboard(owner, command)) {
            agent.Add(EditorAgentEntryKind::Info, "Copied the sign-in command. Paste it into a terminal, sign in, then choose Refresh in the Agent menu.");
        }
        return true;
    }
    case AgentPanelHitKind::ConnectTools:
        return agent.ConnectEngineTools(Workspace());
    case AgentPanelHitKind::Suggestion:
        agent.SetInput(std::string{ AgentPanelRenderer::Suggestion(hit.index) });
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

void EditorAgentPanelController::ShowChoiceMenu(HWND owner, const RECT& anchor, bool models) {
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    const EditorAgent* current = agent.Current();
    if (current == nullptr) {
        return;
    }
    const std::vector<std::string>& choices = models ? current->models : current->efforts;
    const std::string& selected = models ? agent.Model() : agent.Effort();
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    // The agent's own default first, then what it offers; a disabled note when it offers nothing.
    AppendMenuW(menu, MF_STRING | (selected.empty() ? MF_CHECKED : 0U), kFirstChoiceCommand,
        Widen(models ? EditorText("agent.defaultModel", "Default (the agent's own setting)") : EditorText("agent.defaultEffort", "Default (the agent's own setting)")).c_str());
    for (std::size_t index = 0; index < choices.size(); ++index) {
        std::wstring label = Widen(choices[index]);
        if (!label.empty()) label[0] = static_cast<wchar_t>(towupper(label[0]));
        AppendMenuW(menu, MF_STRING | (choices[index] == selected ? MF_CHECKED : 0U), kFirstChoiceCommand + 1U + static_cast<UINT>(index), label.c_str());
    }
    if (choices.empty()) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0U, Widen(models ? EditorText("agent.noModels", "This agent picks its model itself")
                                                              : EditorText("agent.noEfforts", "This agent has no reasoning levels")).c_str());
    }
    POINT point{ anchor.left, anchor.bottom };
    ClientToScreen(owner, &point);
    const int chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, point.x, point.y, 0, owner, nullptr);
    DestroyMenu(menu);
    if (chosen < static_cast<int>(kFirstChoiceCommand)) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(chosen) - kFirstChoiceCommand;
    const std::string value = index == 0U ? std::string{} : choices[index - 1U];
    if (models) {
        agent.SelectModel(value);
    } else {
        agent.SelectEffort(value);
    }
}

void EditorAgentPanelController::ShowModelMenu(HWND owner, POINT screenAnchor) {
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    agent.Prepare();
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    // Each agent (provider and profile) as a heading over its models; an agent that picks its
    // model itself is one item.
    const std::vector<EditorAgent>& agents = agent.Agents();
    for (std::size_t index = 0; index < agents.size(); ++index) {
        const EditorAgent& entry = agents[index];
        const bool current = static_cast<int>(index) == agent.CurrentIndex();
        const UINT base = kFirstModelCommand + static_cast<UINT>(index) * kModelsPerAgent;
        if (index > 0U) {
            AppendMenuW(menu, MF_SEPARATOR, 0U, nullptr);
        }
        if (entry.models.empty()) {
            AppendMenuW(menu, MF_STRING | (current ? MF_CHECKED : 0U), base, Widen(entry.Label()).c_str());
            continue;
        }
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0U, Widen(entry.Label()).c_str());
        AppendMenuW(menu, MF_STRING | (current && agent.Model().empty() ? MF_CHECKED : 0U), base,
            Widen("    " + std::string{ EditorText("agent.defaultModelShort", "Default model") }).c_str());
        for (std::size_t model = 0; model < entry.models.size() && model + 1U < kModelsPerAgent; ++model) {
            std::string label = entry.models[model];
            if (!label.empty()) label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
            AppendMenuW(menu, MF_STRING | (current && agent.Model() == entry.models[model] ? MF_CHECKED : 0U),
                base + 1U + static_cast<UINT>(model), Widen("    " + label).c_str());
        }
    }
    // The reasoning level of the current agent, in plain words.
    if (const EditorAgent* current = agent.Current(); current != nullptr && !current->efforts.empty()) {
        HMENU efforts = CreatePopupMenu();
        AppendMenuW(efforts, MF_STRING | (agent.Effort().empty() ? MF_CHECKED : 0U), kFirstEffortCommand,
            Widen(EditorText("agent.defaultEffort", "Default (the agent's own setting)")).c_str());
        for (std::size_t index = 0; index < current->efforts.size(); ++index) {
            AppendMenuW(efforts, MF_STRING | (agent.Effort() == current->efforts[index] ? MF_CHECKED : 0U), kFirstEffortCommand + 1U + static_cast<UINT>(index),
                Widen(AgentBarRenderer::EffortLabel(current->efforts[index])).c_str());
        }
        if (!agents.empty()) {
            AppendMenuW(menu, MF_SEPARATOR, 0U, nullptr);
        }
        const std::string title = std::string{ EditorText("agent.reasoningMenu", "Reasoning") } +
            (agent.Effort().empty() ? std::string{} : ": " + AgentBarRenderer::EffortLabel(agent.Effort()));
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(efforts), Widen(title).c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0U, nullptr);
    AppendMenuW(menu, MF_STRING, kRefreshCommand, Widen(EditorText("agent.refresh", "Refresh (find installed agents again)")).c_str());
    AppendMenuW(menu, MF_STRING, kEditAgentsCommand, Widen(EditorText("agent.editAgents", "Add your own agent...")).c_str());
    const int chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_NONOTIFY, screenAnchor.x, screenAnchor.y, 0, owner, nullptr);
    DestroyMenu(menu);
    if (chosen <= 0) {
        return;
    }
    const UINT command = static_cast<UINT>(chosen);
    if (command >= kFirstModelCommand) {
        const UINT index = (command - kFirstModelCommand) / kModelsPerAgent;
        const UINT model = (command - kFirstModelCommand) % kModelsPerAgent;
        if (index >= agents.size()) {
            return;
        }
        agent.SelectAgent(static_cast<int>(index));
        agent.SelectModel(model == 0U ? std::string{} : agents[index].models[model - 1U]);
    } else if (command >= kFirstEffortCommand) {
        const EditorAgent* current = agent.Current();
        const UINT index = command - kFirstEffortCommand;
        if (current != nullptr && index <= current->efforts.size()) {
            agent.SelectEffort(index == 0U ? std::string{} : current->efforts[index - 1U]);
        }
    } else if (command == kRefreshCommand) {
        agent.RefreshAgents();
    } else if (command == kEditAgentsCommand && EnsureEditorAgentsFile()) {
        ShellExecuteW(owner, L"open", L"notepad.exe", EditorAgentsFile().wstring().c_str(), nullptr, SW_SHOWNORMAL);
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
    // The agent works on the files: every unsaved edit (the scene and any open asset editor) goes to
    // disk first, so it sees what you see and an editor never writes an older copy over its work.
    if (!sceneContext_.CurrentScenePath().empty() && !sceneContext_.SaveOpenDocuments()) {
        agent.Add(EditorAgentEntryKind::Error, "Your unsaved changes could not be saved, so the request was not sent. Save them, then send again.");
        return false;
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
        agent.SetInput({});
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
        // Esc closes the bubble's picker first, then lets go of the keyboard.
        if (agent.ModelMenuOpen()) {
            agent.SetModelMenuOpen(false);
        } else {
            agent.FocusInput(false);
        }
        return true;
    default:
        // Plain keys belong to the request box; Ctrl shortcuts (Ctrl+S) still reach the editor.
        return !ControlDown();
    }
}

} // namespace kb::editor

#endif
