#pragma once

#include "agent/EditorAgentCatalog.hpp"
#include "agent/EditorAgentGuidance.hpp"
#include "agent/EditorAgentPlan.hpp"
#include "agent/EditorAgentPrices.hpp"
#include "agent/EditorAgentProcess.hpp"
#include "engine/project/AgentUndoJournal.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
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
    Image,  // a picture the agent made (a capture of the scene); `image` is its file
};

// Where a tool call stands: running until its result arrives.
enum class EditorAgentStepState : std::uint8_t {
    Running,
    Done,
    Failed,
};

// A file of the project the agent's request added or changed, for the marks in Project Files.
enum class EditorAgentFileChange : std::uint8_t {
    Added,
    Changed,
};

// One file the latest request changed, in the review under the conversation.
struct EditorAgentReviewRow {
    std::string path;
    kb::project::AgentChangeKind kind = kb::project::AgentChangeKind::Modified;
    // False for a file too large to keep a copy of: it is listed, an undo leaves it.
    bool restorable = true;
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
    // A tool: the agent's id of the call and how it stands.
    std::string toolId;
    EditorAgentStepState step = EditorAgentStepState::Done;
    // An image entry's file; a request's attached pictures.
    std::filesystem::path image;
    std::vector<std::filesystem::path> images;
};

// Where the agent works: the project folder and the kb_cli next to the editor, whose MCP server
// gives the agent the engine's commands.
struct EditorAgentWorkspace {
    std::filesystem::path project;
    std::filesystem::path kbcli;
    // The engine's own folders the agent may not read: all of them but its documentation and the SDK headers. Empty when the
    // engine's sources are not beside the editor.
    std::vector<std::filesystem::path> hiddenFolders;
    // What the person has open in the editor (one line), sent with each request.
    std::string context;
};

// What a conversation has used so far, as the agent reports it (zero while unknown).
struct EditorAgentUsage {
    // Everything the model read: new input, input taken from the agent's cache, and input written to that cache.
    std::uint64_t inputTokens = 0U;
    std::uint64_t cacheReadTokens = 0U;
    std::uint64_t cacheWriteTokens = 0U;
    std::uint64_t outputTokens = 0U;
    // The last request's prompt as the model saw it, against the model's whole context window.
    std::uint64_t contextTokens = 0U;
    std::uint64_t contextWindow = 0U;
    // The cost the agent program itself reports (in USD), and the cost by the user's price table (in its currency), which
    // counts only models that have a price.
    double programCost = 0.0;
    double pricedCost = 0.0;
    std::string currency;
    bool unpriced = false;

    // Input the model read fresh, not from the cache and not written to it.
    [[nodiscard]] std::uint64_t UncachedInput() const noexcept {
        return inputTokens > cacheReadTokens + cacheWriteTokens ? inputTokens - cacheReadTokens - cacheWriteTokens : 0U;
    }
    // What it cost: by the price table when every model used has a price, else the program's own figure; false when neither is known.
    [[nodiscard]] bool Money(double& amount, std::string& currencyName) const {
        if (!unpriced && pricedCost > 0.0) {
            amount = pricedCost;
            currencyName = currency;
            return true;
        }
        amount = programCost;
        currencyName = "USD";
        return programCost > 0.0;
    }
};

// What the agent program reported in total for one model since its process started, so a turn adds only the part that is new.
struct EditorAgentModelTotals {
    EditorAgentTokens tokens;
    double programCost = 0.0;
};

// A bubble that stops the person to decide: the agent got stuck, or a plan waits to be run. Plain words only.
enum class EditorAgentCardKind : std::uint8_t {
    Stuck,
    Plan,
    Advice, // a suggestion (another model, a plan, a fresh conversation): the person decides, nothing changes by itself
};

enum class EditorAgentCardAction : std::uint8_t {
    ContinueStronger, // the same conversation goes on with a stronger model
    SplitIntoSteps,
    TryAgain,
    Stop,
    RunPlan,
    CancelPlan,
    AcceptAdvice,
    DismissAdvice,
    ChooseAgent, // the remembered agent is gone: this one (value: its index)
    ChooseModel, // the remembered model is gone: this one (value: its name, empty for the program's own)
};

struct EditorAgentCardButton {
    std::string label;
    EditorAgentCardAction action = EditorAgentCardAction::Stop;
    std::string value;
};

struct EditorAgentCard {
    EditorAgentCardKind kind = EditorAgentCardKind::Stuck;
    std::string title;
    std::string body;
    std::vector<EditorAgentCardButton> buttons;
};

// A quiet suggestion over the request box, one at a time, that does nothing unless it is clicked.
enum class EditorAgentHintAction : std::uint8_t {
    UseModel,    // the model that suits the request
    PlanInSteps, // a big request, split into small steps
    StartFresh,  // a new conversation with a one-line summary of this one
};

struct EditorAgentHint {
    EditorAgentHintAction action = EditorAgentHintAction::UseModel;
    std::string text;
    std::string label;
    std::string model;
};

// One conversation tab: its own history, draft, agent session and request in progress, so
// several can run side by side.
struct EditorAgentConversation {
    // The name of its history file under Saved/Agent/Conversations.
    std::string id;
    std::string title;
    std::vector<EditorAgentEntry> entries;
    std::string draft;
    // The draft's caret and the other end of its selection, as byte offsets on character
    // boundaries, and earlier drafts for Ctrl+Z / Ctrl+Y.
    std::size_t caret = 0U;
    std::size_t anchor = 0U;
    std::vector<std::string> undo;
    std::vector<std::string> redo;
    // Pictures pasted or chosen for the next request.
    std::vector<std::filesystem::path> attachments;
    // The agent's own conversation id, so the next request continues it, and whose it is.
    std::string session;
    std::string sessionAgent;
    std::unique_ptr<EditorAgentProcess> run;
    EditorAgentOutputFormat runFormat = EditorAgentOutputFormat::PlainText;
    // The request in progress or last made: what the person asked (for splitting it or trying it again), how big it looked, and
    // the loop guard that watches it. `paused` is set while the stuck bubble waits for a decision.
    std::string goal;
    EditorAgentRequestSize runSize = EditorAgentRequestSize::Feature;
    EditorAgentLoopGuard guard;
    bool paused = false;
    EditorAgentStuck pausedBy = EditorAgentStuck::None;
    // The model the running request was started with, when it was not the panel's choice (a plan's step).
    std::string runModel;
    // A big request split into steps, and the one-line summary of an earlier conversation this one starts from.
    EditorAgentPlan plan;
    std::string carried;
    // The agent, model and reasoning level a running agent was started with, and whether its
    // current turn has ended (an agent that stays running reports that instead of exiting).
    std::string runKey;
    bool turnEnded = false;
    bool running = false;
    bool requestRun = false;
    bool sawError = false;
    std::chrono::steady_clock::time_point started{};
    // The project's files (and when each was written) as the last finished tool left them, so each
    // step that changes something is shown at once.
    std::filesystem::path project;
    std::map<std::filesystem::path, std::filesystem::file_time_type> projectFiles;
    std::filesystem::file_time_type requestStart{};
    EditorAgentUsage usage;
    // What the running agent process has reported in total, by model: some agents' result totals count every turn
    // of their process, so each turn adds only the part that is new. Starts again with a new process.
    std::map<std::string, EditorAgentModelTotals> processModels;
    // The tokens each step of the current request has used so far, by the agent's id of the step, so a
    // request can be held to a budget while it runs (the totals above arrive only when it ends). Only an
    // agent that reports its steps' usage fills it.
    std::map<std::string, std::uint64_t> stepTokens;
    [[nodiscard]] std::uint64_t LiveTokens() const noexcept {
        std::uint64_t total = 0U;
        for (const auto& step : stepTokens) total += step.second;
        return total;
    }
    int scrollFromBottom = 0;
    bool saveDue = false;
    std::chrono::steady_clock::time_point savedAt{};
};

// A conversation kept on disk, for the history menu.
struct EditorAgentSavedConversation {
    std::string id;
    std::string title;
    bool open = false;
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
    // The model (or agent) the person used last is not offered any more: nothing starts until they choose another.
    [[nodiscard]] bool ModelUnavailable() const;
    // The agent's program gave no list of models, so it works with its own.
    [[nodiscard]] bool NoModelList() const;
    [[nodiscard]] const std::string& Effort() const noexcept { return effort_; }
    void SelectModel(std::string model);
    void SelectEffort(std::string effort);
    // Whether a choice of agent, model or level is remembered for the next editor session; an
    // automated run turns it off so it leaves the user's own choice alone.
    void SetRememberSelection(bool remember) noexcept { rememberSelection_ = remember; }
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

    // The active tab's draft, as UTF-8; WM_CHAR delivers UTF-16, surrogate pairs included. Typing
    // and pasting replace the selection at the caret, as in any text box.
    [[nodiscard]] const std::string& Input() const noexcept { return Active().draft; }
    [[nodiscard]] bool InputFocused() const noexcept { return inputFocused_; }
    void FocusInput(bool focused) noexcept;
    void TypeCharacter(wchar_t character);
    void InsertText(std::string_view text);
    void SetInput(std::string text);
    void Backspace(bool word = false);
    void Delete(bool word = false);
    [[nodiscard]] std::size_t Caret() const noexcept { return Active().caret; }
    [[nodiscard]] std::size_t SelectionStart() const noexcept { return std::min(Active().caret, Active().anchor); }
    [[nodiscard]] std::size_t SelectionEnd() const noexcept { return std::max(Active().caret, Active().anchor); }
    [[nodiscard]] std::string SelectedText() const;
    enum class CaretMove : std::uint8_t { Left, Right, WordLeft, WordRight, LineStart, LineEnd, Up, Down, Start, End };
    void MoveCaret(CaretMove move, bool extend);
    // Puts the caret at a byte offset (from a click), keeping the selection's other end when extending.
    void PlaceCaret(std::size_t offset, bool extend);
    void SelectAll() noexcept;
    void Undo();
    void Redo();

    // Pictures for the next request: pasted screenshots and chosen images. They go to the agent as
    // files it opens itself, so every agent that reads images sees them.
    [[nodiscard]] const std::vector<std::filesystem::path>& Attachments() const noexcept { return Active().attachments; }
    void Attach(std::filesystem::path image);
    void RemoveAttachment(std::size_t index);

    // Conversations are kept in the project (Saved/Agent/Conversations) as they go, so nothing
    // is lost when the editor closes; the open tabs come back with the project.
    [[nodiscard]] std::vector<EditorAgentSavedConversation> SavedConversations() const;
    void OpenSaved(const std::string& id);

    // The files the latest request of any tab added or changed, by full path.
    [[nodiscard]] const std::map<std::filesystem::path, EditorAgentFileChange>& ChangedFiles() const noexcept { return changedFiles_; }

    // What the latest request changed in the project's files, kept in Saved/Agent/Undo so it also survives a restart: the files
    // changed, added and deleted since the request began, until the person keeps them or undoes them (all, or one file).
    [[nodiscard]] const std::vector<EditorAgentReviewRow>& Review() const noexcept { return review_; }
    // What sending a request does first, and what the end of a request does, as steps of their own (a headless scenario
    // stands in for the agent between them): the project's files are recorded, then the changes since are read into the review.
    void BeginUndoPointNow(const std::string& label);
    // A record with nothing changed is closed when `closeWhenEmpty` (the end of a request), else it stays for what comes next.
    void RefreshReview(bool closeWhenEmpty = false);
    void KeepChanges();
    // Puts back every changed file (`row` < 0) or the one in that row of the review, exactly as it was, deleted files included.
    void UndoChanges(int row = -1);
    // Whether the project's files were last reloaded because of an undo (the note after the reload differs); read once.
    [[nodiscard]] bool TakeUndoReload() noexcept { const bool value = reloadFromUndo_; reloadFromUndo_ = false; return value; }

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

    // The bubble that waits for a decision in the active tab - the agent got stuck, or a plan waits to be run - and the button
    // the keyboard is on (Tab moves it, Enter presses it; -1 for none). Pressing a button carries its action out.
    [[nodiscard]] std::optional<EditorAgentCard> Card() const;
    [[nodiscard]] int CardFocus() const noexcept { return cardFocus_; }
    void MoveCardFocus(int step);
    void ClearCardFocus() noexcept { cardFocus_ = -1; }
    bool PressCard(int button, const EditorAgentWorkspace& workspace);
    // The active tab's plan: its steps can be switched off, moved, and have their text changed (the text goes to the request
    // box, and sending saves it into the step).
    [[nodiscard]] const EditorAgentPlan& StepPlan() const noexcept { return Active().plan; }
    void TogglePlanStep(int row);
    void MovePlanStep(int row, int step);
    void EditPlanStep(int row);
    // The one suggestion for what is typed or for the conversation so far; nothing happens until it is pressed.
    [[nodiscard]] std::optional<EditorAgentHint> Hint() const;
    bool PressHint(const EditorAgentWorkspace& workspace);
    // The user's price table, read with the agents.
    [[nodiscard]] const EditorAgentPrices& Prices() const noexcept { return prices_; }

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
    void Remember(EditorAgentConversation& conversation);
    void ReplaceSelection(std::string_view text, bool typing = false);
    void UseProject(const std::filesystem::path& project);
    // Every session an agent keeps a record of for this project that the history does not have yet
    // goes into it (closed, in the conversations menu), so no conversation is lost, whatever ended it.
    void ImportAgentSessions();
    void Save(EditorAgentConversation& conversation);
    [[nodiscard]] std::filesystem::path HistoryFolder() const;
    // Compares the project's files with the conversation's last look, notes what was added or
    // changed and returns whether anything was.
    bool TakeProjectChanges(EditorAgentConversation& conversation);
    // Records the project's files before a request, so its changes can be undone (a request that starts while another still
    // works shares the first one's record). Notes in the conversation when a record cannot be made.
    void BeginUndoPoint(EditorAgentConversation& conversation, const std::filesystem::path& project, const std::string& request);
    void FinishStep(EditorAgentConversation& conversation, const std::string& toolId, bool failed, std::string_view result);
    void ReadLine(EditorAgentConversation& conversation, const std::string& line);
    void ReadStreamJsonEvent(EditorAgentConversation& conversation, const std::string& line);
    void ReadCodexEvent(EditorAgentConversation& conversation, const std::string& line);

    // One request to the agent, whoever asks: the person's own, a plan, a plan's step, a continuation.
    struct Launch {
        // What the conversation shows as the person's request; empty when the panel's own note says what runs.
        std::string shown;
        // What the agent is told, after the mode and what the editor knows (`withContext`).
        std::string text;
        std::vector<std::filesystem::path> images;
        bool withContext = true;
        // A model that is not the panel's choice (a plan's step on a light model, a stronger one for a stuck request).
        std::optional<std::string> model;
        std::optional<EditorAgentMode> mode;
        // False for a request that goes on from an earlier one: its changes stay in the same undo point.
        bool newUndoPoint = true;
        // False starts a conversation of the agent's own, with none of the earlier history.
        bool resume = true;
        // The request was the person's draft: the box is emptied and its pictures are used.
        bool fromDraft = false;
        EditorAgentRequestSize size = EditorAgentRequestSize::Feature;
    };
    void AddRequest(EditorAgentConversation& conversation, const Launch& launch, const std::filesystem::path& project);
    bool Start(EditorAgentConversation& conversation, const EditorAgentWorkspace& workspace, const Launch& launch);
    // The end of a turn, whether the agent finished or was stopped: steps left running are closed, the project's changes read.
    void EndTurn(EditorAgentConversation& conversation, int code, bool cut);
    // What follows a turn that ended by itself: a plan read, a step judged and the next one begun.
    void AfterTurn(EditorAgentConversation& conversation);
    // The agent is looping: it is stopped and the person is asked what to do; nothing goes on by itself.
    void PauseStuck(EditorAgentConversation& conversation);
    // A request that stopped for `reason` waits for the person's decision (a plan's step is marked as stuck).
    void Pause(EditorAgentConversation& conversation, EditorAgentStuck reason);
    // The model a plan's step runs on, and the one a stuck step could go on with; empty when there is no such model.
    [[nodiscard]] std::string StepModel(const EditorAgentPlanStep& step) const;
    [[nodiscard]] std::string StrongerStepModel(const EditorAgentPlanStep& step) const;
    [[nodiscard]] bool StepIsStuck(const EditorAgentConversation& conversation) const;
    // Asks the strongest model for the steps of `goal`; `fromDraft` when the person just typed it.
    void StartPlan(EditorAgentConversation& conversation, const EditorAgentWorkspace& workspace, std::string goal, bool fromDraft);
    void RunPlanStep(EditorAgentConversation& conversation, const EditorAgentWorkspace& workspace, bool retry);
    [[nodiscard]] std::string Strongest() const;
    void AddUsage(EditorAgentConversation& conversation, const std::string& model, const EditorAgentTokens& delta, double programCost);
    [[nodiscard]] std::string LastAnswer(const EditorAgentConversation& conversation, std::size_t from) const;
    // The model a conversation's request runs on: the panel's choice, its name for the person.
    [[nodiscard]] std::string ModelName(const EditorAgentConversation& conversation) const;
    void FinishAccountCheck();
    void StartAccountCheck();
    // While the profile is not signed in, its status is asked again now and then, so signing in
    // in the window the panel opened shows here by itself.
    std::chrono::steady_clock::time_point accountCheckedAt_{};
    void RememberSelection() const;

    std::vector<EditorAgent> agents_;
    EditorAgentPrices prices_;
    // The workspace of the last request, for the requests the panel makes by itself (the next step of a plan).
    EditorAgentWorkspace workspace_;
    int cardFocus_ = -1;
    mutable std::string hintKey_;
    mutable std::optional<EditorAgentHint> hint_;
    // The suggestion the person answered "stay" to; it does not come back until the next request.
    std::string dismissedAdvice_;
    // The agent the person used last, when it is not installed any more.
    std::string missingAgent_;
    [[nodiscard]] std::optional<EditorAgentHint> ComputedHint() const;
    [[nodiscard]] std::string AdviceKey(const EditorAgentHint& hint) const;
    bool prepared_ = false;
    int current_ = -1;
    std::string model_;
    std::string effort_;
    bool rememberSelection_ = true;
    EditorAgentMode mode_ = EditorAgentMode::Agent;
    std::string plan_;
    bool signedIn_ = false;
    bool checkingAccount_ = false;
    std::vector<std::string> accountOutput_;
    std::unique_ptr<EditorAgentProcess> accountCheck_;

    std::vector<std::unique_ptr<EditorAgentConversation>> conversations_;
    int active_ = 0;
    std::filesystem::path historyProject_;
    std::filesystem::path importedProject_;
    std::map<std::filesystem::path, EditorAgentFileChange> changedFiles_;
    std::unique_ptr<kb::project::AgentUndoJournal> journal_;
    std::vector<EditorAgentReviewRow> review_;
    bool reviewDue_ = false;
    bool reloadFromUndo_ = false;
    bool inputFocused_ = false;
    wchar_t pendingHighSurrogate_ = 0;
    bool typingRun_ = false;
    bool projectChangedNow_ = false;
    // The pictures a running tool call names, shown once it succeeds.
    std::map<std::string, std::vector<std::filesystem::path>> pendingImages_;
    bool barExpanded_ = false;
    bool answerDismissed_ = false;
    bool modelMenuOpen_ = false;
    int menuAgent_ = -1;
    DockRequest dockRequest_ = DockRequest::None;
};

} // namespace kb::editor
