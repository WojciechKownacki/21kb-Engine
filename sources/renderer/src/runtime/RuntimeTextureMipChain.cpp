#include "runtime/RuntimeTextureMipChain.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>

namespace kb::render {
namespace {

constexpr std::size_t kRgba8BytesPerTexel = 4U;

[[nodiscard]] std::size_t LevelByteSize(std::uint16_t width, std::uint16_t height) noexcept {
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * kRgba8BytesPerTexel;
}

[[nodiscard]] float SrgbToLinear(std::uint8_t value) noexcept {
    const float srgb = static_cast<float>(value) / 255.0F;
    return srgb <= 0.04045F
        ? srgb / 12.92F
        : std::pow((srgb + 0.055F) / 1.055F, 2.4F);
}

[[nodiscard]] const std::array<float, 256U>& SrgbDecodeTable() noexcept {
    static const std::array<float, 256U> table = [] {
        std::array<float, 256U> values{};
        for (std::size_t index = 0U; index < values.size(); ++index) {
            values[index] = SrgbToLinear(static_cast<std::uint8_t>(index));
        }
        return values;
    }();
    return table;
}

[[nodiscard]] std::uint8_t LinearToSrgb(float value, const std::array<float, 256U>& decodeTable) noexcept {
    const auto upper = std::lower_bound(decodeTable.begin(), decodeTable.end(), std::clamp(value, 0.0F, 1.0F));
    if (upper == decodeTable.begin()) {
        return 0U;
    }
    if (upper == decodeTable.end()) {
        return 255U;
    }
    const auto lower = upper - 1;
    const auto nearest = value - *lower <= *upper - value ? lower : upper;
    return static_cast<std::uint8_t>(std::distance(decodeTable.begin(), nearest));
}

void DownsampleLevel(
    std::span<const std::uint8_t> source,
    std::uint16_t sourceWidth,
    std::uint16_t sourceHeight,
    std::span<std::uint8_t> destination,
    std::uint16_t destinationWidth,
    std::uint16_t destinationHeight,
    RenderTextureColorSpace colorSpace) {
    const bool srgb = colorSpace == RenderTextureColorSpace::Srgb;
    const std::array<float, 256U>* srgbDecode = srgb ? &SrgbDecodeTable() : nullptr;

    for (std::uint16_t destinationY = 0U; destinationY < destinationHeight; ++destinationY) {
        const std::uint16_t sourceYBegin = static_cast<std::uint16_t>(
            static_cast<std::uint32_t>(destinationY) * sourceHeight / destinationHeight);
        const std::uint16_t sourceYEnd = static_cast<std::uint16_t>(
            static_cast<std::uint32_t>(destinationY + 1U) * sourceHeight / destinationHeight);

        for (std::uint16_t destinationX = 0U; destinationX < destinationWidth; ++destinationX) {
            const std::uint16_t sourceXBegin = static_cast<std::uint16_t>(
                static_cast<std::uint32_t>(destinationX) * sourceWidth / destinationWidth);
            const std::uint16_t sourceXEnd = static_cast<std::uint16_t>(
                static_cast<std::uint32_t>(destinationX + 1U) * sourceWidth / destinationWidth);

            std::array<std::uint32_t, 4U> linearSums{};
            std::array<float, 3U> srgbLinearSums{};
            std::uint32_t sampleCount = 0U;
            for (std::uint16_t sourceY = sourceYBegin; sourceY < sourceYEnd; ++sourceY) {
                for (std::uint16_t sourceX = sourceXBegin; sourceX < sourceXEnd; ++sourceX) {
                    const std::size_t sourceOffset =
                        (static_cast<std::size_t>(sourceY) * sourceWidth + sourceX) * kRgba8BytesPerTexel;
                    if (srgb) {
                        srgbLinearSums[0U] += (*srgbDecode)[source[sourceOffset + 0U]];
                        srgbLinearSums[1U] += (*srgbDecode)[source[sourceOffset + 1U]];
                        srgbLinearSums[2U] += (*srgbDecode)[source[sourceOffset + 2U]];
                    } else {
                        linearSums[0U] += source[sourceOffset + 0U];
                        linearSums[1U] += source[sourceOffset + 1U];
                        linearSums[2U] += source[sourceOffset + 2U];
                    }
                    linearSums[3U] += source[sourceOffset + 3U];
                    ++sampleCount;
                }
            }

            const std::size_t destinationOffset =
                (static_cast<std::size_t>(destinationY) * destinationWidth + destinationX) * kRgba8BytesPerTexel;
            if (srgb) {
                const float inverseSampleCount = 1.0F / static_cast<float>(sampleCount);
                destination[destinationOffset + 0U] = LinearToSrgb(srgbLinearSums[0U] * inverseSampleCount, *srgbDecode);
                destination[destinationOffset + 1U] = LinearToSrgb(srgbLinearSums[1U] * inverseSampleCount, *srgbDecode);
                destination[destinationOffset + 2U] = LinearToSrgb(srgbLinearSums[2U] * inverseSampleCount, *srgbDecode);
            } else {
                destination[destinationOffset + 0U] = static_cast<std::uint8_t>((linearSums[0U] + sampleCount / 2U) / sampleCount);
                destination[destinationOffset + 1U] = static_cast<std::uint8_t>((linearSums[1U] + sampleCount / 2U) / sampleCount);
                destination[destinationOffset + 2U] = static_cast<std::uint8_t>((linearSums[2U] + sampleCount / 2U) / sampleCount);
            }
            destination[destinationOffset + 3U] = static_cast<std::uint8_t>((linearSums[3U] + sampleCount / 2U) / sampleCount);
        }
    }
}

} // namespace

std::optional<RuntimeTextureMipChain> BuildRuntimeTexture2DMipChain(
    std::span<const std::uint8_t> baseLevelRgba8,
    std::uint16_t width,
    std::uint16_t height,
    RenderTextureColorSpace colorSpace) {
    if (width == 0U || height == 0U || baseLevelRgba8.size() != LevelByteSize(width, height)) {
        return std::nullopt;
    }

    std::size_t totalByteSize = 0U;
    std::uint16_t levelWidth = width;
    std::uint16_t levelHeight = height;
    std::uint8_t mipCount = 0U;
    while (true) {
        const std::size_t levelByteSize = LevelByteSize(levelWidth, levelHeight);
        if (levelByteSize > std::numeric_limits<std::size_t>::max() - totalByteSize) {
            return std::nullopt;
        }
        totalByteSize += levelByteSize;
        ++mipCount;
        if (levelWidth == 1U && levelHeight == 1U) {
            break;
        }
        levelWidth = std::max<std::uint16_t>(1U, static_cast<std::uint16_t>(levelWidth / 2U));
        levelHeight = std::max<std::uint16_t>(1U, static_cast<std::uint16_t>(levelHeight / 2U));
    }

    RuntimeTextureMipChain chain{};
    chain.rgba8.resize(totalByteSize);
    chain.mipCount = mipCount;
    std::copy(baseLevelRgba8.begin(), baseLevelRgba8.end(), chain.rgba8.begin());

    std::size_t sourceOffset = 0U;
    std::size_t destinationOffset = baseLevelRgba8.size();
    levelWidth = width;
    levelHeight = height;
    while (levelWidth > 1U || levelHeight > 1U) {
        const std::uint16_t destinationWidth =
            std::max<std::uint16_t>(1U, static_cast<std::uint16_t>(levelWidth / 2U));
        const std::uint16_t destinationHeight =
            std::max<std::uint16_t>(1U, static_cast<std::uint16_t>(levelHeight / 2U));
        const std::size_t sourceByteSize = LevelByteSize(levelWidth, levelHeight);
        const std::size_t destinationByteSize = LevelByteSize(destinationWidth, destinationHeight);

        DownsampleLevel(
            std::span<const std::uint8_t>{ chain.rgba8.data() + sourceOffset, sourceByteSize },
            levelWidth,
            levelHeight,
            std::span<std::uint8_t>{ chain.rgba8.data() + destinationOffset, destinationByteSize },
            destinationWidth,
            destinationHeight,
            colorSpace);

        sourceOffset = destinationOffset;
        destinationOffset += destinationByteSize;
        levelWidth = destinationWidth;
        levelHeight = destinationHeight;
    }
    return chain;
}

} // namespace kb::render
