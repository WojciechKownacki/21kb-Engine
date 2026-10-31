#include "rendering/AgentPanelRenderer.hpp"

#if defined(_WIN32)
#include "localization/EditorLocalization.hpp"
#include "platform/win32/Utf8Gdi.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconKind.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/components/EditorDialogStyle.hpp"
#include "rendering/gdi/EditorFontCache.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <algorithm>
#include <vector>

namespace kb::editor {
namespace {

constexpr int kPad = 12;
constexpr int kHeaderHeight = 44;
constexpr int kInputHeight = 64;
constexpr int kSendWidth = 96;
constexpr int kEntryGap = 10;
constexpr int kBubblePad = 10;
constexpr COLORREF kTeal = RGB(86, 172, 205);
constexpr COLORREF kDanger = RGB(232, 108, 108);
constexpr COLORREF kWarning = RGB(224, 165, 54);

[[nodiscard]] COLORREF Color(const EditorColor& color) noexcept {
    return RGB(color.r, color.g, color.b);
}

void FillRound(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int radius) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ previousBrush = SelectObject(dc, brush);
    HGDIOBJ previousPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, previousPen);
    SelectObject(dc, previousBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void Text(HDC dc, RECT rect, std::string_view text, COLORREF color, int pointSize, int weight, UINT format) {
    const ScopedFont font(pointSize, weight);
    const ScopedGdiObject selected(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextUtf8(dc, text.data(), static_cast<int>(text.size()), &rect, format | DT_NOPREFIX);
}

// The height `text` takes wrapped to `width`, in the font it is drawn with.
[[nodiscard]] int WrappedHeight(HDC dc, std::string_view text, int width, int pointSize, int weight) {
    if (text.empty() || width <= 0) {
        return 0;
    }
    const ScopedFont font(pointSize, weight);
    const ScopedGdiObject selected(dc, font.handle);
    RECT rect{ 0, 0, width, 0 };
    DrawTextUtf8(dc, text.data(), static_cast<int>(text.size()), &rect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    return static_cast<int>(rect.bottom - rect.top);
}

// One conversation entry laid out for a width: how it is drawn and how tall it is.
struct EntryBox {
    int height = 0;
    int textWidth = 0;
    int pointSize = 12;
    int weight = FW_NORMAL;
    bool bubble = false;
};

[[nodiscard]] EntryBox Measure(HDC dc, const EditorAgentEntry& entry, int width) {
    EntryBox box;
    switch (entry.kind) {
    case EditorAgentEntryKind::User:
        box.bubble = true;
        box.textWidth = std::max(80, width * 4 / 5 - kBubblePad * 2);
        box.height = WrappedHeight(dc, entry.text, box.textWidth, 12, FW_NORMAL) + kBubblePad * 2;
        break;
    case EditorAgentEntryKind::Agent:
        box.textWidth = width - 16;
        box.height = WrappedHeight(dc, entry.text, box.textWidth, 12, FW_NORMAL);
        break;
    case EditorAgentEntryKind::Tool:
    case EditorAgentEntryKind::Info:
        box.pointSize = 11;
        box.textWidth = width - 22;
        box.height = 18;
        break;
    case EditorAgentEntryKind::Error:
        box.textWidth = width - 22;
        box.height = WrappedHeight(dc, entry.text, box.textWidth, 12, FW_NORMAL);
        break;
    }
    return box;
}

[[nodiscard]] int ConversationHeight(HDC dc, const EditorAgentPanelState& state, int width) {
    int height = 0;
    for (const EditorAgentEntry& entry : state.Entries()) {
        height += Measure(dc, entry, width).height + kEntryGap;
    }
    return height;
}

[[nodiscard]] HDC MeasuringDc() {
    static HDC dc = CreateCompatibleDC(nullptr);
    return dc;
}

[[nodiscard]] bool Inside(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

void PaintEntry(HDC dc, const RECT& conversation, int top, const EditorAgentEntry& entry, const EntryBox& box, const EditorTheme& theme) {
    const int left = static_cast<int>(conversation.left) + kPad;
    const int right = static_cast<int>(conversation.right) - kPad;
    switch (entry.kind) {
    case EditorAgentEntryKind::User: {
        // Your request: a teal bubble on the right, like a message you sent.
        const int textWidth = std::min(box.textWidth, std::max(40, static_cast<int>(
            [&] { const ScopedFont font(12, FW_NORMAL); const ScopedGdiObject selected(dc, font.handle);
                  RECT probe{ 0, 0, box.textWidth, 0 };
                  DrawTextUtf8(dc, entry.text.data(), static_cast<int>(entry.text.size()), &probe, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
                  return probe.right - probe.left; }())));
        const RECT bubble{ right - textWidth - kBubblePad * 2, top, right, top + box.height };
        FillRound(dc, bubble, RGB(28, 66, 82), kTeal, 14);
        Text(dc, RECT{ bubble.left + kBubblePad, bubble.top + kBubblePad, bubble.right - kBubblePad, bubble.bottom - kBubblePad },
            entry.text, RGB(235, 250, 255), 12, FW_NORMAL, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL);
        break;
    }
    case EditorAgentEntryKind::Agent:
        GdiDrawing::FillRectColor(dc, RECT{ left, top + 3, left + 3, top + box.height - 1 }, kTeal);
        Text(dc, RECT{ left + 12, top, right, top + box.height }, entry.text, Color(theme.textPrimary), 12, FW_NORMAL,
            DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL);
        break;
    case EditorAgentEntryKind::Tool:
        HeroIconPainter::Draw(dc, RECT{ left + 2, top + 3, left + 14, top + 15 }, HeroIconKind::WrenchScrewdriver, Color(theme.textDisabled), 1);
        Text(dc, RECT{ left + 20, top, right, top + box.height }, entry.text, Color(theme.textSecondary), 11, FW_NORMAL,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        break;
    case EditorAgentEntryKind::Info:
        Text(dc, RECT{ left + 20, top, right, top + box.height }, entry.text, Color(theme.textDisabled), 11, FW_NORMAL,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        break;
    case EditorAgentEntryKind::Error:
        HeroIconPainter::Draw(dc, RECT{ left + 2, top + 2, left + 14, top + 14 }, HeroIconKind::Warning, kDanger, 1);
        Text(dc, RECT{ left + 20, top, right, top + box.height }, entry.text, kDanger, 12, FW_NORMAL, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL);
        break;
    }
}

void PaintEmptyState(HDC dc, const RECT& conversation, const EditorTheme& theme, const EditorAgentPanelState& state) {
    const int centerTop = static_cast<int>(conversation.top + conversation.bottom) / 2 - 60;
    RECT title{ conversation.left + kPad, centerTop, conversation.right - kPad, centerTop + 24 };
    if (state.Agents().empty()) {
        Text(dc, title, EditorText("agent.noAgentTitle", "No agent found on this computer"), Color(theme.textPrimary), 13, FW_SEMIBOLD,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        RECT body{ title.left, title.bottom + 6, title.right, title.bottom + 70 };
        Text(dc, body, EditorText("agent.noAgentBody",
            "Install Claude Code, Codex, Gemini CLI, Qwen Code or Grok (or Ollama with Codex for local models), "
            "or add your own agent from the agent menu, then choose Refresh."),
            Color(theme.textSecondary), 11, FW_NORMAL, DT_CENTER | DT_WORDBREAK);
        return;
    }
    Text(dc, title, EditorText("agent.emptyTitle", "Tell the agent what to build"), Color(theme.textPrimary), 13, FW_SEMIBOLD,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    RECT body{ title.left, title.bottom + 6, title.right, title.bottom + 90 };
    Text(dc, body, EditorText("agent.emptyBody",
        "It works on this project with the engine's own tools and reloads the scene when it is done.\n"
        "For example: \"Add a red cube above the plane with a rigidbody\" or \"Write a script that rotates the selected object\"."),
        Color(theme.textSecondary), 11, FW_NORMAL, DT_CENTER | DT_WORDBREAK);
}

} // namespace

AgentPanelLayout AgentPanelRenderer::Resolve(const RECT& content, const EditorAgentPanelState& state) {
    AgentPanelLayout layout;
    layout.header = RECT{ content.left, content.top, content.right, std::min(content.bottom, content.top + kHeaderHeight) };
    const int buttonTop = static_cast<int>(layout.header.top) + 8;
    const int buttonBottom = static_cast<int>(layout.header.bottom) - 8;
    int right = static_cast<int>(content.right) - kPad;
    layout.newButton = RECT{ right - 120, buttonTop, right, buttonBottom };
    right = static_cast<int>(layout.newButton.left) - 8;
    if (state.Current() != nullptr && !state.Current()->connectArguments.empty()) {
        layout.connectButton = RECT{ right - 150, buttonTop, right, buttonBottom };
        right = static_cast<int>(layout.connectButton.left) - 8;
    }
    const int left = static_cast<int>(content.left) + kPad;
    layout.agentMenu = RECT{ left, buttonTop, std::max(left, std::min(left + 260, right)), buttonBottom };
    layout.status = RECT{ layout.agentMenu.right + 10, buttonTop, std::max(static_cast<int>(layout.agentMenu.right) + 10, right), buttonBottom };

    const int inputBottom = static_cast<int>(content.bottom) - kPad;
    const int inputTop = std::max(static_cast<int>(layout.header.bottom), inputBottom - kInputHeight);
    layout.sendButton = RECT{ content.right - kPad - kSendWidth, inputBottom - 40, content.right - kPad, inputBottom };
    layout.input = RECT{ content.left + kPad, inputTop, std::max(static_cast<int>(content.left) + kPad, static_cast<int>(layout.sendButton.left) - 10), inputBottom };
    layout.conversation = RECT{ content.left, layout.header.bottom, content.right, std::max(static_cast<int>(layout.header.bottom), inputTop - 8) };
    return layout;
}

int AgentPanelRenderer::MaximumScroll(const RECT& content, const EditorAgentPanelState& state) {
    const AgentPanelLayout layout = Resolve(content, state);
    const int width = static_cast<int>(layout.conversation.right - layout.conversation.left) - kPad * 2;
    const int visible = static_cast<int>(layout.conversation.bottom - layout.conversation.top) - kPad;
    return std::max(0, ConversationHeight(MeasuringDc(), state, width) - visible);
}

std::string AgentPanelRenderer::SignInCommand(const EditorAgentPanelState& state) {
    const EditorAgent* agent = state.Current();
    if (agent == nullptr) {
        return {};
    }
    std::string command;
    for (const auto& [name, value] : agent->environment) {
        command += "set \"" + std::filesystem::path{ name }.string() + "=" + std::filesystem::path{ value }.string() + "\" && ";
    }
    const std::string program = agent->executable.stem().string();
    command += program + (agent->output == EditorAgentOutputFormat::ClaudeStreamJson ? " auth login" : " login");
    return command;
}

void AgentPanelRenderer::Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorAgentPanelState& state) {
    const AgentPanelLayout layout = Resolve(content, state);
    GdiDrawing::FillRectColor(dc, content, Color(theme.panel));

    // Header: which agent answers, who it is signed in as, and the conversation commands.
    GdiDrawing::FillRectColor(dc, layout.header, Color(theme.strip));
    GdiDrawing::FillRectColor(dc, RECT{ layout.header.left, layout.header.bottom - 1, layout.header.right, layout.header.bottom }, Color(theme.borderChrome));
    const EditorAgent* agent = state.Current();
    FillRound(dc, layout.agentMenu, Color(theme.chrome), Color(theme.borderPanel), 10);
    HeroIconPainter::Draw(dc, RECT{ layout.agentMenu.left + 9, layout.agentMenu.top + 7, layout.agentMenu.left + 23, layout.agentMenu.top + 21 },
        HeroIconKind::CommandLine, kTeal, 1);
    Text(dc, RECT{ layout.agentMenu.left + 30, layout.agentMenu.top, layout.agentMenu.right - 24, layout.agentMenu.bottom },
        agent != nullptr ? agent->Label() : std::string{ EditorText("agent.chooseAgent", "Choose an agent") },
        Color(theme.textPrimary), 12, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    HeroIconPainter::Draw(dc, RECT{ layout.agentMenu.right - 20, layout.agentMenu.top + 9, layout.agentMenu.right - 10, layout.agentMenu.top + 19 },
        HeroIconKind::ChevronDown, Color(theme.textSecondary), 1);

    if (agent != nullptr) {
        COLORREF dot = kTeal;
        std::string status;
        if (state.Running()) {
            dot = kWarning;
            status = std::string{ EditorText("agent.working", "Working...") };
        } else if (state.CheckingAccount()) {
            dot = Color(theme.textDisabled);
            status = std::string{ EditorText("agent.checking", "Checking the account...") };
        } else if (!state.SignedIn()) {
            dot = kDanger;
            status = std::string{ EditorText("agent.notSignedIn", "Not signed in - click to copy the sign-in command") };
        } else {
            dot = RGB(63, 194, 123);
            status = state.Account().empty() ? std::string{ EditorText("agent.ready", "Ready") } : state.Account();
        }
        const int dotTop = static_cast<int>(layout.status.top + layout.status.bottom) / 2 - 4;
        FillRound(dc, RECT{ layout.status.left, dotTop, layout.status.left + 8, dotTop + 8 }, dot, dot, 8);
        Text(dc, RECT{ layout.status.left + 14, layout.status.top, layout.status.right, layout.status.bottom }, status,
            Color(theme.textSecondary), 11, FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    if (layout.connectButton.right > layout.connectButton.left) {
        EditorDialogStyle::PaintOptionButton(dc, layout.connectButton, theme, EditorText("agent.connectTools", "Connect engine tools"), false, !state.Running());
    }
    EditorDialogStyle::PaintOptionButton(dc, layout.newButton, theme, EditorText("agent.newConversation", "New conversation"), false,
        !state.Entries().empty() && !state.Running());

    // The conversation, newest at the bottom, scrolled up by the reader.
    const RECT& area = layout.conversation;
    const int width = static_cast<int>(area.right - area.left) - kPad * 2;
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
    if (state.Entries().empty()) {
        PaintEmptyState(dc, area, theme, state);
    } else {
        std::vector<EntryBox> boxes;
        int total = 0;
        for (const EditorAgentEntry& entry : state.Entries()) {
            boxes.push_back(Measure(dc, entry, width));
            total += boxes.back().height + kEntryGap;
        }
        const int visible = static_cast<int>(area.bottom - area.top) - kPad;
        const int scroll = std::clamp(state.ScrollFromBottom(), 0, std::max(0, total - visible));
        // Short conversations start at the top; long ones keep their newest line in view.
        int top = total <= visible ? static_cast<int>(area.top) + kPad : static_cast<int>(area.bottom) - total + scroll;
        for (std::size_t index = 0; index < boxes.size(); ++index) {
            if (top + boxes[index].height >= area.top && top <= area.bottom) {
                PaintEntry(dc, area, top, state.Entries()[index], boxes[index], theme);
            }
            top += boxes[index].height + kEntryGap;
        }
    }
    RestoreDC(dc, saved);

    // The request box: rounded, teal-edged while it has the keyboard, with a placeholder.
    const bool focused = state.InputFocused();
    FillRound(dc, layout.input, Color(theme.chrome), focused ? kTeal : Color(theme.borderPanel), 16);
    const RECT inputText{ layout.input.left + 14, layout.input.top + 10, layout.input.right - 14, layout.input.bottom - 8 };
    if (state.Input().empty() && !focused) {
        Text(dc, inputText, EditorText("agent.placeholder", "Describe what the agent should do... (Enter sends, Ctrl+K from anywhere)"),
            Color(theme.textDisabled), 12, FW_NORMAL, DT_LEFT | DT_WORDBREAK);
    } else {
        Text(dc, inputText, state.Input() + (focused ? "|" : ""), Color(theme.textPrimary), 12, FW_NORMAL,
            DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL | DT_BOTTOM);
    }
    const bool canSend = state.Running() || (agent != nullptr && !state.Input().empty());
    if (state.Running()) {
        FillRound(dc, layout.sendButton, RGB(74, 34, 38), kDanger, 16);
        Text(dc, layout.sendButton, EditorText("agent.stop", "Stop"), RGB(255, 220, 220), 12, FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        FillRound(dc, layout.sendButton, canSend ? RGB(28, 66, 82) : Color(theme.chrome), canSend ? kTeal : Color(theme.borderPanel), 16);
        Text(dc, layout.sendButton, EditorText("agent.send", "Send"), canSend ? RGB(235, 250, 255) : Color(theme.textDisabled), 12, FW_SEMIBOLD,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

AgentPanelHitKind AgentPanelRenderer::HitTest(const RECT& content, const EditorAgentPanelState& state, int x, int y) {
    const AgentPanelLayout layout = Resolve(content, state);
    if (Inside(layout.agentMenu, x, y)) return AgentPanelHitKind::AgentMenu;
    if (Inside(layout.connectButton, x, y)) return AgentPanelHitKind::ConnectTools;
    if (Inside(layout.newButton, x, y)) return AgentPanelHitKind::NewConversation;
    if (Inside(layout.status, x, y) && state.Current() != nullptr && !state.SignedIn() && !state.CheckingAccount()) {
        return AgentPanelHitKind::SignInHint;
    }
    if (Inside(layout.sendButton, x, y)) return AgentPanelHitKind::Send;
    if (Inside(layout.input, x, y)) return AgentPanelHitKind::Input;
    return AgentPanelHitKind::None;
}

} // namespace kb::editor

#endif
