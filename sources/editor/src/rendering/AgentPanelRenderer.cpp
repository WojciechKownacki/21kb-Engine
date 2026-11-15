#include "rendering/AgentPanelRenderer.hpp"

#if defined(_WIN32)
#include "localization/EditorLocalization.hpp"
#include "platform/win32/Utf8Gdi.hpp"
#include "rendering/EditorPanelStyle.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconKind.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

namespace kb::editor {
namespace {

constexpr int kPad = 12;
constexpr int kTabStripHeight = 34;
constexpr int kColumnMaxWidth = 880;
constexpr int kComposerHeight = 104;
constexpr int kComposerRowHeight = 26;
constexpr int kHintHeight = 18;
constexpr int kEntryGap = 12;
constexpr int kHeaderLine = 18;
constexpr int kCardPad = 12;
constexpr COLORREF kTeal = RGB(0, 112, 224);
constexpr COLORREF kGreen = RGB(63, 194, 123);
constexpr COLORREF kAmber = RGB(224, 165, 54);
constexpr COLORREF kDanger = RGB(232, 108, 108);

[[nodiscard]] COLORREF Color(const EditorColor& color) noexcept {
    return RGB(color.r, color.g, color.b);
}

// `radius` is the corner's diameter, as RoundRect took it; every control here stays flat and tight.
void FillRound(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int radius) {
    panel_style::DrawRoundedFrame(dc, rect, fill, border, static_cast<float>(std::min(radius, 8)) / 2.0F, 1.0F);
}

void Text(HDC dc, RECT rect, std::string_view text, COLORREF color, int pointSize, int weight, UINT format) {
    const ScopedFont font(pointSize, weight);
    const ScopedGdiObject selected(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextUtf8(dc, text.data(), static_cast<int>(text.size()), &rect, format | DT_NOPREFIX);
}

// The size `text` takes wrapped to `width` in the given font.
[[nodiscard]] SIZE Wrapped(HDC dc, std::string_view text, int width, int pointSize, int weight = FW_NORMAL) {
    if (text.empty() || width <= 0) {
        return SIZE{ 0, 0 };
    }
    const ScopedFont font(pointSize, weight);
    const ScopedGdiObject selected(dc, font.handle);
    RECT rect{ 0, 0, width, 0 };
    DrawTextUtf8(dc, text.data(), static_cast<int>(text.size()), &rect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    return SIZE{ rect.right - rect.left, rect.bottom - rect.top };
}

[[nodiscard]] int TextWidth(HDC dc, std::string_view text, int pointSize, int weight = FW_NORMAL) {
    const ScopedFont font(pointSize, weight);
    const ScopedGdiObject selected(dc, font.handle);
    RECT rect{ 0, 0, 0, 0 };
    DrawTextUtf8(dc, text.data(), static_cast<int>(text.size()), &rect, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    return static_cast<int>(rect.right - rect.left);
}

[[nodiscard]] HDC MeasuringDc() {
    static HDC dc = CreateCompatibleDC(nullptr);
    return dc;
}

[[nodiscard]] bool Inside(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] std::string Tokens(std::uint64_t count) {
    char text[32]{};
    if (count >= 1000000U) {
        std::snprintf(text, sizeof(text), "%.1fM", static_cast<double>(count) / 1000000.0);
    } else if (count >= 1000U) {
        std::snprintf(text, sizeof(text), "%.1fk", static_cast<double>(count) / 1000.0);
    } else {
        std::snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(count));
    }
    return text;
}

// Whether an entry opens with a header: "You" over every request, the agent's name over the
// first answer or tool that follows it.
[[nodiscard]] bool HasHeader(const std::vector<EditorAgentEntry>& entries, std::size_t index) {
    const EditorAgentEntryKind kind = entries[index].kind;
    if (kind == EditorAgentEntryKind::User) {
        return true;
    }
    if (kind != EditorAgentEntryKind::Agent && kind != EditorAgentEntryKind::Tool) {
        return false;
    }
    return index == 0U || entries[index - 1U].kind == EditorAgentEntryKind::User;
}

// The height of one entry in a column of `width`.
[[nodiscard]] int EntryHeight(HDC dc, const std::vector<EditorAgentEntry>& entries, std::size_t index, int width) {
    const EditorAgentEntry& entry = entries[index];
    const int header = HasHeader(entries, index) ? kHeaderLine + 4 : 0;
    switch (entry.kind) {
    case EditorAgentEntryKind::User:
        return header + Wrapped(dc, entry.text, width - kCardPad * 2, 12).cy + kCardPad * 2;
    case EditorAgentEntryKind::Agent:
        return header + Wrapped(dc, entry.text, width - 14, 12).cy;
    case EditorAgentEntryKind::Tool:
        return header + 20;
    case EditorAgentEntryKind::Info:
        return 18;
    case EditorAgentEntryKind::Error:
        return std::max(18, static_cast<int>(Wrapped(dc, entry.text, width - 22, 12).cy));
    }
    return 18;
}

void PaintEntry(HDC dc, const RECT& column, int top, const std::vector<EditorAgentEntry>& entries, std::size_t index,
    const EditorTheme& theme, std::string_view agentName) {
    const EditorAgentEntry& entry = entries[index];
    const int left = static_cast<int>(column.left);
    const int right = static_cast<int>(column.right);
    const int width = right - left;
    if (HasHeader(entries, index)) {
        const bool user = entry.kind == EditorAgentEntryKind::User;
        const int dotTop = top + 5;
        FillRound(dc, RECT{ left, dotTop, left + 8, dotTop + 8 }, user ? Color(theme.textSecondary) : kTeal, user ? Color(theme.textSecondary) : kTeal, 8);
        Text(dc, RECT{ left + 14, top, right, top + kHeaderLine }, user ? std::string_view{ EditorText("agent.you", "You") } : agentName,
            Color(theme.textSecondary), 11, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        top += kHeaderLine + 4;
    }
    switch (entry.kind) {
    case EditorAgentEntryKind::User: {
        // Your request: a quiet card under "You", on the same left edge as the answer, so the
        // eye never travels across a wide panel.
        const SIZE text = Wrapped(dc, entry.text, width - kCardPad * 2, 12);
        const RECT card{ left, top, std::min(right, left + static_cast<int>(text.cx) + kCardPad * 2 + 4), top + static_cast<int>(text.cy) + kCardPad * 2 };
        FillRound(dc, card, Color(theme.toolbarButton), Color(theme.borderPanel), 14);
        Text(dc, RECT{ card.left + kCardPad, card.top + kCardPad, right - kCardPad, card.bottom - kCardPad }, entry.text,
            Color(theme.textPrimary), 12, FW_NORMAL, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL);
        break;
    }
    case EditorAgentEntryKind::Agent:
        Text(dc, RECT{ left + 14, top, right, top + 100000 }, entry.text, Color(theme.textPrimary), 12, FW_NORMAL,
            DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL);
        break;
    case EditorAgentEntryKind::Tool: {
        const RECT chip{ left + 14, top + 1, std::min(right, left + 14 + TextWidth(dc, entry.text, 11) + 34), top + 19 };
        FillRound(dc, chip, Color(theme.chrome), Color(theme.borderChrome), 10);
        HeroIconPainter::Draw(dc, RECT{ chip.left + 8, chip.top + 3, chip.left + 20, chip.top + 15 }, HeroIconKind::WrenchScrewdriver, Color(theme.textDisabled), 1);
        Text(dc, RECT{ chip.left + 26, chip.top, chip.right - 8, chip.bottom }, entry.text, Color(theme.textSecondary), 11, FW_NORMAL,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        break;
    }
    case EditorAgentEntryKind::Info:
        Text(dc, RECT{ left + 14, top, right, top + 18 }, entry.text, Color(theme.textDisabled), 11, FW_NORMAL,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        break;
    case EditorAgentEntryKind::Error:
        HeroIconPainter::Draw(dc, RECT{ left + 2, top + 2, left + 14, top + 14 }, HeroIconKind::Warning, kDanger, 1);
        Text(dc, RECT{ left + 22, top, right, top + 100000 }, entry.text, kDanger, 12, FW_NORMAL, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL);
        break;
    }
}

// A picker in the composer's row: the current value and a chevron.
void PaintPicker(HDC dc, const RECT& rect, const EditorTheme& theme, std::string_view value, bool enabled) {
    FillRound(dc, rect, Color(theme.panel), Color(theme.panel), 6);
    Text(dc, RECT{ rect.left + 8, rect.top, rect.right - 20, rect.bottom }, value,
        enabled ? Color(theme.textPrimary) : Color(theme.textDisabled), 11, FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    const int chevronTop = static_cast<int>(rect.top + rect.bottom) / 2 - 5;
    HeroIconPainter::Draw(dc, RECT{ rect.right - 16, chevronTop, rect.right - 6, chevronTop + 10 }, HeroIconKind::ChevronDown,
        enabled ? Color(theme.textSecondary) : Color(theme.textDisabled), 1);
}

[[nodiscard]] std::string Capitalized(std::string text) {
    if (!text.empty()) {
        text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    }
    return text;
}

// Entries bottom-aligned in `column`, scrolled up by the conversation's scroll, clipped to `clip`.
void PaintEntries(HDC dc, const RECT& column, const RECT& clip, const EditorTheme& theme, const EditorAgentPanelState& state) {
    const std::vector<EditorAgentEntry>& entries = state.Active().entries;
    const int width = static_cast<int>(column.right - column.left);
    std::vector<int> heights;
    int total = 0;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        heights.push_back(EntryHeight(dc, entries, index, width));
        total += heights.back() + kEntryGap;
    }
    const int visible = static_cast<int>(column.bottom - column.top);
    const int scroll = std::clamp(state.Active().scrollFromBottom, 0, std::max(0, total - visible));
    int top = total <= visible ? static_cast<int>(column.top) : static_cast<int>(column.bottom) - total + scroll;
    const std::string agentName = state.Current() != nullptr ? state.Current()->name : std::string{ "Agent" };
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (top + heights[index] >= clip.top && top <= clip.bottom) {
            PaintEntry(dc, column, top, entries, index, theme, agentName);
        }
        top += heights[index] + kEntryGap;
    }
}

[[nodiscard]] int EntriesHeight(const std::vector<EditorAgentEntry>& entries, int width) {
    int total = 0;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        total += EntryHeight(MeasuringDc(), entries, index, width) + kEntryGap;
    }
    return total;
}

} // namespace

std::string AgentPanelRenderer::TokenCount(std::uint64_t count) {
    return Tokens(count);
}

void AgentPanelRenderer::PaintConversation(HDC dc, const RECT& area, const EditorTheme& theme, const EditorAgentPanelState& state) {
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, area.left, area.top, area.right, area.bottom);
    PaintEntries(dc, area, area, theme, state);
    RestoreDC(dc, saved);
}

int AgentPanelRenderer::ConversationMaximumScroll(const RECT& area, const EditorAgentPanelState& state) {
    return std::max(0, EntriesHeight(state.Active().entries, static_cast<int>(area.right - area.left)) - static_cast<int>(area.bottom - area.top));
}

std::string_view AgentPanelRenderer::Suggestion(int index) {
    switch (index) {
    case 0: return EditorText("agent.suggestion.cube", "Add a red cube that falls with physics");
    case 1: return EditorText("agent.suggestion.script", "Make the selected object rotate slowly");
    default: return EditorText("agent.suggestion.list", "Describe the objects in this scene");
    }
}

AgentPanelLayout AgentPanelRenderer::Resolve(const RECT& content, const EditorAgentPanelState& state) {
    AgentPanelLayout layout;
    HDC dc = MeasuringDc();

    // The top row: the conversations and "+", then history and search on the right.
    layout.tabStrip = RECT{ content.left, content.top, content.right, std::min(content.bottom, content.top + kTabStripHeight) };
    layout.search = RECT{ layout.tabStrip.right - kPad - 26, layout.tabStrip.top + 4, layout.tabStrip.right - kPad, layout.tabStrip.top + 30 };
    layout.chats = RECT{ layout.search.left - 4 - 26, layout.search.top, layout.search.left - 4, layout.search.bottom };
    const int stripRight = static_cast<int>(layout.chats.left) - 8 - 30;
    int x = static_cast<int>(content.left) + 8;
    const int count = state.ConversationCount();
    const int fair = count > 0 ? std::max(90, (stripRight - x) / count) : 200;
    for (int index = 0; index < count; ++index) {
        const int width = std::clamp(TextWidth(dc, state.Conversation(index).title, 11, FW_SEMIBOLD) + 60, 110, std::min(240, fair));
        const RECT tab{ x, layout.tabStrip.top + 5, x + width, layout.tabStrip.bottom };
        layout.tabs.push_back(tab);
        layout.tabCloses.push_back(RECT{ tab.right - 24, tab.top + 6, tab.right - 8, tab.top + 22 });
        x += width + 4;
    }
    layout.newTab = RECT{ std::min(x + 2, stripRight), layout.tabStrip.top + 4, std::min(x + 2, stripRight) + 26, layout.tabStrip.top + 30 };
    layout.toolbar = RECT{ content.left, layout.tabStrip.bottom, content.right, layout.tabStrip.bottom };

    // The composer sits at the panel's foot: the text on top, its controls in one row under it.
    const int areaLeft = static_cast<int>(content.left);
    const int areaWidth = static_cast<int>(content.right - content.left);
    const int columnWidth = std::max(0, std::min(kColumnMaxWidth, areaWidth - kPad * 2));
    const int columnLeft = areaLeft + std::max(kPad, (areaWidth - columnWidth) / 2);
    const int composerBottom = static_cast<int>(content.bottom) - 6 - kHintHeight;
    const int composerTop = std::max(static_cast<int>(layout.toolbar.bottom), composerBottom - kComposerHeight);
    layout.composer = RECT{ columnLeft, composerTop, columnLeft + columnWidth, composerBottom };
    const int rowTop = static_cast<int>(layout.composer.bottom) - 6 - kComposerRowHeight;
    const int rowBottom = rowTop + kComposerRowHeight;
    int rowLeft = static_cast<int>(layout.composer.left) + 6;
    layout.attach = RECT{ rowLeft, rowTop, rowLeft + kComposerRowHeight, rowBottom };
    rowLeft += kComposerRowHeight + 4;
    layout.effortMenu = RECT{ rowLeft, rowTop, rowLeft + kComposerRowHeight, rowBottom };
    rowLeft += kComposerRowHeight + 4;
    layout.modeMenu = RECT{ rowLeft, rowTop, rowLeft + 70, rowBottom };
    rowLeft += 70 + 4;
    layout.sendButton = RECT{ layout.composer.right - 6 - 30, rowTop, layout.composer.right - 6, rowBottom };
    layout.modelMenu = RECT{ layout.sendButton.left - 4 - 76, rowTop, layout.sendButton.left - 4, rowBottom };
    layout.agentMenu = RECT{ rowLeft, rowTop, std::max(rowLeft, static_cast<int>(layout.modelMenu.left) - 4), rowBottom };
    layout.input = RECT{ layout.composer.left + 10, layout.composer.top + 8, layout.composer.right - 10, rowTop - 4 };

    // One quiet line under it: account on the left, usage and the one-off connect on the right.
    const int footerTop = static_cast<int>(layout.composer.bottom) + 3;
    const int footerBottom = footerTop + kHintHeight - 3;
    int footerRight = static_cast<int>(layout.composer.right);
    if (state.Current() != nullptr && !state.Current()->connectArguments.empty()) {
        layout.connectButton = RECT{ footerRight - 140, footerTop, footerRight, footerBottom };
        footerRight = static_cast<int>(layout.connectButton.left) - 8;
    }
    const int footerMiddle = (static_cast<int>(layout.composer.left) + footerRight) / 2;
    layout.status = RECT{ layout.composer.left, footerTop, footerMiddle, footerBottom };
    layout.usage = RECT{ footerMiddle + 8, footerTop, footerRight, footerBottom };

    layout.conversation = RECT{ content.left, layout.toolbar.bottom, content.right, std::max(static_cast<int>(layout.toolbar.bottom), composerTop - 8) };
    layout.column = RECT{ columnLeft, layout.conversation.top + kPad, columnLeft + columnWidth, layout.conversation.bottom };
    // The example requests stand one above another, so they read in a narrow side panel too.
    constexpr int chipHeight = 32;
    constexpr int chipGap = 6;
    constexpr int chipsHeight = chipHeight * 3 + chipGap * 2;
    const int conversationTop = static_cast<int>(layout.conversation.top);
    const int conversationBottom = static_cast<int>(layout.conversation.bottom);
    const int chipsTop = std::clamp((conversationTop + conversationBottom - chipsHeight) / 2 + 40, conversationTop + 96,
        std::max(conversationTop + 96, conversationBottom - chipsHeight - 6));
    for (int index = 0; index < 3; ++index) {
        const int chipTop = chipsTop + index * (chipHeight + chipGap);
        layout.suggestions[static_cast<std::size_t>(index)] = RECT{ columnLeft, chipTop, columnLeft + columnWidth, chipTop + chipHeight };
    }
    return layout;
}

int AgentPanelRenderer::MaximumScroll(const RECT& content, const EditorAgentPanelState& state) {
    const AgentPanelLayout layout = Resolve(content, state);
    const int total = EntriesHeight(state.Active().entries, static_cast<int>(layout.column.right - layout.column.left));
    return std::max(0, total - static_cast<int>(layout.column.bottom - layout.column.top));
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
    command += agent->executable.stem().string() + (agent->output == EditorAgentOutputFormat::ClaudeStreamJson ? " auth login" : " login");
    return command;
}

void AgentPanelRenderer::Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorAgentPanelState& state) {
    const AgentPanelLayout layout = Resolve(content, state);
    const EditorAgent* agent = state.Current();
    GdiDrawing::FillRectColor(dc, content, Color(theme.panel));

    // Top row: the open conversation runs into the panel, as the dock tabs do.
    GdiDrawing::FillRectColor(dc, layout.tabStrip, Color(theme.strip));
    for (int index = 0; index < static_cast<int>(layout.tabs.size()); ++index) {
        const RECT& tab = layout.tabs[static_cast<std::size_t>(index)];
        const EditorAgentConversation& conversation = state.Conversation(index);
        const bool active = index == state.ActiveConversation();
        if (active) {
            panel_style::FillDockTab(dc, tab, Color(theme.panel), panel_style::kDockTabCornerRadius);
        }
        int textLeft = static_cast<int>(tab.left) + 12;
        if (conversation.running) {
            const int dotTop = static_cast<int>(tab.top + tab.bottom) / 2 - 3;
            FillRound(dc, RECT{ textLeft, dotTop, textLeft + 7, dotTop + 7 }, kAmber, kAmber, 7);
            textLeft += 13;
        }
        Text(dc, RECT{ textLeft, tab.top, tab.right - 28, tab.bottom }, conversation.title,
            active ? Color(theme.textPrimary) : Color(theme.textSecondary), 11, active ? FW_SEMIBOLD : FW_NORMAL,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        const RECT& close = layout.tabCloses[static_cast<std::size_t>(index)];
        HeroIconPainter::Draw(dc, RECT{ close.left + 3, close.top + 3, close.right - 3, close.bottom - 3 }, HeroIconKind::XMark, Color(theme.textDisabled), 1);
    }
    const auto headerIcon = [&](const RECT& rect, HeroIconKind icon) {
        HeroIconPainter::Draw(dc, RECT{ rect.left + 5, rect.top + 5, rect.right - 5, rect.bottom - 5 }, icon, Color(theme.textSecondary), 1);
    };
    headerIcon(layout.newTab, HeroIconKind::Plus);
    headerIcon(layout.chats, HeroIconKind::ListBullet);
    headerIcon(layout.search, HeroIconKind::MagnifyingGlass);

    // The conversation, newest at the bottom.
    const std::vector<EditorAgentEntry>& entries = state.Active().entries;
    const RECT& column = layout.column;
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, layout.conversation.left, layout.conversation.top, layout.conversation.right, layout.conversation.bottom);
    if (entries.empty()) {
        const int titleTop = static_cast<int>(layout.suggestions[0].top) - 96;
        if (agent == nullptr) {
            Text(dc, RECT{ column.left, titleTop, column.right, titleTop + 26 }, EditorText("agent.noAgentTitle", "No agent found on this computer"),
                Color(theme.textPrimary), 14, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            Text(dc, RECT{ column.left, titleTop + 30, column.right, titleTop + 92 }, EditorText("agent.noAgentBody",
                "Install Claude Code, Codex, Gemini CLI, Qwen Code or Grok (or Ollama, for local models with Codex), "
                "or add your own from the Agent menu, then choose Refresh."),
                Color(theme.textSecondary), 11, FW_NORMAL, DT_LEFT | DT_WORDBREAK);
        } else {
            Text(dc, RECT{ column.left, titleTop, column.right, titleTop + 26 }, EditorText("agent.emptyTitle", "What should the agent build?"),
                Color(theme.textPrimary), 14, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            Text(dc, RECT{ column.left, titleTop + 30, column.right, titleTop + 92 }, EditorText("agent.emptyBody",
                "It works on this project with the engine's own tools; the scene reloads when it changes something."),
                Color(theme.textSecondary), 11, FW_NORMAL, DT_LEFT | DT_WORDBREAK);
            for (int index = 0; index < 3; ++index) {
                const RECT& chip = layout.suggestions[static_cast<std::size_t>(index)];
                FillRound(dc, chip, Color(theme.toolbarButton), Color(theme.toolbarButton), 8);
                Text(dc, RECT{ chip.left + 10, chip.top, chip.right - 10, chip.bottom }, Suggestion(index), Color(theme.textPrimary), 11, FW_NORMAL,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
        }
    } else {
        PaintEntries(dc, column, layout.conversation, theme, state);
    }
    RestoreDC(dc, saved);

    // The composer: the request on top, attach, reasoning, mode, agent, model and send under it.
    const bool focused = state.InputFocused();
    FillRound(dc, layout.composer, Color(theme.chrome), focused ? Color(theme.accent) : Color(theme.borderPanel), 8);
    if (state.Input().empty() && !focused) {
        Text(dc, layout.input, EditorText("agent.placeholder", "Describe what the agent should do..."), Color(theme.textDisabled), 12, FW_NORMAL,
            DT_LEFT | DT_WORDBREAK);
    } else {
        Text(dc, layout.input, state.Input() + (focused ? "|" : ""), Color(theme.textPrimary), 12, FW_NORMAL,
            DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL);
    }
    const auto iconButton = [&](const RECT& rect, HeroIconKind icon, bool enabled) {
        FillRound(dc, rect, Color(theme.panel), Color(theme.panel), 6);
        HeroIconPainter::Draw(dc, RECT{ rect.left + 6, rect.top + 6, rect.right - 6, rect.bottom - 6 }, icon,
            enabled ? Color(theme.textSecondary) : Color(theme.textDisabled), 1);
    };
    iconButton(layout.attach, HeroIconKind::Plus, true);
    iconButton(layout.effortMenu, HeroIconKind::AdjustmentsHorizontal, agent != nullptr && !agent->efforts.empty());
    const std::string defaultValue{ EditorText("agent.default", "Default") };
    PaintPicker(dc, layout.modeMenu, theme, state.Mode() == EditorAgentMode::Plan ? EditorText("agent.mode.plan", "Plan")
        : state.Mode() == EditorAgentMode::Ask ? EditorText("agent.mode.ask", "Ask") : EditorText("agent.mode.agent", "Agent"), true);
    if (layout.agentMenu.right - layout.agentMenu.left > 40) {
        PaintPicker(dc, layout.agentMenu, theme, agent != nullptr ? agent->Label() : std::string{ EditorText("agent.none", "None found") }, agent != nullptr);
    }
    PaintPicker(dc, layout.modelMenu, theme, state.Model().empty() ? defaultValue : Capitalized(state.Model()), agent != nullptr && !agent->models.empty());
    const bool canSend = state.Running() || (agent != nullptr && !state.Input().empty());
    const COLORREF sendFill = state.Running() ? kDanger : (canSend ? Color(theme.accent) : Color(theme.panel));
    FillRound(dc, layout.sendButton, sendFill, sendFill, 6);
    Text(dc, layout.sendButton, state.Running() ? std::string_view{ "\xE2\x96\xA0" } : std::string_view{ "\xE2\x86\x91" },
        canSend ? RGB(255, 255, 255) : Color(theme.textDisabled), 13, FW_BOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Account on the left of the line under the composer; usage on its right.
    if (agent != nullptr) {
        COLORREF dot = kGreen;
        std::string status;
        if (state.Running()) {
            dot = kAmber;
            status = std::string{ EditorText("agent.working", "Working...") };
        } else if (state.CheckingAccount()) {
            dot = Color(theme.textDisabled);
            status = std::string{ EditorText("agent.checking", "Checking the account...") };
        } else if (!state.SignedIn()) {
            dot = kDanger;
            status = std::string{ EditorText("agent.notSignedIn", "Not signed in - click to copy the sign-in command") };
        } else {
            status = std::string{ EditorText("agent.signedIn", "Signed in") } + (state.Plan().empty() ? std::string{} : " \xC2\xB7 " + state.Plan());
        }
        const int dotTop = static_cast<int>(layout.status.top + layout.status.bottom) / 2 - 3;
        FillRound(dc, RECT{ layout.status.left + 2, dotTop, layout.status.left + 8, dotTop + 6 }, dot, dot, 6);
        Text(dc, RECT{ layout.status.left + 13, layout.status.top, layout.status.right, layout.status.bottom }, status, Color(theme.textSecondary), 10, FW_NORMAL,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    const EditorAgentUsage& usage = state.Active().usage;
    if (usage.contextTokens > 0U) {
        std::string text = Tokens(usage.contextTokens);
        if (usage.contextWindow > 0U) {
            text += " / " + Tokens(usage.contextWindow);
        }
        Text(dc, layout.usage, text, Color(theme.textDisabled), 10, FW_NORMAL, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    if (layout.connectButton.right > layout.connectButton.left) {
        Text(dc, layout.connectButton, EditorText("agent.connectTools", "Connect engine tools"), Color(theme.accent), 10, FW_SEMIBOLD,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

AgentPanelHit AgentPanelRenderer::HitTest(const RECT& content, const EditorAgentPanelState& state, int x, int y) {
    const AgentPanelLayout layout = Resolve(content, state);
    for (int index = 0; index < static_cast<int>(layout.tabs.size()); ++index) {
        if (Inside(layout.tabCloses[static_cast<std::size_t>(index)], x, y)) return { AgentPanelHitKind::TabClose, index };
        if (Inside(layout.tabs[static_cast<std::size_t>(index)], x, y)) return { AgentPanelHitKind::Tab, index };
    }
    if (Inside(layout.newTab, x, y)) return { AgentPanelHitKind::NewTab };
    if (Inside(layout.chats, x, y)) return { AgentPanelHitKind::Chats };
    if (Inside(layout.search, x, y)) return { AgentPanelHitKind::Search };
    if (Inside(layout.attach, x, y)) return { AgentPanelHitKind::Attach };
    if (Inside(layout.modeMenu, x, y)) return { AgentPanelHitKind::ModeMenu };
    if (Inside(layout.agentMenu, x, y)) return { AgentPanelHitKind::AgentMenu };
    if (Inside(layout.modelMenu, x, y)) return { AgentPanelHitKind::ModelMenu };
    if (Inside(layout.effortMenu, x, y)) return { AgentPanelHitKind::EffortMenu };
    if (Inside(layout.connectButton, x, y)) return { AgentPanelHitKind::ConnectTools };
    if (Inside(layout.status, x, y) && state.Current() != nullptr && !state.SignedIn() && !state.CheckingAccount()) {
        return { AgentPanelHitKind::SignInHint };
    }
    if (state.Active().entries.empty() && state.Current() != nullptr) {
        for (int index = 0; index < 3; ++index) {
            if (Inside(layout.suggestions[static_cast<std::size_t>(index)], x, y)) return { AgentPanelHitKind::Suggestion, index };
        }
    }
    if (Inside(layout.sendButton, x, y)) return { AgentPanelHitKind::Send };
    if (Inside(layout.composer, x, y)) return { AgentPanelHitKind::Input };
    return {};
}

} // namespace kb::editor

#endif
