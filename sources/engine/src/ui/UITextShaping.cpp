#include "engine/ui/visual/UITextShaping.hpp"

#include <SheenBidi/SheenBidi.h>
#include <hb.h>
#include <hb-ot.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <memory>
#include <type_traits>

namespace kb::scene {
namespace {
template <class Pointer, auto Release>
using Owned = std::unique_ptr<std::remove_pointer_t<Pointer>, decltype(Release)>;

struct ScriptSpan {
    std::size_t begin;
    std::size_t end;
    hb_script_t script;
};
} // namespace

bool ParseUITextFontVariations(std::string_view text, std::vector<UITextFontVariation>& output) {
    output.clear();
    if (text.empty()) return true;
    std::vector<UITextFontVariation> parsed;
    while (!text.empty()) {
        const auto comma = text.find(',');
        const auto part = text.substr(0U, comma);
        if (part.size() < 6U || part[4] != '=') return false;
        std::uint32_t tag = 0U;
        for (std::size_t i = 0U; i < 4U; ++i) {
            const auto c = static_cast<unsigned char>(part[i]);
            if (c < 0x21U || c > 0x7EU || c == '=' || c == ',') return false;
            tag = (tag << 8U) | c;
        }
        float value = 0.0F;
        const auto converted = std::from_chars(part.data() + 5U, part.data() + part.size(), value);
        if (converted.ec != std::errc{} || converted.ptr != part.data() + part.size() || !std::isfinite(value) ||
            std::ranges::any_of(parsed, [&](const auto& axis) { return axis.tag == tag; })) return false;
        parsed.push_back({tag, value});
        if (comma == std::string_view::npos) break;
        text.remove_prefix(comma + 1U);
        if (text.empty()) return false;
    }
    output = std::move(parsed);
    return true;
}

bool ShapeUIText(std::span<const std::byte> fontBytes, std::span<const std::uint32_t> paragraph,
                 std::size_t lineBegin, std::size_t lineEnd, float fontUnitScale,
                 std::vector<UITextShapedGlyph>& output, std::span<const std::uint32_t> styleBoundaries) {
    return ShapeUIText(fontBytes, paragraph, lineBegin, lineEnd, fontUnitScale, output, styleBoundaries, {});
}

static bool ShapeUITextImpl(std::span<const std::byte> fontBytes, std::span<const std::byte> fallbackBytes,
                 std::span<const std::uint32_t> paragraph, std::size_t lineBegin, std::size_t lineEnd,
                 float fontUnitScale, float fallbackUnitScale, std::span<const std::uint32_t> graphemeBoundaries,
                 std::vector<UITextFontShapedGlyph>& output, std::span<const std::uint32_t> styleBoundaries,
                 std::span<const UITextFontVariation> variations, std::string_view language) {
    output.clear();
    if (lineBegin > lineEnd || lineEnd > paragraph.size() ||
        paragraph.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        fontBytes.empty() || fontBytes.size() > std::numeric_limits<unsigned>::max() ||
        !std::isfinite(fontUnitScale) || fontUnitScale <= 0.0F || language.size() > 128U ||
        language.find('\0') != std::string_view::npos) return false;
    const hb_language_t shapingLanguage = language.empty() ? hb_language_from_string("und", -1) :
        hb_language_from_string(language.data(), static_cast<int>(language.size()));
    if (!std::is_sorted(styleBoundaries.begin(), styleBoundaries.end()) ||
        (!styleBoundaries.empty() && styleBoundaries.back() > paragraph.size())) return false;
    for (const auto codepoint : paragraph) {
        if (codepoint > 0x10FFFFU || (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) return false;
    }

    Owned<hb_blob_t*, hb_blob_destroy> blob(hb_blob_create(
        reinterpret_cast<const char*>(fontBytes.data()), static_cast<unsigned>(fontBytes.size()),
        HB_MEMORY_MODE_READONLY, nullptr, nullptr), hb_blob_destroy);
    Owned<hb_face_t*, hb_face_destroy> face(hb_face_create(blob.get(), 0), hb_face_destroy);
    if (hb_face_get_glyph_count(face.get()) == 0U) return false;
    Owned<hb_font_t*, hb_font_destroy> font(hb_font_create(face.get()), hb_font_destroy);
    hb_ot_font_set_funcs(font.get());
    if (variations.size() > hb_ot_var_get_axis_count(face.get())) return false;
    std::vector<hb_variation_t> axes;
    axes.reserve(variations.size());
    for (const auto& variation : variations) {
        hb_ot_var_axis_info_t info{};
        if (!std::isfinite(variation.value) || !hb_ot_var_find_axis_info(face.get(), variation.tag, &info) ||
            variation.value < info.min_value || variation.value > info.max_value ||
            std::ranges::any_of(axes, [&](const auto& axis) { return axis.tag == variation.tag; })) return false;
        axes.push_back({variation.tag, variation.value});
    }
    if (!axes.empty()) hb_font_set_variations(font.get(), axes.data(), static_cast<unsigned>(axes.size()));
    const int units = static_cast<int>(hb_face_get_upem(face.get()));
    hb_font_set_scale(font.get(), units, units);
    Owned<hb_blob_t*, hb_blob_destroy> fallbackBlob(nullptr, hb_blob_destroy);
    Owned<hb_face_t*, hb_face_destroy> fallbackFace(nullptr, hb_face_destroy);
    Owned<hb_font_t*, hb_font_destroy> fallbackFont(nullptr, hb_font_destroy);
    if (!fallbackBytes.empty()) {
        if (fallbackBytes.size() > std::numeric_limits<unsigned>::max() ||
            !std::isfinite(fallbackUnitScale) || fallbackUnitScale <= 0.0F ||
            graphemeBoundaries.empty() || graphemeBoundaries.front() != lineBegin ||
            graphemeBoundaries.back() != lineEnd ||
            std::adjacent_find(graphemeBoundaries.begin(), graphemeBoundaries.end(),
                [](auto a, auto b) { return a >= b; }) != graphemeBoundaries.end()) return false;
        fallbackBlob.reset(hb_blob_create(reinterpret_cast<const char*>(fallbackBytes.data()),
            static_cast<unsigned>(fallbackBytes.size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr));
        fallbackFace.reset(hb_face_create(fallbackBlob.get(), 0));
        if (hb_face_get_glyph_count(fallbackFace.get()) == 0U) return false;
        fallbackFont.reset(hb_font_create(fallbackFace.get()));
        hb_ot_font_set_funcs(fallbackFont.get());
        const int fallbackUnits = static_cast<int>(hb_face_get_upem(fallbackFace.get()));
        hb_font_set_scale(fallbackFont.get(), fallbackUnits, fallbackUnits);
    }
    if (lineBegin == lineEnd) return true;

    const SBCodepointSequence sequence{SBStringEncodingUTF32, paragraph.data(), paragraph.size()};
    Owned<SBAlgorithmRef, SBAlgorithmRelease> algorithm(SBAlgorithmCreate(&sequence), SBAlgorithmRelease);
    Owned<SBScriptLocatorRef, SBScriptLocatorRelease> locator(SBScriptLocatorCreate(), SBScriptLocatorRelease);
    if (!algorithm || !locator) return false;
    SBScriptLocatorLoadCodepoints(locator.get(), &sequence);
    std::vector<ScriptSpan> scripts;
    while (SBScriptLocatorMoveNext(locator.get())) {
        const SBScriptAgent& agent = *SBScriptLocatorGetAgent(locator.get());
        scripts.push_back({agent.offset, agent.offset + agent.length,
                           hb_script_from_iso15924_tag(SBScriptGetUnicodeTag(agent.script))});
    }

    std::vector<UITextFontShapedGlyph> shaped;
    std::vector<int> graphemeFonts(graphemeBoundaries.size(), -1);
    for (std::size_t paragraphBegin = 0U; paragraphBegin < lineEnd;) {
        Owned<SBParagraphRef, SBParagraphRelease> bidiParagraph(
            SBAlgorithmCreateParagraph(algorithm.get(), paragraphBegin, paragraph.size() - paragraphBegin,
                                       SBLevelDefaultLTR), SBParagraphRelease);
        if (!bidiParagraph) return false;
        const std::size_t paragraphEnd = paragraphBegin + SBParagraphGetLength(bidiParagraph.get());
        if (paragraphEnd <= paragraphBegin) return false;
        const std::size_t begin = std::max(lineBegin, paragraphBegin);
        const std::size_t end = std::min(lineEnd, paragraphEnd);
        if (begin < end) {
            if (fallbackFont && (!std::binary_search(graphemeBoundaries.begin(), graphemeBoundaries.end(), begin) ||
                                 !std::binary_search(graphemeBoundaries.begin(), graphemeBoundaries.end(), end))) return false;
            Owned<SBLineRef, SBLineRelease> line(SBParagraphCreateLine(bidiParagraph.get(), begin, end - begin),
                                                SBLineRelease);
            if (!line) return false;
            const SBRun* runs = SBLineGetRunsPtr(line.get());
            for (std::size_t runIndex = 0; runIndex < SBLineGetRunCount(line.get()); ++runIndex) {
                const SBRun& run = runs[runIndex];
                const bool rtl = (run.level & 1U) != 0U;
                std::vector<ScriptSpan> segments;
                for (const ScriptSpan& script : scripts) {
                    const std::size_t a = std::max<std::size_t>(run.offset, script.begin);
                    const std::size_t b = std::min<std::size_t>(run.offset + run.length, script.end);
                    if (a < b) {
                        std::size_t cursor = a;
                        for (auto boundary = std::upper_bound(styleBoundaries.begin(), styleBoundaries.end(), a);
                             boundary != styleBoundaries.end() && *boundary < b; ++boundary) {
                            if (cursor < *boundary) segments.push_back({cursor, *boundary, script.script});
                            cursor = *boundary;
                        }
                        segments.push_back({cursor, b, script.script});
                    }
                }
                if (fallbackFont) {
                    std::vector<ScriptSpan> fontSegments;
                    for (const auto& segment : segments) {
                        auto it = std::upper_bound(graphemeBoundaries.begin(), graphemeBoundaries.end(), segment.begin);
                        std::size_t index = static_cast<std::size_t>(it - graphemeBoundaries.begin() - 1);
                        std::size_t cursor = segment.begin;
                        while (cursor < segment.end) {
                            const std::size_t next = std::min<std::size_t>(segment.end, graphemeBoundaries[index + 1U]);
                            if (graphemeFonts[index] < 0) {
                                bool onlyControls = true;
                                for (std::size_t cp = graphemeBoundaries[index]; cp < graphemeBoundaries[index + 1U]; ++cp) {
                                    const auto category = hb_unicode_general_category(hb_unicode_funcs_get_default(), paragraph[cp]);
                                    if (category != HB_UNICODE_GENERAL_CATEGORY_CONTROL &&
                                        category != HB_UNICODE_GENERAL_CATEGORY_FORMAT &&
                                        category != HB_UNICODE_GENERAL_CATEGORY_LINE_SEPARATOR &&
                                        category != HB_UNICODE_GENERAL_CATEGORY_PARAGRAPH_SEPARATOR) onlyControls = false;
                                }
                                auto supported = [&](hb_font_t* candidate) {
                                    Owned<hb_buffer_t*, hb_buffer_destroy> probe(hb_buffer_create(), hb_buffer_destroy);
                                    hb_buffer_set_direction(probe.get(), rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
                                    hb_buffer_set_script(probe.get(), segment.script);
                                    hb_buffer_set_language(probe.get(), shapingLanguage);
                                    hb_buffer_set_cluster_level(probe.get(), HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
                                    hb_buffer_set_flags(probe.get(), HB_BUFFER_FLAG_REMOVE_DEFAULT_IGNORABLES);
                                    hb_buffer_add_codepoints(probe.get(), paragraph.data() + begin, static_cast<int>(end - begin),
                                        static_cast<unsigned>(graphemeBoundaries[index] - begin),
                                        static_cast<int>(graphemeBoundaries[index + 1U] - graphemeBoundaries[index]));
                                    if (!hb_shape_full(candidate, probe.get(), nullptr, 0, nullptr) ||
                                        !hb_buffer_allocation_successful(probe.get())) return -1;
                                    unsigned count = 0U;
                                    const auto* infos = hb_buffer_get_glyph_infos(probe.get(), &count);
                                    for (unsigned i = 0U; i < count; ++i) if (infos[i].codepoint == 0U) return 0;
                                    return 1;
                                };
                                const int primarySupported = onlyControls ? 1 : supported(font.get());
                                if (primarySupported < 0) return false;
                                int fallbackSupported = 0;
                                if (!primarySupported) fallbackSupported = supported(fallbackFont.get());
                                if (fallbackSupported < 0) return false;
                                graphemeFonts[index] = !primarySupported && fallbackSupported ? 1 : 0;
                            }
                            if (!fontSegments.empty() && fontSegments.back().end == cursor &&
                                fontSegments.back().script == segment.script && cursor != segment.begin &&
                                graphemeFonts[index - 1U] == graphemeFonts[index]) {
                                fontSegments.back().end = next;
                            } else {
                                fontSegments.push_back({cursor, next, segment.script});
                            }
                            cursor = next;
                            ++index;
                        }
                    }
                    segments = std::move(fontSegments);
                }
                if (rtl) std::reverse(segments.begin(), segments.end());
                for (const ScriptSpan& segment : segments) {
                    const auto boundary = fallbackFont ? std::upper_bound(graphemeBoundaries.begin(), graphemeBoundaries.end(), segment.begin) : graphemeBoundaries.end();
                    const bool useFallback = fallbackFont && graphemeFonts[static_cast<std::size_t>(boundary - graphemeBoundaries.begin() - 1)] == 1;
                    hb_font_t* segmentFont = useFallback ? fallbackFont.get() : font.get();
                    const float segmentScale = useFallback ? fallbackUnitScale : fontUnitScale;
                    Owned<hb_buffer_t*, hb_buffer_destroy> buffer(hb_buffer_create(), hb_buffer_destroy);
                    hb_buffer_set_direction(buffer.get(), rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
                    hb_buffer_set_script(buffer.get(), segment.script);
                    hb_buffer_set_language(buffer.get(), shapingLanguage);
                    hb_buffer_set_cluster_level(buffer.get(), HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
                    unsigned flags = 0U;
                    if (segment.begin == begin) flags |= HB_BUFFER_FLAG_BOT;
                    if (segment.end == end) flags |= HB_BUFFER_FLAG_EOT;
                    hb_buffer_set_flags(buffer.get(), static_cast<hb_buffer_flags_t>(flags));
                    hb_buffer_add_codepoints(buffer.get(), paragraph.data() + begin, static_cast<int>(end - begin),
                                             static_cast<unsigned>(segment.begin - begin),
                                             static_cast<int>(segment.end - segment.begin));
                    if (!hb_shape_full(segmentFont, buffer.get(), nullptr, 0, nullptr) ||
                        !hb_buffer_allocation_successful(buffer.get())) return false;
                    unsigned count = 0;
                    hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer.get(), &count);
                    for (unsigned i = 0; i < count; ++i) infos[i].cluster += static_cast<unsigned>(begin);
                    const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer.get(), nullptr);
                    std::vector<std::uint32_t> boundaries;
                    boundaries.reserve(static_cast<std::size_t>(count) + 1U);
                    for (unsigned i = 0; i < count; ++i) boundaries.push_back(infos[i].cluster);
                    boundaries.push_back(static_cast<std::uint32_t>(segment.end));
                    std::sort(boundaries.begin(), boundaries.end());
                    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
                    for (unsigned i = 0; i < count; ++i) {
                        const auto next = std::upper_bound(boundaries.begin(), boundaries.end(), infos[i].cluster);
                        if (next == boundaries.end()) return false;
                        shaped.push_back({{infos[i].codepoint, infos[i].cluster, *next,
                                          static_cast<float>(positions[i].x_advance) * segmentScale,
                                          static_cast<float>(positions[i].x_offset) * segmentScale,
                                          static_cast<float>(positions[i].y_offset) * segmentScale, rtl, {}}, useFallback});
                        auto& glyph = shaped.back().glyph;
                        unsigned caretCount = hb_ot_layout_get_ligature_carets(segmentFont,
                            rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR, infos[i].codepoint, 0U, nullptr, nullptr);
                        if (caretCount > 0U && caretCount < segment.end - segment.begin) {
                            std::vector<hb_position_t> carets(caretCount);
                            hb_ot_layout_get_ligature_carets(segmentFont, rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR,
                                infos[i].codepoint, 0U, &caretCount, carets.data());
                            for (unsigned caret = 0U; caret < caretCount; ++caret) {
                                const float position = static_cast<float>(carets[caret]) * segmentScale;
                                if (!std::isfinite(position)) return false;
                                glyph.ligatureCarets.push_back(position);
                            }
                            std::sort(glyph.ligatureCarets.begin(), glyph.ligatureCarets.end());
                        }
                        if (!std::isfinite(glyph.advance) || !std::isfinite(glyph.offsetX) ||
                            !std::isfinite(glyph.offsetY)) return false;
                    }
                }
            }
        }
        paragraphBegin = paragraphEnd;
    }
    output = std::move(shaped);
    return true;
}
bool ShapeUIText(std::span<const std::byte> fontBytes, std::span<const std::uint32_t> paragraph,
                 std::size_t lineBegin, std::size_t lineEnd, float fontUnitScale,
                 std::vector<UITextShapedGlyph>& output, std::span<const std::uint32_t> styleBoundaries,
                 std::span<const UITextFontVariation> variations) {
    output.clear();
    std::vector<UITextFontShapedGlyph> shaped;
    if (!ShapeUITextImpl(fontBytes, {}, paragraph, lineBegin, lineEnd, fontUnitScale, 0.0F, {},
                         shaped, styleBoundaries, variations, {})) return false;
    output.reserve(shaped.size());
    for (auto& item : shaped) output.push_back(std::move(item.glyph));
    return true;
}

bool ShapeUITextWithFallback(std::span<const std::byte> fontBytes, std::span<const std::byte> fallbackBytes,
    std::span<const std::uint32_t> paragraph, std::size_t lineBegin, std::size_t lineEnd,
    float fontUnitScale, float fallbackUnitScale, std::span<const std::uint32_t> graphemeBoundaries,
    std::vector<UITextFontShapedGlyph>& output, std::span<const std::uint32_t> styleBoundaries,
    std::span<const UITextFontVariation> variations, std::string_view language) {
    return ShapeUITextImpl(fontBytes, fallbackBytes, paragraph, lineBegin, lineEnd, fontUnitScale,
                           fallbackUnitScale, graphemeBoundaries, output, styleBoundaries, variations, language);
}

} // namespace kb::scene
