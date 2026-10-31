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
    SignInHint,    // the "not signed in" note: copies the sign-in command
    ConnectTools,  // registers the engine's tools with an agent that needs it once
    Suggestion,    // an example request on an empty conversation (index)
    Input,
    Send,          // Send, or Stop while the agent works
};

struct AgentPanelHit {
    AgentPanelHitKind kind = AgentPanelHitKind::None;
    int index = -1;
};

struct AgentPanelLayout {
    RECT tabStrip{};
    std::vector<RECT> tabs;
    std::vector<RECT> tabCloses;
    RECT newTab{};
    RECT undock{};
    RECT toolbar{};
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

    // The active conversation alone, newest at the bottom of `area`, for the agent bar's
    // unfolded view; the same entries the panel shows.
    static void PaintConversation(HDC dc, const RECT& area, const EditorTheme& theme, const EditorAgentPanelState& state);
    [[nodiscard]] static int ConversationMaximumScroll(const RECT& area, const EditorAgentPanelState& state);
    // "12.4k" for a token count.
    [[nodiscard]] static std::string TokenCount(std::uint64_t count);
};

#endif

} // namespace kb::editor
