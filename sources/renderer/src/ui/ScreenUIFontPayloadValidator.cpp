#include "private/ui/ScreenUIFontPayloadValidator.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace kb::render {
namespace {

constexpr std::uint32_t Tag(char a, char b, char c, char d) noexcept {
    return (static_cast<std::uint32_t>(a) << 24U) | (static_cast<std::uint32_t>(b) << 16U) |
           (static_cast<std::uint32_t>(c) << 8U) | static_cast<std::uint32_t>(d);
}

struct FontTable {
    std::uint32_t tag = 0U;
    std::size_t offset = 0U;
    std::size_t length = 0U;
};

[[nodiscard]] bool RangeFits(std::size_t offset, std::size_t length, std::size_t size) noexcept {
    return offset <= size && length <= size - offset;
}

[[nodiscard]] bool ReadU16(std::span<const std::byte> bytes, std::size_t offset, std::uint16_t& value) noexcept {
    if (!RangeFits(offset, 2U, bytes.size())) {
        return false;
    }
    value = (static_cast<std::uint16_t>(bytes[offset]) << 8U) | static_cast<std::uint16_t>(bytes[offset + 1U]);
    return true;
}

[[nodiscard]] bool ReadI16(std::span<const std::byte> bytes, std::size_t offset, std::int16_t& value) noexcept {
    std::uint16_t unsignedValue = 0U;
    if (!ReadU16(bytes, offset, unsignedValue)) {
        return false;
    }
    value = static_cast<std::int16_t>(unsignedValue);
    return true;
}

[[nodiscard]] bool ReadU32(std::span<const std::byte> bytes, std::size_t offset, std::uint32_t& value) noexcept {
    if (!RangeFits(offset, 4U, bytes.size())) {
        return false;
    }
    value = (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
            (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
            (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
            static_cast<std::uint32_t>(bytes[offset + 3U]);
    return true;
}

[[nodiscard]] const FontTable* FindTable(std::span<const FontTable> tables, std::uint32_t tag) noexcept {
    const auto found = std::ranges::find(tables, tag, &FontTable::tag);
    return found == tables.end() ? nullptr : &*found;
}

[[nodiscard]] bool ValidateCmap(std::span<const std::byte> bytes, const FontTable& cmap,
                                std::uint16_t glyphCount) noexcept {
    std::uint16_t tableCount = 0U;
    if (cmap.length < 4U || !ReadU16(bytes, cmap.offset + 2U, tableCount) || tableCount == 0U ||
        !RangeFits(4U, static_cast<std::size_t>(tableCount) * 8U, cmap.length)) {
        return false;
    }
    std::size_t selected = 0U;
    for (std::uint16_t index = 0U; index < tableCount; ++index) {
        const std::size_t record = cmap.offset + 4U + static_cast<std::size_t>(index) * 8U;
        std::uint16_t platform = 0U;
        std::uint16_t encoding = 0U;
        std::uint32_t relativeOffset = 0U;
        if (!ReadU16(bytes, record, platform) || !ReadU16(bytes, record + 2U, encoding) ||
            !ReadU32(bytes, record + 4U, relativeOffset)) {
            return false;
        }
        if (platform == 0U || (platform == 3U && (encoding == 1U || encoding == 10U))) {
            if (relativeOffset >= cmap.length) {
                return false;
            }
            selected = cmap.offset + relativeOffset;
        }
    }
    if (selected == 0U) {
        return false;
    }
    std::uint16_t format = 0U;
    if (!ReadU16(bytes, selected, format)) {
        return false;
    }
    std::size_t length = 0U;
    if (format == 12U || format == 13U) {
        std::uint32_t longLength = 0U;
        if (!ReadU32(bytes, selected + 4U, longLength)) {
            return false;
        }
        length = longLength;
    } else {
        std::uint16_t shortLength = 0U;
        if (!ReadU16(bytes, selected + 2U, shortLength)) {
            return false;
        }
        length = shortLength;
    }
    const std::size_t cmapEnd = cmap.offset + cmap.length;
    if (selected > cmapEnd || !RangeFits(selected, length, cmapEnd) || length < 6U) {
        return false;
    }
    if (format == 0U) {
        if (length < 262U) {
            return false;
        }
        for (std::size_t index = 0U; index < 256U; ++index) {
            if (static_cast<std::uint8_t>(bytes[selected + 6U + index]) >= glyphCount) {
                return false;
            }
        }
        return true;
    }
    if (format == 6U) {
        std::uint16_t count = 0U;
        if (length < 10U || !ReadU16(bytes, selected + 8U, count) ||
            !RangeFits(10U, static_cast<std::size_t>(count) * 2U, length)) {
            return false;
        }
        for (std::size_t index = 0U; index < count; ++index) {
            std::uint16_t glyph = 0U;
            if (!ReadU16(bytes, selected + 10U + index * 2U, glyph) || glyph >= glyphCount) {
                return false;
            }
        }
        return true;
    }
    if (format == 4U) {
        std::uint16_t segmentBytes = 0U;
        std::uint16_t searchBytes = 0U;
        std::uint16_t entrySelector = 0U;
        std::uint16_t rangeBytes = 0U;
        if (length < 16U || !ReadU16(bytes, selected + 6U, segmentBytes) || segmentBytes == 0U ||
            (segmentBytes & 1U) != 0U || !ReadU16(bytes, selected + 8U, searchBytes) ||
            !ReadU16(bytes, selected + 10U, entrySelector) || !ReadU16(bytes, selected + 12U, rangeBytes)) {
            return false;
        }
        const std::uint16_t segments = segmentBytes / 2U;
        std::uint16_t power = 1U;
        std::uint16_t selector = 0U;
        while (static_cast<std::uint32_t>(power) * 2U <= segments) {
            power = static_cast<std::uint16_t>(power * 2U);
            ++selector;
        }
        if (searchBytes != power * 2U || entrySelector != selector || rangeBytes != segmentBytes - searchBytes ||
            !RangeFits(0U, 16U + static_cast<std::size_t>(segments) * 8U, length)) {
            return false;
        }
        const std::size_t endBase = selected + 14U;
        const std::size_t startBase = endBase + static_cast<std::size_t>(segments) * 2U + 2U;
        const std::size_t deltaBase = startBase + static_cast<std::size_t>(segments) * 2U;
        const std::size_t rangeBase = deltaBase + static_cast<std::size_t>(segments) * 2U;
        std::uint16_t previousEnd = 0U;
        for (std::uint16_t index = 0U; index < segments; ++index) {
            std::uint16_t start = 0U;
            std::uint16_t end = 0U;
            std::uint16_t delta = 0U;
            std::uint16_t rangeOffset = 0U;
            if (!ReadU16(bytes, startBase + static_cast<std::size_t>(index) * 2U, start) ||
                !ReadU16(bytes, endBase + static_cast<std::size_t>(index) * 2U, end) ||
                !ReadU16(bytes, deltaBase + static_cast<std::size_t>(index) * 2U, delta) ||
                !ReadU16(bytes, rangeBase + static_cast<std::size_t>(index) * 2U, rangeOffset) || start > end ||
                (index > 0U && start <= previousEnd)) {
                return false;
            }
            previousEnd = end;
            for (std::uint32_t codepoint = start; codepoint <= end; ++codepoint) {
                std::uint16_t glyph = 0U;
                if (rangeOffset == 0U) {
                    glyph = static_cast<std::uint16_t>(codepoint + delta);
                } else {
                    const std::size_t word = rangeBase + static_cast<std::size_t>(index) * 2U;
                    const std::size_t glyphOffset = word + rangeOffset + (codepoint - start) * 2U;
                    if (!ReadU16(bytes, glyphOffset, glyph) || glyphOffset + 2U > selected + length) {
                        return false;
                    }
                    if (glyph != 0U) {
                        glyph = static_cast<std::uint16_t>(glyph + delta);
                    }
                }
                if (glyph >= glyphCount) {
                    return false;
                }
            }
        }
        return true;
    }
    if (format == 12U || format == 13U) {
        std::uint32_t groups = 0U;
        if (length < 16U || !ReadU32(bytes, selected + 12U, groups) || groups > (length - 16U) / 12U) {
            return false;
        }
        std::uint32_t previousEnd = 0U;
        for (std::uint32_t index = 0U; index < groups; ++index) {
            const std::size_t group = selected + 16U + static_cast<std::size_t>(index) * 12U;
            std::uint32_t start = 0U;
            std::uint32_t end = 0U;
            std::uint32_t firstGlyph = 0U;
            if (!ReadU32(bytes, group, start) || !ReadU32(bytes, group + 4U, end) ||
                !ReadU32(bytes, group + 8U, firstGlyph) || start > end || end > 0x10FFFFU ||
                (index > 0U && start <= previousEnd)) {
                return false;
            }
            const std::uint64_t lastGlyph =
                format == 12U ? static_cast<std::uint64_t>(firstGlyph) + (end - start) : firstGlyph;
            if (lastGlyph >= glyphCount) {
                return false;
            }
            previousEnd = end;
        }
        return true;
    }
    return false;
}

[[nodiscard]] bool ValidateSimpleGlyph(std::span<const std::byte> glyph, std::uint16_t contours) {
    const std::size_t endpointsEnd = 10U + static_cast<std::size_t>(contours) * 2U;
    std::uint16_t lastEndpoint = 0U;
    std::uint16_t previousEndpoint = 0U;
    std::vector<std::uint16_t> endpoints;
    endpoints.reserve(contours);
    for (std::uint16_t index = 0U; index < contours; ++index) {
        std::uint16_t endpoint = 0U;
        if (!ReadU16(glyph, 10U + static_cast<std::size_t>(index) * 2U, endpoint) ||
            (index > 0U && endpoint <= previousEndpoint)) {
            return false;
        }
        previousEndpoint = endpoint;
        lastEndpoint = endpoint;
        endpoints.push_back(endpoint);
    }
    std::uint16_t instructionBytes = 0U;
    if (!ReadU16(glyph, endpointsEnd, instructionBytes)) {
        return false;
    }
    std::size_t cursor = endpointsEnd + 2U + instructionBytes;
    if (cursor > glyph.size()) {
        return false;
    }
    const std::size_t pointCount = static_cast<std::size_t>(lastEndpoint) + 1U;
    std::vector<std::uint8_t> flags;
    flags.reserve(pointCount);
    while (flags.size() < pointCount) {
        if (cursor >= glyph.size()) {
            return false;
        }
        const auto flag = static_cast<std::uint8_t>(glyph[cursor++]);
        std::size_t repeats = 0U;
        if ((flag & 0x08U) != 0U) {
            if (cursor >= glyph.size()) {
                return false;
            }
            repeats = static_cast<std::uint8_t>(glyph[cursor++]);
        }
        if (repeats >= pointCount - flags.size()) {
            return false;
        }
        flags.insert(flags.end(), repeats + 1U, flag);
    }
    std::size_t coordinateBytes = 0U;
    for (const std::uint8_t flag : flags) {
        coordinateBytes += (flag & 0x02U) != 0U ? 1U : ((flag & 0x10U) != 0U ? 0U : 2U);
        coordinateBytes += (flag & 0x04U) != 0U ? 1U : ((flag & 0x20U) != 0U ? 0U : 2U);
    }
    return RangeFits(cursor, coordinateBytes, glyph.size());
}

[[nodiscard]] bool ValidateGlyphs(std::span<const std::byte> bytes, const FontTable& head, const FontTable& loca,
                                  const FontTable& glyf, std::uint16_t glyphCount) {
    std::uint16_t locationFormat = 0U;
    if (head.length < 54U || !ReadU16(bytes, head.offset + 50U, locationFormat) || locationFormat > 1U) {
        return false;
    }
    const std::size_t locationBytes = locationFormat == 0U ? 2U : 4U;
    if (!RangeFits(0U, (static_cast<std::size_t>(glyphCount) + 1U) * locationBytes, loca.length)) {
        return false;
    }
    std::vector<std::uint32_t> locations(static_cast<std::size_t>(glyphCount) + 1U);
    for (std::size_t index = 0U; index < locations.size(); ++index) {
        if (locationFormat == 0U) {
            std::uint16_t value = 0U;
            if (!ReadU16(bytes, loca.offset + index * 2U, value)) {
                return false;
            }
            locations[index] = static_cast<std::uint32_t>(value) * 2U;
        } else if (!ReadU32(bytes, loca.offset + index * 4U, locations[index])) {
            return false;
        }
        if (locations[index] > glyf.length || (index > 0U && locations[index] < locations[index - 1U])) {
            return false;
        }
    }

    std::vector<std::vector<std::uint16_t>> references(glyphCount);
    for (std::uint16_t glyphIndex = 0U; glyphIndex < glyphCount; ++glyphIndex) {
        const std::size_t begin = locations[glyphIndex];
        const std::size_t end = locations[static_cast<std::size_t>(glyphIndex) + 1U];
        if (begin == end) {
            continue;
        }
        const std::span<const std::byte> glyph = bytes.subspan(glyf.offset + begin, end - begin);
        std::int16_t contourCount = 0;
        if (glyph.size() < 10U || !ReadI16(glyph, 0U, contourCount)) {
            return false;
        }
        if (contourCount > 0) {
            if (!ValidateSimpleGlyph(glyph, static_cast<std::uint16_t>(contourCount))) {
                return false;
            }
            continue;
        }
        if (contourCount == 0) {
            continue;
        }
        if (contourCount != -1) {
            return false;
        }
        std::size_t cursor = 10U;
        bool more = true;
        while (more) {
            std::uint16_t flags = 0U;
            std::uint16_t referencedGlyph = 0U;
            if (!ReadU16(glyph, cursor, flags) || !ReadU16(glyph, cursor + 2U, referencedGlyph) ||
                referencedGlyph >= glyphCount || (flags & 0x0002U) == 0U) {
                return false;
            }
            cursor += 4U;
            const std::size_t argumentBytes = (flags & 0x0001U) != 0U ? 4U : 2U;
            const std::uint16_t transformFlags = flags & 0x00C8U;
            if (transformFlags != 0U && transformFlags != 0x0008U && transformFlags != 0x0040U &&
                transformFlags != 0x0080U) {
                return false;
            }
            const std::size_t transformBytes =
                transformFlags == 0x0008U ? 2U :
                (transformFlags == 0x0040U ? 4U : (transformFlags == 0x0080U ? 8U : 0U));
            if (!RangeFits(cursor, argumentBytes + transformBytes, glyph.size())) {
                return false;
            }
            cursor += argumentBytes + transformBytes;
            references[glyphIndex].push_back(referencedGlyph);
            more = (flags & 0x0020U) != 0U;
            if (!more && (flags & 0x0100U) != 0U) {
                std::uint16_t instructions = 0U;
                if (!ReadU16(glyph, cursor, instructions) || !RangeFits(cursor + 2U, instructions, glyph.size())) {
                    return false;
                }
            }
        }
    }

    std::vector<std::uint8_t> state(glyphCount, 0U);
    std::function<bool(std::uint16_t, std::uint32_t)> visit = [&](std::uint16_t glyph, std::uint32_t depth) {
        if (depth > 32U || state[glyph] == 1U) {
            return false;
        }
        if (state[glyph] == 2U) {
            return true;
        }
        state[glyph] = 1U;
        for (const std::uint16_t dependency : references[glyph]) {
            if (!visit(dependency, depth + 1U)) {
                return false;
            }
        }
        state[glyph] = 2U;
        return true;
    };
    for (std::uint16_t glyph = 0U; glyph < glyphCount; ++glyph) {
        if (!visit(glyph, 0U)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool ScreenUIFontPayloadValidator::SupportsExtension(std::string_view extension) noexcept {
    return extension == ".ttf" || extension == ".otf";
}

bool ScreenUIFontPayloadValidator::Validate(const kb::assets::ImportedAsset& asset) {
    if (asset.category != kb::assets::AssetImportCategory::Font || !SupportsExtension(asset.sourceExtension) ||
        asset.payload.size() < kMinimumPayloadBytes || asset.payload.size() > kMaximumPayloadBytes ||
        asset.sourceSize != asset.payload.size()) {
        return false;
    }
    const std::span<const std::byte> bytes = asset.payload;
    std::uint32_t version = 0U;
    std::uint16_t tableCount = 0U;
    if (!ReadU32(bytes, 0U, version) || version != 0x00010000U || !ReadU16(bytes, 4U, tableCount) ||
        tableCount == 0U || tableCount > 128U ||
        !RangeFits(12U, static_cast<std::size_t>(tableCount) * 16U, bytes.size())) {
        return false;
    }
    const std::size_t directoryEnd = 12U + static_cast<std::size_t>(tableCount) * 16U;
    std::vector<FontTable> tables;
    tables.reserve(tableCount);
    for (std::uint16_t index = 0U; index < tableCount; ++index) {
        const std::size_t record = 12U + static_cast<std::size_t>(index) * 16U;
        std::uint32_t tag = 0U;
        std::uint32_t offset = 0U;
        std::uint32_t length = 0U;
        if (!ReadU32(bytes, record, tag) || !ReadU32(bytes, record + 8U, offset) ||
            !ReadU32(bytes, record + 12U, length) || offset < directoryEnd ||
            !RangeFits(offset, length, bytes.size()) ||
            std::ranges::find(tables, tag, &FontTable::tag) != tables.end()) {
            return false;
        }
        tables.push_back(FontTable{tag, offset, length});
    }
    const FontTable* cmap = FindTable(tables, Tag('c', 'm', 'a', 'p'));
    const FontTable* head = FindTable(tables, Tag('h', 'e', 'a', 'd'));
    const FontTable* hhea = FindTable(tables, Tag('h', 'h', 'e', 'a'));
    const FontTable* hmtx = FindTable(tables, Tag('h', 'm', 't', 'x'));
    const FontTable* maxp = FindTable(tables, Tag('m', 'a', 'x', 'p'));
    const FontTable* loca = FindTable(tables, Tag('l', 'o', 'c', 'a'));
    const FontTable* glyf = FindTable(tables, Tag('g', 'l', 'y', 'f'));
    std::uint16_t glyphCount = 0U;
    std::uint16_t longMetrics = 0U;
    if (cmap == nullptr || head == nullptr || hhea == nullptr || hmtx == nullptr || maxp == nullptr ||
        loca == nullptr || glyf == nullptr || maxp->length < 6U || hhea->length < 36U ||
        !ReadU16(bytes, maxp->offset + 4U, glyphCount) || glyphCount == 0U ||
        !ReadU16(bytes, hhea->offset + 34U, longMetrics) || longMetrics == 0U || longMetrics > glyphCount) {
        return false;
    }
    const std::size_t requiredMetrics =
        static_cast<std::size_t>(longMetrics) * 4U + static_cast<std::size_t>(glyphCount - longMetrics) * 2U;
    return requiredMetrics <= hmtx->length && ValidateCmap(bytes, *cmap, glyphCount) &&
           ValidateGlyphs(bytes, *head, *loca, *glyf, glyphCount);
}

} // namespace kb::render
