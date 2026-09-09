#include "private/ui/ScreenUITextMarkupParser.hpp"

#include "engine/input/InputText.hpp"

#include <cstddef>

namespace kb::render {
namespace {

[[nodiscard]] int HexDigit(char value) noexcept {
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

[[nodiscard]] bool ParseHexByte(std::string_view text, std::size_t offset, float& value) noexcept {
    const int high = HexDigit(text[offset]);
    const int low = HexDigit(text[offset + 1U]);
    if (high < 0 || low < 0)
        return false;
    value = static_cast<float>((high << 4) | low) / 255.0F;
    return true;
}

[[nodiscard]] bool ParseColorTag(std::string_view text, std::size_t offset, std::array<float, 4U>& color,
                                 std::size_t& length) noexcept {
    constexpr std::string_view prefix = "<color=#";
    if (!text.substr(offset).starts_with(prefix))
        return false;
    const std::size_t digits =
        text.size() > offset + prefix.size() + 6U && text[offset + prefix.size() + 6U] == '>' ? 6U : 8U;
    length = prefix.size() + digits + 1U;
    if (offset + length > text.size() || text[offset + length - 1U] != '>')
        return false;
    color = {1.0F, 1.0F, 1.0F, 1.0F};
    for (std::size_t channel = 0U; channel < digits / 2U; ++channel) {
        if (!ParseHexByte(text, offset + prefix.size() + channel * 2U, color[channel])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool AppendText(std::string_view text, std::size_t baseOffset, const std::array<float, 4U>& color,
                              ScreenUITextMarkup& output) {
    if (text.empty())
        return true;
    std::vector<char32_t> decoded(text.size());
    const kb::input::Utf8DecodeResult result = kb::input::DecodeUtf8(text, decoded);
    if (!result.wellFormed || result.truncated)
        return false;
    output.glyphs.reserve(output.glyphs.size() + result.codePointCount);
    // The decode above already proved the sequence is well formed, so walking continuation
    // bytes here cannot run past the end.
    std::size_t byteOffset = 0U;
    for (std::size_t index = 0U; index < result.codePointCount; ++index) {
        output.glyphs.push_back(ScreenUITextMarkupGlyph{
            .codepoint = static_cast<std::uint32_t>(decoded[index]),
            .color = color,
            .sourceOffset = static_cast<std::uint32_t>(baseOffset + byteOffset),
        });
        do {
            ++byteOffset;
        } while (byteOffset < text.size() && (static_cast<unsigned char>(text[byteOffset]) & 0xC0U) == 0x80U);
    }
    return true;
}

} // namespace

ScreenUITextMarkup ScreenUITextMarkupParser::Parse(std::string_view text, bool enabled) const {
    ScreenUITextMarkup output;
    const std::array<float, 4U> white{1.0F, 1.0F, 1.0F, 1.0F};
    if (!enabled) {
        output.succeeded = AppendText(text, 0U, white, output);
        return output;
    }

    std::vector<std::array<float, 4U>> colors{white};
    std::size_t segmentStart = 0U;
    std::size_t cursor = 0U;
    while (cursor < text.size()) {
        std::size_t tagLength = 0U;
        std::array<float, 4U> tagColor{};
        enum class Tag : std::uint8_t { None, Color, EndColor, Break };
        Tag tag = Tag::None;
        if (ParseColorTag(text, cursor, tagColor, tagLength)) {
            tag = Tag::Color;
        } else if (colors.size() > 1U && text.substr(cursor).starts_with("</color>")) {
            tag = Tag::EndColor;
            tagLength = 8U;
        } else if (text.substr(cursor).starts_with("<br>")) {
            tag = Tag::Break;
            tagLength = 4U;
        } else if (text.substr(cursor).starts_with("<br/>")) {
            tag = Tag::Break;
            tagLength = 5U;
        }
        if (tag == Tag::None) {
            ++cursor;
            continue;
        }
        if (!AppendText(text.substr(segmentStart, cursor - segmentStart), segmentStart, colors.back(), output)) {
            output.glyphs.clear();
            output.succeeded = false;
            return output;
        }
        if (tag == Tag::Color) {
            colors.push_back(tagColor);
        } else if (tag == Tag::EndColor) {
            colors.pop_back();
        } else {
            output.glyphs.push_back(ScreenUITextMarkupGlyph{
                .codepoint = static_cast<std::uint32_t>('\n'),
                .color = colors.back(),
                .sourceOffset = static_cast<std::uint32_t>(cursor),
            });
        }
        cursor += tagLength;
        segmentStart = cursor;
    }
    output.succeeded = AppendText(text.substr(segmentStart), segmentStart, colors.back(), output);
    if (!output.succeeded)
        output.glyphs.clear();
    return output;
}

} // namespace kb::render
