#include "../private/ui/ScreenUIVariableFontRasterizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>
#include <utility>

#include <hb-ot.h>

namespace {
struct RasterAllocationFailure {};
struct alignas(std::max_align_t) Allocation {
    Allocation* previous;
    Allocation* next;
};
struct AllocationContext {
    Allocation* head = nullptr;
    ~AllocationContext() {
        while (head != nullptr) {
            Allocation* next = head->next;
            std::free(head);
            head = next;
        }
    }
    AllocationContext() = default;
    AllocationContext(const AllocationContext&) = delete;
    AllocationContext& operator=(const AllocationContext&) = delete;
};
void* CheckedAlloc(std::size_t size, void* userdata) {
    auto& context = *static_cast<AllocationContext*>(userdata);
    if (size > std::numeric_limits<std::size_t>::max() - sizeof(Allocation)) throw RasterAllocationFailure{};
    auto* block = static_cast<Allocation*>(std::malloc(sizeof(Allocation) + size));
    if (block == nullptr) throw RasterAllocationFailure{};
    block->previous = nullptr;
    block->next = context.head;
    if (context.head != nullptr) context.head->previous = block;
    context.head = block;
    return block + 1;
}
void CheckedFree(void* pointer, void* userdata) noexcept {
    if (pointer == nullptr) return;
    auto& context = *static_cast<AllocationContext*>(userdata);
    auto* block = static_cast<Allocation*>(pointer) - 1;
    if (block->previous != nullptr) block->previous->next = block->next;
    else context.head = block->next;
    if (block->next != nullptr) block->next->previous = block->previous;
    std::free(block);
}
} // namespace

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_malloc(size, userdata) CheckedAlloc((size), (userdata))
#define STBTT_free(pointer, userdata) CheckedFree((pointer), (userdata))
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include <stb/stb_truetype.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace kb::render {

struct ScreenUIVariableFontRasterizer::Shape {
    std::vector<stbtt_vertex> vertices;
    bool failed = false;
    void Append(unsigned char type, float x, float y, float cx = 0, float cy = 0, float cx1 = 0, float cy1 = 0) noexcept {
        const auto fits = [](float value) { return std::isfinite(value) && value >= -32768.0F && value <= 32767.0F; };
        if (failed || vertices.size() >= 65536U || !fits(x) || !fits(y) || !fits(cx) || !fits(cy) || !fits(cx1) || !fits(cy1)) {
            failed = true;
            return;
        }
        stbtt_vertex vertex{};
        vertex.type = type;
        vertex.x = static_cast<short>(std::lround(x)); vertex.y = static_cast<short>(std::lround(y));
        vertex.cx = static_cast<short>(std::lround(cx)); vertex.cy = static_cast<short>(std::lround(cy));
        vertex.cx1 = static_cast<short>(std::lround(cx1)); vertex.cy1 = static_cast<short>(std::lround(cy1));
        try { vertices.push_back(vertex); } catch (...) { failed = true; }
    }
};

ScreenUIVariableFontRasterizer::ScreenUIVariableFontRasterizer(
    std::span<const std::byte> bytes, std::span<const kb::scene::UITextFontVariation> variations) {
    if (bytes.empty() || bytes.size() > std::numeric_limits<unsigned>::max()) return;
    blob_.reset(hb_blob_create(reinterpret_cast<const char*>(bytes.data()), static_cast<unsigned>(bytes.size()),
                               HB_MEMORY_MODE_READONLY, nullptr, nullptr));
    face_.reset(hb_face_create(blob_.get(), 0U));
    if (hb_face_get_glyph_count(face_.get()) == 0U || variations.size() > hb_ot_var_get_axis_count(face_.get())) return;
    std::vector<hb_variation_t> axes;
    axes.reserve(variations.size());
    for (const auto& axis : variations) {
        hb_ot_var_axis_info_t info{};
        if (!std::isfinite(axis.value) || !hb_ot_var_find_axis_info(face_.get(), axis.tag, &info) ||
            axis.value < info.min_value || axis.value > info.max_value ||
            std::ranges::any_of(axes, [&](const auto& prior) { return prior.tag == axis.tag; })) return;
        axes.push_back({axis.tag, axis.value});
    }
    font_.reset(hb_font_create(face_.get()));
    if (font_.get() == hb_font_get_empty()) return;
    hb_ot_font_set_funcs(font_.get());
    const unsigned units = hb_face_get_upem(face_.get());
    if (units == 0U || units > static_cast<unsigned>(std::numeric_limits<int>::max())) return;
    hb_font_set_scale(font_.get(), static_cast<int>(units), static_cast<int>(units));
    if (!axes.empty()) hb_font_set_variations(font_.get(), axes.data(), static_cast<unsigned>(axes.size()));
    draw_.reset(hb_draw_funcs_create());
    if (draw_.get() == hb_draw_funcs_get_empty()) return;
    hb_draw_funcs_set_move_to_func(draw_.get(), Move, nullptr, nullptr);
    hb_draw_funcs_set_line_to_func(draw_.get(), Line, nullptr, nullptr);
    hb_draw_funcs_set_quadratic_to_func(draw_.get(), Quadratic, nullptr, nullptr);
    hb_draw_funcs_set_cubic_to_func(draw_.get(), Cubic, nullptr, nullptr);
    valid_ = true;
}
ScreenUIVariableFontRasterizer::~ScreenUIVariableFontRasterizer() = default;

bool ScreenUIVariableFontRasterizer::Metrics(float scale, float& ascent, float& descent, float& lineGap) const noexcept {
    if (!valid_ || !std::isfinite(scale) || scale <= 0.0F) return false;
    hb_position_t a = 0, d = 0, gap = 0;
    hb_ot_metrics_get_position_with_fallback(font_.get(), HB_OT_METRICS_TAG_HORIZONTAL_ASCENDER, &a);
    hb_ot_metrics_get_position_with_fallback(font_.get(), HB_OT_METRICS_TAG_HORIZONTAL_DESCENDER, &d);
    hb_ot_metrics_get_position_with_fallback(font_.get(), HB_OT_METRICS_TAG_HORIZONTAL_LINE_GAP, &gap);
    ascent = static_cast<float>(a) * scale;
    descent = static_cast<float>(d) * scale;
    lineGap = static_cast<float>(gap) * scale;
    return std::isfinite(ascent) && std::isfinite(descent) && std::isfinite(lineGap) && ascent > descent;
}

bool ScreenUIVariableFontRasterizer::Rasterize(std::uint32_t glyphId, float scale, Bitmap& result) const {
    result = {};
    if (!valid_ || !std::isfinite(scale) || scale <= 0.0F || glyphId >= hb_face_get_glyph_count(face_.get())) return false;
    hb_glyph_extents_t extents{};
    if (!hb_font_get_glyph_extents(font_.get(), glyphId, &extents)) return true;
    const double left = std::floor(static_cast<double>(extents.x_bearing) * scale);
    const double top = std::floor(-static_cast<double>(extents.y_bearing) * scale);
    const double right = std::ceil((static_cast<double>(extents.x_bearing) + extents.width) * scale);
    const double bottom = std::ceil((-static_cast<double>(extents.y_bearing) - extents.height) * scale);
    constexpr double limit = 4096.0;
    if (left < -limit || top < -limit || right > limit || bottom > limit || right < left || bottom < top ||
        right - left > limit || bottom - top > limit) return false;
    const int x0 = static_cast<int>(left), y0 = static_cast<int>(top);
    const int x1 = static_cast<int>(right), y1 = static_cast<int>(bottom);
    const int width = x1 - x0, height = y1 - y0;
    if (width == 0 || height == 0) return true;
    Shape shape;
    if (!hb_font_draw_glyph_or_fail(font_.get(), glyphId, draw_.get(), &shape) || shape.failed) return false;
    if (shape.vertices.empty()) return true;
    Bitmap bitmapResult{x0, y0, x1, y1, {}};
    AllocationContext allocations;
    try {
        bitmapResult.alpha.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
        stbtt__bitmap bitmap{width, height, width, bitmapResult.alpha.data()};
        stbtt_Rasterize(&bitmap, 0.35F, shape.vertices.data(), static_cast<int>(shape.vertices.size()),
                        scale, scale, 0.0F, 0.0F, x0, y0, 1, &allocations);
        result = std::move(bitmapResult);
        return true;
    } catch (const RasterAllocationFailure&) {
        return false;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

void ScreenUIVariableFontRasterizer::Move(hb_draw_funcs_t*, void* data, hb_draw_state_t*, float x, float y, void*) { static_cast<Shape*>(data)->Append(STBTT_vmove, x, y); }
void ScreenUIVariableFontRasterizer::Line(hb_draw_funcs_t*, void* data, hb_draw_state_t*, float x, float y, void*) { static_cast<Shape*>(data)->Append(STBTT_vline, x, y); }
void ScreenUIVariableFontRasterizer::Quadratic(hb_draw_funcs_t*, void* data, hb_draw_state_t*, float cx, float cy, float x, float y, void*) { static_cast<Shape*>(data)->Append(STBTT_vcurve, x, y, cx, cy); }
void ScreenUIVariableFontRasterizer::Cubic(hb_draw_funcs_t*, void* data, hb_draw_state_t*, float cx, float cy, float cx1, float cy1, float x, float y, void*) { static_cast<Shape*>(data)->Append(STBTT_vcubic, x, y, cx, cy, cx1, cy1); }
} // namespace kb::render
