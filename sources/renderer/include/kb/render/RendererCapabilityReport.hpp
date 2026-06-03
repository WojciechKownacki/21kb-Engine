#pragma once

#include "kb/render/SceneRenderTargetFormat.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <string_view>

namespace kb::render {

enum class RendererBackendPreference : std::uint8_t {
    Auto,
    Explicit,
};

struct RendererCapabilityLimits {
    std::uint32_t maxDrawCalls = 0;
    std::uint32_t maxBlits = 0;
    std::uint32_t maxTextureSize = 0;
    std::uint32_t maxViews = 0;
    std::uint32_t maxFrameBuffers = 0;
    std::uint32_t maxTextures = 0;
    std::uint32_t maxPrograms = 0;
    std::uint32_t maxShaders = 0;
    std::uint32_t maxComputeBindings = 0;
    std::uint32_t maxEncoders = 0;
};

struct RendererCapabilityReport {
    bgfx::RendererType::Enum requestedBackend = bgfx::RendererType::Count;
    bgfx::RendererType::Enum selectedBackend = bgfx::RendererType::Count;
    RendererBackendPreference backendPreference = RendererBackendPreference::Auto;
    const char* selectedBackendName = "Unknown";
    const char* backendSelectionReason = "bgfx auto-selected backend";
    std::uint64_t supportedFlags = 0;
    std::uint16_t vendorId = 0;
    std::uint16_t deviceId = 0;
    bool initialized = false;
    bool hdrTextureSupported = false;
    bool depthPreferredSupported = false;
    bool computeSupported = false;
    bool indirectDrawSupported = false;
    bool textureBlitSupported = false;
    bool textureReadbackSupported = false;
    bool hdrExposureMeteringSupported = false;
    bool gpuDrivenComputeCullingSupported = false;
    bool gpuDrivenIndirectSubmitSupported = false;
    bool gpuDrivenMeshletSubmitSupported = false;
    bool multipleWindowsSupported = false;
    bool nativePresentColorOnly = true;
    bool homogeneousDepth = false;
    bool originBottomLeft = false;
    SceneColorFormatSelection sceneColor{};
    SceneDepthFormatSelection sceneDepth{};
    bgfx::TextureFormat::Enum nativePresentColor = bgfx::TextureFormat::BGRA8;
    bgfx::TextureFormat::Enum nativePresentDepth = bgfx::TextureFormat::Count;
    RendererCapabilityLimits limits{};
    std::array<const char*, 8U> fallbackReasons{};
    std::uint32_t fallbackReasonCount = 0;
};

[[nodiscard]] const char* RendererBackendName(bgfx::RendererType::Enum renderer) noexcept;
[[nodiscard]] bgfx::RendererType::Enum ResolvePreferredRendererBackend(const bgfx::RendererType::Enum* supportedBackends, std::uint8_t supportedBackendCount) noexcept;
[[nodiscard]] RendererCapabilityReport BuildRendererCapabilityReport(bgfx::RendererType::Enum requestedBackend) noexcept;
[[nodiscard]] std::string_view RendererCapabilitySummary(const RendererCapabilityReport& report) noexcept;

} // namespace kb::render
