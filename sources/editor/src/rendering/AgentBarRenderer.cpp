#include "rendering/AgentBarRenderer.hpp"

#if defined(_WIN32)
#include "localization/EditorLocalization.hpp"
#include "rendering/AgentPanelRenderer.hpp"
#include "rendering/HeroIconGdiplusRuntime.hpp"

#pragma warning(push, 0)
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#pragma warning(pop)

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <unordered_map>

namespace kb::editor {
namespace {

// The bubble keeps clear of its window's edges for the shadow.
constexpr int kMargin = 24;
constexpr int kLineMinHeight = 48;
constexpr int kTextLine = 20;
constexpr int kMaxInputLines = 6;
constexpr int kMaxAnswerLines = 4;
constexpr int kPanelMaxHeight = 460;
constexpr int kHeaderHeight = 40;
constexpr int kPad = 20;
constexpr int kSend = 30;
// The model picker.
constexpr int kMenuGap = 10;
constexpr int kMenuMaxWidth = 520;
constexpr int kMenuPad = 8;
constexpr int kMenuAgentsWidth = 210;
constexpr int kMenuAgentRow = 44;
constexpr int kMenuLinkRow = 30;
constexpr int kMenuHeading = 26;
constexpr int kMenuModelRow = 32;
constexpr int kMenuPill = 28;

const Gdiplus::Color kFill{ 244, 21, 23, 27 };
const Gdiplus::Color kText{ 255, 230, 232, 235 };
const Gdiplus::Color kDim{ 255, 146, 152, 162 };
const Gdiplus::Color kFaint{ 255, 98, 104, 114 };
const Gdiplus::Color kAccent{ 255, 98, 186, 218 };
const Gdiplus::Color kAmber{ 255, 226, 170, 64 };
const Gdiplus::Color kDanger{ 255, 236, 116, 116 };

[[nodiscard]] std::wstring Wide(std::string_view text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

// Fonts and a measuring surface, made once GDI+ runs.
struct Typography {
    Gdiplus::Font body{ L"Segoe UI", 13.0F, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel };
    Gdiplus::Font caption{ L"Segoe UI", 12.0F, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel };
    Gdiplus::Font strong{ L"Segoe UI Semibold", 12.0F, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel };
    Gdiplus::Bitmap scratch{ 1, 1, PixelFormat32bppPARGB };
    Gdiplus::Graphics measure{ &scratch };
    Gdiplus::StringFormat format{ Gdiplus::StringFormat::GenericTypographic() };
    std::unordered_map<std::wstring, std::vector<std::wstring>> wrapped;

    Typography() {
        format.SetFormatFlags(format.GetFormatFlags() | Gdiplus::StringFormatFlagsMeasureTrailingSpaces | Gdiplus::StringFormatFlagsNoWrap);
        measure.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    }
};

[[nodiscard]] Typography& Type() {
    HeroIconGdiplusRuntime::EnsureStarted();
    static Typography typography;
    return typography;
}

[[nodiscard]] float Width(std::wstring_view text, const Gdiplus::Font& font) {
    if (text.empty()) return 0.0F;
    Typography& type = Type();
    Gdiplus::RectF bounds;
    type.measure.MeasureString(text.data(), static_cast<INT>(text.size()), &font, Gdiplus::PointF{ 0.0F, 0.0F }, &type.format, &bounds);
    return bounds.Width;
}

// Lines of `text` no wider than `width`, broken at spaces (and inside a word too long for a line);
// at most `maxLines`, the last ending in an ellipsis when text was left out.
[[nodiscard]] std::vector<std::wstring> Wrap(const std::wstring& text, const Gdiplus::Font& font, float width, int maxLines) {
    Typography& type = Type();
    wchar_t keyTail[64]{};
    std::swprintf(keyTail, 64, L"\x1F%d\x1F%d\x1F%p", static_cast<int>(width), maxLines, static_cast<const void*>(&font));
    const std::wstring key = text + keyTail;
    if (const auto found = type.wrapped.find(key); found != type.wrapped.end()) {
        return found->second;
    }
    std::vector<std::wstring> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = std::min(text.find(L'\n', start), text.size());
        const std::wstring paragraph = text.substr(start, end - start);
        std::wstring line;
        std::size_t at = 0;
        while (at < paragraph.size()) {
            const std::size_t space = paragraph.find(L' ', at);
            const std::size_t wordEnd = space == std::wstring::npos ? paragraph.size() : space + 1U;
            const std::wstring word = paragraph.substr(at, wordEnd - at);
            if (line.empty() || Width(line + word, font) <= width) {
                line += word;
                // A single word wider than the line breaks where it overflows.
                while (line.size() > 1U && Width(line, font) > width) {
                    std::size_t fit = line.size() - 1U;
                    while (fit > 1U && Width(line.substr(0, fit), font) > width) --fit;
                    lines.push_back(line.substr(0, fit));
                    line.erase(0, fit);
                }
                at = wordEnd;
            } else {
                lines.push_back(line);
                line.clear();
            }
        }
        lines.push_back(line);
        if (end >= text.size()) break;
        start = end + 1U;
    }
    if (static_cast<int>(lines.size()) > maxLines) {
        lines.resize(static_cast<std::size_t>(maxLines));
        std::wstring& last = lines.back();
        while (!last.empty() && Width(last + L"\x2026", font) > width) last.pop_back();
        while (!last.empty() && last.back() == L' ') last.pop_back();
        last += L"\x2026";
    }
    if (type.wrapped.size() > 512U) type.wrapped.clear();
    type.wrapped.emplace(key, lines);
    return lines;
}

[[nodiscard]] bool Blank(std::string_view text) noexcept {
    return text.find_first_not_of(" \t\r\n") == std::string_view::npos;
}

[[nodiscard]] bool Inside(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] std::string ModelText(const EditorAgentPanelState& state) {
    const EditorAgent* agent = state.Current();
    if (agent == nullptr) {
        return std::string{ EditorText("agent.bar.noAgent", "Choose an agent") };
    }
    std::string text = state.Model().empty() ? agent->name : state.Model();
    if (!text.empty()) text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    if (!state.Effort().empty()) text += " \xC2\xB7 " + AgentBarRenderer::EffortLabel(state.Effort());
    return text;
}

// The latest answer: what the agent is doing while it works, then what it answered (or the error).
struct Latest {
    bool shown = false;
    bool running = false;
    bool error = false;
    std::string text;
};

[[nodiscard]] Latest LatestAnswer(const EditorAgentPanelState& state) {
    Latest latest;
    if (state.BarExpanded() || state.AnswerDismissed()) return latest;
    const EditorAgentConversation& conversation = state.Active();
    const std::vector<EditorAgentEntry>& entries = conversation.entries;
    std::size_t lastRequest = entries.size();
    for (std::size_t index = entries.size(); index > 0U; --index) {
        if (entries[index - 1U].kind == EditorAgentEntryKind::User) {
            lastRequest = index - 1U;
            break;
        }
    }
    if (lastRequest == entries.size()) return latest;
    latest.running = conversation.running;
    for (std::size_t index = entries.size(); index > lastRequest + 1U; --index) {
        const EditorAgentEntry& entry = entries[index - 1U];
        if (Blank(entry.text)) continue;
        if ((latest.running && entry.kind == EditorAgentEntryKind::Tool) || entry.kind == EditorAgentEntryKind::Agent ||
            entry.kind == EditorAgentEntryKind::Error) {
            latest.error = entry.kind == EditorAgentEntryKind::Error;
            latest.text = entry.text;
            break;
        }
    }
    if (latest.text.empty()) {
        if (!latest.running) return latest;
        latest.text = std::string{ EditorText("agent.working", "Working...") };
    }
    latest.shown = true;
    return latest;
}

// The unfolded conversation as quiet blocks: requests dim, answers bright, the tools of a turn
// folded into one faint line; the panel's own notes are left out.
struct Block {
    enum class Kind : std::uint8_t { Request, Answer, Tools, Error } kind = Kind::Answer;
    std::vector<std::wstring> lines;
    int gapBefore = 0;
};

[[nodiscard]] std::vector<Block> Blocks(const EditorAgentPanelState& state, float width) {
    Typography& type = Type();
    std::vector<Block> blocks;
    std::wstring tools;
    auto flushTools = [&] {
        if (tools.empty()) return;
        blocks.push_back(Block{ Block::Kind::Tools, Wrap(tools, type.caption, width, 1), 4 });
        tools.clear();
    };
    for (const EditorAgentEntry& entry : state.Active().entries) {
        if (Blank(entry.text)) continue;
        switch (entry.kind) {
        case EditorAgentEntryKind::Tool:
            tools += (tools.empty() ? L"" : L"  \x00B7  ") + Wide(entry.text.substr(0U, entry.text.find(' ')));
            break;
        case EditorAgentEntryKind::User:
            flushTools();
            blocks.push_back(Block{ Block::Kind::Request, Wrap(Wide(entry.text), type.body, width, 1000), blocks.empty() ? 0 : 22 });
            break;
        case EditorAgentEntryKind::Agent:
            flushTools();
            blocks.push_back(Block{ Block::Kind::Answer, Wrap(Wide(entry.text), type.body, width, 1000), 8 });
            break;
        case EditorAgentEntryKind::Error:
            flushTools();
            blocks.push_back(Block{ Block::Kind::Error, Wrap(Wide(entry.text), type.body, width, 1000), 8 });
            break;
        case EditorAgentEntryKind::Info:
            break;
        }
    }
    flushTools();
    return blocks;
}

[[nodiscard]] int BlocksHeight(const std::vector<Block>& blocks) {
    int total = 0;
    for (const Block& block : blocks) {
        total += block.gapBefore + static_cast<int>(block.lines.size()) * kTextLine;
    }
    return total;
}

void RoundedPath(Gdiplus::GraphicsPath& path, Gdiplus::RectF rect, float radius) {
    const float diameter = std::min({ radius * 2.0F, rect.Width, rect.Height });
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0F, 90.0F);
    path.AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270.0F, 90.0F);
    path.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter, diameter, diameter, 0.0F, 90.0F);
    path.AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter, 90.0F, 90.0F);
    path.CloseFigure();
}

[[nodiscard]] Gdiplus::RectF ToRectF(const RECT& rect) {
    return Gdiplus::RectF{ static_cast<float>(rect.left), static_cast<float>(rect.top),
        static_cast<float>(rect.right - rect.left), static_cast<float>(rect.bottom - rect.top) };
}

void DrawLabel(Gdiplus::Graphics& graphics, std::wstring_view text, const Gdiplus::Font& font, const Gdiplus::Color& color, float x, float y) {
    if (text.empty()) return;
    const Gdiplus::SolidBrush brush(color);
    graphics.DrawString(text.data(), static_cast<INT>(text.size()), &font, Gdiplus::PointF{ x, y }, &Type().format, &brush);
}

void Round(Gdiplus::Pen& pen) {
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
}

// A chevron centred on (x, y), pointing down, or up when `up`.
void Chevron(Gdiplus::Graphics& graphics, float x, float y, float size, bool up, const Gdiplus::Color& color) {
    Gdiplus::Pen pen(color, 1.6F);
    Round(pen);
    const float rise = up ? -size * 0.5F : size * 0.5F;
    const Gdiplus::PointF points[] = { { x - size, y - rise }, { x, y + rise }, { x + size, y - rise } };
    graphics.DrawLines(&pen, points, 3);
}

void Cross(Gdiplus::Graphics& graphics, const RECT& rect, const Gdiplus::Color& color) {
    Gdiplus::Pen pen(color, 1.4F);
    Round(pen);
    const float x = (rect.left + rect.right) / 2.0F;
    const float y = (rect.top + rect.bottom) / 2.0F;
    graphics.DrawLine(&pen, x - 4.0F, y - 4.0F, x + 4.0F, y + 4.0F);
    graphics.DrawLine(&pen, x + 4.0F, y - 4.0F, x - 4.0F, y + 4.0F);
}

[[nodiscard]] double ContextShare(const EditorAgentUsage& usage) noexcept {
    if (usage.contextWindow == 0U) return 0.0;
    return std::min(1.0, static_cast<double>(usage.contextTokens) / static_cast<double>(usage.contextWindow));
}

// "context 48.2k / 1.0M   $0.14": how much of the model's window the conversation fills, and
// what it has cost so far.
[[nodiscard]] std::wstring UsageText(const EditorAgentUsage& usage) {
    if (usage.contextTokens == 0U) return {};
    std::string text = std::string{ EditorText("agent.bar.context", "context") } + " " + AgentPanelRenderer::TokenCount(usage.contextTokens);
    if (usage.contextWindow > 0U) {
        text += " / " + AgentPanelRenderer::TokenCount(usage.contextWindow);
    }
    if (usage.costUsd > 0.0) {
        char cost[32]{};
        std::snprintf(cost, sizeof(cost), "   $%.2f", usage.costUsd);
        text += cost;
    }
    return Wide(text);
}

// An agent's account as the picker shows it under the agent's name.
[[nodiscard]] std::wstring ProfileText(const EditorAgent& agent) {
    if (agent.profile.empty() || agent.profile == "default") {
        return Wide(EditorText("agent.menu.defaultAccount", "Default account"));
    }
    std::string profile = agent.profile;
    while (!profile.empty() && profile.front() == '.') profile.erase(profile.begin());
    return Wide(profile);
}

[[nodiscard]] const EditorAgent* MenuAgentOf(const EditorAgentPanelState& state) {
    const int index = state.MenuAgent() >= 0 ? state.MenuAgent() : state.CurrentIndex();
    return index >= 0 && index < static_cast<int>(state.Agents().size()) ? &state.Agents()[static_cast<std::size_t>(index)] : nullptr;
}

// Lays the picker out in a panel whose top-left corner is (left, top); returns its height.
int LayOutMenu(AgentBarLayout& layout, const EditorAgentPanelState& state, int left, int top, int width) {
    Typography& type = Type();
    layout.menuAgents.clear();
    layout.menuModels.clear();
    layout.menuEfforts.clear();
    int y = top + kMenuPad;
    for (std::size_t index = 0; index < state.Agents().size(); ++index) {
        layout.menuAgents.push_back(RECT{ left + kMenuPad, y, left + kMenuAgentsWidth - kMenuPad, y + kMenuAgentRow });
        y += kMenuAgentRow;
    }
    y += kMenuPad + 1;
    layout.menuRefresh = RECT{ left + kMenuPad, y, left + kMenuAgentsWidth - kMenuPad, y + kMenuLinkRow };
    y += kMenuLinkRow;
    layout.menuAddAgent = RECT{ left + kMenuPad, y, left + kMenuAgentsWidth - kMenuPad, y + kMenuLinkRow };
    const int leftHeight = y + kMenuLinkRow + kMenuPad - top;

    const int columnLeft = left + kMenuAgentsWidth + kMenuPad;
    const int columnRight = left + width - kMenuPad;
    y = top + kMenuPad + kMenuHeading;
    const EditorAgent* agent = MenuAgentOf(state);
    if (agent != nullptr && !agent->models.empty()) {
        for (std::size_t index = 0; index <= agent->models.size(); ++index) {
            layout.menuModels.push_back(RECT{ columnLeft, y, columnRight, y + kMenuModelRow });
            y += kMenuModelRow;
        }
    } else {
        y += kMenuModelRow;
    }
    if (agent != nullptr && !agent->efforts.empty()) {
        y += 12 + kMenuHeading;
        int x = columnLeft;
        for (std::size_t index = 0; index <= agent->efforts.size(); ++index) {
            const std::wstring label = index == 0U ? Wide(EditorText("agent.menu.defaultEffort", "Default")) : Wide(AgentBarRenderer::EffortLabel(agent->efforts[index - 1U]));
            const int pillWidth = static_cast<int>(Width(label, type.caption)) + 26;
            if (x + pillWidth > columnRight && x > columnLeft) {
                x = columnLeft;
                y += kMenuPill + 6;
            }
            layout.menuEfforts.push_back(RECT{ x, y, x + pillWidth, y + kMenuPill });
            x += pillWidth + 6;
        }
        y += kMenuPill;
    }
    const int rightHeight = y + kMenuPad + 4 - top;
    return std::max(leftHeight, rightHeight);
}

// A floating surface: soft shadow, the bubble's fill and a faint outline.
void PaintSurface(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect, float radius, bool focused) {
    for (int step = 12; step >= 1; step -= 2) {
        Gdiplus::GraphicsPath shadow;
        Gdiplus::RectF spread = rect;
        spread.Inflate(static_cast<float>(step), static_cast<float>(step));
        spread.Offset(0.0F, 5.0F);
        RoundedPath(shadow, spread, radius + static_cast<float>(step));
        const Gdiplus::SolidBrush brush(Gdiplus::Color(static_cast<BYTE>(8 + (12 - step) / 2), 0, 0, 0));
        graphics.FillPath(&brush, &shadow);
    }
    Gdiplus::GraphicsPath outline;
    RoundedPath(outline, rect, radius);
    const Gdiplus::SolidBrush fill(kFill);
    graphics.FillPath(&fill, &outline);
    const Gdiplus::Pen border(Gdiplus::Color(focused ? 46 : 26, 255, 255, 255), 1.0F);
    graphics.DrawPath(&border, &outline);
}

void FillRow(Gdiplus::Graphics& graphics, const RECT& rect, BYTE alpha) {
    Gdiplus::GraphicsPath path;
    RoundedPath(path, ToRectF(rect), 8.0F);
    const Gdiplus::SolidBrush brush(Gdiplus::Color(alpha, 255, 255, 255));
    graphics.FillPath(&brush, &path);
}

void Check(Gdiplus::Graphics& graphics, float x, float y, const Gdiplus::Color& color) {
    Gdiplus::Pen pen(color, 1.7F);
    Round(pen);
    const Gdiplus::PointF points[] = { { x - 4.5F, y }, { x - 1.5F, y + 3.0F }, { x + 4.5F, y - 3.5F } };
    graphics.DrawLines(&pen, points, 3);
}

// The model picker: agents and accounts on the left, the listed agent's models and reasoning
// levels on the right; the pointer's row lights up.
void PaintMenu(Gdiplus::Graphics& graphics, const AgentBarLayout& layout, const EditorAgentPanelState& state, POINT hover) {
    Typography& type = Type();
    PaintSurface(graphics, ToRectF(layout.menu), 14.0F, false);
    const Gdiplus::Pen divider(Gdiplus::Color(16, 255, 255, 255), 1.0F);
    const float dividerX = static_cast<float>(layout.menu.left + kMenuAgentsWidth);
    graphics.DrawLine(&divider, dividerX, static_cast<float>(layout.menu.top + 12), dividerX, static_cast<float>(layout.menu.bottom - 12));
    auto hovered = [&](const RECT& rect) { return Inside(rect, hover.x, hover.y); };

    const std::vector<EditorAgent>& agents = state.Agents();
    const int listed = state.MenuAgent() >= 0 ? state.MenuAgent() : state.CurrentIndex();
    for (std::size_t index = 0; index < layout.menuAgents.size() && index < agents.size(); ++index) {
        const RECT& row = layout.menuAgents[index];
        const bool isListed = static_cast<int>(index) == listed;
        if (isListed || hovered(row)) {
            FillRow(graphics, row, isListed ? 16 : 9);
        }
        DrawLabel(graphics, Wide(agents[index].name), type.body, kText, static_cast<float>(row.left + 12), static_cast<float>(row.top + 5));
        DrawLabel(graphics, ProfileText(agents[index]), type.caption, kFaint, static_cast<float>(row.left + 12), static_cast<float>(row.top + 24));
        if (static_cast<int>(index) == state.CurrentIndex()) {
            const Gdiplus::SolidBrush dot(kAccent);
            graphics.FillEllipse(&dot, static_cast<float>(row.right - 18), static_cast<float>(row.top + row.bottom) / 2.0F - 3.0F, 6.0F, 6.0F);
        }
    }
    const Gdiplus::Pen footer(Gdiplus::Color(16, 255, 255, 255), 1.0F);
    const float footerY = static_cast<float>(layout.menuRefresh.top) - 1.0F;
    graphics.DrawLine(&footer, static_cast<float>(layout.menu.left + 16), footerY, dividerX - 12.0F, footerY);
    for (const auto& [rect, text] : { std::pair{ layout.menuRefresh, Wide(EditorText("agent.menu.refresh", "Find installed agents")) },
             std::pair{ layout.menuAddAgent, Wide(EditorText("agent.menu.add", "Add your own agent")) } }) {
        if (hovered(rect)) FillRow(graphics, rect, 9);
        DrawLabel(graphics, text, type.caption, hovered(rect) ? kText : kDim, static_cast<float>(rect.left + 12), static_cast<float>(rect.top + 7));
    }

    const float columnLeft = static_cast<float>(layout.menu.left + kMenuAgentsWidth + kMenuPad);
    const EditorAgent* agent = MenuAgentOf(state);
    const bool listedIsCurrent = listed == state.CurrentIndex();
    DrawLabel(graphics, Wide(EditorText("agent.menu.model", "MODEL")), type.caption, kFaint, columnLeft + 12.0F, static_cast<float>(layout.menu.top + kMenuPad + 6));
    if (agent != nullptr && !agent->models.empty()) {
        for (std::size_t index = 0; index < layout.menuModels.size(); ++index) {
            const RECT& row = layout.menuModels[index];
            std::string name = index == 0U ? std::string{ EditorText("agent.menu.defaultModel", "Default") } : agent->models[index - 1U];
            if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
            const bool selected = listedIsCurrent && (index == 0U ? state.Model().empty() : state.Model() == agent->models[index - 1U]);
            if (hovered(row)) FillRow(graphics, row, 9);
            DrawLabel(graphics, Wide(name), type.body, selected ? kText : kDim, static_cast<float>(row.left + 12), static_cast<float>(row.top + 6));
            if (selected) {
                Check(graphics, static_cast<float>(row.right - 16), static_cast<float>(row.top + row.bottom) / 2.0F, kAccent);
            }
        }
    } else {
        DrawLabel(graphics, Wide(EditorText("agent.menu.noModels", "This agent chooses its model")), type.caption, kFaint,
            columnLeft + 12.0F, static_cast<float>(layout.menu.top + kMenuPad + kMenuHeading + 7));
    }
    if (agent != nullptr && !layout.menuEfforts.empty()) {
        const float headingY = static_cast<float>(layout.menuEfforts.front().top) - kMenuHeading + 6.0F;
        DrawLabel(graphics, Wide(EditorText("agent.menu.reasoning", "REASONING")), type.caption, kFaint, columnLeft + 12.0F, headingY);
        for (std::size_t index = 0; index < layout.menuEfforts.size(); ++index) {
            const RECT& pill = layout.menuEfforts[index];
            const bool selected = listedIsCurrent && (index == 0U ? state.Effort().empty() : state.Effort() == agent->efforts[index - 1U]);
            Gdiplus::GraphicsPath path;
            RoundedPath(path, ToRectF(pill), kMenuPill / 2.0F);
            if (selected) {
                const Gdiplus::SolidBrush brush(kText);
                graphics.FillPath(&brush, &path);
            } else {
                const Gdiplus::SolidBrush brush(Gdiplus::Color(hovered(pill) ? 22 : 10, 255, 255, 255));
                graphics.FillPath(&brush, &path);
            }
            const std::wstring label = index == 0U ? Wide(EditorText("agent.menu.defaultEffort", "Default")) : Wide(AgentBarRenderer::EffortLabel(agent->efforts[index - 1U]));
            DrawLabel(graphics, label, type.caption, selected ? Gdiplus::Color(255, 21, 23, 27) : kDim, static_cast<float>(pill.left + 13), static_cast<float>(pill.top + 6));
        }
    }
}

} // namespace

std::string AgentBarRenderer::EffortLabel(std::string_view effort) {
    // The agent's own name for the level, as its documentation and command line spell it.
    return std::string{ effort };
}

AgentBarLayout AgentBarRenderer::Resolve(int bubbleWidth, int maximumBubbleHeight, const EditorAgentPanelState& state) {
    Typography& type = Type();
    AgentBarLayout layout;
    const int width = bubbleWidth;

    // The request line's pieces, measured in the bubble's own coordinates first.
    const int modelWidth = static_cast<int>(Width(Wide(ModelText(state)), type.caption)) + 16;
    const int sendLeft = width - 9 - kSend;
    const int expandLeft = sendLeft - 4 - 26;
    const std::uint64_t used = state.Active().usage.contextTokens;
    const int tokensWidth = used > 0U ? static_cast<int>(Width(Wide(AgentPanelRenderer::TokenCount(used)), type.caption)) + 2 : 0;
    const int tokensLeft = expandLeft - 8 - tokensWidth;
    const int ringLeft = tokensLeft - (tokensWidth > 0 ? 7 : 0) - 16;
    const int inputLeft = kPad + modelWidth + 18;
    const int inputRight = ringLeft - 16;
    const std::vector<std::wstring> inputLines = Wrap(Wide(state.Input()), type.body, static_cast<float>(inputRight - inputLeft), kMaxInputLines);
    const int lineHeight = std::max(kLineMinHeight, static_cast<int>(inputLines.size()) * kTextLine + 28);

    const Latest latest = LatestAnswer(state);
    layout.expanded = state.BarExpanded();
    layout.answerShown = latest.shown;
    int above = 0;
    if (layout.expanded) {
        above = std::min(maximumBubbleHeight - lineHeight, kPanelMaxHeight);
        if (above < 140) {
            above = 0;
            layout.expanded = false;
        }
    } else if (layout.answerShown) {
        const int lines = static_cast<int>(Wrap(Wide(latest.text), type.body, static_cast<float>(width - kPad * 2 - 30), kMaxAnswerLines).size());
        above = lines * kTextLine + 30;
    }
    const int height = above + lineHeight;
    // The picker opens from the model label: inside the unfolded bubble, over the conversation's
    // left side, when it fits there; otherwise above the bubble, in the same window.
    layout.menuShown = state.ModelMenuOpen();
    const int menuWidth = std::min(kMenuMaxWidth, width - 16);
    int menuHeight = layout.menuShown ? LayOutMenu(layout, state, kMargin, kMargin, menuWidth) : 0;
    const bool menuInside = layout.menuShown && layout.expanded && above >= menuHeight + 16;
    const int menuSpace = layout.menuShown && !menuInside ? menuHeight + kMenuGap : 0;
    layout.windowWidth = width + kMargin * 2;
    layout.windowHeight = height + menuSpace + kMargin * 2;
    layout.menu = RECT{ kMargin, kMargin, kMargin + menuWidth, kMargin + menuHeight };
    const int left = kMargin;
    const int top = kMargin + menuSpace;
    layout.bubble = RECT{ left, top, left + width, top + height };
    layout.line = RECT{ left, top + above, left + width, top + height };

    // Controls sit on the request line's last text line, so a growing request lifts only the text.
    const int centre = static_cast<int>(layout.line.bottom) - kLineMinHeight / 2;
    layout.model = RECT{ left + kPad - 6, centre - 14, left + kPad + modelWidth, centre + 14 };
    layout.ring = RECT{ left + ringLeft, centre - 8, left + ringLeft + 16, centre + 8 };
    layout.tokens = RECT{ left + tokensLeft, centre - 10, left + tokensLeft + tokensWidth, centre + 10 };
    layout.expand = RECT{ left + expandLeft, centre - 13, left + expandLeft + 26, centre + 13 };
    layout.send = RECT{ left + sendLeft, centre - kSend / 2, left + sendLeft + kSend, centre + kSend / 2 };
    layout.input = RECT{ left + inputLeft, layout.line.top + 10, left + inputRight, layout.line.bottom - 10 };

    if (menuInside) {
        const int menuTop = static_cast<int>(layout.line.top) - 8 - menuHeight;
        menuHeight = LayOutMenu(layout, state, left + 8, menuTop, menuWidth);
        layout.menu = RECT{ left + 8, menuTop, left + 8 + menuWidth, menuTop + menuHeight };
    }
    if (layout.answerShown && !layout.expanded) {
        layout.answer = RECT{ left, top, left + width, top + above };
        layout.answerClose = RECT{ left + width - 36, top + 10, left + width - 12, top + 34 };
    }
    if (layout.expanded) {
        int x = left + kPad;
        const int usageWidth = static_cast<int>(Width(UsageText(state.Active().usage), type.caption));
        const int tabsRight = left + width - 52 - usageWidth - 24;
        for (int index = 0; index < state.ConversationCount(); ++index) {
            const bool active = index == state.ActiveConversation();
            const int tabWidth = std::min(180, static_cast<int>(Width(Wide(state.Conversation(index).title), active ? type.strong : type.caption)) + 2);
            if (x + tabWidth + (active ? 22 : 0) > tabsRight) break;
            layout.tabs.push_back(RECT{ x, top + 8, x + tabWidth, top + kHeaderHeight - 8 });
            x += tabWidth;
            if (active) {
                layout.closeTab = RECT{ x + 2, top + 12, x + 20, top + kHeaderHeight - 12 };
                x += 20;
            }
            x += 18;
        }
        layout.newTab = RECT{ x - 4, top + 10, x + 18, top + kHeaderHeight - 10 };
        layout.dock = RECT{ left + width - 16 - 22, top + 9, left + width - 16, top + kHeaderHeight - 9 };
        layout.usage = RECT{ layout.dock.left - 12 - usageWidth, top + 8, layout.dock.left - 12, top + kHeaderHeight - 8 };
        layout.conversation = RECT{ left + kPad, top + kHeaderHeight + 14, left + width - kPad, top + above - 14 };
    }
    return layout;
}

RECT AgentBarRenderer::WindowRect(const RECT& sceneContent, const EditorAgentPanelState& state) {
    const int contentWidth = static_cast<int>(sceneContent.right - sceneContent.left);
    const int contentHeight = static_cast<int>(sceneContent.bottom - sceneContent.top);
    const int width = std::clamp(contentWidth * 3 / 4, std::min(480, contentWidth - 32), std::max(0, contentWidth - 32));
    if (width < 300 || contentHeight < 140) {
        return RECT{};
    }
    // Clear of the Scene View's toolbar at the top, 18 px above its bottom edge.
    const AgentBarLayout layout = Resolve(width, contentHeight - 72, state);
    const int left = static_cast<int>(sceneContent.left) + (contentWidth - width) / 2 - kMargin;
    const int bottom = static_cast<int>(sceneContent.bottom) - 18 + kMargin;
    return RECT{ left, bottom - layout.windowHeight, left + layout.windowWidth, bottom };
}

int AgentBarRenderer::MaximumScroll(const AgentBarLayout& layout, const EditorAgentPanelState& state) {
    if (!layout.expanded) return 0;
    const std::vector<Block> blocks = Blocks(state, static_cast<float>(layout.conversation.right - layout.conversation.left));
    return std::max(0, BlocksHeight(blocks) - static_cast<int>(layout.conversation.bottom - layout.conversation.top));
}

void AgentBarRenderer::Paint(void* pixels, const AgentBarLayout& layout, const EditorAgentPanelState& state, POINT hover) {
    Typography& type = Type();
    std::memset(pixels, 0, static_cast<std::size_t>(layout.windowWidth) * static_cast<std::size_t>(layout.windowHeight) * 4U);
    Gdiplus::Bitmap bitmap(layout.windowWidth, layout.windowHeight, layout.windowWidth * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(pixels));
    Gdiplus::Graphics graphics(&bitmap);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

    const bool focused = state.InputFocused();
    const Gdiplus::RectF bubble = ToRectF(layout.bubble);
    const float radius = layout.line.top > layout.bubble.top ? 18.0F : bubble.Height / 2.0F;

    // A soft shadow under the bubble, a little lower than it.
    for (int step = 12; step >= 1; step -= 2) {
        Gdiplus::GraphicsPath shadow;
        Gdiplus::RectF spread = bubble;
        spread.Inflate(static_cast<float>(step), static_cast<float>(step));
        spread.Offset(0.0F, 5.0F);
        RoundedPath(shadow, spread, radius + static_cast<float>(step));
        const Gdiplus::SolidBrush brush(Gdiplus::Color(static_cast<BYTE>(8 + (12 - step) / 2), 0, 0, 0));
        graphics.FillPath(&brush, &shadow);
    }
    Gdiplus::GraphicsPath outline;
    RoundedPath(outline, bubble, radius);
    const Gdiplus::SolidBrush fill(kFill);
    graphics.FillPath(&fill, &outline);
    const Gdiplus::Pen border(Gdiplus::Color(focused ? 46 : 26, 255, 255, 255), 1.0F);
    graphics.DrawPath(&border, &outline);
    const Gdiplus::Pen hairline(Gdiplus::Color(18, 255, 255, 255), 1.0F);
    if (layout.line.top > layout.bubble.top) {
        graphics.DrawLine(&hairline, bubble.X + 16.0F, static_cast<float>(layout.line.top), bubble.GetRight() - 16.0F, static_cast<float>(layout.line.top));
    }

    // The latest answer.
    if (layout.answerShown && !layout.expanded) {
        const Latest latest = LatestAnswer(state);
        const float textLeft = static_cast<float>(layout.answer.left + kPad);
        float y = static_cast<float>(layout.answer.top) + 15.0F;
        if (latest.running) {
            const Gdiplus::SolidBrush dot(kAmber);
            graphics.FillEllipse(&dot, textLeft, y + 7.0F, 6.0F, 6.0F);
        }
        const float indent = latest.running ? 14.0F : 0.0F;
        const Gdiplus::Color color = latest.error ? kDanger : latest.running ? kDim : kText;
        const float textWidth = static_cast<float>(layout.answer.right - layout.answer.left - kPad * 2 - 30);
        for (const std::wstring& line : Wrap(Wide(latest.text), type.body, textWidth - indent, kMaxAnswerLines)) {
            DrawLabel(graphics, line, type.body, color, textLeft + indent, y);
            y += kTextLine;
        }
        Cross(graphics, layout.answerClose, kFaint);
    }

    // The unfolded conversation.
    if (layout.expanded) {
        for (std::size_t index = 0; index < layout.tabs.size(); ++index) {
            const RECT& tab = layout.tabs[index];
            const bool active = static_cast<int>(index) == state.ActiveConversation();
            const EditorAgentConversation& conversation = state.Conversation(static_cast<int>(index));
            const std::wstring full = Wide(conversation.title);
            std::wstring title = full;
            const Gdiplus::Font& font = active ? type.strong : type.caption;
            while (title.size() > 1U && Width(title + L"\x2026", font) > static_cast<float>(tab.right - tab.left)) {
                title.pop_back();
            }
            if (title.size() < full.size()) title += L"\x2026";
            DrawLabel(graphics, title, font, active ? kText : kFaint, static_cast<float>(tab.left), static_cast<float>(tab.top) + 4.0F);
            if (conversation.running) {
                const Gdiplus::SolidBrush dot(kAmber);
                graphics.FillEllipse(&dot, static_cast<float>(tab.left) - 10.0F, static_cast<float>(tab.top) + 10.0F, 5.0F, 5.0F);
            }
        }
        if (layout.closeTab.right > layout.closeTab.left) {
            Cross(graphics, layout.closeTab, kFaint);
        }
        {
            Gdiplus::Pen plus(kFaint, 1.4F);
    Round(plus);
            const float x = (layout.newTab.left + layout.newTab.right) / 2.0F;
            const float y = (layout.newTab.top + layout.newTab.bottom) / 2.0F;
            graphics.DrawLine(&plus, x - 5.0F, y, x + 5.0F, y);
            graphics.DrawLine(&plus, x, y - 5.0F, x, y + 5.0F);
        }
        DrawLabel(graphics, UsageText(state.Active().usage), type.caption, kFaint, static_cast<float>(layout.usage.left), static_cast<float>(layout.usage.top) + 4.0F);
        {
            // Dock into the layout: a window whose bottom panel is taken.
            Gdiplus::Pen pen(kDim, 1.4F);
            Round(pen);
            const float x = (layout.dock.left + layout.dock.right) / 2.0F;
            const float y = (layout.dock.top + layout.dock.bottom) / 2.0F;
            Gdiplus::GraphicsPath frame;
            RoundedPath(frame, Gdiplus::RectF{ x - 7.5F, y - 6.5F, 15.0F, 13.0F }, 2.5F);
            graphics.DrawPath(&pen, &frame);
            const Gdiplus::SolidBrush panel(kDim);
            graphics.FillRectangle(&panel, x - 6.8F, y + 1.5F, 13.6F, 4.3F);
        }
        const float headerBottom = static_cast<float>(layout.bubble.top + kHeaderHeight);
        graphics.DrawLine(&hairline, bubble.X + 16.0F, headerBottom, bubble.GetRight() - 16.0F, headerBottom);

        const RECT& area = layout.conversation;
        const std::vector<Block> blocks = Blocks(state, static_cast<float>(area.right - area.left));
        if (blocks.empty()) {
            const std::wstring hint = Wide(EditorText("agent.bar.empty", "Ask for anything in this scene. The conversation shows here."));
            DrawLabel(graphics, hint, type.caption, kFaint, static_cast<float>(area.left), static_cast<float>(area.top));
        } else {
            const int total = BlocksHeight(blocks);
            const int visible = static_cast<int>(area.bottom - area.top);
            const int scroll = std::clamp(state.Active().scrollFromBottom, 0, std::max(0, total - visible));
            float y = static_cast<float>(total <= visible ? area.top : area.bottom - total + scroll);
            graphics.SetClip(ToRectF(area));
            for (const Block& block : blocks) {
                y += static_cast<float>(block.gapBefore);
                const Gdiplus::Font& font = block.kind == Block::Kind::Tools ? type.caption : type.body;
                const Gdiplus::Color color = block.kind == Block::Kind::Request ? kDim
                    : block.kind == Block::Kind::Tools ? kFaint
                    : block.kind == Block::Kind::Error ? kDanger
                    : kText;
                for (const std::wstring& line : block.lines) {
                    if (y + kTextLine >= area.top && y <= area.bottom) {
                        DrawLabel(graphics, line, font, color, static_cast<float>(area.left), y);
                    }
                    y += kTextLine;
                }
            }
            graphics.ResetClip();
        }
    }

    // The request line: model, the request, context ring, tokens, unfold, send.
    DrawLabel(graphics, Wide(ModelText(state)), type.caption, kDim, static_cast<float>(layout.model.left + 6), static_cast<float>(layout.model.top) + 6.0F);
    Chevron(graphics, static_cast<float>(layout.model.right) - 5.0F, static_cast<float>(layout.model.top + layout.model.bottom) / 2.0F + 1.0F, 3.0F, false, kFaint);

    const float inputLeft = static_cast<float>(layout.input.left);
    const float inputWidth = static_cast<float>(layout.input.right - layout.input.left);
    const std::vector<std::wstring> lines = Wrap(Wide(state.Input()), type.body, inputWidth, kMaxInputLines);
    const float lastLine = static_cast<float>(layout.line.bottom) - static_cast<float>(kLineMinHeight) / 2.0F - static_cast<float>(kTextLine) / 2.0F + 1.0F;
    const float firstLine = lastLine - static_cast<float>(lines.size() - 1U) * kTextLine;
    if (state.Input().empty()) {
        DrawLabel(graphics, Wide(EditorText("agent.bar.placeholder", "Ask the agent to change this scene")), type.body, kFaint, inputLeft, lastLine);
    } else {
        float y = firstLine;
        for (const std::wstring& line : lines) {
            DrawLabel(graphics, line, type.body, kText, inputLeft, y);
            y += kTextLine;
        }
    }
    if (focused) {
        const float caretX = inputLeft + (state.Input().empty() ? 0.0F : Width(lines.back(), type.body)) + 1.0F;
        const Gdiplus::Pen caret(kAccent, 1.5F);
        graphics.DrawLine(&caret, caretX, lastLine + 1.0F, caretX, lastLine + 17.0F);
    }

    const EditorAgentUsage& usage = state.Active().usage;
    const double share = ContextShare(usage);
    const Gdiplus::RectF ring{ static_cast<float>(layout.ring.left) + 1.0F, static_cast<float>(layout.ring.top) + 1.0F, 14.0F, 14.0F };
    const Gdiplus::Pen track(Gdiplus::Color(34, 255, 255, 255), 2.0F);
    graphics.DrawEllipse(&track, ring);
    if (share > 0.0) {
        Gdiplus::Pen arc(share >= 0.9 ? kDanger : share >= 0.7 ? kAmber : kAccent, 2.0F);
    Round(arc);
        graphics.DrawArc(&arc, ring, -90.0F, static_cast<float>(std::max(share, 0.02) * 360.0));
    }
    if (layout.tokens.right > layout.tokens.left) {
        DrawLabel(graphics, Wide(AgentPanelRenderer::TokenCount(usage.contextTokens)), type.caption, kFaint,
            static_cast<float>(layout.tokens.left), static_cast<float>(layout.tokens.top) + 2.0F);
    }
    Chevron(graphics, static_cast<float>(layout.expand.left + layout.expand.right) / 2.0F, static_cast<float>(layout.expand.top + layout.expand.bottom) / 2.0F,
        4.5F, !state.BarExpanded(), kDim);

    const bool running = state.Running();
    const bool canSend = running || (state.Current() != nullptr && !state.Input().empty());
    const Gdiplus::RectF send = ToRectF(layout.send);
    const Gdiplus::SolidBrush sendFill(canSend ? kText : Gdiplus::Color(20, 255, 255, 255));
    graphics.FillEllipse(&sendFill, send);
    const float sendX = send.X + send.Width / 2.0F;
    const float sendY = send.Y + send.Height / 2.0F;
    const Gdiplus::Color glyph = canSend ? Gdiplus::Color(255, 21, 23, 27) : kFaint;
    if (running) {
        Gdiplus::GraphicsPath square;
        RoundedPath(square, Gdiplus::RectF{ sendX - 4.5F, sendY - 4.5F, 9.0F, 9.0F }, 1.5F);
        const Gdiplus::SolidBrush brush(glyph);
        graphics.FillPath(&brush, &square);
    } else {
        Gdiplus::Pen pen(glyph, 1.8F);
    Round(pen);
        graphics.DrawLine(&pen, sendX, sendY + 5.5F, sendX, sendY - 5.5F);
        const Gdiplus::PointF head[] = { { sendX - 4.5F, sendY - 1.0F }, { sendX, sendY - 5.5F }, { sendX + 4.5F, sendY - 1.0F } };
        graphics.DrawLines(&pen, head, 3);
    }

    if (layout.menuShown) {
        PaintMenu(graphics, layout, state, hover);
    }
    graphics.Flush(Gdiplus::FlushIntentionSync);
}

AgentBarHit AgentBarRenderer::HitTest(const AgentBarLayout& layout, const EditorAgentPanelState& state, int x, int y) {
    if (layout.menuShown && Inside(layout.menu, x, y)) {
        for (std::size_t index = 0; index < layout.menuAgents.size(); ++index) {
            if (Inside(layout.menuAgents[index], x, y)) return { AgentBarHitKind::MenuAgent, static_cast<int>(index) };
        }
        for (std::size_t index = 0; index < layout.menuModels.size(); ++index) {
            if (Inside(layout.menuModels[index], x, y)) return { AgentBarHitKind::MenuModel, static_cast<int>(index) };
        }
        for (std::size_t index = 0; index < layout.menuEfforts.size(); ++index) {
            if (Inside(layout.menuEfforts[index], x, y)) return { AgentBarHitKind::MenuEffort, static_cast<int>(index) };
        }
        if (Inside(layout.menuRefresh, x, y)) return { AgentBarHitKind::MenuRefresh };
        if (Inside(layout.menuAddAgent, x, y)) return { AgentBarHitKind::MenuAddAgent };
        return { AgentBarHitKind::MenuPanel };
    }
    if (!Inside(layout.bubble, x, y)) return {};
    if (layout.expanded && y < layout.line.top) {
        for (std::size_t index = 0; index < layout.tabs.size(); ++index) {
            if (Inside(layout.tabs[index], x, y)) return { AgentBarHitKind::Tab, static_cast<int>(index) };
        }
        if (Inside(layout.closeTab, x, y)) return { AgentBarHitKind::CloseTab, state.ActiveConversation() };
        if (Inside(layout.newTab, x, y)) return { AgentBarHitKind::NewTab };
        if (Inside(layout.dock, x, y)) return { AgentBarHitKind::Dock };
        return { AgentBarHitKind::Conversation };
    }
    if (layout.answerShown && y < layout.line.top) {
        return { Inside(layout.answerClose, x, y) ? AgentBarHitKind::AnswerClose : AgentBarHitKind::Answer };
    }
    if (Inside(layout.model, x, y)) return { AgentBarHitKind::Model };
    if (Inside(layout.send, x, y)) return { AgentBarHitKind::Send };
    if (Inside(layout.expand, x, y)) return { AgentBarHitKind::Expand };
    return { AgentBarHitKind::Input };
}

} // namespace kb::editor

#endif
