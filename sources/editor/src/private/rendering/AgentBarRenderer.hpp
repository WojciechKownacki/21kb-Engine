#pragma once

#include "agent/EditorAgentPanelState.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

#if defined(_WIN32)

enum class AgentBarHitKind : std::uint8_t {
    None,
    Model,         // the model label: agents, their models and the reasoning level in one menu
    Input,
    Expand,        // unfolds or folds the conversation inside the bubble
    Send,          // Send, or Stop while the agent works
    Answer,        // the latest answer: unfolds the conversation
    AnswerClose,
    Tab,           // a conversation tab in the unfolded view (index)
    NewTab,
    CloseTab,
    Dock,          // moves the conversation into the docked Agent panel
    Conversation,
    MenuAgent,     // the model picker: an agent or account (index)
    MenuModel,     // a model of the listed agent (index; 0 is its default)
    MenuEffort,    // a reasoning level (index; 0 is the agent's default)
    MenuRefresh,
    MenuAddAgent,
    MenuPanel,     // the picker's background
};

struct AgentBarHit {
    AgentBarHitKind kind = AgentBarHitKind::None;
    int index = -1;
};

// Positions in the bubble's window, which leaves room around the bubble for its shadow.
struct AgentBarLayout {
    int windowWidth = 0;
    int windowHeight = 0;
    RECT bubble{};
    RECT line{};           // the request line at the bubble's bottom
    RECT model{};
    RECT input{};
    RECT ring{};
    RECT tokens{};
    RECT expand{};
    RECT send{};
    bool answerShown = false;
    RECT answer{};
    RECT answerClose{};
    bool expanded = false;
    std::vector<RECT> tabs;
    RECT newTab{};
    RECT closeTab{};
    RECT usage{};
    RECT dock{};
    RECT conversation{};
    // The model picker, above the bubble while it is open.
    bool menuShown = false;
    RECT menu{};
    std::vector<RECT> menuAgents;
    std::vector<RECT> menuModels;
    std::vector<RECT> menuEfforts;
    RECT menuRefresh{};
    RECT menuAddAgent{};
};

// The agent bubble over the bottom of the Scene View: one quiet rounded window holding the request
// line, and above it the latest answer or, unfolded, the conversation. Drawn antialiased into a
// per-pixel-alpha surface, with its shadow, so it sits on the scene like a native control.
class AgentBarRenderer final {
public:
    AgentBarRenderer() = delete;

    // The window's place over `sceneContent` (owner client coordinates), shadow margin included.
    [[nodiscard]] static RECT WindowRect(const RECT& sceneContent, const EditorAgentPanelState& state);
    [[nodiscard]] static AgentBarLayout Resolve(int bubbleWidth, int maximumBubbleHeight, const EditorAgentPanelState& state);
    // Draws into a top-down 32-bit premultiplied-alpha pixel buffer of the layout's window size.
    // `hover` is the pointer in window coordinates, for the picker's highlight.
    static void Paint(void* pixels, const AgentBarLayout& layout, const EditorAgentPanelState& state, POINT hover);
    [[nodiscard]] static AgentBarHit HitTest(const AgentBarLayout& layout, const EditorAgentPanelState& state, int x, int y);
    // An agent's reasoning level as the agent itself names it ("low", "high", "xhigh").
    [[nodiscard]] static std::string EffortLabel(std::string_view effort);
    // How far the unfolded conversation can scroll up.
    [[nodiscard]] static int MaximumScroll(const AgentBarLayout& layout, const EditorAgentPanelState& state);
};

#endif

} // namespace kb::editor
