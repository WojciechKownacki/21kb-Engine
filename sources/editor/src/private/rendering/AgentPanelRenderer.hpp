#pragma once

#include "agent/EditorAgentPanelState.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <string>

namespace kb::editor {

#if defined(_WIN32)

enum class AgentPanelHitKind : std::uint8_t {
    None,
    AgentMenu,     // the agent and profile picker
    SignInHint,    // the "not signed in" note: copies the sign-in command
    ConnectTools,  // registers the engine's tools with an agent that needs it once
    NewConversation,
    Input,
    Send,          // Send, or Stop while the agent works
};

struct AgentPanelLayout {
    RECT header{};
    RECT agentMenu{};
    RECT status{};
    RECT connectButton{};
    RECT newButton{};
    RECT conversation{};
    RECT input{};
    RECT sendButton{};
};

class AgentPanelRenderer final {
public:
    AgentPanelRenderer() = delete;

    [[nodiscard]] static AgentPanelLayout Resolve(const RECT& content, const EditorAgentPanelState& state);
    static void Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorAgentPanelState& state);
    [[nodiscard]] static AgentPanelHitKind HitTest(const RECT& content, const EditorAgentPanelState& state, int x, int y);
    // How far the conversation can scroll up: its height beyond the visible area.
    [[nodiscard]] static int MaximumScroll(const RECT& content, const EditorAgentPanelState& state);
    // The command that signs the selected profile in, for the terminal.
    [[nodiscard]] static std::string SignInCommand(const EditorAgentPanelState& state);
};

#endif

} // namespace kb::editor
