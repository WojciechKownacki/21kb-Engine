#include "rendering/EditorRenderBackendSettings.hpp"

namespace kb::editor {

EditorRenderBackend EditorRenderBackendSettings::Backend() const noexcept {
    return backend_;
}

std::uint64_t EditorRenderBackendSettings::Generation() const noexcept {
    return generation_;
}

bool EditorRenderBackendSettings::ShadowsEnabled() const noexcept {
    return shadowsEnabled_;
}

bool EditorRenderBackendSettings::PostProcessEnabled() const noexcept {
    return postProcessEnabled_;
}

EditorAntiAliasingMode EditorRenderBackendSettings::AntiAliasingMode() const noexcept {
    return antiAliasingMode_;
}

bool EditorRenderBackendSettings::FxaaEnabled() const noexcept {
    return antiAliasingMode_ == EditorAntiAliasingMode::Fxaa;
}

bool EditorRenderBackendSettings::TemporalAntiAliasingEnabled() const noexcept {
    return antiAliasingMode_ == EditorAntiAliasingMode::Taa;
}

bool EditorRenderBackendSettings::BloomEnabled() const noexcept {
    return bloomEnabled_;
}

bool EditorRenderBackendSettings::SelectionOutlineEnabled() const noexcept {
    return selectionOutlineEnabled_;
}

bool EditorRenderBackendSettings::GpuDrivenEnabled() const noexcept {
    return gpuDrivenEnabled_;
}

std::uint8_t EditorRenderBackendSettings::MsaaSamples() const noexcept {
    return antiAliasingMode_ == EditorAntiAliasingMode::Msaa ? msaaSamples_ : 0U;
}

std::uint64_t EditorRenderBackendSettings::BackendGeneration() const noexcept {
    return backendGeneration_;
}

void EditorRenderBackendSettings::SetBackend(EditorRenderBackend backend) noexcept {
    if (backend_ == backend) {
        return;
    }
    backend_ = backend;
    ++backendGeneration_;
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

void EditorRenderBackendSettings::SetShadowsEnabled(bool enabled) noexcept {
    if (shadowsEnabled_ == enabled) {
        return;
    }
    shadowsEnabled_ = enabled;
    ++generation_;
}

void EditorRenderBackendSettings::SetPostProcessEnabled(bool enabled) noexcept {
    if (postProcessEnabled_ == enabled) {
        return;
    }
    postProcessEnabled_ = enabled;
    ++generation_;
}

void EditorRenderBackendSettings::SetAntiAliasingMode(EditorAntiAliasingMode mode) noexcept {
    if (antiAliasingMode_ == mode) {
        return;
    }
    const bool restartsBackend = antiAliasingMode_ == EditorAntiAliasingMode::Msaa || mode == EditorAntiAliasingMode::Msaa;
    antiAliasingMode_ = mode;
    if (antiAliasingMode_ == EditorAntiAliasingMode::Msaa && msaaSamples_ == 0U) {
        msaaSamples_ = 2U;
    }
    if (restartsBackend) {
        ++backendGeneration_;
    }
    ++generation_;
}

void EditorRenderBackendSettings::SetBloomEnabled(bool enabled) noexcept {
    if (bloomEnabled_ == enabled) {
        return;
    }
    bloomEnabled_ = enabled;
    ++generation_;
}

void EditorRenderBackendSettings::SetSelectionOutlineEnabled(bool enabled) noexcept {
    if (selectionOutlineEnabled_ == enabled) {
        return;
    }
    selectionOutlineEnabled_ = enabled;
    ++generation_;
}

void EditorRenderBackendSettings::SetGpuDrivenEnabled(bool enabled) noexcept {
    if (gpuDrivenEnabled_ == enabled) {
        return;
    }
    gpuDrivenEnabled_ = enabled;
    ++generation_;
}

void EditorRenderBackendSettings::SetMsaaSamples(std::uint8_t samples) noexcept {
    if (samples != 2U && samples != 4U && samples != 8U && samples != 16U) {
        samples = 0U;
    }
    if (msaaSamples_ == samples && antiAliasingMode_ == EditorAntiAliasingMode::Msaa) {
        return;
    }
    const EditorAntiAliasingMode previousMode = antiAliasingMode_;
    msaaSamples_ = samples;
    SetAntiAliasingMode(samples == 0U ? EditorAntiAliasingMode::None : EditorAntiAliasingMode::Msaa);
    if (previousMode == EditorAntiAliasingMode::Msaa && antiAliasingMode_ == EditorAntiAliasingMode::Msaa) {
        ++backendGeneration_;
        ++generation_;
    }
}

void EditorRenderBackendSettings::ToggleShadowsEnabled() noexcept {
    SetShadowsEnabled(!shadowsEnabled_);
}

void EditorRenderBackendSettings::TogglePostProcessEnabled() noexcept {
    SetPostProcessEnabled(!postProcessEnabled_);
}

void EditorRenderBackendSettings::ToggleBloomEnabled() noexcept {
    SetBloomEnabled(!bloomEnabled_);
}

void EditorRenderBackendSettings::ToggleSelectionOutlineEnabled() noexcept {
    SetSelectionOutlineEnabled(!selectionOutlineEnabled_);
}

void EditorRenderBackendSettings::ToggleGpuDrivenEnabled() noexcept {
    SetGpuDrivenEnabled(!gpuDrivenEnabled_);
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
