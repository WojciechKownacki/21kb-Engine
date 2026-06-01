#pragma once

#include "kb/render/DisplayConfig.hpp"

#include <cstdint>
#include <memory>

namespace kb::render {

class BgfxContext;
class RenderSurface;

class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    [[nodiscard]] bool Initialize(RenderSurface& surface, const DisplayConfig* config = nullptr);
    void Shutdown();

    [[nodiscard]] bool BeginFrame();
    void EndFrame();
    void SubmitClear(std::uint32_t rgba, float depth = 1.0F, std::uint8_t stencil = 0);
    void OnResize(std::uint32_t width, std::uint32_t height);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool IsFrameActive() const noexcept;
    [[nodiscard]] std::uint32_t BackbufferWidth() const noexcept;
    [[nodiscard]] std::uint32_t BackbufferHeight() const noexcept;
    [[nodiscard]] void* NativeWindowHandle() const noexcept;

private:
    std::unique_ptr<BgfxContext> context_;
    DisplayConfig displayConfig_{};
    bool frameActive_ = false;
};

} // namespace kb::render
