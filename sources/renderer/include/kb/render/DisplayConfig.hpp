#pragma once

#include <cstdint>
#include <string>

namespace kb::render {

enum class DisplaySyncMode : std::uint8_t {
    VSync,
    Uncapped,
    TargetFps
};

// Auto presents in HDR10 (PQ, Rec.2020, 10-bit swap chain) when the backend and a connected display support it.
enum class DisplayHdrOutputMode : std::uint8_t {
    Off,
    Auto
};

// What the renderer decided for HDR output; `reason` always says why it is on or off.
struct DisplayHdrOutputState {
    bool active = false;
    // Paper white and the brightest level the display image may reach, in nits.
    float paperWhiteNits = 200.0F;
    float maxNits = 1000.0F;
    const char* reason = "HDR output is off";
};

struct DisplayConfig {
    DisplaySyncMode syncMode = DisplaySyncMode::VSync;
    std::uint32_t targetFps = 120;
    bool requestGpuDebugLayers = false;
    bool flushAfterRender = false;
    bool allowHeadlessNoop = false;
    // Editor-only overlays own shader programs and render passes that are deliberately absent
    // from shipped game packages. Game hosts disable them before initialization; editor and
    // development hosts retain the established default.
    bool enableEditorRendering = true;
    std::uint8_t msaaSamples = 0;
    DisplayHdrOutputMode hdrOutputMode = DisplayHdrOutputMode::Off;
    float hdrPaperWhiteNits = 200.0F;
    float hdrMaxNits = 1000.0F;
    std::int32_t preferredBgfxRendererType = -1;
    // Writable per-application storage for renderer caches and diagnostics. Empty preserves
    // the desktop host's existing current-directory/temp behavior. Sandboxed hosts such as
    // Android must provide their internal data directory explicitly.
    std::string writableStorageRoot;
    // Logical GPU particle buffers across this renderer; zero uses the engine limit.
    std::uint64_t particleGpuBufferBudgetBytes = 0U;

    [[nodiscard]] std::uint32_t ComputeResetFlags() const noexcept;
};

} // namespace kb::render
