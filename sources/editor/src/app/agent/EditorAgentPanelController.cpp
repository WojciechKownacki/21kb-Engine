#include "app/agent/EditorAgentPanelController.hpp"

#if defined(_WIN32)
#include "app/EditorTextInputShortcuts.hpp"
#include "platform/win32/EditorFileDialogFilter.hpp"
#include "platform/win32/EditorTextEntryDialog.hpp"
#include "localization/EditorLocalization.hpp"
#include "project/EditorProjectPaths.hpp"
#include "rendering/AgentBarRenderer.hpp"
#include "rendering/AgentPanelRenderer.hpp"
#include "scene/EditorSceneContext.hpp"

#include <CommDlg.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <optional>
#include <string>
#include <vector>

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

[[nodiscard]] std::string Lowered(std::string_view text) {
    std::string lowered{ text };
    for (char& character : lowered) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return lowered;
}

[[nodiscard]] std::string ConversationLabel(const EditorAgentConversation& conversation) {
    return conversation.title.empty() ? std::string{ EditorText("agent.newConversation", "New conversation") } : conversation.title;
}

// A small popup of the given rows under `anchor`; the chosen row, or nothing.
[[nodiscard]] std::optional<std::size_t> PickFromMenu(HWND owner, const RECT& anchor, const std::vector<std::string>& rows, int checked) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < rows.size(); ++index) {
        AppendMenuW(menu, MF_STRING | (static_cast<int>(index) == checked ? MF_CHECKED : 0U), kFirstChoiceCommand + static_cast<UINT>(index), Widen(rows[index]).c_str());
    }
    POINT point{ anchor.left, anchor.bottom };
    ClientToScreen(owner, &point);
    const int chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, point.x, point.y, 0, owner, nullptr);
    DestroyMenu(menu);
    if (chosen < static_cast<int>(kFirstChoiceCommand)) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(chosen) - kFirstChoiceCommand;
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

void EditorAgentPanelController::ShowModeMenu(HWND owner, const RECT& anchor) {
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    const std::vector<std::string> rows{
        std::string{ EditorText("agent.mode.agentRow", "Agent: changes the project") },
        std::string{ EditorText("agent.mode.planRow", "Plan: only says what it would do") },
        std::string{ EditorText("agent.mode.askRow", "Ask: only answers") },
    };
    if (const std::optional<std::size_t> chosen = PickFromMenu(owner, anchor, rows, static_cast<int>(agent.Mode()))) {
        agent.SelectMode(static_cast<EditorAgentMode>(*chosen));
    }
}

void EditorAgentPanelController::AttachFile(HWND owner) {
    std::array<wchar_t, 32768> buffer{};
    const std::wstring filter = EditorFileDialogFilter({ { EditorText("fileDialog.allFiles", "All files (*.*)"), "*.*" } });
    const std::wstring title = Widen(EditorText("agent.attachTitle", "Attach a file to the request"));
    const std::wstring directory = EditorProjectPaths::AssetsRoot().wstring();
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrInitialDir = directory.c_str();
    dialog.lpstrTitle = title.c_str();
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog) == FALSE) {
        return;
    }
    // The agent works in the project folder, so a file inside it is named from there.
    std::filesystem::path file{ buffer.data() };
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::relative(file, EditorProjectPaths::ProjectRoot(), error);
    if (!error && !relative.empty() && *relative.begin() != "..") {
        file = relative;
    }
    const std::u8string text = file.generic_u8string();
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    agent.InsertText("\"" + std::string{ reinterpret_cast<const char*>(text.data()), text.size() } + "\" ");
    agent.FocusInput(true);
}

void EditorAgentPanelController::ShowConversations(HWND owner, const RECT& anchor) {
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    std::vector<std::string> rows;
    for (int index = 0; index < agent.ConversationCount(); ++index) {
        rows.push_back(ConversationLabel(agent.Conversation(index)));
    }
    if (const std::optional<std::size_t> chosen = PickFromMenu(owner, anchor, rows, agent.ActiveConversation())) {
        agent.SelectConversation(static_cast<int>(*chosen));
    }
}

void EditorAgentPanelController::SearchConversations(HWND owner, const RECT& anchor) {
    const std::optional<std::string> query = EditorTextEntryDialog::Show(owner, {
        .title = std::string{ EditorText("agent.searchTitle", "Find in Conversations") },
        .label = std::string{ EditorText("agent.searchLabel", "Text to find") },
        .hint = std::string{ EditorText("agent.searchHint", "Looks through what you and the agent wrote.") },
        .acceptLabel = std::string{ EditorText("agent.searchAccept", "Find") },
        .icon = HeroIconKind::MagnifyingGlass,
    });
    if (!query.has_value() || query->empty()) {
        return;
    }
    EditorAgentPanelState& agent = sceneContext_.AgentPanel();
    const std::string needle = Lowered(*query);
    std::vector<int> found;
    std::vector<std::string> rows;
    for (int index = 0; index < agent.ConversationCount(); ++index) {
        const EditorAgentConversation& conversation = agent.Conversation(index);
        bool matches = Lowered(conversation.title).find(needle) != std::string::npos;
        for (const EditorAgentEntry& entry : conversation.entries) {
            matches = matches || Lowered(entry.text).find(needle) != std::string::npos;
        }
        if (matches) {
            found.push_back(index);
            rows.push_back(ConversationLabel(conversation));
        }
    }
    if (found.empty()) {
        agent.Add(EditorAgentEntryKind::Info, EditorLocalization::Format("agent.searchNothing", "No conversation mentions \"{text}\".", { { "text", *query } }));
        return;
    }
    if (const std::optional<std::size_t> chosen = PickFromMenu(owner, anchor, rows, -1)) {
        agent.SelectConversation(found[*chosen]);
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
    case AgentPanelHitKind::ModeMenu:
        ShowModeMenu(owner, layout.modeMenu);
        return true;
    case AgentPanelHitKind::Attach:
        AttachFile(owner);
        return true;
    case AgentPanelHitKind::Chats:
        ShowConversations(owner, layout.chats);
        return true;
    case AgentPanelHitKind::Search:
        SearchConversations(owner, layout.search);
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
