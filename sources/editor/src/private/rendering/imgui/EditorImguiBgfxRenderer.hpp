#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

struct ImDrawData;
struct ImGuiContext;

namespace kb::editor {

class EditorImguiBgfxRenderer {
public:
    EditorImguiBgfxRenderer() = default;
    ~EditorImguiBgfxRenderer();

    EditorImguiBgfxRenderer(const EditorImguiBgfxRenderer&) = delete;
    EditorImguiBgfxRenderer& operator=(const EditorImguiBgfxRenderer&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;

    [[nodiscard]] bool BeginFrame(std::uint32_t width, std::uint32_t height, float deltaSeconds);
    [[nodiscard]] bool SubmitFrame(bgfx::ViewId viewId, bgfx::FrameBufferHandle frameBuffer);
    void ClearCurrentContext() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    [[nodiscard]] bool CreateDeviceObjects();
    [[nodiscard]] bool CreateFontTexture();
    void DestroyDeviceObjects() noexcept;
    void ApplyStyle() noexcept;
    void SubmitDrawData(ImDrawData& drawData, bgfx::ViewId viewId, bgfx::FrameBufferHandle frameBuffer) noexcept;

    ImGuiContext* context_ = nullptr;
    bgfx::VertexLayout vertexLayout_{};
    bgfx::UniformHandle textureUniform_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fontTexture_ = BGFX_INVALID_HANDLE;
};

} // namespace kb::editor
