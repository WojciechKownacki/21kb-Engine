#include "rendering/ProjectFilesTileTextRenderer.hpp"

#if defined(_WIN32)
#include "rendering/ProjectFilesPanelDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::editor {
namespace {

using Draw = ProjectFilesPanelDrawing;

[[nodiscard]] int TextWidth(HDC dc, std::string_view text) noexcept {
    if (text.empty()) {
        return 0;
    }

    SIZE size{};
    GetTextExtentPoint32A(dc, text.data(), static_cast<int>(text.size()), &size);
    return size.cx;
}

[[nodiscard]] bool FitsText(HDC dc, std::string_view text, int width) noexcept {
    return TextWidth(dc, text) <= width;
}

[[nodiscard]] std::string TrimLeadingBreaks(std::string text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '_' || text.front() == '-')) {
        text.erase(text.begin());
    }
    return text;
}

[[nodiscard]] std::string EllipsizeToWidth(HDC dc, std::string text, int width) {
    if (FitsText(dc, text, width)) {
        return text;
    }

    constexpr std::string_view dots = "...";
    if (!FitsText(dc, dots, width)) {
        return {};
    }

    while (!text.empty()) {
        text.pop_back();
        std::string candidate = text + std::string{ dots };
        if (FitsText(dc, candidate, width)) {
            return candidate;
        }
    }
    return std::string{ dots };
}

[[nodiscard]] std::size_t MaxPrefixThatFits(HDC dc, std::string_view text, int width) {
    std::size_t best = 0;
    for (std::size_t count = 1; count <= text.size(); ++count) {
        if (!FitsText(dc, text.substr(0, count), width)) {
            break;
        }
        best = count;
    }
    return best;
}

[[nodiscard]] std::size_t PreferredLineBreak(HDC dc, std::string_view text, int width) {
    const std::size_t maxPrefix = MaxPrefixThatFits(dc, text, width);
    if (maxPrefix >= text.size()) {
        return text.size();
    }

    for (std::size_t index = maxPrefix; index > 0; --index) {
        const char character = text[index - 1U];
        if ((character == ' ' || character == '_' || character == '-') && FitsText(dc, text.substr(0, index), width)) {
            return index;
        }
    }
    return std::max<std::size_t>(1U, maxPrefix);
}

} // namespace

void ProjectFilesTileTextRenderer::PaintWrapped(HDC dc, RECT rect, const char* text, COLORREF color, int pointSize, int weight) {
    ScopedFont font{ pointSize, weight };
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);

    const int width = Draw::RectWidth(rect);
    const int lineHeight = pointSize + 5;
    std::string value = text == nullptr ? std::string{} : std::string{ text };
    if (value.empty()) {
        return;
    }

    std::vector<std::string> lines;
    if (FitsText(dc, value, width)) {
        lines.push_back(std::move(value));
    } else {
        const std::size_t split = PreferredLineBreak(dc, value, width);
        lines.push_back(std::string{ value.substr(0, split) });
        lines.push_back(EllipsizeToWidth(dc, TrimLeadingBreaks(value.substr(split)), width));
        if (lines[0].empty()) {
            lines[0] = EllipsizeToWidth(dc, value, width);
            lines.resize(1);
        }
        if (lines.size() > 1U && lines[1].empty()) {
            lines.resize(1);
        }
    }

    const int blockHeight = static_cast<int>(lines.size()) * lineHeight;
    int top = rect.top + std::max(0, (Draw::RectHeight(rect) - blockHeight) / 2);
    for (const std::string& line : lines) {
        RECT lineRect{ rect.left, top, rect.right, top + lineHeight };
        DrawTextA(dc, line.c_str(), -1, &lineRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        top += lineHeight;
    }
}

} // namespace kb::editor

#endif
