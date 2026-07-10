#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>

namespace kb::render {

// Identifies one independently compiled/bound material graph program family.  The
// binaryInputHash is the revision/toolchain/dependency identity; the remaining
// fields are the stable variant dimensions used to replace an older revision.
enum class RenderMaterialGraphVariantUsage : std::uint8_t {
    Runtime,
    Preview,
    Scene,
    NodePreview,
};

struct RenderMaterialGraphVariantKey {
    std::uint64_t materialAssetId = 0U;
    RenderMaterialGraphVariantUsage usage = RenderMaterialGraphVariantUsage::Runtime;
    std::uint8_t qualityLevel = 0U;
    std::uint8_t featureLevel = 0U;
    std::uint8_t shadingPath = 0U;
    std::uint8_t shaderStage = 0U;
    std::string pass;
    std::uint32_t backend = 0U;
    std::uint64_t binaryInputHash = 0U;

    [[nodiscard]] bool operator==(const RenderMaterialGraphVariantKey&) const noexcept = default;

    [[nodiscard]] bool SameProgramFamily(const RenderMaterialGraphVariantKey& rhs) const noexcept {
        return materialAssetId == rhs.materialAssetId &&
            usage == rhs.usage &&
            qualityLevel == rhs.qualityLevel &&
            featureLevel == rhs.featureLevel &&
            shadingPath == rhs.shadingPath &&
            shaderStage == rhs.shaderStage &&
            pass == rhs.pass &&
            backend == rhs.backend;
    }
};

struct RenderMaterialGraphVariantKeyHash {
    [[nodiscard]] std::size_t operator()(const RenderMaterialGraphVariantKey& key) const noexcept {
        constexpr std::uint64_t offset = 1469598103934665603ULL;
        constexpr std::uint64_t prime = 1099511628211ULL;
        std::uint64_t hash = offset;
        const auto byte = [&hash](std::uint8_t value) {
            hash ^= value;
            hash *= prime;
        };
        const auto u64 = [&byte](std::uint64_t value) {
            for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
                byte(static_cast<std::uint8_t>((value >> shift) & 0xffU));
            }
        };
        const auto text = [&u64, &byte](std::string_view value) {
            u64(static_cast<std::uint64_t>(value.size()));
            for (const char ch : value) {
                byte(static_cast<std::uint8_t>(static_cast<unsigned char>(ch)));
            }
        };
        u64(key.materialAssetId);
        byte(static_cast<std::uint8_t>(key.usage));
        byte(key.qualityLevel);
        byte(key.featureLevel);
        byte(key.shadingPath);
        byte(key.shaderStage);
        text(key.pass);
        u64(key.backend);
        u64(key.binaryInputHash);
        return static_cast<std::size_t>(hash);
    }
};

} // namespace kb::render
