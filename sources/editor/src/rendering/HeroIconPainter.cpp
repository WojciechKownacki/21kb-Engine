#include "rendering/HeroIconPainter.hpp"

#include "rendering/HeroIconCatalog.hpp"
#include "rendering/HeroIconDrawFrame.hpp"
#include "rendering/HeroIconGdiplusRuntime.hpp"
#include "rendering/HeroIconPathPainter.hpp"
#include "rendering/SvgGraphicsPathBuilder.hpp"

#if defined(_WIN32)
#pragma warning(push, 0)
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#pragma warning(pop)
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace kb::editor {

#if defined(_WIN32)
namespace {

struct CachedHeroIconPath {
    std::unique_ptr<Gdiplus::GraphicsPath> path{};
    bool filled = false;
};

struct CachedHeroIconGlyph {
    std::vector<CachedHeroIconPath> paths{};
    float viewBoxSize = 24.0F;
    float strokeWidth = 1.5F;
    bool initialized = false;
};

struct CachedRasterIconKey {
    HeroIconKind icon = HeroIconKind::Folder;
    COLORREF color = 0;
    int strokeWidth = 1;
    int width = 0;
    int height = 0;

    [[nodiscard]] bool operator==(const CachedRasterIconKey& other) const noexcept {
        return icon == other.icon
            && color == other.color
            && strokeWidth == other.strokeWidth
            && width == other.width
            && height == other.height;
    }
};

struct CachedRasterIconKeyHash {
    [[nodiscard]] std::size_t operator()(const CachedRasterIconKey& key) const noexcept {
        std::uint64_t value = static_cast<std::uint64_t>(static_cast<std::uint8_t>(key.icon));
        value ^= (static_cast<std::uint64_t>(key.color) + 0x9E3779B97F4A7C15ULL + (value << 6) + (value >> 2));
        value ^= (static_cast<std::uint64_t>(static_cast<std::uint32_t>(key.strokeWidth)) << 48);
        value ^= (static_cast<std::uint64_t>(static_cast<std::uint32_t>(key.width)) << 24);
        value ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(key.height));
        return static_cast<std::size_t>(value);
    }
};

struct CachedRasterIcon {
    HBITMAP bitmap = nullptr;
    HDC dc = nullptr;
    int width = 0;
    int height = 0;

    CachedRasterIcon() = default;

    CachedRasterIcon(HBITMAP bitmapHandle, HDC dcHandle, int bitmapWidth, int bitmapHeight) noexcept
        : bitmap(bitmapHandle)
        , dc(dcHandle)
        , width(bitmapWidth)
        , height(bitmapHeight) {}

    ~CachedRasterIcon() {
        if (dc != nullptr) {
            DeleteDC(dc);
        }
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
    }

    CachedRasterIcon(const CachedRasterIcon&) = delete;
    CachedRasterIcon& operator=(const CachedRasterIcon&) = delete;

    CachedRasterIcon(CachedRasterIcon&& other) noexcept
        : bitmap(other.bitmap)
        , dc(other.dc)
        , width(other.width)
        , height(other.height) {
        other.bitmap = nullptr;
        other.dc = nullptr;
        other.width = 0;
        other.height = 0;
    }

    CachedRasterIcon& operator=(CachedRasterIcon&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        if (dc != nullptr) {
            DeleteDC(dc);
        }
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        bitmap = other.bitmap;
        dc = other.dc;
        width = other.width;
        height = other.height;
        other.bitmap = nullptr;
        other.dc = nullptr;
        other.width = 0;
        other.height = 0;
        return *this;
    }
};

[[nodiscard]] CachedHeroIconGlyph& CachedGlyph(HeroIconKind icon) {
    static std::array<CachedHeroIconGlyph, static_cast<std::size_t>(HeroIconKind::Count)> cache{};
    CachedHeroIconGlyph& cached = cache[static_cast<std::size_t>(icon)];
    if (cached.initialized) {
        return cached;
    }

    const HeroIconGlyph glyph = HeroIconCatalog::Glyph(icon);
    cached.viewBoxSize = glyph.viewBoxSize;
    cached.strokeWidth = glyph.strokeWidth;
    cached.paths.reserve(glyph.paths.size());
    for (const HeroIconPath& pathData : glyph.paths) {
        auto path = std::make_unique<Gdiplus::GraphicsPath>(
            pathData.filled && pathData.evenOdd ? Gdiplus::FillModeAlternate : Gdiplus::FillModeWinding);
        SvgGraphicsPathBuilder(pathData.data).Build(*path);
        cached.paths.push_back(CachedHeroIconPath{ .path = std::move(path), .filled = pathData.filled });
    }
    cached.initialized = true;
    return cached;
}

[[nodiscard]] Gdiplus::Color ToGdiplusColor(COLORREF color) noexcept {
    return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color));
}

[[nodiscard]] float ResolveStrokeWidth(const CachedHeroIconGlyph& glyph, int strokeWidth) noexcept {
    return glyph.strokeWidth > 0.0F ? glyph.strokeWidth : static_cast<float>(std::max(1, strokeWidth));
}

void PaintVectorIcon(Gdiplus::Graphics& graphics, const RECT& rect, CachedHeroIconGlyph& glyph, COLORREF color, int strokeWidth) {
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    const HeroIconDrawFrame frame = HeroIconDrawFrame::FromRect(rect, glyph.viewBoxSize);
    Gdiplus::Matrix transform;
    transform.Translate(frame.left, frame.top);
    transform.Scale(frame.scale, frame.scale);
    graphics.SetTransform(&transform);

    const Gdiplus::Color iconColor = ToGdiplusColor(color);
    const float effectiveStrokeWidth = ResolveStrokeWidth(glyph, strokeWidth);

    for (CachedHeroIconPath& pathData : glyph.paths) {
        HeroIconPath paintInfo{ .filled = pathData.filled };
        HeroIconPathPainter::Paint(graphics, *pathData.path, paintInfo, iconColor, effectiveStrokeWidth);
    }

    graphics.ResetTransform();
}

[[nodiscard]] HBITMAP CreateAlphaDib(int width, int height, void** bits) noexcept {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, bits, nullptr, 0);
}

[[nodiscard]] CachedRasterIcon* RasterIcon(
    HDC target,
    HeroIconKind icon,
    COLORREF color,
    int strokeWidth,
    int width,
    int height) {
    static std::unordered_map<CachedRasterIconKey, CachedRasterIcon, CachedRasterIconKeyHash> rasters;
    const CachedRasterIconKey key{ icon, color, strokeWidth, width, height };
    if (const auto existing = rasters.find(key); existing != rasters.end()) {
        return &existing->second;
    }

    CachedHeroIconGlyph& glyph = CachedGlyph(icon);
    if (glyph.paths.empty()) {
        return nullptr;
    }

    void* bits = nullptr;
    HBITMAP bitmap = CreateAlphaDib(width, height, &bits);
    if (bitmap == nullptr || bits == nullptr) {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        return nullptr;
    }

    HDC memoryDc = CreateCompatibleDC(target);
    if (memoryDc == nullptr) {
        DeleteObject(bitmap);
        return nullptr;
    }

    SelectObject(memoryDc, bitmap);
    std::fill_n(static_cast<std::uint32_t*>(bits), static_cast<std::size_t>(width * height), 0U);

    Gdiplus::Graphics graphics(memoryDc);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    PaintVectorIcon(graphics, RECT{ 0, 0, width, height }, glyph, color, strokeWidth);
    GdiFlush();

    auto [inserted, _] = rasters.emplace(key, CachedRasterIcon{ bitmap, memoryDc, width, height });
    return &inserted->second;
}

} // namespace

void HeroIconPainter::Draw(HDC dc, const RECT& rect, HeroIconKind icon, COLORREF color, int strokeWidth) {
    if (dc == nullptr || rect.right <= rect.left || rect.bottom <= rect.top) {
        return;
    }

    HeroIconGdiplusRuntime::EnsureStarted();

    const int width = static_cast<int>(rect.right - rect.left);
    const int height = static_cast<int>(rect.bottom - rect.top);
    CachedRasterIcon* raster = RasterIcon(dc, icon, color, strokeWidth, width, height);
    if (raster == nullptr) {
        return;
    }

    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    AlphaBlend(dc, rect.left, rect.top, width, height, raster->dc, 0, 0, raster->width, raster->height, blend);
}

#endif

} // namespace kb::editor
