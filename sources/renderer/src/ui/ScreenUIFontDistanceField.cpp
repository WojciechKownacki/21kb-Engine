#include "private/ui/ScreenUIFontDistanceField.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <new>

namespace kb::render {
namespace {

// Exact squared Euclidean distance transform of a single row or column. The
// finite sentinel keeps the intersection arithmetic defined for empty rows.
void Transform1D(const float* source, int count, float* result, std::vector<int>& sites,
                 std::vector<float>& boundaries) {
    int k = 0;
    sites[0] = 0;
    boundaries[0] = -std::numeric_limits<float>::infinity();
    boundaries[1] = std::numeric_limits<float>::infinity();
    for (int q = 1; q < count; ++q) {
        float intersection = 0.0F;
        do {
            const int p = sites[k];
            intersection = ((source[q] + static_cast<float>(q * q)) -
                            (source[p] + static_cast<float>(p * p))) / (2.0F * static_cast<float>(q - p));
            if (intersection > boundaries[k] || k == 0) break;
            --k;
        } while (true);
        ++k;
        sites[k] = q;
        boundaries[k] = intersection;
        boundaries[k + 1] = std::numeric_limits<float>::infinity();
    }
    k = 0;
    for (int q = 0; q < count; ++q) {
        while (boundaries[k + 1] < static_cast<float>(q)) ++k;
        const float delta = static_cast<float>(q - sites[k]);
        result[q] = delta * delta + source[sites[k]];
    }
}

void Transform2D(std::vector<float>& pixels, int width, int height) {
    const int length = std::max(width, height);
    std::vector<float> source(static_cast<std::size_t>(length));
    std::vector<float> result(static_cast<std::size_t>(length));
    std::vector<int> sites(static_cast<std::size_t>(length));
    std::vector<float> boundaries(static_cast<std::size_t>(length) + 1U);
    for (int y = 0; y < height; ++y) {
        auto* row = pixels.data() + static_cast<std::size_t>(y) * width;
        Transform1D(row, width, result.data(), sites, boundaries);
        std::copy_n(result.data(), width, row);
    }
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) source[y] = pixels[static_cast<std::size_t>(y) * width + x];
        Transform1D(source.data(), height, result.data(), sites, boundaries);
        for (int y = 0; y < height; ++y) pixels[static_cast<std::size_t>(y) * width + x] = result[y];
    }
}

} // namespace

bool BuildFontDistanceField(std::span<const std::uint8_t> coverage, int width, int height,
                            std::vector<std::uint8_t>& output) {
    output.clear();
    if (width < 0 || height < 0 || width > 512 - 2 * kFontDistanceFieldPadding ||
        height > 512 - 2 * kFontDistanceFieldPadding ||
        coverage.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) return false;
    if (width == 0 || height == 0) return true;
    // A contour with no pixel reaching half coverage cannot be represented by
    // the binary interior; the caller can retain its original bitmap instead.
    const bool hasCoverage = std::ranges::any_of(coverage, [](auto value) { return value != 0U; });
    const bool hasInterior = std::ranges::any_of(coverage, [](auto value) { return value >= 128U; });
    if (hasCoverage && !hasInterior) return false;
    try {
    const int paddedWidth = width + 2 * kFontDistanceFieldPadding;
    const int paddedHeight = height + 2 * kFontDistanceFieldPadding;
    const std::size_t count = static_cast<std::size_t>(paddedWidth) * paddedHeight;
    constexpr float farAway = 1.0e10F;
    std::vector<float> inside(count, farAway);
    std::vector<float> outside(count, 0.0F);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto index = static_cast<std::size_t>(y + kFontDistanceFieldPadding) * paddedWidth + x + kFontDistanceFieldPadding;
            if (coverage[static_cast<std::size_t>(y) * width + x] >= 128U) {
                inside[index] = 0.0F;
                outside[index] = farAway;
            }
        }
    }
    Transform2D(inside, paddedWidth, paddedHeight);
    Transform2D(outside, paddedWidth, paddedHeight);
    output.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const float signedDistance = std::sqrt(outside[i]) - std::sqrt(inside[i]);
        const float edgeDistance = signedDistance > 0.0F ? signedDistance - 0.5F : signedDistance + 0.5F;
        output[i] = static_cast<std::uint8_t>(std::clamp(std::lround(128.0F + edgeDistance * kFontDistanceFieldScale), 0L, 255L));
    }
    // Preserve the original subpixel contour at antialiased boundary samples.
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const auto alpha = coverage[static_cast<std::size_t>(y) * width + x];
        if (alpha != 0U && alpha != 255U) {
            const float edgeDistance = static_cast<float>(alpha) / 255.0F - 0.5F;
            output[static_cast<std::size_t>(y + kFontDistanceFieldPadding) * paddedWidth + x + kFontDistanceFieldPadding] =
                static_cast<std::uint8_t>(std::clamp(std::lround(128.0F + edgeDistance * kFontDistanceFieldScale), 0L, 255L));
        }
    }
    return true;
    } catch (const std::bad_alloc&) {
        output.clear();
        return false;
    }
}

} // namespace kb::render
