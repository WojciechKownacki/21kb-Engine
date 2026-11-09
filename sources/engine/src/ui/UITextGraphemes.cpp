#include "engine/ui/visual/UITextGraphemes.hpp"

#include <algorithm>
#include <iterator>

namespace kb::scene {
namespace {

struct Decoded {
    std::uint32_t codepoint = 0U;
    std::size_t size = 1U;
    bool valid = false;
};

// Reads the code point at `offset`; an invalid or truncated sequence is one byte of its own.
[[nodiscard]] Decoded Decode(std::string_view text, std::size_t offset) noexcept {
    const auto byte = [&](std::size_t index) { return static_cast<unsigned char>(text[index]); };
    const unsigned char lead = byte(offset);
    if (lead < 0x80U) {
        return { lead, 1U, true };
    }
    std::size_t length = 0U;
    std::uint32_t value = 0U;
    std::uint32_t minimum = 0U;
    if ((lead & 0xE0U) == 0xC0U) {
        length = 2U;
        value = lead & 0x1FU;
        minimum = 0x80U;
    } else if ((lead & 0xF0U) == 0xE0U) {
        length = 3U;
        value = lead & 0x0FU;
        minimum = 0x800U;
    } else if ((lead & 0xF8U) == 0xF0U) {
        length = 4U;
        value = lead & 0x07U;
        minimum = 0x10000U;
    } else {
        return { 0xFFFDU, 1U, false };
    }
    if (offset + length > text.size()) {
        return { 0xFFFDU, 1U, false };
    }
    for (std::size_t index = 1U; index < length; ++index) {
        const unsigned char next = byte(offset + index);
        if ((next & 0xC0U) != 0x80U) {
            return { 0xFFFDU, 1U, false };
        }
        value = (value << 6U) | (next & 0x3FU);
    }
    if (value < minimum || value > 0x10FFFFU || (value >= 0xD800U && value <= 0xDFFFU)) {
        return { 0xFFFDU, 1U, false };
    }
    return { value, length, true };
}

enum class Grapheme : std::uint8_t {
    Other, CR, LF, Control, Extend, ZWJ, Regional_Indicator, Prepend, SpacingMark, L, V, T, LV, LVT
};
enum class Indic : std::uint8_t { None, Consonant, Extend, Linker };

template <typename T> struct PropertyRange {
    std::uint32_t first;
    std::uint32_t last;
    T value;
};

#include "UITextGraphemeData.inc"

template <typename T, std::size_t N>
[[nodiscard]] T Property(const PropertyRange<T> (&ranges)[N], std::uint32_t codepoint, T fallback) noexcept {
    const auto found = std::upper_bound(ranges, ranges + N, codepoint,
        [](std::uint32_t value, const PropertyRange<T>& range) { return value < range.first; });
    return found != ranges && codepoint <= std::prev(found)->last ? std::prev(found)->value : fallback;
}

[[nodiscard]] bool IsControl(Grapheme value) noexcept {
    return value == Grapheme::Control || value == Grapheme::CR || value == Grapheme::LF;
}

// State for UAX #29 extended grapheme rules GB9c, GB11 and GB12/13.
struct ClusterState {
    Grapheme previous = Grapheme::Other;
    std::size_t regionalRun = 0U;
    bool pictographicWithExtenders = false;
    bool joinedPictographic = false;
    bool indicConsonant = false;
    bool indicLinker = false;

    void Consume(std::uint32_t codepoint, Grapheme value, Indic indic) noexcept {
        regionalRun = value == Grapheme::Regional_Indicator ? regionalRun + 1U : 0U;
        joinedPictographic = value == Grapheme::ZWJ && pictographicWithExtenders;
        pictographicWithExtenders = Property(kPictographicRanges, codepoint, false) ||
            (value == Grapheme::Extend && pictographicWithExtenders);
        if (indic == Indic::Consonant) {
            indicConsonant = true;
            indicLinker = false;
        } else if (indic == Indic::Linker && indicConsonant) {
            indicLinker = true;
        } else if (indic != Indic::Extend) {
            indicConsonant = false;
            indicLinker = false;
        }
        previous = value;
    }

    [[nodiscard]] bool Continues(std::uint32_t codepoint, Grapheme next, Indic indic) const noexcept {
        if (previous == Grapheme::CR && next == Grapheme::LF) return true;
        if (IsControl(previous) || IsControl(next)) return false;
        if (previous == Grapheme::L && (next == Grapheme::L || next == Grapheme::V ||
                next == Grapheme::LV || next == Grapheme::LVT)) return true;
        if ((previous == Grapheme::LV || previous == Grapheme::V) &&
                (next == Grapheme::V || next == Grapheme::T)) return true;
        if ((previous == Grapheme::LVT || previous == Grapheme::T) && next == Grapheme::T) return true;
        if (next == Grapheme::Extend || next == Grapheme::ZWJ || next == Grapheme::SpacingMark) return true;
        if (previous == Grapheme::Prepend) return true;
        if (indic == Indic::Consonant && indicConsonant && indicLinker) return true;
        if (joinedPictographic && Property(kPictographicRanges, codepoint, false)) return true;
        return previous == Grapheme::Regional_Indicator && next == Grapheme::Regional_Indicator && regionalRun % 2U == 1U;
    }
};

// The caller supplies a boundary. Malformed UTF-8 bytes always form independent clusters.
[[nodiscard]] std::size_t ClusterEnd(std::string_view utf8, std::size_t start) noexcept {
    const Decoded first = Decode(utf8, start);
    std::size_t end = start + first.size;
    if (!first.valid) return end;
    ClusterState state;
    state.Consume(first.codepoint, Property(kGraphemeRanges, first.codepoint, Grapheme::Other),
        Property(kIndicRanges, first.codepoint, Indic::None));
    while (end < utf8.size()) {
        const Decoded next = Decode(utf8, end);
        if (!next.valid) break;
        const auto property = Property(kGraphemeRanges, next.codepoint, Grapheme::Other);
        const auto indic = Property(kIndicRanges, next.codepoint, Indic::None);
        if (!state.Continues(next.codepoint, property, indic)) break;
        state.Consume(next.codepoint, property, indic);
        end += next.size;
    }
    return end;
}

} // namespace

bool IsUITextGraphemeExtender(std::uint32_t codepoint) noexcept {
    const auto value = Property(kGraphemeRanges, codepoint, Grapheme::Other);
    return value == Grapheme::Extend || value == Grapheme::ZWJ || value == Grapheme::SpacingMark;
}

std::size_t SnapToUITextGraphemeBoundary(std::string_view utf8, std::size_t offset) noexcept {
    if (offset >= utf8.size()) {
        return utf8.size();
    }
    // Clusters are found from the start of the text; the texts of the runtime UI are at most about a kilobyte.
    std::size_t start = 0U;
    while (start < offset) {
        const std::size_t end = ClusterEnd(utf8, start);
        if (end > offset) {
            return start;
        }
        start = end;
    }
    return start;
}

std::size_t NextUITextGraphemeBoundary(std::string_view utf8, std::size_t offset) noexcept {
    if (offset >= utf8.size()) {
        return utf8.size();
    }
    return ClusterEnd(utf8, SnapToUITextGraphemeBoundary(utf8, offset));
}

std::size_t PreviousUITextGraphemeBoundary(std::string_view utf8, std::size_t offset) noexcept {
    if (offset == 0U) {
        return 0U;
    }
    if (offset > utf8.size()) {
        offset = utf8.size();
    }
    return SnapToUITextGraphemeBoundary(utf8, offset - 1U);
}

std::size_t UITextGraphemeCount(std::string_view utf8) noexcept {
    std::size_t count = 0U;
    for (std::size_t offset = 0U; offset < utf8.size(); offset = ClusterEnd(utf8, offset)) {
        ++count;
    }
    return count;
}

} // namespace kb::scene
