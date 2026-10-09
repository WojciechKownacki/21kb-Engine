#include "engine/ui/visual/UITextLineBreaking.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace kb::scene {
namespace {

constexpr std::size_t kNoBreak = std::numeric_limits<std::size_t>::max();

// Closing punctuation, small kana, prolonged sound and iteration marks (JIS X 4051 line-start prohibited
// characters, with the matching ASCII, general and full-width punctuation). Sorted for binary search.
constexpr std::array<std::uint32_t, 94U> kLineStartProhibited{ {
    0x0021U, 0x0025U, 0x0029U, 0x002CU, 0x002EU, 0x003AU, 0x003BU, 0x003FU, 0x005DU, 0x007DU,
    0x00B0U, 0x2010U, 0x2013U, 0x2019U, 0x201DU, 0x2025U, 0x2026U, 0x2030U, 0x2032U, 0x2033U,
    0x203AU, 0x203CU, 0x2047U, 0x2048U, 0x2049U, 0x2103U,
    0x3001U, 0x3002U, 0x3005U, 0x3009U, 0x300BU, 0x300DU, 0x300FU, 0x3011U, 0x3015U, 0x3017U,
    0x3019U, 0x301BU, 0x301CU, 0x301EU, 0x301FU, 0x303BU,
    0x3041U, 0x3043U, 0x3045U, 0x3047U, 0x3049U, 0x3063U, 0x3083U, 0x3085U, 0x3087U, 0x308EU,
    0x3095U, 0x3096U, 0x309BU, 0x309CU, 0x309DU, 0x309EU, 0x30A0U,
    0x30A1U, 0x30A3U, 0x30A5U, 0x30A7U, 0x30A9U, 0x30C3U, 0x30E3U, 0x30E5U, 0x30E7U, 0x30EEU,
    0x30F5U, 0x30F6U, 0x30FBU, 0x30FCU, 0x30FDU, 0x30FEU,
    0xFE50U, 0xFE51U, 0xFE52U, 0xFE54U, 0xFE55U, 0xFE56U, 0xFE57U, 0xFE5AU, 0xFE5CU, 0xFE5EU,
    0xFF01U, 0xFF05U, 0xFF09U, 0xFF0CU, 0xFF0EU, 0xFF1AU, 0xFF1BU, 0xFF1FU, 0xFF3DU,
} };

// Additional line-start prohibited characters past the table: full-width right brackets and the half-width
// punctuation and small katakana block.
constexpr std::array<std::uint32_t, 8U> kLineStartProhibitedHigh{ {
    0xFF5DU, 0xFF60U, 0xFF61U, 0xFF63U, 0xFF64U, 0xFF65U, 0xFF9EU, 0xFF9FU,
} };

// Opening brackets and quotes, and currency signs written before amounts. Sorted for binary search.
constexpr std::array<std::uint32_t, 27U> kLineEndProhibited{ {
    0x0028U, 0x005BU, 0x007BU, 0x2018U, 0x201CU, 0x2039U,
    0x3008U, 0x300AU, 0x300CU, 0x300EU, 0x3010U, 0x3014U, 0x3016U, 0x3018U, 0x301AU, 0x301DU,
    0xFE59U, 0xFE5BU, 0xFE5DU,
    0xFF04U, 0xFF08U, 0xFF3BU, 0xFF5BU, 0xFF5FU, 0xFF62U, 0xFFE1U, 0xFFE5U,
} };

[[nodiscard]] bool IsBreakSpace(std::uint32_t codepoint) noexcept {
    return codepoint == static_cast<std::uint32_t>(' ') || codepoint == 0x3000U;
}

[[nodiscard]] bool InRange(std::uint32_t codepoint, std::uint32_t first, std::uint32_t last) noexcept {
    return codepoint >= first && codepoint <= last;
}

} // namespace

bool IsUITextBreakableIdeographic(std::uint32_t codepoint) noexcept {
    return InRange(codepoint, 0x2E80U, 0x2FFFU) ||   // CJK radicals, Kangxi radicals, description characters
        InRange(codepoint, 0x3001U, 0x303FU) ||      // CJK symbols and punctuation
        InRange(codepoint, 0x3040U, 0x30FFU) ||      // Hiragana, Katakana
        InRange(codepoint, 0x3100U, 0x312FU) ||      // Bopomofo
        InRange(codepoint, 0x3130U, 0x318FU) ||      // Hangul compatibility jamo
        InRange(codepoint, 0x3190U, 0x31FFU) ||      // Kanbun, Bopomofo extended, CJK strokes, small katakana
        InRange(codepoint, 0x3200U, 0x33FFU) ||      // Enclosed CJK letters, CJK compatibility
        InRange(codepoint, 0x3400U, 0x4DBFU) ||      // CJK unified ideographs extension A
        InRange(codepoint, 0x4E00U, 0x9FFFU) ||      // CJK unified ideographs
        InRange(codepoint, 0xA000U, 0xA4CFU) ||      // Yi syllables and radicals
        InRange(codepoint, 0xAC00U, 0xD7AFU) ||      // Hangul syllables
        InRange(codepoint, 0xF900U, 0xFAFFU) ||      // CJK compatibility ideographs
        InRange(codepoint, 0xFE30U, 0xFE4FU) ||      // CJK compatibility forms
        InRange(codepoint, 0xFF01U, 0xFF60U) ||      // Full-width ASCII forms
        InRange(codepoint, 0xFF61U, 0xFF9FU) ||      // Half-width CJK punctuation and katakana
        InRange(codepoint, 0xFFE0U, 0xFFE6U) ||      // Full-width signs
        InRange(codepoint, 0x1B000U, 0x1B16FU) ||    // Kana supplement and extensions
        InRange(codepoint, 0x20000U, 0x3FFFDU);      // Supplementary and tertiary ideographic planes
}

bool IsUITextLineStartProhibited(std::uint32_t codepoint) noexcept {
    return std::binary_search(kLineStartProhibited.begin(), kLineStartProhibited.end(), codepoint) ||
        std::binary_search(kLineStartProhibitedHigh.begin(), kLineStartProhibitedHigh.end(), codepoint) ||
        InRange(codepoint, 0x31F0U, 0x31FFU) || InRange(codepoint, 0xFF67U, 0xFF70U);
}

bool IsUITextLineEndProhibited(std::uint32_t codepoint) noexcept {
    return std::binary_search(kLineEndProhibited.begin(), kLineEndProhibited.end(), codepoint);
}

bool UITextBreakAllowed(std::uint32_t before, std::uint32_t after, UITextWrapMode wrapMode) noexcept {
    if (wrapMode == UITextWrapMode::NoWrap || IsBreakSpace(after)) return false;
    if (IsUITextLineStartProhibited(after) || IsUITextLineEndProhibited(before)) return false;
    if (wrapMode == UITextWrapMode::Character || IsBreakSpace(before)) return true;
    return IsUITextBreakableIdeographic(before) || IsUITextBreakableIdeographic(after);
}

void BreakUITextLines(std::span<const std::uint32_t> codepoints, std::span<const float> advances,
                      UITextWrapMode wrapMode, float availableWidth, float spacing,
                      std::vector<UITextLineSpan>& lines) {
    lines.clear();
    const std::size_t count = std::min(codepoints.size(), advances.size());
    const bool wrap = wrapMode != UITextWrapMode::NoWrap;
    const auto emit = [&](std::size_t begin, std::size_t end, bool hardBreak, bool trimSpaces) {
        if (trimSpaces) {
            while (end > begin && IsBreakSpace(codepoints[end - 1U])) --end;
        }
        float width = 0.0F;
        for (std::size_t index = begin; index < end; ++index) width += advances[index] + spacing;
        lines.push_back(UITextLineSpan{ .begin = begin, .end = end, .width = width, .hardBreak = hardBreak });
    };

    std::size_t lineStart = 0U;
    std::size_t index = 0U;
    std::size_t breakAt = kNoBreak;
    float width = 0.0F;
    while (index < count) {
        const std::uint32_t codepoint = codepoints[index];
        if (codepoint == static_cast<std::uint32_t>('\n')) {
            emit(lineStart, index, true, false);
            lineStart = index + 1U;
            index = lineStart;
            breakAt = kNoBreak;
            width = 0.0F;
            continue;
        }
        if (wrap && index > lineStart) {
            if (UITextBreakAllowed(codepoints[index - 1U], codepoint, wrapMode)) breakAt = index;
            // Spaces may hang past the edge; anything else that overflows wraps the line.
            if (!IsBreakSpace(codepoint) && width + advances[index] > availableWidth) {
                std::size_t next = breakAt;
                if (next == kNoBreak) {
                    // No break opportunity on this line: split where it overflows, moved back past characters
                    // that may not start or end a line while the line keeps at least one character.
                    next = index;
                    while (next > lineStart + 1U &&
                           (IsUITextLineStartProhibited(codepoints[next]) ||
                            IsUITextLineEndProhibited(codepoints[next - 1U]))) {
                        --next;
                    }
                }
                emit(lineStart, next, false, true);
                lineStart = next;
                index = lineStart;
                breakAt = kNoBreak;
                width = 0.0F;
                continue;
            }
        }
        width += advances[index] + spacing;
        ++index;
    }
    emit(lineStart, count, false, false);
}

} // namespace kb::scene
