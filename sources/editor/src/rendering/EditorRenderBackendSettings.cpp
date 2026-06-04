#include "rendering/EditorRenderBackendSettings.hpp"

namespace kb::editor {

EditorRenderBackend EditorRenderBackendSettings::Backend() const noexcept {
    return backend_;
}

std::uint64_t EditorRenderBackendSettings::Generation() const noexcept {
    return generation_;
}

void EditorRenderBackendSettings::SetBackend(EditorRenderBackend backend) noexcept {
    if (backend_ == backend) {
        return;
    }
    backend_ = backend;
    ++generation_;
}

void EditorRenderBackendSettings::CycleBackend() noexcept {
    switch (backend_) {
    case EditorRenderBackend::Auto:
        SetBackend(EditorRenderBackend::DirectX12);
        return;
    case EditorRenderBackend::DirectX12:
        SetBackend(EditorRenderBackend::Vulkan);
        return;
    case EditorRenderBackend::Vulkan:
        SetBackend(EditorRenderBackend::Auto);
        return;
    }
    SetBackend(EditorRenderBackend::Auto);
}

const char* EditorRenderBackendLabel(EditorRenderBackend backend) noexcept {
    switch (backend) {
    case EditorRenderBackend::Auto:
        return "Auto";
    case EditorRenderBackend::DirectX12:
        return "DX12";
    case EditorRenderBackend::Vulkan:
        return "Vulkan";
    }
    return "Auto";
}

} // namespace kb::editor
