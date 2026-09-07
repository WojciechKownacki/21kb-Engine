#pragma once

#include "private/ui/UserWidgetDrawData.hpp"

#include <span>

namespace kb::render {

class UserWidgetDrawBatchBuilder {
  public:
    [[nodiscard]] const UserWidgetDrawList& Build(const kb::scene::UIPresentationSnapshot& snapshot,
                                                  std::span<const UserWidgetImageBinding> images,
                                                  std::span<const UserWidgetTextRun> textRuns);

  private:
    void AppendItem(const kb::scene::UIPresentationItem& item, std::span<const UserWidgetImageBinding> images,
                    std::span<const UserWidgetTextRun> textRuns);
    void AppendShape(const kb::scene::UIPresentationItem& item, const kb::scene::UIPresentationRect& rect,
                     const UserWidgetDrawStyle& style);
    void AppendControlPrimitives(const kb::scene::UIPresentationItem& item, bool hasAuthoredPaint);
    void AppendImage(const kb::scene::UIPresentationItem& item, const UserWidgetImageBinding& image);
    void AppendText(const kb::scene::UIPresentationItem& item, const UserWidgetTextRun& run);
    void AppendTextLayer(const kb::scene::UIPresentationItem& item, const UserWidgetTextRun& run,
                         const std::array<float, 4>& color, const std::array<float, 4>& outlineColor, float offsetX,
                         float offsetY, float outlineWidth);
    void AppendQuad(const kb::scene::UIPresentationItem& item, const kb::scene::UIPresentationRect& fullRect,
                    const kb::scene::UIPresentationRect& quadRect, float u0, float v0, float u1, float v1,
                    const UserWidgetTextureKey& texture, const UserWidgetDrawStyle& style);

    UserWidgetDrawList drawList_;
    std::uint32_t viewportWidth_ = 0U;
    std::uint32_t viewportHeight_ = 0U;
};

} // namespace kb::render
