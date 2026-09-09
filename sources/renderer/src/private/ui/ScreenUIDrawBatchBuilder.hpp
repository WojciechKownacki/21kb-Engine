#pragma once

#include "private/ui/ScreenUIDrawData.hpp"

#include <span>

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

    ScreenUIDrawList drawList_;
    std::uint32_t viewportWidth_ = 0U;
    std::uint32_t viewportHeight_ = 0U;
};

} // namespace kb::render
