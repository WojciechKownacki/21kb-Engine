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

// The Agent panel: which agent and profile answer, who that profile is signed in as, the
// conversation, the text being typed, and the request in progress.
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

    // Who the selected profile is signed in as: empty while being checked.
    [[nodiscard]] const std::string& Account() const noexcept { return account_; }
    [[nodiscard]] bool SignedIn() const noexcept { return signedIn_; }
    [[nodiscard]] bool CheckingAccount() const noexcept { return checkingAccount_; }

    [[nodiscard]] const std::vector<EditorAgentEntry>& Entries() const noexcept { return entries_; }
    void NewConversation();

    // The request being typed, as UTF-8; WM_CHAR delivers UTF-16, surrogate pairs included.
    [[nodiscard]] const std::string& Input() const noexcept { return input_; }
    [[nodiscard]] bool InputFocused() const noexcept { return inputFocused_; }
    void FocusInput(bool focused) noexcept;
    void TypeCharacter(wchar_t character);
    void InsertText(std::string_view text);
    void Backspace();

    [[nodiscard]] bool Running() const noexcept;
    // Sends the typed request to the selected agent. Fails, leaving a note in the conversation,
    // when there is no agent, no text or a request is still running.
    bool Send(const EditorAgentWorkspace& workspace);
    void Stop();
    // Registers the engine's tools with an agent that needs it once (connectArguments).
    bool ConnectEngineTools(const EditorAgentWorkspace& workspace);

    // Moves what the agent wrote since the last call into the conversation. Returns whether the
    // panel changed; `finished` turns true on the call that sees a request end.
    bool Pump(bool& finished);
    // Whether the request that just ended changed the project's files (so the editor reloads them).
    [[nodiscard]] bool ChangedProject() const noexcept { return changedProject_; }

    // How far the conversation is scrolled up from its newest line, in pixels.
    [[nodiscard]] int ScrollFromBottom() const noexcept { return scrollFromBottom_; }
    void ScrollBy(int pixels, int maximum) noexcept;

    // Adds a line of the panel's own to the conversation (a note, an error).
    void Add(EditorAgentEntryKind kind, std::string text);

private:
    void ReadLine(const std::string& line);
    void ReadClaudeEvent(const std::string& line);
    void ReadCodexEvent(const std::string& line);
    void FinishAccountCheck();
    void StartAccountCheck();
    void RememberSelection() const;

    std::vector<EditorAgent> agents_;
    bool prepared_ = false;
    int current_ = -1;
    std::string account_;
    bool signedIn_ = false;
    bool checkingAccount_ = false;
    std::vector<std::string> accountOutput_;
    std::unique_ptr<EditorAgentProcess> accountCheck_;

    std::vector<EditorAgentEntry> entries_;
    std::string input_;
    bool inputFocused_ = false;
    wchar_t pendingHighSurrogate_ = 0;

    std::unique_ptr<EditorAgentProcess> run_;
    bool running_ = false;
    // Whether the run in progress is a request (whose end reloads the scene) or the one-off
    // registration of the engine's tools.
    bool requestRun_ = false;
    bool sawError_ = false;
    // The agent's own conversation id, so the next request continues it (Claude Code).
    std::string session_;
    std::string sessionAgent_;
    std::chrono::steady_clock::time_point started_{};
    // The project's files as they were when the request was sent: how many, and the newest write.
    std::filesystem::path project_;
    std::uintmax_t projectFiles_ = 0U;
    std::filesystem::file_time_type projectNewest_{};
    bool changedProject_ = false;
    int scrollFromBottom_ = 0;
};

} // namespace kb::editor
