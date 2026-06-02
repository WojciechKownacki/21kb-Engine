#pragma once

#include "kb/render/DisplayConfig.hpp"

#include <bgfx/bgfx.h>
#include <cstdint>
#include <memory>

namespace kb::render {

class RenderSurface;

class BgfxEngineCallback;

class BgfxContext {
public:
    BgfxContext();
    ~BgfxContext();

    BgfxContext(const BgfxContext&) = delete;
    BgfxContext& operator=(const BgfxContext&) = delete;

    [[nodiscard]] bool Initialize(RenderSurface& surface, const DisplayConfig& config, bgfx::RendererType::Enum preferredBackend = bgfx::RendererType::Count);
    void Shutdown();
    void Reset(std::uint32_t width, std::uint32_t height, std::uint32_t resetFlags);

    [[nodiscard]] bool BeginFrame() const noexcept;
    [[nodiscard]] std::uint32_t EndFrame() const;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] std::uint32_t Width() const noexcept;
    [[nodiscard]] std::uint32_t Height() const noexcept;
    [[nodiscard]] void* NativeWindowHandle() const noexcept;

private:
    [[nodiscard]] bool InitializeImpl(std::uint32_t width, std::uint32_t height, void* nwh, void* ndt, const DisplayConfig& config, bgfx::RendererType::Enum preferredBackend);

    bool initialized_ = false;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t resetFlags_ = 0;
    void* nativeWindowHandle_ = nullptr;
    void* nativeDisplayHandle_ = nullptr;
    std::unique_ptr<BgfxEngineCallback> callback_;
};

} // namespace kb::render
