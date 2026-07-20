#include "rendering/ProjectFilesMaterialPreviewThumbnailModel.hpp"

#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "rendering/EditorMaterialThumbnailService.hpp"
#include "rendering/MaterialPreviewAppearanceResolver.hpp"
#include "rendering/ProjectFilesMaterialPreviewThumbnailPolicy.hpp"

#include <algorithm>
#include <cmath>

namespace kb::editor {
namespace {

[[nodiscard]] std::uint8_t ToColorByte(float value) noexcept {
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F)), 0, 255));
}

[[nodiscard]] ProjectFilesMaterialPreviewColor ToPreviewColor(float red, float green, float blue) noexcept {
    return ProjectFilesMaterialPreviewColor{ ToColorByte(red), ToColorByte(green), ToColorByte(blue) };
}

[[nodiscard]] float ColorChannel(ProjectFilesMaterialPreviewColor color, int channel) noexcept {
    switch (channel) {
    case 0:
        return static_cast<float>(color.r) / 255.0F;
    case 1:
        return static_cast<float>(color.g) / 255.0F;
    default:
        return static_cast<float>(color.b) / 255.0F;
    }
}

[[nodiscard]] ProjectFilesMaterialPreviewColor Blend(ProjectFilesMaterialPreviewColor a, ProjectFilesMaterialPreviewColor b, int alpha) noexcept {
    const int inv = 255 - std::clamp(alpha, 0, 255);
    return ProjectFilesMaterialPreviewColor{
        static_cast<std::uint8_t>((static_cast<int>(a.r) * inv + static_cast<int>(b.r) * alpha) / 255),
        static_cast<std::uint8_t>((static_cast<int>(a.g) * inv + static_cast<int>(b.g) * alpha) / 255),
        static_cast<std::uint8_t>((static_cast<int>(a.b) * inv + static_cast<int>(b.b) * alpha) / 255),
    };
}

[[nodiscard]] std::uint32_t PackBgra(float red, float green, float blue, float alpha = 1.0F) noexcept {
    return static_cast<std::uint32_t>(ToColorByte(blue))
        | (static_cast<std::uint32_t>(ToColorByte(green)) << 8U)
        | (static_cast<std::uint32_t>(ToColorByte(red)) << 16U)
        | (static_cast<std::uint32_t>(ToColorByte(alpha)) << 24U);
}

[[nodiscard]] std::uint32_t CompositeOver(std::uint32_t background, float red, float green, float blue, float alpha) noexcept {
    const float inv = 1.0F - alpha;
    const float bgB = static_cast<float>(background & 0xFFU) / 255.0F;
    const float bgG = static_cast<float>((background >> 8U) & 0xFFU) / 255.0F;
    const float bgR = static_cast<float>((background >> 16U) & 0xFFU) / 255.0F;
    return PackBgra(red * alpha + bgR * inv, green * alpha + bgG * inv, blue * alpha + bgB * inv);
}

[[nodiscard]] float SmoothCoverage(float signedDistance) noexcept {
    return std::clamp(signedDistance + 0.5F, 0.0F, 1.0F);
}

} // namespace

ProjectFilesMaterialPreviewStyle ProjectFilesMaterialPreviewThumbnailModel::StyleFromAsset(
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetManager* assets,
    MaterialPreviewTextureAverageColorFn textureAverageColor) {
    ProjectFilesMaterialPreviewStyle style{};
    const ProjectFilesMaterialPreviewThumbnailPolicy policy = ProjectFilesMaterialPreviewThumbnailPolicy::Resolve(metadata);
    style.usesPreviewScenePrimitive = policy.usesPreviewScenePrimitive;
    style.previewBoundsRadius = std::max(0.001F, policy.boundsRadius);
    style.previewTriangleCount = policy.triangleCount;
    if (metadata.type != "RenderMaterial" || metadata.physicalPath.empty()) {
        return style;
    }

    const std::optional<kb::render::RenderMaterialAssetData> material = kb::render::RenderMaterialAssetLoader::LoadMaterial(metadata.physicalPath);
    if (!material.has_value()) {
        style.errorFallback = true;
        style.baseColor = ProjectFilesMaterialPreviewColor{ 214U, 64U, 116U };
        style.emissiveColor = ProjectFilesMaterialPreviewColor{ 120U, 12U, 44U };
        style.emissiveStrength = 0.35F;
        style.roughness = 0.35F;
        return style;
    }

    // Same reason as the Inspector ball: for a graph-backed material desc stays at its white fallbacks,
    // so ask the graph what actually reaches Material Output.
    const MaterialPreviewAppearance appearance = MaterialPreviewAppearanceResolver::Resolve(*material, assets, textureAverageColor);
    style.baseColor = ToPreviewColor(appearance.baseColor[0], appearance.baseColor[1], appearance.baseColor[2]);
    style.emissiveColor = ToPreviewColor(appearance.emissiveColor[0], appearance.emissiveColor[1], appearance.emissiveColor[2]);
    style.roughness = appearance.roughness;
    style.emissiveStrength = appearance.emissiveStrength;
    style.loadedFromAsset = true;
    return style;
}

ProjectFilesMaterialPreviewImage ProjectFilesMaterialPreviewThumbnailModel::RenderImage(int width, int height, const ProjectFilesMaterialPreviewStyle& style, bool selected) {
    ProjectFilesMaterialPreviewImage image{ .width = width, .height = height };
    if (width <= 1 || height <= 1) {
        return image;
    }
    image.bgra.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));

    const ProjectFilesMaterialPreviewColor frameFill = selected
        ? ProjectFilesMaterialPreviewColor{ 31U, 34U, 39U }
        : ProjectFilesMaterialPreviewColor{ 24U, 27U, 31U };
    const ProjectFilesMaterialPreviewColor frameBorder = selected
        ? ProjectFilesMaterialPreviewColor{ 123U, 143U, 170U }
        : ProjectFilesMaterialPreviewColor{ 48U, 54U, 62U };
    const std::uint32_t fill = PackBgra(ColorChannel(frameFill, 0), ColorChannel(frameFill, 1), ColorChannel(frameFill, 2));
    const std::uint32_t border = PackBgra(ColorChannel(frameBorder, 0), ColorChannel(frameBorder, 1), ColorChannel(frameBorder, 2));
    std::fill(image.bgra.begin(), image.bgra.end(), fill);

    for (int x = 0; x < width; ++x) {
        image.bgra[static_cast<std::size_t>(x)] = border;
        image.bgra[static_cast<std::size_t>(height - 1) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = border;
    }
    for (int y = 0; y < height; ++y) {
        image.bgra[static_cast<std::size_t>(y) * static_cast<std::size_t>(width)] = border;
        image.bgra[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(width - 1)] = border;
    }

    const ProjectFilesMaterialPreviewColor baseColor = style.loadedFromAsset || style.errorFallback
        ? style.baseColor
        : Blend(style.baseColor, ProjectFilesMaterialPreviewColor{ 232U, 212U, 170U }, 34);
    const float baseR = ColorChannel(baseColor, 0);
    const float baseG = ColorChannel(baseColor, 1);
    const float baseB = ColorChannel(baseColor, 2);
    const float emissiveR = ColorChannel(style.emissiveColor, 0) * style.emissiveStrength;
    const float emissiveG = ColorChannel(style.emissiveColor, 1) * style.emissiveStrength;
    const float emissiveB = ColorChannel(style.emissiveColor, 2) * style.emissiveStrength;
    // Same fraction the rendered thumbnail normalises its silhouette to, so swapping one for the other
    // does not change the size of the ball in the tile.
    const float radius = static_cast<float>(std::max(8, std::min(width, height))) * kMaterialPreviewBallFraction * 0.5F;
    const float centerX = static_cast<float>(width) * 0.5F;
    const float centerY = static_cast<float>(height) * 0.5F;
    const float shadowCenterY = centerY + radius * 0.72F;
    const float shadowRx = radius * 0.78F;
    const float shadowRy = std::max(2.0F, radius * 0.18F);
    const float roughness = std::clamp(style.roughness, 0.0F, 1.0F);

    constexpr float lightX = -0.46F;
    constexpr float lightY = -0.62F;
    constexpr float lightZ = 0.63F;
    constexpr float halfX = -0.27F;
    constexpr float halfY = -0.36F;
    constexpr float halfZ = 0.89F;

    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            const float px = static_cast<float>(x) + 0.5F;
            const float py = static_cast<float>(y) + 0.5F;

            const float shadowDx = (px - (centerX + radius * 0.10F)) / shadowRx;
            const float shadowDy = (py - shadowCenterY) / shadowRy;
            const float shadowDistance = shadowDx * shadowDx + shadowDy * shadowDy;
            if (shadowDistance < 1.0F) {
                const float shadowAlpha = std::pow(1.0F - shadowDistance, 1.35F) * 0.24F;
                image.bgra[index] = CompositeOver(image.bgra[index], 0.02F, 0.025F, 0.032F, shadowAlpha);
            }

            const float nx = (px - centerX) / radius;
            const float ny = (py - centerY) / radius;
            const float distance2 = nx * nx + ny * ny;
            if (distance2 > 1.08F) {
                continue;
            }

            const float distance = std::sqrt(distance2);
            const float coverage = SmoothCoverage((1.0F - distance) * radius);
            if (coverage <= 0.0F) {
                continue;
            }

            const float nz = std::sqrt(std::max(0.0F, 1.0F - distance2));
            const float diffuse = std::max(0.0F, nx * lightX + ny * lightY + nz * lightZ);
            const float lowerShade = 1.0F - std::max(0.0F, ny) * 0.28F;
            const float rim = std::pow(std::clamp(1.0F - nz, 0.0F, 1.0F), 1.85F) * (selected ? 0.28F : 0.18F);
            const float specPower = 72.0F - roughness * 54.0F;
            const float specular = std::pow(std::max(0.0F, nx * halfX + ny * halfY + nz * halfZ), specPower) * (0.62F - roughness * 0.38F);
            const float sheen = std::pow(std::max(0.0F, (-nx * 0.35F) + (-ny * 0.72F) + (nz * 0.60F)), 18.0F) * 0.10F;
            const float shade = (0.34F + diffuse * 0.66F) * lowerShade;

            float red = baseR * shade + emissiveR + rim * 0.28F + specular + sheen;
            float green = baseG * shade + emissiveG + rim * 0.28F + specular + sheen;
            float blue = baseB * shade + emissiveB + rim * 0.30F + specular + sheen;

            const float edgeDarken = std::clamp((distance - 0.78F) / 0.22F, 0.0F, 1.0F) * 0.22F;
            red *= 1.0F - edgeDarken;
            green *= 1.0F - edgeDarken;
            blue *= 1.0F - edgeDarken;

            image.bgra[index] = CompositeOver(image.bgra[index], red, green, blue, coverage);
        }
    }
    return image;
}

} // namespace kb::editor
