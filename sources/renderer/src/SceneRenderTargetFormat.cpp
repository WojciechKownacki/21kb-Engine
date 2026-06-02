#include "kb/render/SceneRenderTargetFormat.hpp"

namespace kb::render {

bool SceneColorFormatSelection::IsSupported() const noexcept {
    return format != bgfx::TextureFormat::Count && status != SceneTargetFormatSelectionStatus::Unsupported;
}

bool SceneColorFormatSelection::IsHdr() const noexcept {
    return SceneColorFormatIsHdr(format);
}

bool SceneDepthFormatSelection::IsSupported() const noexcept {
    return format != bgfx::TextureFormat::Count && status != SceneTargetFormatSelectionStatus::Unsupported;
}

bool SceneDepthFormatSelection::IsPreferred() const noexcept {
    return format == bgfx::TextureFormat::D32F || format == bgfx::TextureFormat::D32;
}

const char* SceneColorFormatPolicyName(SceneColorFormatPolicy policy) noexcept {
    switch (policy) {
    case SceneColorFormatPolicy::Auto:
        return "Auto";
    case SceneColorFormatPolicy::Rgba16F:
        return "RGBA16F";
    case SceneColorFormatPolicy::Rgba16:
        return "RGBA16";
    case SceneColorFormatPolicy::Rgba8:
        return "RGBA8";
    }
    return "Unknown";
}

const char* SceneTargetFormatSelectionStatusName(SceneTargetFormatSelectionStatus status) noexcept {
    switch (status) {
    case SceneTargetFormatSelectionStatus::Selected:
        return "Selected";
    case SceneTargetFormatSelectionStatus::CapabilityFallback:
        return "CapabilityFallback";
    case SceneTargetFormatSelectionStatus::Unsupported:
        return "Unsupported";
    }
    return "Unknown";
}

const char* SceneTextureFormatName(bgfx::TextureFormat::Enum format) noexcept {
    switch (format) {
    case bgfx::TextureFormat::RGBA16F:
        return "RGBA16F";
    case bgfx::TextureFormat::RGBA16:
        return "RGBA16";
    case bgfx::TextureFormat::RGBA8:
        return "RGBA8";
    case bgfx::TextureFormat::D32F:
        return "D32F";
    case bgfx::TextureFormat::D32:
        return "D32";
    case bgfx::TextureFormat::D24S8:
        return "D24S8";
    default:
        return "Unsupported";
    }
}

bool SceneColorFormatIsHdr(bgfx::TextureFormat::Enum format) noexcept {
    return format == bgfx::TextureFormat::RGBA16F;
}

bgfx::TextureFormat::Enum SceneColorFormatForPolicy(SceneColorFormatPolicy policy) noexcept {
    switch (policy) {
    case SceneColorFormatPolicy::Rgba16F:
        return bgfx::TextureFormat::RGBA16F;
    case SceneColorFormatPolicy::Rgba16:
        return bgfx::TextureFormat::RGBA16;
    case SceneColorFormatPolicy::Rgba8:
        return bgfx::TextureFormat::RGBA8;
    case SceneColorFormatPolicy::Auto:
        return bgfx::TextureFormat::Count;
    }
    return bgfx::TextureFormat::Count;
}

bool SceneColorFormatSupported(bgfx::TextureFormat::Enum format, std::uint64_t textureFlags) noexcept {
    return format != bgfx::TextureFormat::Count && bgfx::isTextureValid(0, false, 1, format, textureFlags);
}

SceneColorFormatSelection SelectSceneColorFormat(SceneColorFormatPolicy policy, std::uint64_t textureFlags) noexcept {
    if (policy != SceneColorFormatPolicy::Auto) {
        const bgfx::TextureFormat::Enum requested = SceneColorFormatForPolicy(policy);
        if (SceneColorFormatSupported(requested, textureFlags)) {
            return SceneColorFormatSelection{requested, policy, SceneTargetFormatSelectionStatus::Selected};
        }
        return SceneColorFormatSelection{bgfx::TextureFormat::Count, policy, SceneTargetFormatSelectionStatus::Unsupported};
    }

    if (SceneColorFormatSupported(bgfx::TextureFormat::RGBA16F, textureFlags)) {
        return SceneColorFormatSelection{bgfx::TextureFormat::RGBA16F, policy, SceneTargetFormatSelectionStatus::Selected};
    }
    if (SceneColorFormatSupported(bgfx::TextureFormat::RGBA16, textureFlags)) {
        return SceneColorFormatSelection{bgfx::TextureFormat::RGBA16, policy, SceneTargetFormatSelectionStatus::CapabilityFallback};
    }
    if (SceneColorFormatSupported(bgfx::TextureFormat::RGBA8, textureFlags)) {
        return SceneColorFormatSelection{bgfx::TextureFormat::RGBA8, policy, SceneTargetFormatSelectionStatus::CapabilityFallback};
    }
    return SceneColorFormatSelection{bgfx::TextureFormat::Count, policy, SceneTargetFormatSelectionStatus::Unsupported};
}

SceneDepthFormatSelection SelectSceneDepthFormat(std::uint64_t textureFlags) noexcept {
    if (bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::D32F, textureFlags)) {
        return SceneDepthFormatSelection{bgfx::TextureFormat::D32F, SceneTargetFormatSelectionStatus::Selected};
    }
    if (bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::D32, textureFlags)) {
        return SceneDepthFormatSelection{bgfx::TextureFormat::D32, SceneTargetFormatSelectionStatus::Selected};
    }
    if (bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::D24S8, textureFlags)) {
        return SceneDepthFormatSelection{bgfx::TextureFormat::D24S8, SceneTargetFormatSelectionStatus::CapabilityFallback};
    }
    return SceneDepthFormatSelection{bgfx::TextureFormat::Count, SceneTargetFormatSelectionStatus::Unsupported};
}

} // namespace kb::render
