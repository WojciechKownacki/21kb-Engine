#include "kb/render/Renderer.hpp"
#include "kb/render/RendererMemory.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d11.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <bgfx/bgfx.h>
#endif

namespace kb::render {

std::optional<std::uint64_t> ProcessGpuMemoryBytes(const Renderer& renderer) noexcept {
    if (!renderer.IsInitialized()) return std::nullopt;
#if defined(_WIN32)
    if (bgfx::getRendererType() != bgfx::RendererType::Direct3D11) return std::nullopt;
    const auto* internal = bgfx::getInternalData();
    if (!internal || !internal->context) return std::nullopt;
    auto* device = static_cast<ID3D11Device*>(internal->context);
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(dxgi.GetAddressOf())))) return std::nullopt;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgi->GetAdapter(adapter.GetAddressOf()))) return std::nullopt;
    Microsoft::WRL::ComPtr<IDXGIAdapter3> memoryAdapter;
    if (FAILED(adapter.As(&memoryAdapter))) return std::nullopt;
    DXGI_QUERY_VIDEO_MEMORY_INFO local{}, shared{};
    if (FAILED(memoryAdapter->QueryVideoMemoryInfo(0U, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local)) ||
        FAILED(memoryAdapter->QueryVideoMemoryInfo(0U, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &shared))) {
        return std::nullopt;
    }
    return local.CurrentUsage + shared.CurrentUsage;
#else
    return std::nullopt;
#endif
}

} // namespace kb::render
