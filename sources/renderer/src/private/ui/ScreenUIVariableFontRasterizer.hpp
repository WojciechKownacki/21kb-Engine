#pragma once

#include "engine/ui/visual/UITextShaping.hpp"

#include <hb.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace kb::render {

// Uses the same OpenType variation coordinates as the shaper. The bitmap is in
// font coordinates and can be packed alongside ordinary stb glyphs.
class ScreenUIVariableFontRasterizer {
public:
    // The font bytes must remain alive and unchanged for this object's lifetime.
    ScreenUIVariableFontRasterizer(std::span<const std::byte> bytes,
                                   std::span<const kb::scene::UITextFontVariation> variations);
    ~ScreenUIVariableFontRasterizer();
    ScreenUIVariableFontRasterizer(const ScreenUIVariableFontRasterizer&) = delete;
    ScreenUIVariableFontRasterizer& operator=(const ScreenUIVariableFontRasterizer&) = delete;
    [[nodiscard]] bool IsValid() const noexcept { return valid_; }
    [[nodiscard]] bool Metrics(float scale, float& ascent, float& descent, float& lineGap) const noexcept;

    struct Bitmap {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        std::vector<std::uint8_t> alpha;
    };
    [[nodiscard]] bool Rasterize(std::uint32_t glyphId, float scale, Bitmap& result) const;
private:
    struct Shape;
    static void Move(hb_draw_funcs_t*, void*, hb_draw_state_t*, float, float, void*);
    static void Line(hb_draw_funcs_t*, void*, hb_draw_state_t*, float, float, void*);
    static void Quadratic(hb_draw_funcs_t*, void*, hb_draw_state_t*, float, float, float, float, void*);
    static void Cubic(hb_draw_funcs_t*, void*, hb_draw_state_t*, float, float, float, float, float, float, void*);
    std::unique_ptr<hb_blob_t, decltype(&hb_blob_destroy)> blob_{nullptr, &hb_blob_destroy};
    std::unique_ptr<hb_face_t, decltype(&hb_face_destroy)> face_{nullptr, &hb_face_destroy};
    std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)> font_{nullptr, &hb_font_destroy};
    std::unique_ptr<hb_draw_funcs_t, decltype(&hb_draw_funcs_destroy)> draw_{nullptr, &hb_draw_funcs_destroy};
    bool valid_ = false;
};
} // namespace kb::render
