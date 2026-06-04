#include "rendering/EditorBgfxBackendSelector.hpp"

#include "kb/render/RendererCapabilityReport.hpp"

namespace kb::editor {

bgfx::RendererType::Enum EditorBgfxBackendSelector::Resolve(
    const bgfx::RendererType::Enum* supportedBackends,
    std::uint8_t supportedBackendCount,
    const EditorRenderBackendSettings* backendSettings) noexcept {
    const bgfx::RendererType::Enum requested = backendSettings == nullptr
        ? bgfx::RendererType::Count
        : RequestedBgfxBackend(backendSettings->Backend());
    if (requested != bgfx::RendererType::Count && Contains(supportedBackends, supportedBackendCount, requested)) {
        return requested;
    }
    return render::ResolvePreferredRendererBackend(supportedBackends, supportedBackendCount);
}

bool EditorBgfxBackendSelector::Contains(
    const bgfx::RendererType::Enum* supportedBackends,
    std::uint8_t supportedBackendCount,
    bgfx::RendererType::Enum backend) noexcept {
    if (backend == bgfx::RendererType::Count) {
        return false;
    }
    for (std::uint8_t index = 0; index < supportedBackendCount; ++index) {
        if (supportedBackends[index] == backend) {
            return true;
        }
    }
    return false;
}

bgfx::RendererType::Enum EditorBgfxBackendSelector::RequestedBgfxBackend(EditorRenderBackend backend) noexcept {
    switch (backend) {
    case EditorRenderBackend::Auto:
        return bgfx::RendererType::Count;
    case EditorRenderBackend::DirectX12:
        return bgfx::RendererType::Direct3D12;
    case EditorRenderBackend::Vulkan:
        return bgfx::RendererType::Vulkan;
    }
    return bgfx::RendererType::Count;
}

} // namespace kb::editor
