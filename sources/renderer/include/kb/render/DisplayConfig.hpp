#pragma once

#include <cstdint>
#include <string>

namespace kb::render {

enum class DisplaySyncMode : std::uint8_t {
    VSync,
    Uncapped,
    TargetFps
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
    std::int32_t preferredBgfxRendererType = -1;
    // Writable per-application storage for renderer caches and diagnostics. Empty preserves
    // the desktop host's existing current-directory/temp behavior. Sandboxed hosts such as
    // Android must provide their internal data directory explicitly.
    std::string writableStorageRoot;

    [[nodiscard]] std::uint32_t ComputeResetFlags() const noexcept;
};

} // namespace kb::render
