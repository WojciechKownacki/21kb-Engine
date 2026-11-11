#pragma once

#include "engine/project/ProjectSettings.hpp"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace kb::game {

// The renderers an Android package tries, in order, for the project's graphics API. Each value is what
// DisplayConfig::preferredBgfxRendererType takes: -1 leaves the choice to the renderer.
struct AndroidRendererCandidates {
    std::array<std::int32_t, 2U> types{ -1, -1 };
    std::uint8_t count = 0U;
};

// Auto is a single candidate that leaves the choice to the renderer (Vulkan where the build offers it, else
// OpenGL ES). A named API comes first, followed by the other one, so a device that cannot start the named API
// still runs; an API this build does not offer is left out. A build that offers neither falls back to the renderer's choice.
[[nodiscard]] inline AndroidRendererCandidates ResolveAndroidRenderers(
    kb::project::ProjectAndroidGraphicsApi api, const bgfx::RendererType::Enum* supported, std::uint8_t supportedCount) noexcept {
    using Api = kb::project::ProjectAndroidGraphicsApi;
    AndroidRendererCandidates candidates;
    if (api == Api::Auto) {
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
