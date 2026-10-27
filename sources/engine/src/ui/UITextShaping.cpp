#include "engine/ui/visual/UITextShaping.hpp"

#include <SheenBidi/SheenBidi.h>
#include <hb.h>
#include <hb-ot.h>

#include <algorithm>
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

bool ShapeUIText(std::span<const std::byte> fontBytes, std::span<const std::uint32_t> paragraph,
                 std::size_t lineBegin, std::size_t lineEnd, float fontUnitScale,
                 std::vector<UITextShapedGlyph>& output, std::span<const std::uint32_t> styleBoundaries) {
    output.clear();
    if (lineBegin > lineEnd || lineEnd > paragraph.size() ||
        paragraph.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        fontBytes.empty() || fontBytes.size() > std::numeric_limits<unsigned>::max() ||
        !std::isfinite(fontUnitScale) || fontUnitScale <= 0.0F) return false;
    if (lineBegin == lineEnd) return true;
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
    const int units = static_cast<int>(hb_face_get_upem(face.get()));
    hb_font_set_scale(font.get(), units, units);

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

    std::vector<UITextShapedGlyph> shaped;
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
                if (rtl) std::reverse(segments.begin(), segments.end());
                for (const ScriptSpan& segment : segments) {
                    Owned<hb_buffer_t*, hb_buffer_destroy> buffer(hb_buffer_create(), hb_buffer_destroy);
                    hb_buffer_set_direction(buffer.get(), rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
                    hb_buffer_set_script(buffer.get(), segment.script);
                    hb_buffer_set_language(buffer.get(), hb_language_from_string("und", -1));
                    hb_buffer_set_cluster_level(buffer.get(), HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
                    unsigned flags = 0U;
                    if (segment.begin == begin) flags |= HB_BUFFER_FLAG_BOT;
                    if (segment.end == end) flags |= HB_BUFFER_FLAG_EOT;
                    hb_buffer_set_flags(buffer.get(), static_cast<hb_buffer_flags_t>(flags));
                    hb_buffer_add_codepoints(buffer.get(), paragraph.data() + begin, static_cast<int>(end - begin),
                                             static_cast<unsigned>(segment.begin - begin),
                                             static_cast<int>(segment.end - segment.begin));
                    if (!hb_shape_full(font.get(), buffer.get(), nullptr, 0, nullptr) ||
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
                        shaped.push_back({infos[i].codepoint, infos[i].cluster, *next,
                                          static_cast<float>(positions[i].x_advance) * fontUnitScale,
                                          static_cast<float>(positions[i].x_offset) * fontUnitScale,
                                          static_cast<float>(positions[i].y_offset) * fontUnitScale, rtl});
                        const auto& glyph = shaped.back();
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
} // namespace kb::scene
