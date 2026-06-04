#pragma once

#include <cstdint>

namespace kb::editor {

enum class EditorRenderBackend : std::uint8_t {
    Auto,
    DirectX12,
    Vulkan,
};

class EditorRenderBackendSettings {
public:
    [[nodiscard]] EditorRenderBackend Backend() const noexcept;
    [[nodiscard]] std::uint64_t Generation() const noexcept;

    void SetBackend(EditorRenderBackend backend) noexcept;
    void CycleBackend() noexcept;

private:
    EditorRenderBackend backend_ = EditorRenderBackend::Auto;
    std::uint64_t generation_ = 0;
};

[[nodiscard]] const char* EditorRenderBackendLabel(EditorRenderBackend backend) noexcept;

} // namespace kb::editor
