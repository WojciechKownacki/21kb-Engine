#include "scene/EditorViewportMessages.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace kb::editor {
namespace {

constexpr float kTextHeight = 13.0F;
constexpr float kLineHeight = 17.0F;
constexpr float kPadding = 8.0F;
constexpr float kGap = 6.0F;
constexpr float kMargin = 10.0F;
constexpr float kRadius = 6.0F;
// Roboto's average advance, to size a plate without measuring text.
constexpr float kAverageGlyph = 0.54F;

// The editor theme's panel and text colors and the status bar's warning and error colors.
constexpr std::array<std::uint8_t, 4> kPlateColor{ 36U, 36U, 36U, 255U };
constexpr std::array<std::uint8_t, 4> kTextColor{ 226U, 226U, 226U, 255U };
constexpr std::array<std::uint8_t, 4> kWarningBorder{ 224U, 165U, 54U, 255U };
constexpr std::array<std::uint8_t, 4> kErrorBorder{ 232U, 88U, 88U, 255U };

[[nodiscard]] std::size_t Glyphs(std::string_view text) noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(text, [](char c) { return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U; }));
}

// The bytes of the first `glyphs` characters of a UTF-8 text.
[[nodiscard]] std::size_t Bytes(std::string_view text, std::size_t glyphs) noexcept {
    std::size_t counted = 0U;
    for (std::size_t index = 0U; index < text.size(); ++index) {
        if ((static_cast<unsigned char>(text[index]) & 0xC0U) != 0x80U && counted++ == glyphs) {
            return index;
        }
    }
    return text.size();
}

[[nodiscard]] bool IsBlank(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r';
}

void WrapParagraph(std::string_view paragraph, std::size_t columns, std::vector<std::string>& lines) {
    std::string line;
    std::size_t used = 0U;
    const auto flush = [&] {
        if (used > 0U) {
            lines.push_back(std::move(line));
            line.clear();
            used = 0U;
        }
    };
    std::size_t position = 0U;
    while (position < paragraph.size()) {
        while (position < paragraph.size() && IsBlank(paragraph[position])) ++position;
        const std::size_t start = position;
        while (position < paragraph.size() && !IsBlank(paragraph[position])) ++position;
        std::string_view word = paragraph.substr(start, position - start);
        while (Glyphs(word) > columns) {
            flush();
            const std::size_t cut = Bytes(word, columns);
            lines.emplace_back(word.substr(0U, cut));
            word.remove_prefix(cut);
        }
        const std::size_t length = Glyphs(word);
        if (length == 0U) continue;
        if (used > 0U && used + 1U + length > columns) flush();
        line += used > 0U ? " " : "";
        line += word;
        used += (used > 0U ? 1U : 0U) + length;
    }
    flush();
}

} // namespace

std::vector<EditorViewportMessage> EditorViewportMessages::Recent(const EditorConsoleState& console, std::uint64_t nowMs) {
    std::vector<EditorViewportMessage> messages;
    const std::vector<EditorConsoleEntry>& entries = console.Entries();
    for (auto entry = entries.rbegin(); entry != entries.rend() && messages.size() < kMaxShown; ++entry) {
        if (entry->timestampMs < nowMs && nowMs - entry->timestampMs > kShownMilliseconds) break;
        if (entry->level != EditorConsoleLevel::Info) {
            messages.push_back(EditorViewportMessage{ .level = entry->level, .text = entry->message });
        }
    }
    std::ranges::reverse(messages);
    return messages;
}

std::vector<std::string> EditorViewportMessages::Wrap(std::string_view text, std::size_t columns, std::size_t maxLines) {
    std::vector<std::string> lines;
    if (columns == 0U || maxLines == 0U) return lines;
    for (std::size_t start = 0U; start <= text.size();) {
        const std::size_t end = std::min(text.find('\n', start), text.size());
        WrapParagraph(text.substr(start, end - start), columns, lines);
        start = end + 1U;
    }
    if (lines.size() > maxLines) {
        lines.resize(maxLines);
        std::string& last = lines.back();
        last.resize(Bytes(last, std::min(Glyphs(last), columns - 1U)));
        last += "\xE2\x80\xA6";
    }
    return lines;
}

void EditorViewportMessages::Append(std::vector<EditorSceneViewportTextLabel>& labels, std::vector<EditorSceneViewportOverlayShape>& shapes,
    std::span<const EditorViewportMessage> messages, float x, float y) {
    using Shape = EditorSceneViewportOverlayShape;
    const float glyphWidth = kAverageGlyph * kTextHeight;
    const std::size_t columns = static_cast<std::size_t>((kMaxWidth - 2.0F * kPadding) / glyphWidth);
    float top = y + kMargin;
    const float left = x + kMargin;
    for (const EditorViewportMessage& message : messages) {
        const std::vector<std::string> lines = Wrap(message.text, columns, kMaxLines);
        if (lines.empty()) continue;
        std::size_t widest = 0U;
        for (const std::string& line : lines) widest = std::max(widest, Glyphs(line));
        const float width = std::min(kMaxWidth, std::ceil(static_cast<float>(widest) * glyphWidth) + 2.0F * kPadding);
        const float height = static_cast<float>(lines.size()) * kLineHeight + 2.0F * kPadding;
        // The border is a plate under the plate, one pixel larger on every side.
        shapes.push_back(Shape{ .kind = Shape::Kind::RoundedRect, .points = { { left, top }, { left + width, top + height } },
            .radius = kRadius, .color = message.level == EditorConsoleLevel::Error ? kErrorBorder : kWarningBorder });
        shapes.push_back(Shape{ .kind = Shape::Kind::RoundedRect, .points = { { left + 1.0F, top + 1.0F }, { left + width - 1.0F, top + height - 1.0F } },
            .radius = kRadius - 1.0F, .color = kPlateColor });
        for (std::size_t index = 0U; index < lines.size(); ++index) {
            labels.push_back(EditorSceneViewportTextLabel{ .x = left + kPadding,
                .y = top + kPadding + static_cast<float>(index) * kLineHeight + (kLineHeight - kTextHeight * 1.17F) * 0.5F,
                .pixelHeight = kTextHeight, .text = lines[index], .color = kTextColor, .shadow = false });
        }
        top += height + kGap;
    }
}

} // namespace kb::editor
