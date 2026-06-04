#pragma once

#include "rendering/EditorRenderBackendSettings.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace kb::editor {

class EditorBgfxBackendSelector {
public:
    [[nodiscard]] static bgfx::RendererType::Enum Resolve(
        const bgfx::RendererType::Enum* supportedBackends,
        std::uint8_t supportedBackendCount,
        const EditorRenderBackendSettings* backendSettings) noexcept;

private:
    [[nodiscard]] static bool Contains(
        const bgfx::RendererType::Enum* supportedBackends,
        std::uint8_t supportedBackendCount,
        bgfx::RendererType::Enum backend) noexcept;

    [[nodiscard]] static bgfx::RendererType::Enum RequestedBgfxBackend(EditorRenderBackend backend) noexcept;
};

} // namespace kb::editor
