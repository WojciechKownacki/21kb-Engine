#include "rendering/EditorSceneBgfxViewport.hpp"

#if defined(_WIN32)
#include "rendering/EditorSceneViewportGeometry.hpp"

#include <span>

namespace kb::editor {
namespace {

[[nodiscard]] std::uint32_t RectWidth(const RECT& rect) noexcept {
    return EditorSceneViewportGeometry::RectWidth(rect);
}

[[nodiscard]] std::uint32_t RectHeight(const RECT& rect) noexcept {
    return EditorSceneViewportGeometry::RectHeight(rect);
}

} // namespace

EditorSceneBgfxViewport::PendingPaintSubmitter::PendingPaintSubmitter(EditorSceneBgfxViewport& viewport) noexcept
    : viewport_(viewport) {}

bool EditorSceneBgfxViewport::PendingPaintSubmitter::Submit(std::span<const PendingPresent> pendingPresents) {
    const std::vector<PendingPresentBatch> batches = PendingPresentBatchBuilder::Build(pendingPresents);
    if (!BuildPendingSubmissions(std::span<const PendingPresentBatch>{batches.data(), batches.size()})) {
        return false;
    }
    if (!SubmitPreparedSubmissions()) {
        return false;
    }

    viewport_.hostSurfaceStore_.ShowPresentedWindows();
    return true;
}

bool EditorSceneBgfxViewport::PendingPaintSubmitter::BuildPendingSubmissions(std::span<const PendingPresentBatch> batches) {
    for (const PendingPresentBatch& batch : batches) {
        HostSurface* surface = nullptr;
        if (!PrepareHostSurfaceBatch(batch, surface)) {
            return false;
        }
        if (surface != nullptr && !AppendHostSubmissions(batch, *surface)) {
            return false;
        }
    }
    return true;
}

bool EditorSceneBgfxViewport::PendingPaintSubmitter::PrepareHostSurfaceBatch(const PendingPresentBatch& batch, HostSurface*& surface) {
    surface = viewport_.EnsureHostSurface(batch.host, batch.viewportKey);
    if (surface == nullptr) {
        return false;
    }
    if (surface->presentedInCurrentPaint) {
        surface = nullptr;
        return true;
    }

    if (!viewport_.EnsureHostSurfaceWindow(
            *surface,
            batch.surfaceRect,
            std::span<const PendingPresent* const>{batch.presents.data(), batch.presents.size()})) {
        return false;
    }

    if (!viewport_.EnsurePresentTarget(*surface, RectWidth(surface->rect), RectHeight(surface->rect))) {
        return false;
    }
    surface->presentedInCurrentPaint = true;
    return true;
}

bool EditorSceneBgfxViewport::PendingPaintSubmitter::AppendHostSubmissions(const PendingPresentBatch& batch, const HostSurface& surface) {
    bool clearTarget = true;
    for (const PendingPresent* present : batch.presents) {
        if (present == nullptr) {
            continue;
        }
        render::Renderer::SceneFrameSubmission submission{};
        if (!PendingSubmissionBuilder::Build(*present, surface, clearTarget, submission)) {
            return false;
        }
        viewport_.pendingSubmissions_.push_back(submission);
        clearTarget = false;
    }
    return true;
}

bool EditorSceneBgfxViewport::PendingPaintSubmitter::SubmitPreparedSubmissions() {
    if (viewport_.pendingSubmissions_.empty()) {
        return true;
    }
    if (!viewport_.renderer_.BeginFrame()) {
        return false;
    }

    const bool submitted = viewport_.renderer_.SubmitScenes(viewport_.pendingSubmissions_);
    viewport_.renderer_.EndFrame();
    if (!submitted) {
        return false;
    }
    return true;
}

} // namespace kb::editor

#endif
