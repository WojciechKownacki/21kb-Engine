#include "private/ui/ScreenUIFontAtlasCache.hpp"
#include "private/ui/ScreenUIFontPayloadValidator.hpp"
#include "private/ui/ScreenUIFontDistanceField.hpp"
#include "private/ui/ScreenUIVariableFontRasterizer.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/SceneUI.hpp"
#include "engine/input/InputText.hpp"
#include "engine/ui/visual/UITextLineBreaking.hpp"
#include "engine/ui/visual/UITextShaping.hpp"
#include "engine/ui/visual/UITextGraphemes.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/resources/RenderResources.hpp"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <ranges>
#include <string_view>
#include <utility>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wunused-function"
#endif
#include <stb/stb_truetype.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

namespace kb::render {
namespace {

constexpr std::uint16_t kInitialAtlasExtent = 256U;
constexpr std::uint16_t kMaximumAtlasExtent = 2048U;
constexpr std::uint16_t kMinimumGlyphPadding = 2U;
constexpr std::uint32_t kEllipsisCodepoint = 0x2026U;

struct TextLine {
    std::vector<ScreenUITextMarkupGlyph> glyphs;
    std::vector<kb::scene::UITextFontShapedGlyph> shaped;
    float width = 0.0F;
    std::uint32_t sourceOffset = 0U;
    float columnLeft = 0.0F;
    float columnWidth = 0.0F;
    std::size_t visualLine = 0U;
    bool tableCell = false;
    bool cellContinues = false;
};

[[nodiscard]] std::uint32_t Utf8Width(std::uint32_t codepoint) noexcept {
    return codepoint < 0x80U ? 1U : codepoint < 0x800U ? 2U : codepoint < 0x10000U ? 3U : 4U;
}

[[nodiscard]] std::vector<std::uint32_t> StyleBoundaries(std::span<const ScreenUITextMarkupGlyph> text) {
    std::vector<std::uint32_t> boundaries;
    std::string plain;
    std::vector<std::size_t> offsets;
    for (const auto& glyph : text) {
        offsets.push_back(plain.size());
        std::array<char, 4U> encoded{};
        plain.append(encoded.data(), kb::input::EncodeUtf8(glyph.codepoint, encoded));
    }
    auto next = kb::scene::NextUITextGraphemeBoundary(plain, 0U);
    std::size_t clusterStart = 0U;
    for (std::size_t i = 1; i < text.size(); ++i) {
        if (offsets[i] != next) continue;
        if (text[i].color != text[clusterStart].color || text[i].link != text[clusterStart].link ||
            text[i].underline != text[clusterStart].underline)
            boundaries.push_back(static_cast<std::uint32_t>(i));
        clusterStart = i;
        next = kb::scene::NextUITextGraphemeBoundary(plain, next);
    }
    return boundaries;
}

[[nodiscard]] std::vector<std::uint32_t> GraphemeBoundaries(std::span<const std::uint32_t> text) {
    std::string utf8;
    std::vector<std::size_t> offsets{0U};
    for (auto cp : text) {
        std::array<char, 4U> encoded{};
        utf8.append(encoded.data(), kb::input::EncodeUtf8(cp, encoded));
        offsets.push_back(utf8.size());
    }
    std::vector<std::uint32_t> result{0U};
    for (std::size_t byte = 0U; byte < utf8.size();) {
        byte = kb::scene::NextUITextGraphemeBoundary(utf8, byte);
        result.push_back(static_cast<std::uint32_t>(std::lower_bound(offsets.begin(), offsets.end(), byte) - offsets.begin()));
    }
    return result;
}

[[nodiscard]] bool HardBreak(std::uint32_t cp) noexcept {
    return cp == '\n' || cp == '\r' || cp == 0x85U || cp == 0x2028U || cp == 0x2029U;
}

[[nodiscard]] std::vector<std::uint32_t> SafeBreaks(std::span<const std::uint32_t> text,
    std::span<const kb::scene::UITextFontShapedGlyph> glyphs) {
    auto boundaries = GraphemeBoundaries(text);
    std::vector<std::pair<std::uint32_t, std::uint32_t>> clusters;
    clusters.reserve(glyphs.size());
    for (const auto& item : glyphs) clusters.emplace_back(item.glyph.clusterBegin, item.glyph.clusterEnd);
    std::ranges::sort(clusters);
    std::vector<std::uint32_t> safe;
    safe.reserve(boundaries.size());
    std::size_t cursor = 0U;
    std::uint32_t maximumEnd = 0U;
    for (const auto boundary : boundaries) {
        while (cursor < clusters.size() && clusters[cursor].first < boundary) {
            maximumEnd = std::max(maximumEnd, clusters[cursor].second);
            ++cursor;
        }
        if (maximumEnd <= boundary) safe.push_back(boundary);
    }
    return safe;
}

[[nodiscard]] bool SeparateClusters(const kb::scene::UITextFontShapedGlyph& a,
                                    const kb::scene::UITextFontShapedGlyph& b,
                                    std::span<const std::uint32_t> boundaries) {
    if (a.glyph.clusterBegin == b.glyph.clusterBegin) return false;
    const auto low = std::min(a.glyph.clusterBegin, b.glyph.clusterBegin);
    const auto high = std::max(a.glyph.clusterBegin, b.glyph.clusterBegin);
    const auto next = std::upper_bound(boundaries.begin(), boundaries.end(), low);
    return next != boundaries.end() && *next <= high;
}

[[nodiscard]] bool ShapeCached(const ScreenUIFontAtlasCache::FontEntry& entry, const ScreenUIFontAtlasCache::FontEntry* fallback,
                              std::span<const std::uint32_t> text, std::size_t begin, std::size_t end,
                              std::span<const std::uint32_t> styles,
                              std::vector<kb::scene::UITextFontShapedGlyph>& result) {
    auto& cache = entry.shapingCache;
    for (auto found = cache.begin(); found != cache.end(); ++found) {
        if (found->begin == begin && found->end == end && found->scale == entry.scale &&
            found->fallbackAssetId == (fallback ? fallback->assetId : 0U) &&
            found->fallbackGeneration == (fallback ? fallback->loadGeneration : 0U) &&
            found->fallbackScale == (fallback ? fallback->scale : 0.0F) &&
            found->language == entry.language &&
            std::ranges::equal(found->text, text) && std::ranges::equal(found->styles, styles)) {
            result = found->glyphs;
            ++entry.shapingHits;
            std::rotate(found, found + 1, cache.end());
            return true;
        }
    }
    ++entry.shapingMisses;
    const auto allBoundaries = GraphemeBoundaries(text);
    const auto first = std::lower_bound(allBoundaries.begin(), allBoundaries.end(), begin);
    const auto last = std::lower_bound(allBoundaries.begin(), allBoundaries.end(), end);
    if (first == allBoundaries.end() || last == allBoundaries.end() || *first != begin || *last != end) return false;
    const std::vector<std::uint32_t> boundaries(first, last + 1);
    if (!kb::scene::ShapeUITextWithFallback(entry.asset->payload, fallback ? std::span<const std::byte>{fallback->asset->payload} : std::span<const std::byte>{},
        text, begin, end, entry.scale, fallback ? fallback->scale : 0.0F, boundaries, result, styles, entry.variations, entry.language)) return false;
    ScreenUIFontAtlasCache::FontEntry::ShapingEntry item;
    item.text.assign(text.begin(), text.end());
    item.styles.assign(styles.begin(), styles.end());
    item.glyphs = result;
    item.begin = begin;
    item.end = end;
    item.scale = entry.scale;
    item.fallbackAssetId = fallback ? fallback->assetId : 0U;
    item.fallbackGeneration = fallback ? fallback->loadGeneration : 0U;
    item.fallbackScale = fallback ? fallback->scale : 0.0F;
    item.language = entry.language;
    item.payloadBytes = (item.text.capacity() + item.styles.capacity()) * sizeof(std::uint32_t) +
        item.glyphs.capacity() * sizeof(kb::scene::UITextFontShapedGlyph) + item.language.capacity();
    for (const auto& glyph : item.glyphs) item.payloadBytes += glyph.glyph.ligatureCarets.capacity() * sizeof(float);
    constexpr std::size_t budget = 64U * 1024U;
    if (item.payloadBytes > budget) return true;
    while (!cache.empty() && (cache.size() >= 32U || entry.shapingBytes + item.payloadBytes > budget)) {
        entry.shapingBytes -= cache.front().payloadBytes;
        cache.erase(cache.begin());
    }
    entry.shapingBytes += item.payloadBytes;
    cache.push_back(std::move(item));
    return true;
}

[[nodiscard]] bool Shape(const ScreenUIFontAtlasCache::FontEntry& entry,
                         const ScreenUIFontAtlasCache::FontEntry* fallback,
                         std::span<const ScreenUITextMarkupGlyph> text,
                         std::vector<kb::scene::UITextFontShapedGlyph>& result) {
    std::vector<std::uint32_t> codepoints;
    codepoints.reserve(text.size());
    for (const auto& glyph : text) codepoints.push_back(glyph.codepoint);
    return ShapeCached(entry, fallback, codepoints, 0U, codepoints.size(), StyleBoundaries(text), result);
}

[[nodiscard]] float ShapedWidth(std::span<const kb::scene::UITextFontShapedGlyph> glyphs, float spacing,
                                std::span<const std::uint32_t> boundaries) {
    float width = 0.0F;
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        width += glyphs[i].glyph.advance;
        if (i + 1U == glyphs.size() || SeparateClusters(glyphs[i], glyphs[i + 1U], boundaries)) width += spacing;
    }
    return width;
}

[[nodiscard]] float MeasureCodepoints(const ScreenUIFontAtlasCache::FontEntry& entry,
                                      const ScreenUIFontAtlasCache::FontEntry* fallback,
                                      std::span<const ScreenUITextMarkupGlyph> glyphs, float spacing) {
    std::vector<kb::scene::UITextFontShapedGlyph> shaped;
    if (!Shape(entry, fallback, glyphs, shaped)) return std::numeric_limits<float>::infinity();
    std::vector<std::uint32_t> codepoints;
    for (const auto& glyph : glyphs) codepoints.push_back(glyph.codepoint);
    return ShapedWidth(shaped, spacing, SafeBreaks(codepoints, shaped));
}

// Wrap logical shaping clusters, never individual combining characters or ligature components.
// Each resulting line is reshaped with the original paragraph's direction and surrounding text.
thread_local std::vector<TextLine> tTextLines;

[[nodiscard]] std::vector<TextLine>& BreakLines(const ScreenUIFontAtlasCache::FontEntry& entry,
                                                const ScreenUIFontAtlasCache::FontEntry* fallback,
                                                std::span<const ScreenUITextMarkupGlyph> text,
                                                kb::scene::UITextWrapMode wrapMode, float availableWidth, float spacing) {
    tTextLines.clear();
    std::vector<std::uint32_t> codepoints;
    for (const auto& glyph : text) codepoints.push_back(glyph.codepoint);
    std::vector<kb::scene::UITextFontShapedGlyph> shaped;
    const auto styles = StyleBoundaries(text);
    if (!ShapeCached(entry, fallback, codepoints, 0U, codepoints.size(), styles, shaped))
        return tTextLines;
    const auto safe = SafeBreaks(codepoints, shaped);
    std::vector<std::uint32_t> starts = safe;
    if (!starts.empty()) starts.pop_back(); // terminal boundary is appended after measuring
    std::vector<std::uint32_t> clusterCodepoints;
    std::vector<float> advances(starts.size(), 0.0F);
    for (const auto start : starts) clusterCodepoints.push_back(HardBreak(codepoints[start]) ? '\n' : codepoints[start]);
    for (const auto& glyph : shaped) {
        const auto index = static_cast<std::size_t>(std::upper_bound(starts.begin(), starts.end(), glyph.glyph.clusterBegin) - starts.begin() - 1);
        if (!HardBreak(codepoints[starts[index]])) advances[index] += glyph.glyph.advance;
    }
    std::vector<kb::scene::UITextLineSpan> spans;
    kb::scene::BreakUITextLines(clusterCodepoints, advances, wrapMode, availableWidth, spacing, spans);
    starts.push_back(static_cast<std::uint32_t>(text.size()));
    for (const auto& span : spans) {
        std::uint32_t begin = starts[span.begin];
        const std::uint32_t end = starts[span.end];
        do {
            TextLine line;
            auto lineEnd = end;
            for (;;) {
                if (!ShapeCached(entry, fallback, codepoints, begin, lineEnd, styles, line.shaped)) {
                    tTextLines.clear();
                    return tTextLines;
                }
                const auto lineSafe = SafeBreaks(codepoints, line.shaped);
                line.width = ShapedWidth(line.shaped, spacing, lineSafe);
                if (wrapMode == kb::scene::UITextWrapMode::NoWrap || line.width <= availableWidth + 0.5F) break;
                // Joining and kerning can change when the paragraph is split into lines.
                // Recheck actual line metrics, allowing only an indivisible cluster to overflow.
                auto lastCluster = begin;
                for (const auto boundary : lineSafe)
                    if (boundary > begin && boundary < lineEnd) lastCluster = boundary;
                if (lastCluster == begin) break;
                lineEnd = lastCluster;
            }
            line.glyphs.assign(text.begin() + begin, text.begin() + lineEnd);
            line.sourceOffset = begin < text.size() ? text[begin].sourceOffset :
                text.empty() ? 0U : text.back().sourceOffset + Utf8Width(text.back().codepoint);
            for (auto& glyph : line.shaped) {
                glyph.glyph.clusterBegin -= begin;
                glyph.glyph.clusterEnd -= begin;
            }
            tTextLines.push_back(std::move(line));
            begin = lineEnd;
        } while (begin < end);
    }
    return tTextLines;
}

[[nodiscard]] float LineHeight(const ScreenUIFontAtlasCache::FontEntry& entry, const ScreenUIFontAtlasCache::FontEntry* fallback,
                               const kb::scene::UIText& text) noexcept {
    const float ascent = fallback ? std::max(entry.ascent, fallback->ascent) : entry.ascent;
    const float descent = fallback ? std::min(entry.descent, fallback->descent) : entry.descent;
    const float gap = fallback ? std::max(entry.lineGap, fallback->lineGap) : entry.lineGap;
    return std::max((ascent - descent + gap) * std::max(text.lineSpacing, 0.01F), 1.0F);
}

struct TextLayoutPlan {
    std::vector<TextLine> lines;
    std::size_t visualLines = 0U;
    bool valid = true;
};

[[nodiscard]] TextLayoutPlan BuildTextLayout(const ScreenUIFontAtlasCache::FontEntry& entry,
                                              const ScreenUIFontAtlasCache::FontEntry* fallback,
                                              const ScreenUITextMarkup& markup, kb::scene::UITextWrapMode wrap,
                                              float width, float spacing) {
    TextLayoutPlan plan;
    const auto span = [&](const ScreenUITextMarkupRange& range) {
        return std::span<const ScreenUITextMarkupGlyph>{markup.glyphs}.subspan(range.begin, range.end - range.begin);
    };
    const auto makeLines = [&](std::span<const ScreenUITextMarkupGlyph> content, float lineWidth) {
        if (content.empty()) return std::vector<TextLine>{TextLine{}};
        return std::vector<TextLine>{BreakLines(entry, fallback, content, wrap, lineWidth, spacing)};
    };
    if (markup.blocks.empty()) {
        plan.lines = makeLines(markup.glyphs, width);
        plan.valid = !plan.lines.empty();
        for (auto& line : plan.lines) { line.columnWidth = width; line.visualLine = plan.visualLines++; }
        return plan;
    }
    for (const auto& block : markup.blocks) {
        if (!block.table) {
            auto lines = makeLines(span(block.text), width);
            if (lines.empty()) { plan.valid = false; return plan; }
            for (auto& line : lines) {
                line.columnWidth = width; line.visualLine = plan.visualLines++;
                plan.lines.push_back(std::move(line));
            }
            continue;
        }
        for (const auto& row : block.rows) {
            if (row.cells.empty()) { plan.valid = false; return plan; }
            const auto count = row.cells.size();
            std::vector<std::vector<TextLine>> cells;
            cells.reserve(count);
            std::size_t rowLines = 1U;
            for (std::size_t col = 0U; col < count; ++col) {
                const float left = width * static_cast<float>(col) / static_cast<float>(count);
                const float right = width * static_cast<float>(col + 1U) / static_cast<float>(count);
                auto lines = makeLines(span(row.cells[col]), right - left);
                if (lines.empty()) { plan.valid = false; return plan; }
                rowLines = std::max(rowLines, lines.size());
                for (std::size_t index = 0U; index < lines.size(); ++index) {
                    lines[index].columnLeft = left;
                    lines[index].columnWidth = right - left;
                    lines[index].visualLine = plan.visualLines + index;
                    lines[index].tableCell = true;
                    lines[index].cellContinues = index + 1U < lines.size();
                }
                cells.push_back(std::move(lines));
            }
            for (auto& cell : cells)
                for (auto& line : cell) plan.lines.push_back(std::move(line));
            plan.visualLines += rowLines;
        }
    }
    return plan;
}

} // namespace

std::size_t ScreenUIFontAtlasCache::FontKeyHash::operator()(const FontKey& key) const noexcept {
    std::size_t hash = static_cast<std::size_t>(key.sceneId ^ (key.sceneId >> 32U));
    hash ^= static_cast<std::size_t>(key.assetId ^ (key.assetId >> 32U)) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
    hash ^= static_cast<std::size_t>(key.pixelSize) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
    hash ^= std::hash<std::string>{}(key.variations) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
    hash ^= static_cast<std::size_t>(key.distanceField) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
    return hash;
}

ScreenUIFontPreparation ScreenUIFontAtlasCache::Prepare(std::uint64_t sceneId, kb::assets::AssetManager& assets,
                                                        RenderResourceRegistry& resources,
                                                        const kb::scene::SceneUIFrame& frame, bool distanceFieldAvailable) {
    ++prepareFrame_;
    if (prepareFrame_ == 0U) {
        prepareFrame_ = 1U;
        for (auto& [key, entry] : fonts_) { static_cast<void>(key); entry.lastUsedFrame = 0U; entry.lastCompactedFrame = 0U; }
    }
    textRuns_.clear();
    textRuns_.reserve(frame.elements.size());
    std::uint64_t failedEntity = 0U;
    const char* failureReason = nullptr;
    // Keeps the first failure only: later ones are usually the same authoring mistake
    // repeated across a menu, and naming them all buries the one worth fixing.
    const auto fail = [&failedEntity, &failureReason](std::uint64_t entity, const char* reason) noexcept {
        if (failureReason == nullptr) {
            failedEntity = entity;
            failureReason = reason;
        }
    };
    for (const kb::scene::SceneUIFrameElement& element : frame.elements) {
        if (!element.text.has_value() || (!element.inputField && kb::scene::UITextContent(*element.text).empty()) ||
            element.text->fontAssetId == 0U || (element.mask.has_value() && !element.mask->showGraphic)) {
            continue;
        }
        if (element.effectiveOpacity <= 0.0F || element.rect.width <= 0.0F || element.rect.height <= 0.0F ||
            element.clipRect.width <= 0.0F || element.clipRect.height <= 0.0F) {
            continue;
        }
        const float scaledSize = element.text->fontSize * std::max(element.canvasScale, 0.01F);
        std::uint32_t pixelSize = static_cast<std::uint32_t>(std::clamp(std::lround(scaledSize), 4L, 256L));
        const ScreenUITextMarkup parsed =
            ScreenUITextMarkupParser{}.Parse(kb::scene::UITextContent(*element.text), element.text->richText,
                                             !element.inputField.has_value(), !element.inputField.has_value());
        if (!parsed.succeeded) {
            fail(element.entity.Id(), "Text markup is malformed");
            continue;
        }
        if (parsed.glyphs.empty() && !element.inputField)
            continue;
        const float outline = element.outline.has_value() ? element.outline->width * element.canvasScale : 0.0F;
        const float shadow = element.shadow.has_value() ? element.shadow->blur * element.canvasScale : 0.0F;
        const float requiredPadding =
            std::ceil(std::max(outline, shadow)) * 2.0F + static_cast<float>(kMinimumGlyphPadding);
        if (requiredPadding > static_cast<float>(std::numeric_limits<std::uint16_t>::max())) {
            fail(element.entity.Id(), "Text outline or shadow is too large to pad a glyph atlas");
            continue;
        }
        const auto useDistanceField = [&](std::uint32_t size) {
            return distanceFieldAvailable && element.text->fontRenderMode == kb::scene::UITextFontRenderMode::SignedDistanceField &&
                std::max(outline, shadow) <= (kFontDistanceFieldPadding - 1) * static_cast<float>(size) / 64.0F;
        };
        FontKey key{.sceneId = sceneId, .assetId = element.text->fontAssetId, .pixelSize = pixelSize,
                    .variations = std::string{kb::scene::UITextFontVariations(*element.text)},
                    .distanceField = useDistanceField(pixelSize)};
        FontEntry* entry = Ensure(key, {}, static_cast<std::uint16_t>(requiredPadding), assets, resources);
        if (entry == nullptr) {
            fail(element.entity.Id(), "Font asset is unavailable or its glyphs do not fit an atlas");
            continue;
        }
        const auto setLanguage = [&](FontEntry& fontEntry) {
            fontEntry.language = element.textLanguage.empty() ? frame.language : element.textLanguage;
        };
        setLanguage(*entry);
        FontKey fallbackKey{.sceneId = sceneId, .assetId = element.text->fallbackFontAssetId, .pixelSize = pixelSize,
                            .distanceField = key.distanceField};
        if (fallbackKey.assetId == key.assetId) fallbackKey.assetId = 0U;
        FontEntry* fallback = nullptr;
        if (fallbackKey.assetId != 0U) {
            fallback = Ensure(fallbackKey, {}, static_cast<std::uint16_t>(requiredPadding), assets, resources);
            if (!fallback) {
                fail(element.entity.Id(), "Fallback font asset is unavailable or its glyphs do not fit an atlas");
                continue;
            }
            setLanguage(*fallback);
        }
        // Auto size: the largest size between the minimum and the authored size at which the text fits,
        // found against this atlas's metrics, then laid out with an atlas of that size.
        if (element.text->autoSize && !Fits(element, *entry, fallback, parsed, 1.0F)) {
            float low = std::min(1.0F, element.text->minFontSize / element.text->fontSize);
            float high = 1.0F;
            for (int step = 0; step < 8; ++step) {
                const float middle = (low + high) * 0.5F;
                (Fits(element, *entry, fallback, parsed, middle) ? low : high) = middle;
            }
            const std::uint32_t fitted = static_cast<std::uint32_t>(std::clamp(std::lround(scaledSize * low), 4L, 256L));
            if (fitted != pixelSize) {
                pixelSize = fitted;
                key.pixelSize = fitted;
                key.distanceField = useDistanceField(fitted);
                entry = Ensure(key, {}, static_cast<std::uint16_t>(requiredPadding), assets, resources);
                if (entry == nullptr) {
                    fail(element.entity.Id(), "Font asset is unavailable or its glyphs do not fit an atlas");
                    continue;
                }
                setLanguage(*entry);
                fallbackKey.pixelSize = fitted;
                fallbackKey.distanceField = key.distanceField;
                if (fallbackKey.assetId != 0U) {
                    fallback = Ensure(fallbackKey, {}, static_cast<std::uint16_t>(requiredPadding), assets, resources);
                    if (!fallback) {
                        fail(element.entity.Id(), "Fallback font asset is unavailable or its glyphs do not fit an atlas");
                        continue;
                    }
                    setLanguage(*fallback);
                }
            }
        }
        ScreenUITextRun run{.entity = element.entity.Id(),
                            .fontAssetId = key.assetId,
                            .pixelSize = key.pixelSize,
                            .atlasWidth = entry->atlasWidth,
                            .atlasHeight = entry->atlasHeight};
        run.links = parsed.links;
        run.fontVariations = key.variations;
        run.distanceField = key.distanceField;
        run.fallbackFontAssetId = fallbackKey.assetId;
        if (fallback) { run.fallbackAtlasWidth = fallback->atlasWidth; run.fallbackAtlasHeight = fallback->atlasHeight; }
        if (!Layout(element, key, *entry, fallbackKey, fallback, parsed, run, resources)) {
            fail(element.entity.Id(), "Shaped text cannot be prepared");
            continue;
        }
        textRuns_.push_back(std::move(run));
    }
    // Compact crowded atlases once per key, using all currently drawable runs.
    // Rebuild keeps the previous texture and glyph map on failure.
    std::unordered_map<FontKey, std::vector<std::uint32_t>, FontKeyHash> activeGlyphs;
    for (const auto& run : textRuns_) {
        const FontKey primary{sceneId, run.fontAssetId, run.pixelSize, run.fontVariations, run.distanceField};
        const FontKey secondary{sceneId, run.fallbackFontAssetId, run.pixelSize, {}, run.distanceField};
        auto& primaryGlyphs = activeGlyphs[primary];
        if (run.fallbackFontAssetId != 0U) static_cast<void>(activeGlyphs[secondary]);
        for (const auto& glyph : run.glyphs) {
            if (glyph.fallbackFont && run.fallbackFontAssetId != 0U) activeGlyphs[secondary].push_back(glyph.glyphId);
            else primaryGlyphs.push_back(glyph.glyphId);
        }
    }
    for (auto& [key, needed] : activeGlyphs) {
        const auto found = fonts_.find(key);
        if (found == fonts_.end()) continue;
        auto& entry = found->second;
        if (prepareFrame_ - entry.lastCompactedFrame < 120U ||
            (entry.glyphIds.size() <= 64U && entry.atlasWidth < 1024U)) continue;
        std::ranges::sort(needed);
        needed.erase(std::unique(needed.begin(), needed.end()), needed.end());
        entry.lastCompactedFrame = prepareFrame_;
        if (needed == entry.glyphIds) continue;
        auto previous = entry.glyphIds;
        entry.glyphIds = std::move(needed);
        const auto codepoints = entry.codepoints;
        if (!Rebuild(key, entry, codepoints, entry.glyphPadding, resources)) entry.glyphIds = std::move(previous);
    }
    // Later labels or compaction can move shared glyphs. Refresh against final packing.
    for (auto& run : textRuns_) {
        const auto found = fonts_.find(FontKey{sceneId, run.fontAssetId, run.pixelSize, run.fontVariations, run.distanceField});
        if (found == fonts_.end()) continue;
        const auto& entry = found->second;
        run.atlasWidth = entry.atlasWidth;
        run.atlasHeight = entry.atlasHeight;
        const auto fallback = run.fallbackFontAssetId != 0U
            ? fonts_.find(FontKey{sceneId, run.fallbackFontAssetId, run.pixelSize, {}, run.distanceField}) : fonts_.end();
        if (fallback != fonts_.end()) { run.fallbackAtlasWidth = fallback->second.atlasWidth; run.fallbackAtlasHeight = fallback->second.atlasHeight; }
        for (auto& quad : run.glyphs) {
            const auto* source = quad.fallbackFont && fallback != fonts_.end() ? &fallback->second : &entry;
            const auto glyph = source->glyphs.find(quad.glyphId);
            if (glyph == source->glyphs.end()) continue;
            quad.u0 = glyph->second.u0;
            quad.v0 = glyph->second.v0;
            quad.u1 = glyph->second.u1;
            quad.v1 = glyph->second.v1;
            quad.distanceField = glyph->second.distanceField;
            quad.left = quad.fontOriginX + glyph->second.x0;
            quad.top = quad.fontOriginY + glyph->second.y0;
            quad.right = quad.fontOriginX + glyph->second.x1;
            quad.bottom = quad.fontOriginY + glyph->second.y1;
        }
    }
    for (auto it = fonts_.begin(); it != fonts_.end();) {
        if (it->first.sceneId == sceneId && prepareFrame_ - it->second.lastUsedFrame >= 120U) {
            if (it->second.texture.IsValid()) resources.DestroyTexture(it->second.texture);
            it = fonts_.erase(it);
        } else ++it;
    }
    return ScreenUIFontPreparation{
        .runs = textRuns_, .failedEntity = failedEntity, .failureReason = failureReason};
}

bgfx::TextureHandle ScreenUIFontAtlasCache::Resolve(std::uint64_t sceneId, std::uint64_t fontAssetId,
                                                    std::uint32_t pixelSize,
                                                    std::string_view variations, bool distanceField,
                                                    const RenderResourceRegistry& resources) const noexcept {
    const auto found = std::ranges::find_if(fonts_, [&](const auto& pair) {
        return pair.first.sceneId == sceneId && pair.first.assetId == fontAssetId &&
            pair.first.pixelSize == pixelSize && pair.first.variations == variations && pair.first.distanceField == distanceField;
    });
    if (found == fonts_.end()) {
        return BGFX_INVALID_HANDLE;
    }
    const RenderTextureResource* texture = resources.FindTexture(found->second.texture);
    return texture != nullptr ? texture->texture : bgfx::TextureHandle{bgfx::kInvalidHandle};
}

void ScreenUIFontAtlasCache::ReleaseScene(std::uint64_t sceneId, RenderResourceRegistry& resources) noexcept {
    for (auto it = fonts_.begin(); it != fonts_.end();) {
        if (it->first.sceneId == sceneId) {
            resources.DestroyTexture(it->second.texture);
            it = fonts_.erase(it);
        } else {
            ++it;
        }
    }
}

void ScreenUIFontAtlasCache::Shutdown(RenderResourceRegistry& resources) noexcept {
    for (auto& [key, entry] : fonts_) {
        static_cast<void>(key);
        resources.DestroyTexture(entry.texture);
    }
    fonts_.clear();
    textRuns_.clear();
}

ScreenUIFontAtlasCache::TypographyCacheStats ScreenUIFontAtlasCache::TextCacheStats() const noexcept {
    TypographyCacheStats result;
    for (const auto& [key, entry] : fonts_) {
        static_cast<void>(key);
        result.entries += entry.shapingCache.size();
        result.payloadBytes += entry.shapingBytes;
        result.hits += entry.shapingHits;
        result.misses += entry.shapingMisses;
    }
    return result;
}

ScreenUIFontAtlasCache::FontEntry* ScreenUIFontAtlasCache::Ensure(const FontKey& key,
                                                                  std::span<const std::uint32_t> codepoints,
                                                                  std::uint16_t glyphPadding,
                                                                  kb::assets::AssetManager& assets,
                                                                  RenderResourceRegistry& resources) {
    FontEntry& entry = fonts_[key];
    entry.assetId = key.assetId;
    entry.lastUsedFrame = prepareFrame_;
    if (!entry.variationsParsed) {
        if (!kb::scene::ParseUITextFontVariations(key.variations, entry.variations)) {
            if (entry.texture.IsValid()) resources.DestroyTexture(entry.texture);
            fonts_.erase(key);
            return nullptr;
        }
        entry.variationsParsed = true;
    }
    const kb::assets::AssetId assetId{key.assetId};
    const std::uint64_t generation = assets.LoadGeneration(assetId);
    const bool reloaded = !entry.asset.IsLoaded() || entry.loadGeneration != generation;
    if (reloaded) {
        entry.asset = assets.Load<kb::assets::ImportedAsset>(assetId);
        entry.loadGeneration = assets.LoadGeneration(assetId);
        if (!entry.asset.IsLoaded() || !ScreenUIFontPayloadValidator::Validate(*entry.asset)) {
            if (entry.texture.IsValid()) {
                resources.DestroyTexture(entry.texture);
            }
            fonts_.erase(key);
            return nullptr;
        }
        entry.codepoints.clear();
        entry.glyphIds.clear();
        entry.nominalGlyphs.clear();
        entry.shapingCache.clear();
        entry.shapingBytes = 0U;
    }

    std::vector<std::uint32_t> merged = entry.codepoints;
    merged.insert(merged.end(), codepoints.begin(), codepoints.end());
    std::ranges::sort(merged);
    merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
    if ((reloaded || merged != entry.codepoints || glyphPadding > entry.glyphPadding) &&
        !Rebuild(key, entry, merged, std::max(glyphPadding, entry.glyphPadding), resources)) {
        if (!reloaded && entry.texture.IsValid()) {
            const auto previous = entry.glyphIds;
            entry.glyphIds.clear();
            for (const auto& prior : textRuns_) {
                const FontKey primary{key.sceneId, prior.fontAssetId, prior.pixelSize, prior.fontVariations, prior.distanceField};
                const FontKey secondary{key.sceneId, prior.fallbackFontAssetId, prior.pixelSize, {}, prior.distanceField};
                for (const auto& glyph : prior.glyphs)
                    if ((glyph.fallbackFont ? secondary : primary) == key) entry.glyphIds.push_back(glyph.glyphId);
            }
            std::ranges::sort(entry.glyphIds);
            entry.glyphIds.erase(std::unique(entry.glyphIds.begin(), entry.glyphIds.end()), entry.glyphIds.end());
            if (Rebuild(key, entry, merged, std::max(glyphPadding, entry.glyphPadding), resources)) {
                entry.lastCompactedFrame = prepareFrame_;
                return &entry;
            }
            entry.glyphIds = previous;
        } else {
            if (entry.texture.IsValid()) resources.DestroyTexture(entry.texture);
            fonts_.erase(key);
        }
        return nullptr;
    }
    return &entry;
}

bool ScreenUIFontAtlasCache::Rebuild(const FontKey& key, FontEntry& entry, std::span<const std::uint32_t> codepoints,
                                     std::uint16_t glyphPadding, RenderResourceRegistry& resources, bool forceBitmap) {
    if (entry.asset->payload.empty()) {
        return false;
    }
    const auto* bytes = reinterpret_cast<const unsigned char*>(entry.asset->payload.data());
    const int fontOffset = stbtt_GetFontOffsetForIndex(bytes, 0);
    stbtt_fontinfo font{};
    if (fontOffset < 0 || stbtt_InitFont(&font, bytes, fontOffset) == 0) {
        return false;
    }
    entry.scale = stbtt_ScaleForPixelHeight(&font, static_cast<float>(key.pixelSize));
    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    entry.ascent = static_cast<float>(ascent) * entry.scale;
    entry.descent = static_cast<float>(descent) * entry.scale;
    entry.lineGap = static_cast<float>(lineGap) * entry.scale;

    std::unique_ptr<ScreenUIVariableFontRasterizer> variableRasterizer;
    if (!entry.variations.empty() || key.distanceField) {
        try { variableRasterizer = std::make_unique<ScreenUIVariableFontRasterizer>(entry.asset->payload, entry.variations); }
        catch (const std::bad_alloc&) { if (!entry.variations.empty()) return false; }
        if (variableRasterizer && !variableRasterizer->IsValid()) variableRasterizer.reset();
        if (!entry.variations.empty() && (!variableRasterizer ||
            !variableRasterizer->Metrics(entry.scale, entry.ascent, entry.descent, entry.lineGap))) return false;
    }

    std::vector<std::uint32_t> glyphIds = entry.glyphIds;
    std::unordered_map<std::uint32_t, std::uint32_t> nominalGlyphs;
    for (const auto codepoint : codepoints) {
        const auto id = static_cast<std::uint32_t>(stbtt_FindGlyphIndex(&font, static_cast<int>(codepoint)));
        nominalGlyphs.emplace(codepoint, id);
        glyphIds.push_back(id);
    }
    std::ranges::sort(glyphIds);
    glyphIds.erase(std::unique(glyphIds.begin(), glyphIds.end()), glyphIds.end());
    std::uint32_t initialExtent = kInitialAtlasExtent;
    const std::uint64_t minimumExtent = static_cast<std::uint64_t>(glyphPadding) * 2U + key.pixelSize;
    while (initialExtent < minimumExtent && initialExtent < kMaximumAtlasExtent) {
        initialExtent *= 2U;
    }
    if (initialExtent > kMaximumAtlasExtent || initialExtent < minimumExtent) {
        return false;
    }
    std::uint16_t extent = static_cast<std::uint16_t>(initialExtent);
    std::unordered_map<std::uint32_t, Glyph> glyphs;
    std::vector<std::uint8_t> alpha;
    bool packed = false;
    while (!packed && extent <= kMaximumAtlasExtent) {
        alpha.assign(static_cast<std::size_t>(extent) * extent, 0U);
        glyphs.clear();
        const int padding = static_cast<int>(glyphPadding);
        int x = padding;
        int y = padding;
        int rowHeight = 0;
        packed = true;
        for (const std::uint32_t codepoint : glyphIds) {
            if (codepoint >= static_cast<std::uint32_t>(font.numGlyphs)) return false;
            constexpr float basePixels = 64.0F;
            const float fieldScale = stbtt_ScaleForPixelHeight(&font, basePixels);
            const float geometryScale = fieldScale > 0.0F ? entry.scale / fieldScale : 0.0F;
            bool useField = key.distanceField && !forceBitmap && variableRasterizer && fieldScale > 0.0F && std::isfinite(geometryScale);
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            std::vector<std::uint8_t> pixels;
            const auto rasterize = [&](float scale) {
                pixels.clear();
                if (variableRasterizer) {
                    ScreenUIVariableFontRasterizer::Bitmap bitmap;
                    if (!variableRasterizer->Rasterize(codepoint, scale, bitmap)) return false;
                    x0 = bitmap.x0; y0 = bitmap.y0; x1 = bitmap.x1; y1 = bitmap.y1;
                    pixels = std::move(bitmap.alpha);
                } else {
                    stbtt_GetGlyphBitmapBox(&font, static_cast<int>(codepoint), scale, scale, &x0, &y0, &x1, &y1);
                    const int w = std::max(x1 - x0, 0), h = std::max(y1 - y0, 0);
                    if (w > 0 && h > 0) {
                        pixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
                        stbtt_MakeGlyphBitmap(&font, pixels.data(), w, h, w, scale, scale, static_cast<int>(codepoint));
                    }
                }
                const auto w = static_cast<std::size_t>(std::max(x1 - x0, 0));
                const auto h = static_cast<std::size_t>(std::max(y1 - y0, 0));
                return pixels.size() == w * h;
            };
            if (useField) {
                try {
                    if (!rasterize(fieldScale)) useField = false;
                    if (useField && !pixels.empty()) {
                        std::vector<std::uint8_t> field;
                        const int w = x1 - x0, h = y1 - y0;
                        if (!BuildFontDistanceField(pixels, w, h, field)) useField = false;
                        else {
                            pixels = std::move(field);
                            x0 -= kFontDistanceFieldPadding; y0 -= kFontDistanceFieldPadding;
                            x1 += kFontDistanceFieldPadding; y1 += kFontDistanceFieldPadding;
                        }
                    }
                } catch (const std::bad_alloc&) { useField = false; }
            }
            if (!useField) {
                try { if (!rasterize(entry.scale)) return false; }
                catch (const std::bad_alloc&) { return false; }
            }
            const int width = std::max(x1 - x0, 0);
            const int height = std::max(y1 - y0, 0);
            if (width > static_cast<int>(extent) - 2 * padding) {
                packed = false;
                break;
            }
            if (x + width + padding > extent) {
                x = padding;
                y += rowHeight + padding;
                rowHeight = 0;
            }
            if (y + height + padding > extent) {
                packed = false;
                break;
            }
            int advance = 0;
            int leftBearing = 0;
            stbtt_GetGlyphHMetrics(&font, static_cast<int>(codepoint), &advance, &leftBearing);
            static_cast<void>(leftBearing);
            if (width > 0 && height > 0) {
                if (pixels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) return false;
                for (int row = 0; row < height; ++row) {
                    std::copy_n(pixels.data() + static_cast<std::size_t>(row) * width, width,
                                alpha.data() + static_cast<std::size_t>(y + row) * extent + x);
                }
            }
            const float glyphScale = useField ? geometryScale : 1.0F;
            glyphs.emplace(codepoint, Glyph{.codepoint = codepoint,
                                            .advance = static_cast<float>(advance) * entry.scale,
                                            .x0 = static_cast<float>(x0) * glyphScale,
                                            .y0 = static_cast<float>(y0) * glyphScale,
                                            .x1 = static_cast<float>(x1) * glyphScale,
                                            .y1 = static_cast<float>(y1) * glyphScale,
                                            .u0 = static_cast<float>(x) / static_cast<float>(extent),
                                            .v0 = static_cast<float>(y) / static_cast<float>(extent),
                                            .u1 = static_cast<float>(x + width) / static_cast<float>(extent),
                                            .v1 = static_cast<float>(y + height) / static_cast<float>(extent),
                                            .distanceField = useField});
            x += width + padding;
            rowHeight = std::max(rowHeight, height);
        }
        if (!packed) {
            extent = static_cast<std::uint16_t>(extent * 2U);
        }
    }
    if (!packed || extent > kMaximumAtlasExtent) {
        if (key.distanceField && !forceBitmap) return Rebuild(key, entry, codepoints, glyphPadding, resources, true);
        return false;
    }

    std::vector<std::uint8_t> rgba(alpha.size() * 4U, 255U);
    for (std::size_t index = 0U; index < alpha.size(); ++index) {
        rgba[index * 4U + 3U] = alpha[index];
    }
    if (rgba.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    const RenderTextureHandle newTexture = resources.RegisterTexture(RenderTextureDesc{
        .width = extent,
        .height = extent,
        .format = bgfx::TextureFormat::RGBA8,
        .flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
        .memory = bgfx::copy(rgba.data(), static_cast<std::uint32_t>(rgba.size())),
        .colorSpace = RenderTextureColorSpace::Linear,
    });
    if (!newTexture.IsValid()) {
        if (key.distanceField && !forceBitmap) return Rebuild(key, entry, codepoints, glyphPadding, resources, true);
        return false;
    }
    if (entry.texture.IsValid()) {
        resources.DestroyTexture(entry.texture);
    }
    entry.texture = newTexture;
    entry.atlasWidth = extent;
    entry.atlasHeight = extent;
    entry.glyphPadding = glyphPadding;
    entry.codepoints.assign(codepoints.begin(), codepoints.end());
    entry.glyphs = std::move(glyphs);
    entry.glyphIds = std::move(glyphIds);
    entry.nominalGlyphs = std::move(nominalGlyphs);
    return true;
}

bool ScreenUIFontAtlasCache::Fits(const kb::scene::SceneUIFrameElement& element, const FontEntry& entry, const FontEntry* fallback,
                                  const ScreenUITextMarkup& markup, float factor) {
    const float spacing = element.text->characterSpacing * element.canvasScale / factor;
    const float width = element.rect.width / factor;
    const auto plan = BuildTextLayout(entry, fallback, markup, element.text->wrapMode, width, spacing);
    if (!plan.valid || plan.lines.empty()) return false;
    if (element.text->maxLines != 0U && plan.visualLines > element.text->maxLines) return false;
    if (LineHeight(entry, fallback, *element.text) * static_cast<float>(plan.visualLines) > element.rect.height / factor) return false;
    return std::ranges::all_of(plan.lines, [](const TextLine& line) { return line.width <= line.columnWidth + 0.5F; });
}

bool ScreenUIFontAtlasCache::Layout(const kb::scene::SceneUIFrameElement& element, const FontKey& key, FontEntry& entry,
                                    const FontKey& fallbackKey, FontEntry* fallback,
                                    const ScreenUITextMarkup& markup, ScreenUITextRun& run,
                                    RenderResourceRegistry& resources) {
    const float availableWidth = element.rect.width;
    const float spacing = element.text->characterSpacing * element.canvasScale;
    auto plan = BuildTextLayout(entry, fallback, markup, element.text->wrapMode, availableWidth, spacing);
    auto& lines = plan.lines;
    if (!plan.valid || lines.empty()) return false;
    const float lineHeight = LineHeight(entry, fallback, *element.text);
    // Lines past the limit, and with Truncate or Ellipsis the lines past the bottom of the box, are left
    // out; with Ellipsis the last line shown, and any line wider than the box, ends in an ellipsis.
    std::size_t shown = plan.visualLines;
    if (element.text->maxLines != 0U)
        shown = std::min<std::size_t>(shown, element.text->maxLines);
    if (element.text->overflow != kb::scene::UITextOverflow::Overflow)
        shown = std::min(shown, std::max<std::size_t>(1U, static_cast<std::size_t>(element.rect.height / lineHeight)));
    const bool cut = shown < plan.visualLines;
    std::erase_if(lines, [shown](const TextLine& line) { return line.visualLine >= shown; });
    if (element.text->overflow == kb::scene::UITextOverflow::Ellipsis) {
        ScreenUITextMarkupGlyph probe;
        probe.codepoint = kEllipsisCodepoint;
        std::vector<kb::scene::UITextFontShapedGlyph> shapedProbe;
        const bool hasEllipsis = Shape(entry, fallback, std::span{&probe, 1U}, shapedProbe) && !shapedProbe.empty() &&
            std::ranges::none_of(shapedProbe, [](const auto& glyph) { return glyph.glyph.glyphId == 0U; });
        for (std::size_t lineIndex = 0U; lineIndex < lines.size(); ++lineIndex) {
            TextLine& line = lines[lineIndex];
            const bool last = line.visualLine + 1U == shown;
            if (line.width <= line.columnWidth + 0.5F && !(last && cut && (!line.tableCell || line.cellContinues)))
                continue;
            ScreenUITextMarkupGlyph mark = line.glyphs.empty() ? ScreenUITextMarkupGlyph{} : line.glyphs.back();
            std::vector<ScreenUITextMarkupGlyph> suffix;
            mark.codepoint = hasEllipsis ? kEllipsisCodepoint : static_cast<std::uint32_t>('.');
            mark.link = 0U;
            mark.underline = false;
            suffix.assign(hasEllipsis ? 1U : 3U, mark);
            const float suffixWidth = MeasureCodepoints(entry, fallback, suffix, spacing);
            while (!line.glyphs.empty() &&
                   (MeasureCodepoints(entry, fallback, line.glyphs, spacing) + suffixWidth > line.columnWidth ||
                    line.glyphs.back().codepoint == static_cast<std::uint32_t>(' '))) {
                std::vector<kb::scene::UITextFontShapedGlyph> tail;
                if (!Shape(entry, fallback, line.glyphs, tail) || tail.empty()) return false;
                std::uint32_t lastCluster = 0U;
                for (const auto& glyph : tail) lastCluster = std::max(lastCluster, glyph.glyph.clusterBegin);
                std::vector<std::uint32_t> tailCodepoints;
                for (const auto& glyph : line.glyphs) tailCodepoints.push_back(glyph.codepoint);
                const auto boundaries = SafeBreaks(tailCodepoints, tail);
                const auto boundary = std::upper_bound(boundaries.begin(), boundaries.end(), lastCluster);
                if (boundary != boundaries.begin()) lastCluster = *std::prev(boundary);
                line.glyphs.resize(lastCluster);
            }
            if (!line.glyphs.empty()) {
                for (ScreenUITextMarkupGlyph& glyph : suffix) {
                    glyph.sourceOffset = line.glyphs.back().sourceOffset;
                    glyph.color = line.glyphs.back().color;
                }
            }
            line.glyphs.insert(line.glyphs.end(), suffix.begin(), suffix.end());
            if (!Shape(entry, fallback, line.glyphs, line.shaped)) return false;
            std::vector<std::uint32_t> codepoints;
            for (const auto& glyph : line.glyphs) codepoints.push_back(glyph.codepoint);
            line.width = ShapedWidth(line.shaped, spacing, SafeBreaks(codepoints, line.shaped));
        }
    }
    const auto ensureShapedGlyphs = [&](const FontKey& fontKey, FontEntry& fontEntry, bool useFallback) {
        std::vector<std::uint32_t> needed = fontEntry.glyphIds;
        for (const auto& line : lines) for (const auto& glyph : line.shaped)
            if (glyph.fallback == useFallback) needed.push_back(glyph.glyph.glyphId);
        std::ranges::sort(needed);
        needed.erase(std::unique(needed.begin(), needed.end()), needed.end());
        if (needed == fontEntry.glyphIds) return true;
        const auto previous = fontEntry.glyphIds;
        fontEntry.glyphIds = std::move(needed);
        const auto codepoints = fontEntry.codepoints;
        if (!Rebuild(fontKey, fontEntry, codepoints, fontEntry.glyphPadding, resources)) {
            // Historical glyphs may have filled the atlas. Retry with only the
            // current text and previously prepared runs that use this exact font.
            fontEntry.glyphIds.clear();
            for (const auto& line : lines) for (const auto& glyph : line.shaped)
                if (glyph.fallback == useFallback) fontEntry.glyphIds.push_back(glyph.glyph.glyphId);
            for (const auto& prior : textRuns_) {
                const FontKey primary{fontKey.sceneId, prior.fontAssetId, prior.pixelSize, prior.fontVariations, prior.distanceField};
                const FontKey secondary{fontKey.sceneId, prior.fallbackFontAssetId, prior.pixelSize, {}, prior.distanceField};
                for (const auto& glyph : prior.glyphs) {
                    if ((glyph.fallbackFont ? secondary : primary) == fontKey)
                        fontEntry.glyphIds.push_back(glyph.glyphId);
                }
            }
            std::ranges::sort(fontEntry.glyphIds);
            fontEntry.glyphIds.erase(std::unique(fontEntry.glyphIds.begin(), fontEntry.glyphIds.end()), fontEntry.glyphIds.end());
            if (!Rebuild(fontKey, fontEntry, codepoints, fontEntry.glyphPadding, resources)) {
                fontEntry.glyphIds = previous;
                return false;
            }
            fontEntry.lastCompactedFrame = prepareFrame_;
        }
        return true;
    };
    if (!ensureShapedGlyphs(key, entry, false) || (fallback && !ensureShapedGlyphs(fallbackKey, *fallback, true))) return false;
    const float textHeight = lineHeight * static_cast<float>(shown);
    float top = element.rect.y;
    if (element.text->verticalAlignment == kb::scene::UITextVerticalAlignment::Center) {
        top += (element.rect.height - textHeight) * 0.5F;
    } else if (element.text->verticalAlignment == kb::scene::UITextVerticalAlignment::Bottom) {
        top += element.rect.height - textHeight;
    }

    run.glyphs.clear();
    run.carets.clear();
    run.glyphs.reserve(markup.glyphs.size());
    for (std::size_t lineIndex = 0U; lineIndex < lines.size(); ++lineIndex) {
        const TextLine& line = lines[lineIndex];
        std::vector<std::uint32_t> lineCodepoints;
        for (const auto& glyph : line.glyphs) lineCodepoints.push_back(glyph.codepoint);
        const auto lineBoundaries = SafeBreaks(lineCodepoints, line.shaped);
        float x = element.rect.x + line.columnLeft;
        if (element.text->horizontalAlignment == kb::scene::UITextHorizontalAlignment::Center) {
            x += (line.columnWidth - line.width) * 0.5F;
        } else if (element.text->horizontalAlignment == kb::scene::UITextHorizontalAlignment::Right) {
            x += line.columnWidth - line.width;
        }
        const float lineAscent = fallback ? std::max(entry.ascent, fallback->ascent) : entry.ascent;
        const float lineDescent = fallback ? std::min(entry.descent, fallback->descent) : entry.descent;
        const float baseline = top + static_cast<float>(line.visualLine) * lineHeight + lineAscent;
        if (line.shaped.empty()) {
            run.carets.push_back({line.sourceOffset, x, baseline - lineAscent, baseline - lineDescent, true});
        }
        for (std::size_t index = 0; index < line.shaped.size(); ++index) {
            const auto& shapedItem = line.shaped[index];
            const auto& shaped = shapedItem.glyph;
            const auto& styledGlyph = line.glyphs[shaped.clusterBegin];
            const auto& sourceFont = shapedItem.fallback && fallback ? *fallback : entry;
            const auto found = sourceFont.glyphs.find(shaped.glyphId);
            if (found == sourceFont.glyphs.end()) return false;
            const Glyph* glyph = &found->second;
            const auto& finalSource = line.glyphs[shaped.clusterEnd - 1U];
            const std::uint32_t byteCount = Utf8Width(finalSource.codepoint);
            if (index == 0U || line.shaped[index - 1U].glyph.clusterBegin != shaped.clusterBegin) {
                float clusterWidth = 0.0F;
                for (std::size_t next = index; next < line.shaped.size() &&
                     line.shaped[next].glyph.clusterBegin == shaped.clusterBegin; ++next)
                    clusterWidth += line.shaped[next].glyph.advance;
                std::vector<std::uint32_t> stops;
                const auto content = kb::scene::UITextContent(*element.text);
                for (auto source = shaped.clusterBegin; source < shaped.clusterEnd; ++source) {
                    const auto offset = line.glyphs[source].sourceOffset;
                    if (source == shaped.clusterBegin ||
                        kb::scene::SnapToUITextGraphemeBoundary(content, offset) == offset) stops.push_back(offset);
                }
                stops.push_back(finalSource.sourceOffset + byteCount);
                const bool singleGlyphCluster = index + 1U == line.shaped.size() ||
                    line.shaped[index + 1U].glyph.clusterBegin != shaped.clusterBegin;
                const bool fontCarets = singleGlyphCluster && stops.size() > 2U &&
                    shaped.ligatureCarets.size() == stops.size() - 2U &&
                    std::all_of(shaped.ligatureCarets.begin(), shaped.ligatureCarets.end(), [&](float caret) {
                        return caret + shaped.offsetX > 0.0F && caret + shaped.offsetX < clusterWidth;
                    }) && std::adjacent_find(shaped.ligatureCarets.begin(), shaped.ligatureCarets.end()) ==
                        shaped.ligatureCarets.end();
                for (std::size_t stop = 0; stop < stops.size(); ++stop) {
                    // A shaping cluster can cover only part of a Unicode grapheme
                    // (for example an Indic half-form). Keep its edge for geometry,
                    // but never publish that internal edge as an insertion point.
                    if (kb::scene::SnapToUITextGraphemeBoundary(content, stops[stop]) != stops[stop]) continue;
                    const float fraction = stops.size() > 1U ? static_cast<float>(stop) / static_cast<float>(stops.size() - 1U) : 0.0F;
                    float position = clusterWidth * (shaped.rightToLeft ? 1.0F - fraction : fraction);
                    if (fontCarets && stop > 0U && stop + 1U < stops.size()) {
                        const auto caret = shaped.rightToLeft ? stops.size() - 2U - stop : stop - 1U;
                        position = shaped.offsetX + shaped.ligatureCarets[caret];
                    }
                    run.carets.push_back({stops[stop], x + position,
                                          baseline - lineAscent, baseline - lineDescent, stop + 1U < stops.size()});
                }
            }
            run.glyphs.push_back(ScreenUIGlyphQuad{.left = x + shaped.offsetX + glyph->x0,
                                                   .top = baseline - shaped.offsetY + glyph->y0,
                                                   .right = x + shaped.offsetX + glyph->x1,
                                                   .bottom = baseline - shaped.offsetY + glyph->y1,
                                                   .u0 = glyph->u0,
                                                   .v0 = glyph->v0,
                                                   .u1 = glyph->u1,
                                                   .v1 = glyph->v1,
                                                   .color = styledGlyph.color,
                                                   .sourceOffset = styledGlyph.sourceOffset,
                                                   .lineTop = baseline - lineAscent,
                                                   .lineBottom = baseline - lineDescent,
                                                   .advanceRight = x + shaped.advance,
                                                   .glyphId = shaped.glyphId,
                                                   .sourceEnd = finalSource.sourceOffset + byteCount,
                                                   .advanceLeft = x,
                                                   .rightToLeft = shaped.rightToLeft,
                                                   .link = styledGlyph.link,
                                                   .underline = styledGlyph.underline,
                                                   .fallbackFont = shapedItem.fallback,
                                                   .cellClip = line.tableCell && element.text->overflow != kb::scene::UITextOverflow::Overflow,
                                                   .cellLeft = element.rect.x + line.columnLeft,
                                                   .cellRight = element.rect.x + line.columnLeft + line.columnWidth,
                                                   .distanceField = glyph->distanceField,
                                                   .fontOriginX = x + shaped.offsetX,
                                                   .fontOriginY = baseline - shaped.offsetY});
            x += shaped.advance;
            if (index + 1U == line.shaped.size() || SeparateClusters(shapedItem, line.shaped[index + 1U], lineBoundaries)) x += spacing;
        }
    }
    return true;
}

} // namespace kb::render
