#pragma once

#include "engine/assets/AssetMetadata.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <cstdint>
#include <utility>
#include <vector>

namespace kb::editor {

#if defined(_WIN32)

class EditorSceneContext;
class EditorSceneBgfxViewport;

// One number decides how large a material ball is drawn in a Project Files tile, for the rendered
// thumbnail and for the painted stand-in alike. Two sources of truth here is what made the ball change
// size the moment the render replaced the placeholder.
inline constexpr float kMaterialPreviewBallFraction = 0.74F;

struct EditorMaterialThumbnailImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> bgra;
};

// Renders material thumbnails with the real renderer instead of faking them on the CPU.
//
// One material at a time: the preview scene is a single shared object, so a frame can only ever hold one
// material. The material is presented into a small staging surface at thumbnail resolution and captured
// through the scene's async screen-capture channel (the same frame-gated readback the auto-exposure meter
// uses - the CPU never stalls on the GPU). The resulting PNG is the disk cache, keyed by asset id plus
// content hash, so a material is rendered once and survives editor restarts; changing the material changes
// its content hash and therefore its cache entry.
class EditorMaterialThumbnailService {
public:
    // Cached thumbnail at exactly `displaySize` pixels, or nullptr while it is still being produced. A miss
    // queues the render. Scaling here (box filter plus a light sharpen) rather than letting GDI stretch the
    // master keeps texture detail alive at tile sizes, where a plain stretch collapses it into flat colour.
    [[nodiscard]] const EditorMaterialThumbnailImage* ThumbnailFor(const kb::assets::AssetMetadata& metadata, int displaySize);

    // Drives at most one capture; call once per frame. `stagingRect` must lie inside `host` - it is where
    // the material is presented while its thumbnail is captured.
    void Tick(EditorSceneContext& sceneContext, EditorSceneBgfxViewport& viewport, HWND host, const RECT& stagingRect);

    // The post-capture image pipeline, exposed so it can be tested without a GPU: display transform,
    // background punch-out, supersampled downsample to tile resolution, contact shadow.
    static void ProcessCapture(EditorMaterialThumbnailImage& image);

    // Area-filter plus light sharpen to exactly the size a tile draws; exposed for the same reason.
    [[nodiscard]] static EditorMaterialThumbnailImage ScaleForDisplaySize(const EditorMaterialThumbnailImage& source, int size);

    // Bumped whenever a thumbnail becomes available, so panels can invalidate their retained surfaces.
    [[nodiscard]] std::uint64_t Revision() const noexcept;
    [[nodiscard]] bool HasPendingWork() const noexcept;
    void Clear() noexcept;

private:
    enum class EntryState : std::uint8_t {
        Queued,
        Capturing,
        Ready,
        Failed,
    };

    struct Entry {
        std::uint64_t contentHash = 0U;
        EntryState state = EntryState::Queued;
        int attempts = 0;
        EditorMaterialThumbnailImage image;
        std::vector<std::pair<int, EditorMaterialThumbnailImage>> scaled;
    };

    struct Capture {
        std::uint64_t assetId = 0U;
        std::uint64_t contentHash = 0U;
        std::uint64_t captureId = 0U;
        int framesWaited = 0;
        bool presented = false;
    };

    void BeginNextCapture(EditorSceneContext& sceneContext, EditorSceneBgfxViewport& viewport, HWND host, const RECT& stagingRect);
    void PollCapture(EditorSceneContext& sceneContext, EditorSceneBgfxViewport& viewport, HWND host, const RECT& stagingRect);

    std::vector<std::pair<std::uint64_t, Entry>> entries_;
    std::vector<std::uint64_t> queue_;
    Capture capture_{};
    std::uint64_t revision_ = 1U;
};

[[nodiscard]] EditorMaterialThumbnailService& EditorMaterialThumbnailCache();

#endif

} // namespace kb::editor
