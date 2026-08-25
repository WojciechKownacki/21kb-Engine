#pragma once

#include "engine/assets/AssetId.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <cstdint>
#include <memory>
#include <vector>

namespace kb::assets { struct AssetMetadata; }

namespace kb::editor {

class EditorSceneContext;
class EditorSceneBgfxViewport;

struct EditorParticleThumbnailImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> bgra;
};

class EditorParticleThumbnailService final {
public:
    EditorParticleThumbnailService();
    ~EditorParticleThumbnailService();

    EditorParticleThumbnailService(const EditorParticleThumbnailService&) = delete;
    EditorParticleThumbnailService& operator=(const EditorParticleThumbnailService&) = delete;

    [[nodiscard]] const EditorParticleThumbnailImage* ThumbnailFor(
        const kb::assets::AssetMetadata& metadata,
        std::uint64_t animationFrame = 0U);
    [[nodiscard]] const EditorParticleThumbnailImage* ThumbnailForTime(
        const kb::assets::AssetMetadata& metadata,
        double elapsedSeconds);
    [[nodiscard]] std::uint32_t AnimationFrameForTime(
        const kb::assets::AssetMetadata& metadata,
        double elapsedSeconds) const noexcept;
    [[nodiscard]] std::uint32_t AnimationFrameCount(
        const kb::assets::AssetMetadata& metadata) const noexcept;
    [[nodiscard]] float AnimationDurationSeconds(
        const kb::assets::AssetMetadata& metadata) const noexcept;
    // Advances one bounded simulation/capture stage. Frames come from the
    // production renderer's asynchronous capture channel; the UI thread never
    // waits for the GPU.
    [[nodiscard]] bool Tick(
        EditorSceneContext& sceneContext,
        EditorSceneBgfxViewport& viewport,
        HWND host,
        const RECT& stagingRect);
    // Queues one bounded stage into an EditorSceneBgfxViewport paint layout
    // already owned by the editor frame. This avoids a nested BeginFrame /
    // EndFrame from a popup timer while the main Scene surface is active.
    [[nodiscard]] bool TickWithinFrame(
        EditorSceneContext& sceneContext,
        EditorSceneBgfxViewport& viewport,
        HWND host,
        const RECT& stagingRect);
    [[nodiscard]] bool HasPendingWork() const noexcept;
    [[nodiscard]] std::uint64_t Revision() const noexcept;
    void CancelPendingWork(EditorSceneBgfxViewport* viewport = nullptr) noexcept;
    void Clear(EditorSceneBgfxViewport* viewport = nullptr) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] EditorParticleThumbnailService& EditorParticleThumbnailCache();

} // namespace kb::editor
