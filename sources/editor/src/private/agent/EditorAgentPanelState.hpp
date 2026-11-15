#pragma once

#include "agent/EditorAgentCatalog.hpp"
#include "agent/EditorAgentProcess.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

enum class EditorAgentEntryKind : std::uint8_t {
    User,   // what was asked
    Agent,  // what the agent answered
    Tool,   // a tool the agent used (an engine command, a file edit)
    Info,   // the panel's own notes: finished, scene reloaded
    Error,
};

// How much the agent may do with a request: change the project, only say how it would, or only answer.
enum class EditorAgentMode : std::uint8_t {
    Agent,
    Plan,
    Ask,
};

struct EditorAgentEntry {
    EditorAgentEntryKind kind = EditorAgentEntryKind::Info;
    std::string text;
};

// Where the agent works: the project folder and the kb_cli next to the editor, whose MCP server
// gives the agent the engine's commands.
struct EditorAgentWorkspace {
    std::filesystem::path project;
    std::filesystem::path kbcli;
};

// What a conversation has used so far, as the agent reports it (zero while unknown).
struct EditorAgentUsage {
    std::uint64_t inputTokens = 0U;
    std::uint64_t outputTokens = 0U;
    // The last request's prompt as the model saw it, against the model's whole context window.
    std::uint64_t contextTokens = 0U;
    std::uint64_t contextWindow = 0U;
    double costUsd = 0.0;
};

// One conversation tab: its own history, draft, agent session and request in progress, so
// several can run side by side.
struct EditorAgentConversation {
    std::string title;
    std::vector<EditorAgentEntry> entries;
    std::string draft;
    // The agent's own conversation id, so the next request continues it, and whose it is.
    std::string session;
    std::string sessionAgent;
    std::unique_ptr<EditorAgentProcess> run;
    EditorAgentOutputFormat runFormat = EditorAgentOutputFormat::PlainText;
    // The agent, model and reasoning level a running agent was started with, and whether its
    // current turn has ended (an agent that stays running reports that instead of exiting).
    std::string runKey;
    bool turnEnded = false;
    bool running = false;
    bool requestRun = false;
    bool sawError = false;
    std::chrono::steady_clock::time_point started{};
    // The project's files when the request went out: how many, and the newest write.
    std::filesystem::path project;
    std::uintmax_t projectFiles = 0U;
    std::filesystem::file_time_type projectNewest{};
    EditorAgentUsage usage;
    int scrollFromBottom = 0;
};

// The Agent panel: which agent, model and reasoning level answer, whether that profile is
// signed in, and the conversation tabs.
class EditorAgentPanelState final {
public:
    EditorAgentPanelState();
    ~EditorAgentPanelState();

    // The agents found on this machine, found again on demand (after installing one or editing
    // the custom list). The choice is remembered between editor sessions.
    [[nodiscard]] const std::vector<EditorAgent>& Agents() const noexcept { return agents_; }
    [[nodiscard]] const EditorAgent* Current() const noexcept;
    [[nodiscard]] int CurrentIndex() const noexcept { return current_; }
    void RefreshAgents();
    void SelectAgent(int index);
    // Finds the agents the first time the panel is shown, so an editor that never opens the
    // panel (or a headless run) never looks for or starts one.
    void Prepare();

    // The chosen model and reasoning level of the current agent; empty means its default.
    [[nodiscard]] const std::string& Model() const noexcept { return model_; }
    [[nodiscard]] const std::string& Effort() const noexcept { return effort_; }
    void SelectModel(std::string model);
    void SelectEffort(std::string effort);
    [[nodiscard]] EditorAgentMode Mode() const noexcept { return mode_; }
    void SelectMode(EditorAgentMode mode) noexcept { mode_ = mode; }

    // Whether the selected profile is signed in, and its plan; never the account's address, so
    // the panel can be shown on a stream or a screenshot.
    [[nodiscard]] const std::string& Plan() const noexcept { return plan_; }
    [[nodiscard]] bool SignedIn() const noexcept { return signedIn_; }
    [[nodiscard]] bool CheckingAccount() const noexcept { return checkingAccount_; }

    // Conversation tabs. There is always at least one.
    [[nodiscard]] int ConversationCount() const noexcept { return static_cast<int>(conversations_.size()); }
    [[nodiscard]] int ActiveConversation() const noexcept { return active_; }
    [[nodiscard]] const EditorAgentConversation& Conversation(int index) const noexcept;
    [[nodiscard]] const EditorAgentConversation& Active() const noexcept { return Conversation(active_); }
    void NewConversation();
    void SelectConversation(int index);
    void CloseConversation(int index);

    // The active tab's draft, as UTF-8; WM_CHAR delivers UTF-16, surrogate pairs included.
    [[nodiscard]] const std::string& Input() const noexcept { return Active().draft; }
    [[nodiscard]] bool InputFocused() const noexcept { return inputFocused_; }
    void FocusInput(bool focused) noexcept;
    void TypeCharacter(wchar_t character);
    void InsertText(std::string_view text);
    void SetInput(std::string text);
    void Backspace();

    [[nodiscard]] bool Running() const noexcept { return Active().running; }
    [[nodiscard]] bool AnyRunning() const noexcept;
    // Sends the active tab's draft to the selected agent; leaves a note in the tab when it
    // cannot (no agent, a request still running).
    bool Send(const EditorAgentWorkspace& workspace);
    void Stop();
    // Registers the engine's tools with an agent that needs it once (connectArguments).
    bool ConnectEngineTools(const EditorAgentWorkspace& workspace);

    // Moves what the agents wrote since the last call into their tabs. Returns whether the
    // panel changed; `projectChanged` turns true when a request that changed the project's files
    // just ended, so the editor reloads them.
    bool Pump(bool& projectChanged);

    // Adds a line of the panel's own to the active tab (a note, an error).
    void Add(EditorAgentEntryKind kind, std::string text);

    void ScrollBy(int pixels, int maximum) noexcept;

    // The agent bar over the Scene View: whether its conversation is unfolded above it, and
    // whether the one-line latest answer was waved away (it comes back with the next request).
    [[nodiscard]] bool BarExpanded() const noexcept { return barExpanded_; }
    void SetBarExpanded(bool expanded) noexcept { barExpanded_ = expanded; }
    [[nodiscard]] bool AnswerDismissed() const noexcept { return answerDismissed_; }
    // The bubble's model picker: open or not, and the agent whose models it lists.
    [[nodiscard]] bool ModelMenuOpen() const noexcept { return modelMenuOpen_; }
    void SetModelMenuOpen(bool open) noexcept { modelMenuOpen_ = open; menuAgent_ = current_; }
    [[nodiscard]] int MenuAgent() const noexcept { return menuAgent_; }
    void SetMenuAgent(int index) noexcept { menuAgent_ = index; }
    void DismissAnswer() noexcept { answerDismissed_ = true; }
    // Moving the conversation between the bar and the docked Agent panel; the editor's loop
    // carries the request out, since only it owns the dock.
    enum class DockRequest : std::uint8_t { None, Dock, Undock };
    void RequestDock(DockRequest request) noexcept { dockRequest_ = request; }
    [[nodiscard]] DockRequest TakeDockRequest() noexcept { const DockRequest request = dockRequest_; dockRequest_ = DockRequest::None; return request; }

private:
    [[nodiscard]] EditorAgentConversation& ActiveMutable() noexcept { return *conversations_[static_cast<std::size_t>(active_)]; }
    void ReadLine(EditorAgentConversation& conversation, const std::string& line);
    void ReadClaudeEvent(EditorAgentConversation& conversation, const std::string& line);
    void ReadCodexEvent(EditorAgentConversation& conversation, const std::string& line);
    void FinishAccountCheck();
    void StartAccountCheck();
    void RememberSelection() const;

    std::vector<EditorAgent> agents_;
    bool prepared_ = false;
    int current_ = -1;
    std::string model_;
    std::string effort_;
    EditorAgentMode mode_ = EditorAgentMode::Agent;
    std::string plan_;
    bool signedIn_ = false;
    bool checkingAccount_ = false;
    std::vector<std::string> accountOutput_;
    std::unique_ptr<EditorAgentProcess> accountCheck_;

    std::vector<std::unique_ptr<EditorAgentConversation>> conversations_;
    int active_ = 0;
    bool inputFocused_ = false;
    wchar_t pendingHighSurrogate_ = 0;
    bool barExpanded_ = false;
    bool answerDismissed_ = false;
    bool modelMenuOpen_ = false;
    int menuAgent_ = -1;
    DockRequest dockRequest_ = DockRequest::None;
};

} // namespace kb::editor
