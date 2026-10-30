#include "private/ui/ScreenUITextMarkupParser.hpp"

#include "engine/input/InputText.hpp"

#include <algorithm>
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
                              ScreenUITextMarkup& output, std::uint32_t link = 0U, bool underline = false) {
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
            .link = link,
            .underline = underline,
        });
        do {
            ++byteOffset;
        } while (byteOffset < text.size() && (static_cast<unsigned char>(text[byteOffset]) & 0xC0U) == 0x80U);
    }
    return true;
}

// Return the end of a complete candidate, including nested candidates, so a malformed
// table can be emitted literally without consuming its inline markup.
[[nodiscard]] std::size_t TableCandidateEnd(std::string_view text, std::size_t start) {
    std::size_t cursor = start + 7U;
    std::size_t depth = 1U;
    while (cursor < text.size()) {
        if (text.substr(cursor).starts_with("<table>")) { ++depth; cursor += 7U; }
        else if (text.substr(cursor).starts_with("</table>")) {
            cursor += 8U;
            if (--depth == 0U) return cursor;
        } else ++cursor;
    }
    return text.size();
}

[[nodiscard]] bool ValidTable(std::string_view text, std::size_t start, std::size_t end) {
    if (end < start + 15U || text.substr(end - 8U, 8U) != "</table>") return false;
    constexpr std::size_t maxRows = 64U, maxColumns = 64U, maxCells = 1024U;
    bool row = false, cell = false;
    std::size_t rows = 0U, columns = 0U, expectedColumns = 0U, cells = 0U;
    std::size_t cursor = start + 7U;
    const std::size_t close = end - 8U;
    while (cursor < close) {
        const auto remaining = text.substr(cursor, close - cursor);
        if (remaining.starts_with("<tr>")) {
            if (row || cell || ++rows > maxRows) return false;
            row = true; columns = 0U; cursor += 4U;
        } else if (remaining.starts_with("</tr>")) {
            if (!row || cell || columns == 0U || (expectedColumns != 0U && columns != expectedColumns)) return false;
            expectedColumns = columns; row = false; cursor += 5U;
        } else if (remaining.starts_with("<td>")) {
            if (!row || cell || ++columns > maxColumns || ++cells > maxCells) return false;
            cell = true; cursor += 4U;
        } else if (remaining.starts_with("</td>")) {
            if (!cell) return false;
            cell = false; cursor += 5U;
        } else if (remaining.starts_with("<table>") || remaining.starts_with("</table>")) return false;
        else if (!cell) {
            const char c = text[cursor++];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
        } else {
            ++cursor;
        }
    }
    return !row && !cell && rows != 0U;
}

} // namespace

ScreenUITextMarkup ScreenUITextMarkupParser::Parse(std::string_view text, bool enabled, bool parseLinks, bool parseTables) const {
    ScreenUITextMarkup output;
    const std::array<float, 4U> white{1.0F, 1.0F, 1.0F, 1.0F};
    if (!enabled) {
        output.succeeded = AppendText(text, 0U, white, output);
        if (output.succeeded && parseTables && !output.glyphs.empty())
            output.blocks.push_back({false, {0U, output.glyphs.size()}, {}});
        return output;
    }

    std::vector<std::array<float, 4U>> colors{white};
    std::vector<std::uint32_t> links{0U};
    std::size_t underlineDepth = 0U;
    std::size_t segmentStart = 0U;
    std::size_t textBlockStart = 0U;
    std::size_t tableEnd = 0U;
    std::size_t cellBegin = 0U;
    std::vector<std::array<float, 4U>> inheritedColors;
    std::vector<std::uint32_t> inheritedLinks;
    std::size_t inheritedUnderline = 0U;
    const auto appendTextBlock = [&]() {
        if (parseTables && output.glyphs.size() > textBlockStart)
            output.blocks.push_back({false, {textBlockStart, output.glyphs.size()}, {}});
        textBlockStart = output.glyphs.size();
    };
    std::size_t cursor = 0U;
    while (cursor < text.size()) {
        if (parseTables && tableEnd == 0U && text.substr(cursor).starts_with("<table>")) {
            const std::size_t end = TableCandidateEnd(text, cursor);
            if (!ValidTable(text, cursor, end)) { cursor = end; continue; }
            if (!AppendText(text.substr(segmentStart, cursor - segmentStart), segmentStart, colors.back(), output,
                            links.back(), underlineDepth != 0U)) {
                output = {}; output.succeeded = false; return output;
            }
            appendTextBlock();
            output.blocks.push_back({true, {}, {}});
            tableEnd = end;
            inheritedColors = colors; inheritedLinks = links; inheritedUnderline = underlineDepth;
            cursor += 7U; segmentStart = cursor;
            continue;
        }
        if (tableEnd != 0U) {
            std::size_t structuralLength = 0U;
            enum class Structure { None, Row, EndRow, Cell, EndCell, EndTable };
            Structure structure = Structure::None;
            const auto remaining = text.substr(cursor);
            if (remaining.starts_with("<tr>")) { structure = Structure::Row; structuralLength = 4U; }
            else if (remaining.starts_with("</tr>")) { structure = Structure::EndRow; structuralLength = 5U; }
            else if (remaining.starts_with("<td>")) { structure = Structure::Cell; structuralLength = 4U; }
            else if (remaining.starts_with("</td>")) { structure = Structure::EndCell; structuralLength = 5U; }
            else if (cursor + 8U == tableEnd && remaining.starts_with("</table>")) { structure = Structure::EndTable; structuralLength = 8U; }
            if (structure != Structure::None) {
                if (structure == Structure::EndCell &&
                    !AppendText(text.substr(segmentStart, cursor - segmentStart), segmentStart, colors.back(), output,
                                links.back(), underlineDepth != 0U)) {
                    output = {}; output.succeeded = false; return output;
                }
                if (structure == Structure::Row) output.blocks.back().rows.emplace_back();
                if (structure == Structure::Cell) cellBegin = output.glyphs.size();
                if (structure == Structure::EndCell) {
                    output.blocks.back().rows.back().cells.push_back({cellBegin, output.glyphs.size()});
                    colors = inheritedColors; links = inheritedLinks; underlineDepth = inheritedUnderline;
                }
                if (structure == Structure::EndTable) {
                    tableEnd = 0U;
                    textBlockStart = output.glyphs.size();
                }
                cursor += structuralLength; segmentStart = cursor;
                continue;
            }
        }
        std::size_t tagLength = 0U;
        std::array<float, 4U> tagColor{};
        std::string_view linkTarget;
        enum class Tag : std::uint8_t { None, Color, EndColor, Break, Link, EndLink, Underline, EndUnderline };
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
        } else if (parseLinks && text.substr(cursor).starts_with("<link=")) {
            std::size_t end = cursor + 6U;
            while (end < text.size() && end - cursor - 6U < 256U && text[end] != '<' && text[end] != '>') ++end;
            if (end < text.size() && text[end] == '>') {
                linkTarget = text.substr(cursor + 6U, end - cursor - 6U);
                // Targets are opaque event identifiers, never opened by the parser.
                // Keep control characters and nested tags literal.
                if (!linkTarget.empty() && linkTarget.size() < 256U &&
                    std::ranges::all_of(linkTarget, [](unsigned char c) {
                        return c >= 0x20U && c <= 0x7EU && c != '<' && c != '>';
                    })) {
                    tag = Tag::Link;
                    tagLength = end - cursor + 1U;
                }
            }
        } else if (parseLinks && links.size() > 1U && text.substr(cursor).starts_with("</link>")) {
            tag = Tag::EndLink;
            tagLength = 7U;
        } else if (text.substr(cursor).starts_with("<u>")) {
            tag = Tag::Underline;
            tagLength = 3U;
        } else if (underlineDepth > 0U && text.substr(cursor).starts_with("</u>")) {
            tag = Tag::EndUnderline;
            tagLength = 4U;
        }
        if (tag == Tag::None) {
            ++cursor;
            continue;
        }
        if (!AppendText(text.substr(segmentStart, cursor - segmentStart), segmentStart, colors.back(), output,
                        links.back(), underlineDepth != 0U)) {
            output.glyphs.clear();
            output.links.clear();
            output.blocks.clear();
            output.succeeded = false;
            return output;
        }
        if (tag == Tag::Color) {
            colors.push_back(tagColor);
        } else if (tag == Tag::EndColor) {
            colors.pop_back();
        } else if (tag == Tag::Link) {
            output.links.emplace_back(linkTarget);
            links.push_back(static_cast<std::uint32_t>(output.links.size()));
        } else if (tag == Tag::EndLink) {
            links.pop_back();
        } else if (tag == Tag::Underline) {
            ++underlineDepth;
        } else if (tag == Tag::EndUnderline) {
            --underlineDepth;
        } else {
            output.glyphs.push_back(ScreenUITextMarkupGlyph{
                .codepoint = static_cast<std::uint32_t>('\n'),
                .color = colors.back(),
                .sourceOffset = static_cast<std::uint32_t>(cursor),
                .link = links.back(),
                .underline = underlineDepth != 0U,
            });
        }
        cursor += tagLength;
        segmentStart = cursor;
    }
    output.succeeded = AppendText(text.substr(segmentStart), segmentStart, colors.back(), output,
                                  links.back(), underlineDepth != 0U);
    if (!output.succeeded) {
        output.glyphs.clear();
        output.links.clear();
        output.blocks.clear();
    } else {
        appendTextBlock();
    }
    return output;
}

} // namespace kb::render
