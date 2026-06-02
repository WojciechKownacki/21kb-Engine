#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

namespace kb::render {

enum class SceneColorFormatPolicy : std::uint8_t {
    Auto,
    Rgba16F,
    Rgba16,
    Rgba8,
};

enum class SceneTargetFormatSelectionStatus : std::uint8_t {
    Selected,
    CapabilityFallback,
    Unsupported,
};

struct SceneColorFormatSelection {
    bgfx::TextureFormat::Enum format = bgfx::TextureFormat::Count;
    SceneColorFormatPolicy requested = SceneColorFormatPolicy::Auto;
    SceneTargetFormatSelectionStatus status = SceneTargetFormatSelectionStatus::Unsupported;

    [[nodiscard]] bool IsSupported() const noexcept;
    [[nodiscard]] bool IsHdr() const noexcept;
};

struct SceneDepthFormatSelection {
    bgfx::TextureFormat::Enum format = bgfx::TextureFormat::Count;
    SceneTargetFormatSelectionStatus status = SceneTargetFormatSelectionStatus::Unsupported;

    [[nodiscard]] bool IsSupported() const noexcept;
    [[nodiscard]] bool IsPreferred() const noexcept;
};

[[nodiscard]] const char* SceneColorFormatPolicyName(SceneColorFormatPolicy policy) noexcept;
[[nodiscard]] const char* SceneTargetFormatSelectionStatusName(SceneTargetFormatSelectionStatus status) noexcept;
[[nodiscard]] const char* SceneTextureFormatName(bgfx::TextureFormat::Enum format) noexcept;
[[nodiscard]] bool SceneColorFormatIsHdr(bgfx::TextureFormat::Enum format) noexcept;
[[nodiscard]] bgfx::TextureFormat::Enum SceneColorFormatForPolicy(SceneColorFormatPolicy policy) noexcept;
[[nodiscard]] bool SceneColorFormatSupported(bgfx::TextureFormat::Enum format, std::uint64_t textureFlags) noexcept;
[[nodiscard]] SceneColorFormatSelection SelectSceneColorFormat(SceneColorFormatPolicy policy, std::uint64_t textureFlags) noexcept;
[[nodiscard]] SceneDepthFormatSelection SelectSceneDepthFormat(std::uint64_t textureFlags) noexcept;

} // namespace kb::render
