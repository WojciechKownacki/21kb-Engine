#pragma once

#include "engine/project/ProjectSettings.hpp"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace kb::game {

// What the Vulkan loader reports for the device the renderer would start on, plus the Android release.
struct AndroidVulkanDevice {
    bool known = false;
    std::uint32_t vendorId = 0U;
    std::uint32_t deviceId = 0U;
    std::uint32_t driverVersion = 0U;
    std::uint32_t androidApiLevel = 0U;
};

// One entry of the list of Vulkan devices that lose the device or hang in the field. A device matches when the
// vendor is equal, the device id agrees with the value on the masked bits, the driver is not newer than
// maxDriverVersion and the Android release is not newer than maxAndroidApiLevel (0 accepts any release).
struct AndroidVulkanUnreliableRule {
    std::uint32_t vendorId;
    std::uint32_t deviceIdMask;
    std::uint32_t deviceIdValue;
    std::uint32_t maxDriverVersion;
    std::uint32_t maxAndroidApiLevel;
};

// The one place that names the devices a project with Auto must keep off Vulkan. The driver version is the raw
// VkPhysicalDeviceProperties::driverVersion: for vendor 0x13B5 the release number sits in bits 22 and above, so
// (20U << 22) - 1U is every release up to 19. Add an entry only for a device whose Vulkan device was lost while it
// ran, with the newest driver seen failing as the ceiling.
inline constexpr std::array<AndroidVulkanUnreliableRule, 1U> kAndroidVulkanUnreliableDevices{{
    { 0x13B5U, 0xFFFF0000U, 0x62210000U, (20U << 22U) - 1U, 29U },
}};

// The index of the first entry the device matches, or -1.
[[nodiscard]] constexpr int FindAndroidVulkanUnreliableRule(const AndroidVulkanDevice& device) noexcept {
    if (!device.known) {
        return -1;
    }
    for (std::size_t index = 0U; index < kAndroidVulkanUnreliableDevices.size(); ++index) {
        const AndroidVulkanUnreliableRule& rule = kAndroidVulkanUnreliableDevices[index];
        if (rule.vendorId == device.vendorId && (device.deviceId & rule.deviceIdMask) == rule.deviceIdValue &&
            device.driverVersion <= rule.maxDriverVersion &&
            (rule.maxAndroidApiLevel == 0U || device.androidApiLevel <= rule.maxAndroidApiLevel)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

// The renderers an Android package tries, in order, for the project's graphics API. Each value is what
// DisplayConfig::preferredBgfxRendererType takes: -1 leaves the choice to the renderer.
struct AndroidRendererCandidates {
    std::array<std::int32_t, 2U> types{ -1, -1 };
    std::uint8_t count = 0U;
    // The entry of kAndroidVulkanUnreliableDevices that moved an Auto project to OpenGL ES, or -1.
    int unreliableRule = -1;
};

// Auto is a single candidate that leaves the choice to the renderer (Vulkan where the build offers it, else
// OpenGL ES), unless the Vulkan device is on the unreliable list and the build offers OpenGL ES: then OpenGL ES is
// the only candidate. A named API comes first, followed by the other one, so a device that cannot start the named
// API still runs; an API this build does not offer is left out, and a named API is never overridden by the list.
// A build that offers neither falls back to the renderer's choice.
[[nodiscard]] inline AndroidRendererCandidates ResolveAndroidRenderers(kb::project::ProjectAndroidGraphicsApi api,
    const bgfx::RendererType::Enum* supported, std::uint8_t supportedCount, const AndroidVulkanDevice& vulkanDevice = {}) noexcept {
    using Api = kb::project::ProjectAndroidGraphicsApi;
    AndroidRendererCandidates candidates;
    if (api == Api::Auto) {
        const auto offers = [&](bgfx::RendererType::Enum renderer) noexcept {
            return std::find(supported, supported + supportedCount, renderer) != supported + supportedCount;
        };
        const int rule = offers(bgfx::RendererType::Vulkan) && offers(bgfx::RendererType::OpenGLES)
            ? FindAndroidVulkanUnreliableRule(vulkanDevice) : -1;
        if (rule >= 0) {
            candidates.types[0] = static_cast<std::int32_t>(bgfx::RendererType::OpenGLES);
            candidates.unreliableRule = rule;
        }
        candidates.count = 1U;
        return candidates;
    }
    const std::array<bgfx::RendererType::Enum, 2U> order = api == Api::Vulkan
        ? std::array{ bgfx::RendererType::Vulkan, bgfx::RendererType::OpenGLES }
        : std::array{ bgfx::RendererType::OpenGLES, bgfx::RendererType::Vulkan };
    for (const bgfx::RendererType::Enum renderer : order) {
        if (std::find(supported, supported + supportedCount, renderer) != supported + supportedCount) {
            candidates.types[candidates.count++] = static_cast<std::int32_t>(renderer);
        }
    }
    if (candidates.count == 0U) {
        candidates.count = 1U;
    }
    return candidates;
}

} // namespace kb::game
