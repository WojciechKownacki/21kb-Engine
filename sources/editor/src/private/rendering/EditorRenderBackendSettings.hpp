#pragma once

#include <cstdint>

namespace kb::editor {

enum class EditorRenderBackend : std::uint8_t {
    Auto,
    DirectX12,
    Vulkan,
};

enum class EditorAntiAliasingMode : std::uint8_t {
    None,
    Fxaa,
    Taa,
    Msaa,
};

class EditorRenderBackendSettings {
public:
    [[nodiscard]] EditorRenderBackend Backend() const noexcept;
    [[nodiscard]] std::uint64_t Generation() const noexcept;
    [[nodiscard]] bool ShadowsEnabled() const noexcept;
    [[nodiscard]] bool PostProcessEnabled() const noexcept;
    [[nodiscard]] EditorAntiAliasingMode AntiAliasingMode() const noexcept;
    [[nodiscard]] bool FxaaEnabled() const noexcept;
    [[nodiscard]] bool TemporalAntiAliasingEnabled() const noexcept;
    [[nodiscard]] bool BloomEnabled() const noexcept;
    [[nodiscard]] bool SelectionOutlineEnabled() const noexcept;
    [[nodiscard]] bool GpuDrivenEnabled() const noexcept;
    [[nodiscard]] std::uint8_t MsaaSamples() const noexcept;
    [[nodiscard]] std::uint64_t BackendGeneration() const noexcept;

    void SetBackend(EditorRenderBackend backend) noexcept;
    void CycleBackend() noexcept;
    void SetShadowsEnabled(bool enabled) noexcept;
    void SetPostProcessEnabled(bool enabled) noexcept;
    void SetAntiAliasingMode(EditorAntiAliasingMode mode) noexcept;
    void SetBloomEnabled(bool enabled) noexcept;
    void SetSelectionOutlineEnabled(bool enabled) noexcept;
    void SetGpuDrivenEnabled(bool enabled) noexcept;
    void SetMsaaSamples(std::uint8_t samples) noexcept;
    void ToggleShadowsEnabled() noexcept;
    void TogglePostProcessEnabled() noexcept;
    void ToggleBloomEnabled() noexcept;
    void ToggleSelectionOutlineEnabled() noexcept;
    void ToggleGpuDrivenEnabled() noexcept;

private:
    EditorRenderBackend backend_ = EditorRenderBackend::Auto;
    bool shadowsEnabled_ = true;
    bool postProcessEnabled_ = true;
    EditorAntiAliasingMode antiAliasingMode_ = EditorAntiAliasingMode::Taa;
    bool bloomEnabled_ = true;
    bool selectionOutlineEnabled_ = true;
    bool gpuDrivenEnabled_ = true;
    std::uint8_t msaaSamples_ = 0;
    std::uint64_t backendGeneration_ = 0;
    std::uint64_t generation_ = 0;
};

[[nodiscard]] const char* EditorRenderBackendLabel(EditorRenderBackend backend) noexcept;

} // namespace kb::editor
