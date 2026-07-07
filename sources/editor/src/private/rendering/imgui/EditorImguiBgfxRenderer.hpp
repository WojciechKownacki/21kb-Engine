#pragma once

#include <bgfx/bgfx.h>
#include <imgui.h>

#include <cstdint>
#include <span>
#include <vector>

struct ImDrawData;
struct ImGuiContext;

namespace kb::editor {

struct EditorImguiInputState {
    float mouseX = -3.402823466e+38F;
    float mouseY = -3.402823466e+38F;
    bool mouseVisible = false;
    bool leftMouseDown = false;
    bool rightMouseDown = false;
    bool middleMouseDown = false;
    float mouseWheel = 0.0F;
};

class EditorImguiBgfxRenderer {
public:
    EditorImguiBgfxRenderer() = default;
    ~EditorImguiBgfxRenderer();

    EditorImguiBgfxRenderer(const EditorImguiBgfxRenderer&) = delete;
    EditorImguiBgfxRenderer& operator=(const EditorImguiBgfxRenderer&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;

    [[nodiscard]] bool BeginFrame(
        std::uint32_t width,
        std::uint32_t height,
        float deltaSeconds,
        const EditorImguiInputState& input = {});
    [[nodiscard]] bool SubmitFrame(bgfx::ViewId viewId, bgfx::FrameBufferHandle frameBuffer);
    void ClearCurrentContext() noexcept;

    [[nodiscard]] ImTextureID EnsureTexture(
        std::uint64_t cacheKey,
        int width,
        int height,
        std::span<const std::uint32_t> bgraPixels);

    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    struct TextureCacheEntry {
        std::uint64_t cacheKey = 0U;
        std::uint64_t contentHash = 0U;
        int width = 0;
        int height = 0;
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    };

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
    std::vector<TextureCacheEntry> textureCache_;
};

} // namespace kb::editor
