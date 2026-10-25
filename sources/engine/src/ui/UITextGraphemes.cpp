#include "engine/ui/visual/UITextGraphemes.hpp"

#include <algorithm>
#include <array>
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

struct Range {
    std::uint32_t first;
    std::uint32_t last;
};

template <std::size_t N>
[[nodiscard]] bool InRanges(const Range (&ranges)[N], std::uint32_t codepoint) noexcept {
    const Range* const end = ranges + N;
    const Range* const found = std::upper_bound(ranges, end, codepoint,
        [](std::uint32_t value, const Range& range) { return value < range.first; });
    return found != ranges && codepoint <= std::prev(found)->last;
}

// Combining marks (Mn, Me, Mc) of the scripts people type most, plus the joiners and emoji modifiers. Sorted.
constexpr Range kExtenders[] = {
    { 0x0300U, 0x036FU }, { 0x0483U, 0x0489U }, { 0x0591U, 0x05BDU }, { 0x05BFU, 0x05BFU }, { 0x05C1U, 0x05C2U },
    { 0x05C4U, 0x05C5U }, { 0x05C7U, 0x05C7U }, { 0x0610U, 0x061AU }, { 0x064BU, 0x065FU }, { 0x0670U, 0x0670U },
    { 0x06D6U, 0x06DCU }, { 0x06DFU, 0x06E4U }, { 0x06E7U, 0x06E8U }, { 0x06EAU, 0x06EDU }, { 0x0711U, 0x0711U },
    { 0x0730U, 0x074AU }, { 0x0900U, 0x0903U }, { 0x093AU, 0x093CU }, { 0x093EU, 0x094FU }, { 0x0951U, 0x0957U },
    { 0x0962U, 0x0963U }, { 0x0981U, 0x0983U }, { 0x09BCU, 0x09BCU }, { 0x09BEU, 0x09CDU }, { 0x0A01U, 0x0A03U },
    { 0x0A3CU, 0x0A51U }, { 0x0B01U, 0x0B03U }, { 0x0B3CU, 0x0B57U }, { 0x0E31U, 0x0E31U }, { 0x0E34U, 0x0E3AU },
    { 0x0E47U, 0x0E4EU }, { 0x0EB1U, 0x0EB1U }, { 0x0EB4U, 0x0EBCU }, { 0x0EC8U, 0x0ECDU }, { 0x0F18U, 0x0F19U },
    { 0x0F35U, 0x0F35U }, { 0x0F37U, 0x0F37U }, { 0x0F39U, 0x0F39U }, { 0x0F71U, 0x0F84U }, { 0x1AB0U, 0x1AFFU },
    { 0x1DC0U, 0x1DFFU }, { 0x200CU, 0x200DU }, { 0x20D0U, 0x20FFU }, { 0x302AU, 0x302FU }, { 0x3099U, 0x309AU },
    { 0xFE00U, 0xFE0FU }, { 0xFE20U, 0xFE2FU }, { 0xFF9EU, 0xFF9FU }, { 0x1D165U, 0x1D169U }, { 0x1D16DU, 0x1D172U },
    { 0x1D17BU, 0x1D182U }, { 0x1F3FBU, 0x1F3FFU }, { 0xE0020U, 0xE007FU }, { 0xE0100U, 0xE01EFU },
};

constexpr Range kPictographic[] = {
    { 0x00A9U, 0x00A9U }, { 0x00AEU, 0x00AEU }, { 0x203CU, 0x203CU }, { 0x2049U, 0x2049U }, { 0x2122U, 0x2122U },
    { 0x2139U, 0x2139U }, { 0x2194U, 0x21AAU }, { 0x231AU, 0x23FFU }, { 0x24C2U, 0x24C2U }, { 0x25AAU, 0x25FEU },
    { 0x2600U, 0x27BFU }, { 0x2934U, 0x2935U }, { 0x2B05U, 0x2B55U }, { 0x3030U, 0x3030U }, { 0x303DU, 0x303DU },
    { 0x3297U, 0x3297U }, { 0x3299U, 0x3299U }, { 0x1F000U, 0x1FAFFU },
};

[[nodiscard]] bool IsPictographic(std::uint32_t codepoint) noexcept {
    return InRanges(kPictographic, codepoint);
}

[[nodiscard]] bool IsRegionalIndicator(std::uint32_t codepoint) noexcept {
    return codepoint >= 0x1F1E6U && codepoint <= 0x1F1FFU;
}

[[nodiscard]] bool IsControl(std::uint32_t codepoint) noexcept {
    return codepoint < 0x20U || (codepoint >= 0x7FU && codepoint < 0xA0U) || codepoint == 0x2028U || codepoint == 0x2029U;
}

enum class Hangul : std::uint8_t { None, Leading, Vowel, Trailing, SyllableLV, SyllableLVT };

[[nodiscard]] Hangul HangulClass(std::uint32_t codepoint) noexcept {
    if ((codepoint >= 0x1100U && codepoint <= 0x115FU) || (codepoint >= 0xA960U && codepoint <= 0xA97CU)) return Hangul::Leading;
    if ((codepoint >= 0x1160U && codepoint <= 0x11A7U) || (codepoint >= 0xD7B0U && codepoint <= 0xD7C6U)) return Hangul::Vowel;
    if ((codepoint >= 0x11A8U && codepoint <= 0x11FFU) || (codepoint >= 0xD7CBU && codepoint <= 0xD7FBU)) return Hangul::Trailing;
    if (codepoint >= 0xAC00U && codepoint <= 0xD7A3U) {
        return (codepoint - 0xAC00U) % 28U == 0U ? Hangul::SyllableLV : Hangul::SyllableLVT;
    }
    return Hangul::None;
}

// Whether the cluster that ends with `previous` continues with `next`. `regionalRun` is the number of regional
// indicators that end the cluster so far.
[[nodiscard]] bool Continues(std::uint32_t previous, std::uint32_t next, std::size_t regionalRun) noexcept {
    if (previous == 0x0DU && next == 0x0AU) return true;
    if (IsControl(previous) || IsControl(next)) return false;
    if (IsUITextGraphemeExtender(next)) return true;
    if (previous == 0x200DU && IsPictographic(next)) return true;
    if (IsRegionalIndicator(previous) && IsRegionalIndicator(next)) return regionalRun % 2U == 1U;
    const Hangul before = HangulClass(previous);
    const Hangul after = HangulClass(next);
    if (before == Hangul::Leading) {
        return after == Hangul::Leading || after == Hangul::Vowel || after == Hangul::SyllableLV || after == Hangul::SyllableLVT;
    }
    if (before == Hangul::Vowel || before == Hangul::SyllableLV) return after == Hangul::Vowel || after == Hangul::Trailing;
    if (before == Hangul::Trailing || before == Hangul::SyllableLVT) return after == Hangul::Trailing;
    return false;
}

// The end of the cluster that starts at `start`, which must be a boundary.
[[nodiscard]] std::size_t ClusterEnd(std::string_view utf8, std::size_t start) noexcept {
    Decoded current = Decode(utf8, start);
    std::size_t end = start + current.size;
    std::size_t regionalRun = IsRegionalIndicator(current.codepoint) ? 1U : 0U;
    while (end < utf8.size()) {
        const Decoded next = Decode(utf8, end);
        if (!current.valid || !next.valid || !Continues(current.codepoint, next.codepoint, regionalRun)) {
            break;
        }
        regionalRun = IsRegionalIndicator(next.codepoint) ? regionalRun + 1U : 0U;
        current = next;
        end += next.size;
    }
    return end;
}

} // namespace

bool IsUITextGraphemeExtender(std::uint32_t codepoint) noexcept {
    return InRanges(kExtenders, codepoint);
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
