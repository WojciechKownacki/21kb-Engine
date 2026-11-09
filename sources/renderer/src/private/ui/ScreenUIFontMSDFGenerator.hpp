#pragma once
#include "engine/ui/visual/UITextShaping.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
namespace kb::render {
class ScreenUIFontMSDFGenerator final {
public:
    struct Bitmap {int x0=0,y0=0,x1=0,y1=0;std::vector<std::uint8_t> rgba;};
    ScreenUIFontMSDFGenerator(std::span<const std::byte>,std::span<const kb::scene::UITextFontVariation>);
    ~ScreenUIFontMSDFGenerator();
    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] bool Rasterize(std::uint32_t glyph,float scale,Bitmap&) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
