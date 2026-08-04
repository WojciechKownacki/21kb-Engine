#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)
struct AnimatorEditorPanelLayout {
    RECT preview{};
    RECT graph{};
    RECT details{};
};

class AnimatorEditorPanelLayoutResolver {
public:
    [[nodiscard]] static AnimatorEditorPanelLayout Resolve(const RECT& content) noexcept {
        const int width = content.right > content.left ? content.right - content.left : 0;
        const int previewWidth = width / 4;
        const int graphWidth = (width * 55) / 100;
        return AnimatorEditorPanelLayout{
            .preview = { content.left, content.top, content.left + previewWidth, content.bottom },
            .graph = { content.left + previewWidth, content.top, content.left + previewWidth + graphWidth, content.bottom },
            .details = { content.left + previewWidth + graphWidth, content.top, content.right, content.bottom },
        };
    }
};
#endif

} // namespace kb::editor
