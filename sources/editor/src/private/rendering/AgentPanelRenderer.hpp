#pragma once

#include "agent/EditorAgentPanelState.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

#if defined(_WIN32)

enum class AgentPanelHitKind : std::uint8_t {
    None,
    Tab,           // a conversation tab (index)
    TabClose,      // its close button (index)
    NewTab,
    Undock,        // moves the conversation back to the bar over the Scene View
    AgentMenu,     // the agent and profile picker
    ModelMenu,
    EffortMenu,    // the reasoning level
    ModeMenu,      // Agent, Plan or Ask
    Attach,        // adds a file to the request
    Chats,         // the open conversations
    Search,        // finds a conversation by what was said in it
    SignInHint,    // the "not signed in" note: copies the sign-in command
    ConnectTools,  // registers the engine's tools with an agent that needs it once
    Suggestion,    // an example request on an empty conversation (index)
    Input,
    Send,          // Send, or Stop while the agent works
    Attachment,    // a picture waiting for the next request (index): takes it off
    Picture,       // a picture in the conversation (index: entry, item: which of a request's pictures)
    ReviewKeep,    // keeps the files the request changed
    ReviewUndoAll, // puts them all back
    ReviewUndoFile, // puts one back (index: its row)
    CardButton,    // a button of the bubble that waits for a decision (index)
    PlanToggle,    // a plan step's check box (index)
    PlanEdit,      // changes a plan step's words (index)
    PlanUp,        // moves a plan step up (index)
    PlanDown,      // moves it down (index)
};

struct AgentPanelHit {
    AgentPanelHitKind kind = AgentPanelHitKind::None;
    int index = -1;
    int item = -1;
};

struct AgentPanelLayout {
    RECT tabStrip{};
    std::vector<RECT> tabs;
    std::vector<RECT> tabCloses;
    RECT newTab{};
    RECT undock{};
    RECT chats{};
    RECT search{};
    RECT toolbar{};
    RECT attach{};
    RECT modeMenu{};
    RECT agentMenu{};
    RECT modelMenu{};
    RECT effortMenu{};
    RECT status{};
    RECT usage{};
    RECT connectButton{};
    RECT conversation{};
    // The reading column inside the conversation: centred, never wider than a comfortable line.
    RECT column{};
    std::array<RECT, 3> suggestions{};
    RECT composer{};
    RECT input{};
    RECT sendButton{};
    // The pictures for the next request, over the composer.
    std::vector<RECT> attachments;
    // The files the latest request changed, between the conversation and the composer (empty when there are none).
    RECT review{};
    RECT reviewKeep{};
    RECT reviewUndoAll{};
    std::vector<RECT> reviewRows;
    std::vector<RECT> reviewRowUndo;
    // How many files the review does not list for lack of room.
    int reviewMore = 0;
    // The bubble that waits for a decision (a suggestion, the agent got stuck, a plan) with its buttons and, for a plan, one row
    // per step.
    RECT card{};
    std::vector<RECT> cardButtons;
    std::vector<RECT> planRows;
    std::vector<RECT> planChecks;
    std::vector<RECT> planEdits;
    std::vector<RECT> planUps;
    std::vector<RECT> planDowns;
    // How many plan steps the bubble does not list for lack of room.
    int planMore = 0;
};

class AgentPanelRenderer final {
public:
    AgentPanelRenderer() = delete;

    [[nodiscard]] static AgentPanelLayout Resolve(const RECT& content, const EditorAgentPanelState& state);
    static void Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorAgentPanelState& state);
    [[nodiscard]] static AgentPanelHit HitTest(const RECT& content, const EditorAgentPanelState& state, int x, int y);
    // How far the active conversation can scroll up: its height beyond the visible area.
    [[nodiscard]] static int MaximumScroll(const RECT& content, const EditorAgentPanelState& state);
    // The command that signs the selected profile in, for the terminal.
    [[nodiscard]] static std::string SignInCommand(const EditorAgentPanelState& state);
    // The example requests an empty conversation offers.
    [[nodiscard]] static std::string_view Suggestion(int index);
    // The draft's byte offset nearest a point in the request box, for a click.
    [[nodiscard]] static std::size_t InputOffset(const RECT& content, const EditorAgentPanelState& state, int x, int y);

    // The active conversation alone, newest at the bottom of `area`, for the agent bar's
    // unfolded view; the same entries the panel shows.
    static void PaintConversation(HDC dc, const RECT& area, const EditorTheme& theme, const EditorAgentPanelState& state);
    [[nodiscard]] static int ConversationMaximumScroll(const RECT& area, const EditorAgentPanelState& state);
    // "12.4k" for a token count.
    [[nodiscard]] static std::string TokenCount(std::uint64_t count);
    // "0.14 USD · in 3.1k · cached 40k · out 1.2k": what a conversation has cost and used, the kinds of tokens apart.
    [[nodiscard]] static std::string UsageSummary(const EditorAgentUsage& usage);
};

#endif

} // namespace kb::editor
