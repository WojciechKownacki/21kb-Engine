#pragma once

#include "private/ui/ScreenUIDrawData.hpp"

#include <span>
#include <unordered_map>
#include <vector>

namespace kb::scene {
struct SceneUIFrame;
struct SceneUIFrameElement;
} // namespace kb::scene

namespace kb::render {

class ScreenUIDrawBatchBuilder {
  public:
    [[nodiscard]] const ScreenUIDrawList& Build(const kb::scene::SceneUIFrame& frame,
                                                std::span<const ScreenUIImageBinding> images,
                                                std::span<const ScreenUITextRun> textRuns);

  private:
    enum class ImageKind {
        Sprite,
        Image,
        RawImage,
    };

    void AppendElement(const kb::scene::SceneUIFrameElement& element, std::span<const ScreenUIImageBinding> images,
                       std::span<const ScreenUITextRun> textRuns);
    void AppendShape(const kb::scene::SceneUIFrameElement& element, const ScreenUIRect& rect,
                     const ScreenUIDrawStyle& style);
    void AppendImage(const kb::scene::SceneUIFrameElement& element, const ScreenUIImageBinding& image,
                     std::uint64_t assetId, ImageKind kind);
    void AppendText(const kb::scene::SceneUIFrameElement& element, const ScreenUITextRun& run);
    void AppendTextCaret(const kb::scene::SceneUIFrameElement& element, const ScreenUITextRun& run);
    void AppendTextLayer(const kb::scene::SceneUIFrameElement& element, const ScreenUITextRun& run,
                         const std::array<float, 4>& color, const std::array<float, 4>& outlineColor, float offsetX,
                         float offsetY, float outlineWidth, bool applyMarkupColor);
    void AppendQuad(const kb::scene::SceneUIFrameElement& element, const ScreenUIRect& fullRect,
                    const ScreenUIRect& quadRect, float u0, float v0, float u1, float v1,
                    const ScreenUITextureKey& texture, const ScreenUIDrawStyle& style);
    // A convex polygon in element space, drawn as a fan from its first point; each point carries its uv.
    struct PolygonPoint {
        float x = 0.0F;
        float y = 0.0F;
        float u = 0.0F;
        float v = 0.0F;
    };
    void AppendPolygon(const kb::scene::SceneUIFrameElement& element, const ScreenUIRect& fullRect,
                       std::span<const PolygonPoint> points, const ScreenUITextureKey& texture,
                       const ScreenUIDrawStyle& style);
    // Adds a run of indices to the batch of its state that it can join without changing what is drawn over what.
    void AppendIndices(const kb::scene::SceneUIFrameElement& element, std::uint32_t firstIndex, std::uint32_t count,
                       const ScreenUITextureKey& texture, const ScreenUIDrawStyle& style, const ScreenUIRect& bounds);
    void GatherReorderedIndices();

    // A run of indices and the batch it belongs to.
    struct IndexPiece {
        std::uint32_t firstIndex = 0U;
        std::uint32_t count = 0U;
        std::uint32_t batch = 0U;
    };

    ScreenUIDrawList drawList_;
    // Where the last text run was found, so the next lookup starts after it.
    std::size_t textRunCursor_ = 0U;
    // Per batch, a summary of its state; every run of indices with its batch and the area it covers.
    std::vector<std::uint64_t> batchKeys_;
    // The latest batch of each state, by its summary.
    std::unordered_map<std::uint64_t, std::uint32_t> latestBatchOfKey_;
    std::vector<IndexPiece> pieces_;
    std::vector<ScreenUIRect> pieceBounds_;
    bool reordered_ = false;
    // The surface in squares, each listing the runs that reach into it, and the squares listing any.
    float gridCell_ = 32.0F;
    std::size_t gridColumns_ = 1U;
    std::size_t gridRows_ = 1U;
    std::vector<std::vector<std::uint32_t>> cellPieces_;
    std::vector<std::uint32_t> occupiedCells_;
    std::vector<std::uint32_t> batchWriteOffsets_;
    std::vector<std::uint32_t> gatheredIndices_;
    std::uint32_t viewportWidth_ = 0U;
    std::uint32_t viewportHeight_ = 0U;
};

} // namespace kb::render
