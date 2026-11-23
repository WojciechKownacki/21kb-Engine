#include "CliWorkshop.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// Procedural pictures. Every icon is a signed distance field (negative inside, in units where the picture spans
// -1..1), so one set of styles (flat, soft, neon, pixel) draws all of them the same way.
namespace kb::cli::workshop {
namespace {

constexpr float kPi = 3.14159265358979F;

struct Vec2 {
    float x = 0.0F;
    float y = 0.0F;
};

[[nodiscard]] Vec2 operator+(Vec2 a, Vec2 b) { return { a.x + b.x, a.y + b.y }; }
[[nodiscard]] Vec2 operator-(Vec2 a, Vec2 b) { return { a.x - b.x, a.y - b.y }; }
[[nodiscard]] Vec2 operator*(Vec2 a, float s) { return { a.x * s, a.y * s }; }
[[nodiscard]] float Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
[[nodiscard]] float Length(Vec2 a) { return std::sqrt(Dot(a, a)); }
[[nodiscard]] float Clamp01(float v) { return std::clamp(v, 0.0F, 1.0F); }
[[nodiscard]] float Mix(float a, float b, float t) { return a + (b - a) * t; }
[[nodiscard]] Rgb Mix(Rgb a, Rgb b, float t) { return { Mix(a.r, b.r, t), Mix(a.g, b.g, t), Mix(a.b, b.b, t) }; }
[[nodiscard]] Rgb Scale(Rgb a, float s) { return { Clamp01(a.r * s), Clamp01(a.g * s), Clamp01(a.b * s) }; }
[[nodiscard]] float Smooth(float edge0, float edge1, float x) {
    const float t = Clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0F - 2.0F * t);
}

constexpr Rgb kWhite{ 1.0F, 1.0F, 1.0F };

struct Px {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 0.0F;
};

// ---- hashing and tileable noise (a lattice that wraps, so a texture repeats without a seam) ----

[[nodiscard]] float Hash2(std::int32_t x, std::int32_t y, std::uint32_t seed) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393U + static_cast<std::uint32_t>(y) * 668265263U + seed * 2246822519U;
    h = (h ^ (h >> 13U)) * 1274126177U;
    h ^= h >> 16U;
    return static_cast<float>(h & 0xFFFFU) / 65535.0F;
}

[[nodiscard]] std::int32_t Wrap(std::int32_t v, std::int32_t period) { return ((v % period) + period) % period; }

[[nodiscard]] float ValueNoise(float x, float y, std::int32_t period, std::uint32_t seed) {
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const float tx = Smooth(0.0F, 1.0F, x - fx);
    const float ty = Smooth(0.0F, 1.0F, y - fy);
    const std::int32_t x0 = Wrap(static_cast<std::int32_t>(fx), period);
    const std::int32_t y0 = Wrap(static_cast<std::int32_t>(fy), period);
    const std::int32_t x1 = Wrap(x0 + 1, period);
    const std::int32_t y1 = Wrap(y0 + 1, period);
    return Mix(Mix(Hash2(x0, y0, seed), Hash2(x1, y0, seed), tx), Mix(Hash2(x0, y1, seed), Hash2(x1, y1, seed), tx), ty);
}

// Fractal noise in 0..1 whose lattice wraps every `period` cells at the first octave.
[[nodiscard]] float Fbm(float x, float y, std::int32_t period, int octaves, std::uint32_t seed) {
    float sum = 0.0F;
    float amplitude = 0.5F;
    float total = 0.0F;
    for (int octave = 0; octave < octaves; ++octave) {
        sum += amplitude * ValueNoise(x, y, period, seed + static_cast<std::uint32_t>(octave));
        total += amplitude;
        x *= 2.0F;
        y *= 2.0F;
        period *= 2;
        amplitude *= 0.5F;
    }
    return sum / total;
}

// ---- distance fields (units: the picture spans -1..1, y up; negative inside) ----

[[nodiscard]] float Dot2(Vec2 a) { return Dot(a, a); }

[[nodiscard]] float SdCircle(Vec2 p, float radius) { return Length(p) - radius; }

[[nodiscard]] float SdRoundBox(Vec2 p, Vec2 half, float radius) {
    const Vec2 q{ std::abs(p.x) - half.x + radius, std::abs(p.y) - half.y + radius };
    const Vec2 outside{ std::max(q.x, 0.0F), std::max(q.y, 0.0F) };
    return Length(outside) + std::min(std::max(q.x, q.y), 0.0F) - radius;
}

[[nodiscard]] float SdHeart(Vec2 p) {
    p.x = std::abs(p.x);
    if (p.y + p.x > 1.0F) return Length(p - Vec2{ 0.25F, 0.75F }) - std::sqrt(2.0F) / 4.0F;
    const float edge = 0.5F * std::max(p.x + p.y, 0.0F);
    const float nearest = std::min(Dot2(p - Vec2{ 0.0F, 1.0F }), Dot2(p - Vec2{ edge, edge }));
    return std::sqrt(nearest) * ((p.x - p.y) < 0.0F ? -1.0F : 1.0F);
}

[[nodiscard]] float SdStar5(Vec2 p, float radius, float inner) {
    const Vec2 k1{ 0.809016994375F, -0.587785252292F };
    const Vec2 k2{ -k1.x, k1.y };
    p.x = std::abs(p.x);
    p = p - k1 * (2.0F * std::max(Dot(k1, p), 0.0F));
    p = p - k2 * (2.0F * std::max(Dot(k2, p), 0.0F));
    p.x = std::abs(p.x);
    p.y -= radius;
    const Vec2 ba = Vec2{ -k1.y, k1.x } * inner - Vec2{ 0.0F, 1.0F };
    const float h = std::clamp(Dot(p, ba) / Dot(ba, ba), 0.0F, radius);
    return Length(p - ba * h) * ((p.y * ba.x - p.x * ba.y) < 0.0F ? -1.0F : 1.0F);
}

[[nodiscard]] float SdPolygon(const std::vector<Vec2>& v, Vec2 p) {
    float d = Dot2(p - v[0]);
    float sign = 1.0F;
    for (std::size_t i = 0U, j = v.size() - 1U; i < v.size(); j = i, ++i) {
        const Vec2 e = v[j] - v[i];
        const Vec2 w = p - v[i];
        const Vec2 b = w - e * std::clamp(Dot(w, e) / Dot(e, e), 0.0F, 1.0F);
        d = std::min(d, Dot2(b));
        const bool c1 = p.y >= v[i].y;
        const bool c2 = p.y < v[j].y;
        const bool c3 = e.x * w.y > e.y * w.x;
        if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)) sign = -sign;
    }
    return sign * std::sqrt(d);
}

struct ShapeSample {
    float distance = 1.0F; // in the -1..1 units
    float shade = 1.0F;    // brightness of the fill at this point (a coin's rim, a gem's facets)
};

struct ShapeDefault {
    const char* flat;
    const char* neon;
};

struct ShapeInfo {
    std::string_view name;
    ShapeDefault colors;
    ShapeSample (*sample)(Vec2);
};

[[nodiscard]] ShapeSample HeartShape(Vec2 p) {
    constexpr float kHeight = 1.55F;
    return { SdHeart(Vec2{ p.x / kHeight, (p.y + 0.8F) / kHeight }) * kHeight, 1.0F };
}

[[nodiscard]] ShapeSample StarShape(Vec2 p) { return { SdStar5(Vec2{ p.x, p.y + 0.08F }, 0.92F, 0.42F), 1.0F }; }

[[nodiscard]] ShapeSample CircleShape(Vec2 p) { return { SdCircle(p, 0.85F), 1.0F }; }

[[nodiscard]] ShapeSample SquareShape(Vec2 p) { return { SdRoundBox(p, Vec2{ 0.78F, 0.78F }, 0.14F), 1.0F }; }

[[nodiscard]] ShapeSample RingShape(Vec2 p) { return { std::abs(Length(p) - 0.62F) - 0.2F, 1.0F }; }

[[nodiscard]] ShapeSample CoinShape(Vec2 p) {
    const float radius = Length(p);
    float shade = 1.0F;
    if (radius > 0.7F) {
        shade = 0.74F;
    } else if (radius > 0.56F && radius < 0.62F) {
        shade = 0.84F;
    } else if (SdStar5(Vec2{ p.x, p.y + 0.03F }, 0.4F, 0.45F) < 0.0F) {
        shade = 0.82F;
    } else if (Dot(p, Vec2{ -0.6F, 0.8F }) > 0.25F && radius < 0.5F) {
        shade = 1.16F;
    }
    return { radius - 0.86F, shade };
}

[[nodiscard]] ShapeSample ArrowShape(Vec2 p) {
    static const std::vector<Vec2> kPoints{ { -0.85F, -0.26F }, { 0.08F, -0.26F }, { 0.08F, -0.7F }, { 0.88F, 0.0F },
        { 0.08F, 0.7F }, { 0.08F, 0.26F }, { -0.85F, 0.26F } };
    return { SdPolygon(kPoints, p), 1.0F };
}

[[nodiscard]] ShapeSample GemShape(Vec2 p) {
    static const std::vector<Vec2> kPoints{ { -0.82F, 0.2F }, { -0.46F, 0.78F }, { 0.46F, 0.78F }, { 0.82F, 0.2F }, { 0.0F, -0.88F } };
    float shade = 1.0F;
    if (p.y > 0.2F) {
        shade = std::abs(p.x) < 0.4F ? 1.28F : 1.06F;
    } else {
        shade = p.x < 0.0F ? 0.78F : 0.94F;
        if (std::abs(p.x) < 0.1F * (p.y + 0.88F)) shade = 1.12F;
    }
    return { SdPolygon(kPoints, p), shade };
}

constexpr ShapeInfo kShapes[]{
    { "heart", { "#E8334A", "#FF2D6F" }, &HeartShape },
    { "star", { "#FFC72C", "#FFE600" }, &StarShape },
    { "coin", { "#F5B921", "#FFD23F" }, &CoinShape },
    { "circle", { "#2E86DE", "#00E5FF" }, &CircleShape },
    { "ring", { "#2E86DE", "#00E5FF" }, &RingShape },
    { "square", { "#2E86DE", "#39FF14" }, &SquareShape },
    { "arrow", { "#2ECC40", "#39FF14" }, &ArrowShape },
    { "gem", { "#2DA8E0", "#00F0FF" }, &GemShape },
};

[[nodiscard]] const ShapeInfo* FindShape(std::string_view name) {
    for (const ShapeInfo& shape : kShapes) {
        if (shape.name == name) return &shape;
    }
    return nullptr;
}

// ---- styles ----

struct StyleInput {
    std::string_view style;
    float distancePx = 0.0F; // negative inside
    float shade = 1.0F;
    Rgb color;
    Rgb color2;
    bool hasColor2 = false;
    float y01 = 0.0F;        // 0 at the top, 1 at the bottom
    float reference = 256.0F; // the picture's size in pixels
    float fillAlpha = 1.0F;
};

[[nodiscard]] Px StyledShape(const StyleInput& in) {
    const Rgb base = in.hasColor2 ? Mix(in.color, in.color2, in.y01) : in.color;
    if (in.style == "neon") {
        const float line = std::max(1.5F, in.reference * 0.016F);
        const float glowReach = in.reference * 0.04F;
        const float away = std::abs(in.distancePx);
        const float core = 1.0F - Smooth(line - 0.8F, line + 0.8F, away);
        const float glow = std::exp(-away / glowReach) * (in.distancePx > 0.0F ? 0.85F : 0.55F);
        const float fill = in.distancePx < 0.0F ? 0.14F * in.fillAlpha : 0.0F;
        const Rgb coreColor = Mix(in.color, kWhite, 0.7F);
        const Rgb glowColor = in.hasColor2 ? in.color2 : in.color;
        // premultiplied sum, then back to straight alpha
        const float glowAlpha = Clamp01(std::max(glow, fill));
        const float alpha = 1.0F - (1.0F - core) * (1.0F - glowAlpha);
        if (alpha <= 0.0001F) return {};
        const float coreShare = core / alpha;
        const float restShare = 1.0F - coreShare;
        return { coreColor.r * coreShare + glowColor.r * restShare, coreColor.g * coreShare + glowColor.g * restShare,
            coreColor.b * coreShare + glowColor.b * restShare, alpha };
    }
    if (in.style == "soft") {
        // A glossy, rounded look with a clean edge: lighter on top, darker below, a darker rim and a sheen.
        const float alpha = Clamp01(0.5F - in.distancePx);
        const float bevel = Smooth(0.0F, 1.0F, Clamp01(-in.distancePx / (in.reference * 0.09F)));
        const Rgb lit = Mix(Mix(base, kWhite, 0.2F * (1.0F - in.y01)), Scale(base, 0.82F), in.y01 * 0.7F);
        const Rgb shaded = Scale(lit, Mix(0.7F, 1.0F, bevel) * in.shade);
        const float sheen = bevel * (1.0F - Smooth(0.15F, 0.5F, in.y01)) * 0.3F;
        const Rgb result = Mix(shaded, kWhite, sheen);
        return { result.r, result.g, result.b, alpha * in.fillAlpha };
    }
    const Rgb shaded = Scale(base, in.shade);
    return { shaded.r, shaded.g, shaded.b, Clamp01(0.5F - in.distancePx) * in.fillAlpha };
}

// The nearest-neighbour blocks of a pixel-art look: every cell takes the colour of its centre, edges are hard.
void Pixelate(Bitmap& bitmap, std::uint32_t cells) {
    const std::uint32_t cellW = std::max(1U, bitmap.width / cells);
    const std::uint32_t cellH = std::max(1U, bitmap.height / cells);
    const std::vector<std::uint8_t> source = bitmap.rgba;
    for (std::uint32_t y = 0U; y < bitmap.height; ++y) {
        for (std::uint32_t x = 0U; x < bitmap.width; ++x) {
            // the left and top halves sample just before their middle, so the picture stays symmetric
            const std::uint32_t cellX = x / cellW;
            const std::uint32_t cellY = y / cellH;
            const std::uint32_t sx = std::min(bitmap.width - 1U, cellX * cellW + (cellX * cellW * 2U < bitmap.width ? (cellW - 1U) / 2U : cellW / 2U));
            const std::uint32_t sy = std::min(bitmap.height - 1U, cellY * cellH + (cellY * cellH * 2U < bitmap.height ? (cellH - 1U) / 2U : cellH / 2U));
            const std::size_t from = (static_cast<std::size_t>(sy) * bitmap.width + sx) * 4U;
            const std::size_t to = (static_cast<std::size_t>(y) * bitmap.width + x) * 4U;
            for (std::size_t channel = 0U; channel < 3U; ++channel) bitmap.rgba[to + channel] = source[from + channel];
            bitmap.rgba[to + 3U] = source[from + 3U] >= 110U ? 255U : 0U;
        }
    }
}

// The centre of the grid cell a point falls in, for a grid of `cell` pixels whose lines pass through `centre`, so the
// cells are symmetric about the middle of the picture.
[[nodiscard]] float SnapToCell(float coordinate, float centre, float cell) {
    return centre + (std::floor((coordinate - centre) / cell) + 0.5F) * cell;
}

// ---- the picture buffer ----

[[nodiscard]] std::uint8_t Byte(float v) { return static_cast<std::uint8_t>(std::lround(Clamp01(v) * 255.0F)); }

[[nodiscard]] Bitmap MakeBitmap(std::uint32_t width, std::uint32_t height, const std::function<Px(std::uint32_t, std::uint32_t)>& pixel) {
    Bitmap bitmap;
    bitmap.width = width;
    bitmap.height = height;
    bitmap.rgba.resize(static_cast<std::size_t>(width) * height * 4U);
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            const Px p = pixel(x, y);
            const std::size_t at = (static_cast<std::size_t>(y) * width + x) * 4U;
            bitmap.rgba[at] = Byte(p.r);
            bitmap.rgba[at + 1U] = Byte(p.g);
            bitmap.rgba[at + 2U] = Byte(p.b);
            bitmap.rgba[at + 3U] = Byte(p.a);
        }
    }
    return bitmap;
}

[[nodiscard]] Rgb Hex(const char* text) {
    Rgb color;
    static_cast<void>(ParseHexColor(text, color));
    return color;
}

struct Palette {
    Rgb color;
    Rgb color2;
    bool hasColor2 = false;
};

[[nodiscard]] Palette ChoosePalette(const ImageRequest& request, const char* flatDefault, const char* neonDefault) {
    Palette palette;
    palette.color = request.color.value_or(Hex(request.style == "neon" ? neonDefault : flatDefault));
    if (request.color2.has_value()) {
        palette.color2 = *request.color2;
        palette.hasColor2 = true;
    }
    return palette;
}

// The style a pixel picture is drawn in before it is blocked up.
[[nodiscard]] std::string_view DrawStyle(const std::string& style) { return style == "pixel" ? std::string_view{ "flat" } : std::string_view{ style }; }

// ---- presets ----

[[nodiscard]] Bitmap RenderShapeIcon(const ShapeInfo& shape, const ImageRequest& request) {
    const Palette palette = ChoosePalette(request, shape.colors.flat, shape.colors.neon);
    const std::uint32_t size = static_cast<std::uint32_t>(request.size);
    const float half = static_cast<float>(size) * 0.5F;
    // Neon needs room around the shape for its glow.
    const float zoom = request.style == "neon" ? 1.22F : 1.0F;
    // Pixel art: every block of a grid takes the look of its centre, with a hard edge.
    const bool blocks = request.style == "pixel";
    const float cell = static_cast<float>(size) / static_cast<float>(size >= 128U ? 32U : 16U);
    Bitmap bitmap = MakeBitmap(size, size, [&](std::uint32_t x, std::uint32_t y) {
        float px = static_cast<float>(x) + 0.5F;
        float py = static_cast<float>(y) + 0.5F;
        if (blocks) {
            px = SnapToCell(px, half, cell);
            py = SnapToCell(py, half, cell);
        }
        const Vec2 p{ (px / half - 1.0F) * zoom, (1.0F - py / half) * zoom };
        const ShapeSample sample = shape.sample(p);
        StyleInput in;
        in.style = DrawStyle(request.style);
        in.distancePx = sample.distance / zoom * half;
        in.shade = sample.shade;
        in.color = palette.color;
        in.color2 = palette.color2;
        in.hasColor2 = palette.hasColor2;
        in.y01 = py / static_cast<float>(size);
        in.reference = static_cast<float>(size);
        Px result = StyledShape(in);
        if (blocks) result.a = result.a >= 0.5F ? 1.0F : 0.0F;
        return result;
    });
    return bitmap;
}

// A rounded rectangle of width x height (a button, a panel) drawn in the requested style.
[[nodiscard]] Bitmap RenderRoundedRect(const ImageRequest& request, std::uint32_t width, std::uint32_t height, const char* flatDefault,
    const char* neonDefault, float radiusShare, float fillAlpha, bool border) {
    const Palette palette = ChoosePalette(request, flatDefault, neonDefault);
    const float reference = static_cast<float>(height);
    const float margin = request.style == "neon" ? reference * 0.16F : 1.0F;
    const float halfW = static_cast<float>(width) * 0.5F;
    const float halfH = static_cast<float>(height) * 0.5F;
    const bool blocks = request.style == "pixel";
    const float cell = std::max(2.0F, std::round(reference / 14.0F));
    // Pixel art needs a corner of a few blocks to look rounded, and a border one block wide.
    const float radius = blocks ? std::max(reference * radiusShare, cell * 2.0F) : std::max(2.0F, reference * radiusShare);
    const float borderWidth = blocks ? cell : reference * 0.03F;
    Bitmap bitmap = MakeBitmap(width, height, [&](std::uint32_t x, std::uint32_t y) {
        float px = static_cast<float>(x) + 0.5F;
        float py = static_cast<float>(y) + 0.5F;
        if (blocks) {
            px = SnapToCell(px, halfW, cell);
            py = SnapToCell(py, halfH, cell);
        }
        const Vec2 p{ px - halfW, py - halfH };
        const float d = SdRoundBox(p, Vec2{ halfW - margin, halfH - margin }, std::min(radius, std::min(halfW, halfH) - margin));
        StyleInput in;
        in.style = DrawStyle(request.style);
        in.distancePx = d;
        in.shade = border && d > -borderWidth && d <= 0.0F ? 1.7F : 1.0F;
        in.color = palette.color;
        in.color2 = palette.color2;
        in.hasColor2 = palette.hasColor2;
        in.y01 = py / static_cast<float>(height);
        in.reference = reference;
        in.fillAlpha = fillAlpha;
        Px result = StyledShape(in);
        if (blocks) result.a = result.a >= 0.5F * fillAlpha ? fillAlpha : 0.0F;
        return result;
    });
    return bitmap;
}

[[nodiscard]] Bitmap RenderSky(const ImageRequest& request) {
    const bool neon = request.style == "neon";
    const Rgb top = request.color.value_or(Hex(neon ? "#07071F" : "#2F80D8"));
    const Rgb bottom = request.color2.value_or(Hex(neon ? "#FF3EA5" : "#CDEBFF"));
    const std::uint32_t size = static_cast<std::uint32_t>(request.size);
    const float sizeF = static_cast<float>(size);
    Bitmap bitmap = MakeBitmap(size, size, [&](std::uint32_t x, std::uint32_t y) {
        const float u = (static_cast<float>(x) + 0.5F) / sizeF;
        const float v = (static_cast<float>(y) + 0.5F) / sizeF; // 0 top
        Rgb color = Mix(top, bottom, std::pow(v, neon ? 1.6F : 1.0F));
        if (neon) {
            const Vec2 p{ (u - 0.5F) * 2.0F, (0.62F - v) * 2.0F };
            const float sun = SdCircle(p, 0.42F);
            const float stripes = 0.5F + 0.5F * std::sin(p.y * 38.0F);
            const bool cut = p.y < 0.05F && stripes > (0.5F + p.y * 1.1F);
            if (sun < 0.0F && !cut) color = Mix(Hex("#FFD166"), Hex("#FF3EA5"), Clamp01((0.42F - p.y) / 0.84F * 1.1F));
            const float star = Hash2(static_cast<std::int32_t>(x / 3U), static_cast<std::int32_t>(y / 3U), 7U);
            if (v < 0.45F && star > 0.992F) color = Mix(color, kWhite, 0.9F);
        }
        return Px{ color.r, color.g, color.b, 1.0F };
    });
    if (request.style == "pixel") Pixelate(bitmap, 32U);
    return bitmap;
}

[[nodiscard]] Bitmap RenderNoise(const ImageRequest& request) {
    const Rgb tint = request.color.value_or(Hex("#808080"));
    const std::uint32_t size = static_cast<std::uint32_t>(request.size);
    const float sizeF = static_cast<float>(size);
    Bitmap bitmap = MakeBitmap(size, size, [&](std::uint32_t x, std::uint32_t y) {
        const float n = Fbm(static_cast<float>(x) / sizeF * 8.0F, static_cast<float>(y) / sizeF * 8.0F, 8, 5, 11U);
        const Rgb color = Mix(Scale(tint, 0.35F), Scale(tint, 1.2F), Smooth(0.2F, 0.8F, n));
        return Px{ color.r, color.g, color.b, 1.0F };
    });
    if (request.style == "pixel") Pixelate(bitmap, 32U);
    return bitmap;
}

[[nodiscard]] Bitmap RenderGround(const ImageRequest& request) {
    const Rgb base = request.color.value_or(Hex(request.style == "neon" ? "#1A0B2E" : "#7A5C3E"));
    const Rgb accent = request.color2.value_or(Scale(base, 0.55F));
    const std::uint32_t size = static_cast<std::uint32_t>(request.size);
    const float sizeF = static_cast<float>(size);
    Bitmap bitmap = MakeBitmap(size, size, [&](std::uint32_t x, std::uint32_t y) {
        const float fx = static_cast<float>(x) / sizeF;
        const float fy = static_cast<float>(y) / sizeF;
        const float large = Fbm(fx * 6.0F, fy * 6.0F, 6, 4, 21U);
        const float fine = ValueNoise(fx * 64.0F, fy * 64.0F, 64, 22U);
        const float pebbles = Smooth(0.7F, 0.78F, ValueNoise(fx * 16.0F, fy * 16.0F, 16, 23U));
        Rgb color = Mix(accent, base, Smooth(0.25F, 0.75F, large));
        color = Scale(color, 0.92F + 0.16F * fine);
        color = Mix(color, Scale(accent, 0.8F), pebbles * 0.7F);
        if (request.style == "neon") {
            const float vein = 1.0F - Smooth(0.0F, 0.03F, std::abs(large - 0.5F));
            color = Mix(color, Hex("#FF3EA5"), vein * 0.8F);
        }
        return Px{ color.r, color.g, color.b, 1.0F };
    });
    if (request.style == "pixel") Pixelate(bitmap, 32U);
    return bitmap;
}

// Flat-coloured triangles over a jittered grid, tiling without a seam: the look of low-poly models, for a surface.
[[nodiscard]] Bitmap RenderLowPoly(const ImageRequest& request) {
    const Rgb base = request.color.value_or(Hex(request.style == "neon" ? "#2A1450" : "#3A4A5E"));
    const std::uint32_t size = static_cast<std::uint32_t>(request.size);
    const std::int32_t cells = size >= 128U ? 8 : 4;
    const float cellSize = static_cast<float>(size) / static_cast<float>(cells);
    const auto vertex = [&](std::int32_t i, std::int32_t j) {
        const std::int32_t wi = Wrap(i, cells);
        const std::int32_t wj = Wrap(j, cells);
        return Vec2{ (static_cast<float>(i) + 0.5F + (Hash2(wi, wj, 31U) - 0.5F) * 0.7F) * cellSize,
            (static_cast<float>(j) + 0.5F + (Hash2(wi, wj, 32U) - 0.5F) * 0.7F) * cellSize };
    };
    const auto inside = [](Vec2 p, Vec2 a, Vec2 b, Vec2 c) {
        const auto side = [](Vec2 p1, Vec2 p2, Vec2 p3) { return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y); };
        const float d1 = side(p, a, b);
        const float d2 = side(p, b, c);
        const float d3 = side(p, c, a);
        const bool negative = d1 < 0.0F || d2 < 0.0F || d3 < 0.0F;
        const bool positive = d1 > 0.0F || d2 > 0.0F || d3 > 0.0F;
        return !(negative && positive);
    };
    Bitmap bitmap = MakeBitmap(size, size, [&](std::uint32_t x, std::uint32_t y) {
        const Vec2 p{ static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F };
        const std::int32_t ci = static_cast<std::int32_t>(p.x / cellSize);
        const std::int32_t cj = static_cast<std::int32_t>(p.y / cellSize);
        float tone = 1.0F;
        for (std::int32_t j = cj - 1; j <= cj + 1; ++j) {
            for (std::int32_t i = ci - 1; i <= ci + 1; ++i) {
                const Vec2 p00 = vertex(i, j);
                const Vec2 p10 = vertex(i + 1, j);
                const Vec2 p01 = vertex(i, j + 1);
                const Vec2 p11 = vertex(i + 1, j + 1);
                const std::int32_t wi = Wrap(i, cells);
                const std::int32_t wj = Wrap(j, cells);
                if (inside(p, p00, p10, p11)) tone = 0.62F + 0.7F * Hash2(wi, wj, 41U);
                else if (inside(p, p00, p11, p01)) tone = 0.62F + 0.7F * Hash2(wi, wj, 42U);
            }
        }
        const Rgb color = Scale(base, tone);
        return Px{ color.r, color.g, color.b, 1.0F };
    });
    return bitmap;
}

[[nodiscard]] Bitmap RenderGlow(const ImageRequest& request) {
    const Rgb color = request.color.value_or(Hex("#FFB347"));
    const std::uint32_t size = static_cast<std::uint32_t>(request.size);
    const float half = static_cast<float>(size) * 0.5F;
    Bitmap bitmap = MakeBitmap(size, size, [&](std::uint32_t x, std::uint32_t y) {
        const Vec2 p{ (static_cast<float>(x) + 0.5F) / half - 1.0F, (static_cast<float>(y) + 0.5F) / half - 1.0F };
        const float r2 = Dot2(p);
        const float alpha = std::exp(-r2 * 5.5F) * (1.0F - Smooth(0.8F, 1.0F, std::sqrt(r2)));
        const Rgb tint = Mix(color, kWhite, std::exp(-r2 * 22.0F));
        return Px{ tint.r, tint.g, tint.b, alpha };
    });
    if (request.style == "pixel") Pixelate(bitmap, 16U);
    return bitmap;
}

// A row of kFlameFrames frames of a flame that loops: a teardrop whose edge is torn by noise that circles in time.
constexpr std::uint32_t kFlameFrames = 8U;

[[nodiscard]] Bitmap RenderFlame(const ImageRequest& request) {
    const Rgb outer = request.color.value_or(Hex("#E8431A"));
    const Rgb core = request.color2.value_or(Hex("#FFE27A"));
    const Rgb mid = Mix(outer, core, 0.45F);
    const std::uint32_t size = static_cast<std::uint32_t>(request.size);
    const float sizeF = static_cast<float>(size);
    Bitmap bitmap = MakeBitmap(size * kFlameFrames, size, [&](std::uint32_t px, std::uint32_t y) {
        const std::uint32_t frame = px / size;
        const std::uint32_t x = px % size;
        const float t = static_cast<float>(frame) / static_cast<float>(kFlameFrames);
        const float fx = (static_cast<float>(x) + 0.5F) / sizeF * 2.0F - 1.0F;
        const float u = 1.0F - (static_cast<float>(y) + 0.5F) / sizeF; // 0 at the base, 1 at the tip
        const float angle = 2.0F * kPi * t;
        const float sway = 0.1F * std::sin(angle + u * 3.0F) * u;
        const float noise = ValueNoise(fx * 2.6F + std::cos(angle) * 1.1F + 8.0F, u * 3.4F - 4.0F + std::sin(angle) * 1.1F + 8.0F, 64, 51U);
        const float widthAtHeight = 0.62F * std::pow(std::max(1.0F - u, 0.0F), 0.85F) * (0.4F + 0.6F * Smooth(0.0F, 0.22F, u));
        const float edge = std::abs(fx - sway) / std::max(widthAtHeight, 0.02F) + (noise - 0.5F) * 0.55F * (0.3F + u);
        const float heat = Clamp01(1.0F - edge) * (1.0F - 0.35F * u);
        const float alpha = Smooth(0.0F, 0.22F, heat) * (1.0F - Smooth(0.82F, 0.98F, u)) * Smooth(0.0F, 0.06F, u);
        Rgb color = heat < 0.5F ? Mix(outer, mid, heat * 2.0F) : Mix(mid, core, (heat - 0.5F) * 2.0F);
        if (heat > 0.8F) color = Mix(color, kWhite, (heat - 0.8F) * 3.0F);
        return Px{ color.r, color.g, color.b, alpha };
    });
    if (request.style == "pixel") {
        // each frame pixelated on its own, so the blocks do not run across frames
        Bitmap result = bitmap;
        for (std::uint32_t frame = 0U; frame < kFlameFrames; ++frame) {
            Bitmap cell;
            cell.width = size;
            cell.height = size;
            cell.rgba.resize(static_cast<std::size_t>(size) * size * 4U);
            for (std::uint32_t y = 0U; y < size; ++y) {
                std::copy_n(bitmap.rgba.begin() + static_cast<std::ptrdiff_t>((static_cast<std::size_t>(y) * bitmap.width + frame * size) * 4U),
                    static_cast<std::ptrdiff_t>(size) * 4, cell.rgba.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * size * 4U));
            }
            Pixelate(cell, 16U);
            for (std::uint32_t y = 0U; y < size; ++y) {
                std::copy_n(cell.rgba.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * size * 4U), static_cast<std::ptrdiff_t>(size) * 4,
                    result.rgba.begin() + static_cast<std::ptrdiff_t>((static_cast<std::size_t>(y) * bitmap.width + frame * size) * 4U));
            }
        }
        return result;
    }
    return bitmap;
}

constexpr std::string_view kOtherPresets[]{ "button", "panel", "sky", "noise", "ground", "lowpoly", "glow", "flame" };
constexpr std::string_view kStyles[]{ "flat", "soft", "neon", "pixel" };

} // namespace

bool ParseHexColor(std::string_view text, Rgb& out) {
    if (!text.empty() && text.front() == '#') text.remove_prefix(1U);
    if (text.size() != 6U) return false;
    float channels[3]{};
    for (std::size_t i = 0U; i < 3U; ++i) {
        int value = 0;
        for (std::size_t digit = 0U; digit < 2U; ++digit) {
            const char c = text[i * 2U + digit];
            int nibble = 0;
            if (c >= '0' && c <= '9') nibble = c - '0';
            else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
            else return false;
            value = value * 16 + nibble;
        }
        channels[i] = static_cast<float>(value) / 255.0F;
    }
    out = { channels[0], channels[1], channels[2] };
    return true;
}

std::string ImagePresetList() {
    std::string list;
    for (const ShapeInfo& shape : kShapes) list += (list.empty() ? "" : ", ") + std::string{ shape.name };
    for (const std::string_view name : kOtherPresets) list += ", " + std::string{ name };
    return list;
}

std::string ImageStyleList() {
    std::string list;
    for (const std::string_view name : kStyles) list += (list.empty() ? "" : ", ") + std::string{ name };
    return list;
}

std::optional<Bitmap> RenderImage(const ImageRequest& request, std::string& error) {
    if (request.size < kMinImageSize || request.size > kMaxImageSize) {
        error = "--size takes " + std::to_string(kMinImageSize) + " to " + std::to_string(kMaxImageSize) + " pixels";
        return std::nullopt;
    }
    if (std::ranges::find(kStyles, std::string_view{ request.style }) == std::end(kStyles)) {
        error = "unknown style '" + request.style + "'; styles: " + ImageStyleList();
        return std::nullopt;
    }
    const std::string_view preset = request.preset;
    if (preset == "flame" && request.size > kMaxFlameFrameSize) {
        error = "--size of a flame is the size of one of its 8 frames: " + std::to_string(kMinImageSize) + " to " + std::to_string(kMaxFlameFrameSize) + " pixels";
        return std::nullopt;
    }
    if (const ShapeInfo* shape = FindShape(preset)) return RenderShapeIcon(*shape, request);
    const std::uint32_t size = static_cast<std::uint32_t>(request.size);
    if (preset == "button") return RenderRoundedRect(request, size, std::max(16U, size / 3U), "#2E86DE", "#00E5FF", 0.5F, 1.0F, false);
    if (preset == "panel") return RenderRoundedRect(request, size, std::max(16U, size * 3U / 4U), "#1E2233", "#7DF9FF", 0.1F, 0.92F, true);
    if (preset == "sky") return RenderSky(request);
    if (preset == "noise") return RenderNoise(request);
    if (preset == "ground") return RenderGround(request);
    if (preset == "lowpoly") return RenderLowPoly(request);
    if (preset == "glow") return RenderGlow(request);
    if (preset == "flame") return RenderFlame(request);
    error = "unknown preset '" + request.preset + "'; presets: " + ImagePresetList();
    return std::nullopt;
}

} // namespace kb::cli::workshop
