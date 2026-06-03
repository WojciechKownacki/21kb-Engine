#include "kb/render/BgfxContext.hpp"

#include "kb/render/RenderSurface.hpp"

#include <bgfx/platform.h>
#include <cstdarg>
#include <cstdlib>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace kb::render {

class BgfxEngineCallback final : public bgfx::CallbackI {
public:
    void fatal(const char* filePath, std::uint16_t line, bgfx::Fatal::Enum code, const char* message) override {
#if defined(_WIN32)
        if (code == bgfx::Fatal::DeviceLost) {
            MessageBoxA(nullptr, message != nullptr ? message : "bgfx device lost", "21kb Engine - bgfx fatal", MB_OK | MB_ICONERROR);
        }
#else
        (void)message;
#endif
        (void)filePath;
        (void)line;
        if (code != bgfx::Fatal::DebugCheck) {
            std::abort();
        }
    }

    void traceVargs(const char*, std::uint16_t, const char*, va_list) override {}
    void profilerBegin(const char*, std::uint32_t, const char*, std::uint16_t) override {}
    void profilerBeginLiteral(const char*, std::uint32_t, const char*, std::uint16_t) override {}
    void profilerEnd() override {}
    std::uint32_t cacheReadSize(std::uint64_t) override {
        return 0;
    }
    bool cacheRead(std::uint64_t, void*, std::uint32_t) override {
        return false;
    }
    void cacheWrite(std::uint64_t, const void*, std::uint32_t) override {}
    void screenShot(const char*, std::uint32_t, std::uint32_t, std::uint32_t, bgfx::TextureFormat::Enum, const void*, std::uint32_t, bool) override {}
    void captureBegin(std::uint32_t, std::uint32_t, std::uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, std::uint32_t) override {}
};

BgfxContext::BgfxContext() = default;

BgfxContext::~BgfxContext() {
    Shutdown();
}

bool BgfxContext::Initialize(RenderSurface& surface, const DisplayConfig& config, bgfx::RendererType::Enum preferredBackend) {
    const bool headlessNoop = config.allowHeadlessNoop && preferredBackend == bgfx::RendererType::Noop;
    if (surface.NativeWindowHandle() == nullptr && !headlessNoop) {
        return false;
    }

    return InitializeImpl(surface.Width(), surface.Height(), surface.NativeWindowHandle(), surface.NativeDisplayHandle(), config, preferredBackend);
}

bool BgfxContext::InitializeImpl(std::uint32_t width, std::uint32_t height, void* nwh, void* ndt, const DisplayConfig& config, bgfx::RendererType::Enum preferredBackend) {
    if (initialized_) {
        return true;
    }
    const bool headlessNoop = config.allowHeadlessNoop && preferredBackend == bgfx::RendererType::Noop;
    if (width == 0 || height == 0 || (nwh == nullptr && !headlessNoop)) {
        return false;
    }

    callback_ = std::make_unique<BgfxEngineCallback>();
    nativeWindowHandle_ = nwh;
    nativeDisplayHandle_ = ndt;

    bgfx::Init init{};
    resetFlags_ = config.ComputeResetFlags();
    init.callback = callback_.get();
    init.platformData.ndt = ndt;
    init.platformData.nwh = nwh;
    init.platformData.context = nullptr;
    init.platformData.backBuffer = nullptr;
    init.platformData.backBufferDS = nullptr;
    init.platformData.type = bgfx::NativeWindowHandleType::Default;
    init.resolution.width = width;
    init.resolution.height = height;
    init.resolution.reset = resetFlags_;
    init.type = preferredBackend;
    init.vendorId = BGFX_PCI_ID_NONE;

#if !defined(NDEBUG)
    init.debug = config.requestGpuDebugLayers;
#endif

    if (!bgfx::init(init)) {
        callback_.reset();
        nativeWindowHandle_ = nullptr;
        nativeDisplayHandle_ = nullptr;
        resetFlags_ = 0;
        capabilityReport_ = {};
        return false;
    }

    width_ = width;
    height_ = height;
    initialized_ = true;
    capabilityReport_ = BuildRendererCapabilityReport(preferredBackend);
    bgfx::setDebug(BGFX_DEBUG_TEXT);
    return true;
}

void BgfxContext::Shutdown() {
    if (!initialized_) {
        return;
    }

    bgfx::shutdown();
    callback_.reset();
    initialized_ = false;
    width_ = 0;
    height_ = 0;
    resetFlags_ = 0;
    nativeWindowHandle_ = nullptr;
    nativeDisplayHandle_ = nullptr;
    capabilityReport_ = {};
}

void BgfxContext::Reset(std::uint32_t width, std::uint32_t height, std::uint32_t resetFlags) {
    if (!initialized_ || width == 0 || height == 0) {
        return;
    }

    if (width_ == width && height_ == height && resetFlags_ == resetFlags) {
        return;
    }

    width_ = width;
    height_ = height;
    resetFlags_ = resetFlags;
    bgfx::reset(width, height, resetFlags);
}

bool BgfxContext::BeginFrame() const noexcept {
    return initialized_;
}

std::uint32_t BgfxContext::EndFrame() const {
    return initialized_ ? bgfx::frame() : 0;
}

bool BgfxContext::IsInitialized() const noexcept {
    return initialized_;
}

std::uint32_t BgfxContext::Width() const noexcept {
    return width_;
}

std::uint32_t BgfxContext::Height() const noexcept {
    return height_;
}

void* BgfxContext::NativeWindowHandle() const noexcept {
    return nativeWindowHandle_;
}

const RendererCapabilityReport& BgfxContext::CapabilityReport() const noexcept {
    return capabilityReport_;
}

} // namespace kb::render
