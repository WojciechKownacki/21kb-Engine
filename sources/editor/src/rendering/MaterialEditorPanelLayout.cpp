#include "rendering/MaterialEditorPanelRenderer.hpp"

#if defined(_WIN32)
namespace kb::editor {

MaterialEditorPanelLayout MaterialEditorPanelRenderer::ResolveLayout(const RECT& content) noexcept {
    MaterialEditorPanelLayout layout{};
    const int width = std::max(0, static_cast<int>(content.right - content.left));
    layout.headerHeight = MaterialEditorPanelHeaderHeightForWidth(width);
    layout.compactToolbar = width < 600;
    layout.header = RECT{ content.left, content.top, content.right, content.top + layout.headerHeight };
    constexpr int buttonGap = 6;
    const auto placeButton = [](int& cursor, int top, int buttonWidth, int gap) noexcept {
        const RECT rect{ cursor, top, cursor + buttonWidth, top + 26 };
        cursor = rect.right + gap;
        return rect;
    };

    if (width >= 1080) {
        const int buttonTop = content.top + 8;
        const int buttonBottom = buttonTop + 26;
        layout.validateButton = RECT{ content.right - MaterialEditorPanelMetrics::Padding - 72, buttonTop, content.right - MaterialEditorPanelMetrics::Padding, buttonBottom };
        layout.revertButton = RECT{ layout.validateButton.left - buttonGap - 62, buttonTop, layout.validateButton.left - buttonGap, buttonBottom };
        layout.saveButton = RECT{ layout.revertButton.left - buttonGap - 54, buttonTop, layout.revertButton.left - buttonGap, buttonBottom };
        layout.applyButton = RECT{ layout.saveButton.left - buttonGap - 118, buttonTop, layout.saveButton.left - buttonGap, buttonBottom };
        layout.previewNodeButton = RECT{ layout.applyButton.left - buttonGap - 58, buttonTop, layout.applyButton.left - buttonGap, buttonBottom };
        layout.previewNormalButton = RECT{ layout.previewNodeButton.left - buttonGap - 70, buttonTop, layout.previewNodeButton.left - buttonGap, buttonBottom };
        layout.previewQualityButton = RECT{ layout.previewNormalButton.left - buttonGap - 62, buttonTop, layout.previewNormalButton.left - buttonGap, buttonBottom };
        layout.previewSceneButton = RECT{ layout.previewQualityButton.left - buttonGap - 62, buttonTop, layout.previewQualityButton.left - buttonGap, buttonBottom };
        layout.previewPrimitiveButton = RECT{ layout.previewSceneButton.left - buttonGap - 66, buttonTop, layout.previewSceneButton.left - buttonGap, buttonBottom };
        layout.infoButton = RECT{ layout.previewPrimitiveButton.left - buttonGap - 54, buttonTop, layout.previewPrimitiveButton.left - buttonGap, buttonBottom };
        layout.title = RECT{ content.left + MaterialEditorPanelMetrics::Padding, content.top, layout.infoButton.left - 10, content.top + layout.headerHeight };
    } else if (width >= 600) {
        const int previewTop = content.top + 8;
        int previewCursor = content.left + 154;
        layout.title = RECT{ content.left + MaterialEditorPanelMetrics::Padding, content.top, previewCursor - 10, content.top + 42 };
        layout.infoButton = placeButton(previewCursor, previewTop, 48, buttonGap);
        layout.previewPrimitiveButton = placeButton(previewCursor, previewTop, 58, buttonGap);
        layout.previewSceneButton = placeButton(previewCursor, previewTop, 54, buttonGap);
        layout.previewQualityButton = placeButton(previewCursor, previewTop, 54, buttonGap);
        layout.previewNormalButton = placeButton(previewCursor, previewTop, 60, buttonGap);
        layout.previewNodeButton = placeButton(previewCursor, previewTop, 48, buttonGap);
        int actionCursor = content.left + MaterialEditorPanelMetrics::Padding;
        const int actionTop = content.top + 42;
        layout.applyButton = placeButton(actionCursor, actionTop, 112, buttonGap);
        layout.saveButton = placeButton(actionCursor, actionTop, 54, buttonGap);
        layout.revertButton = placeButton(actionCursor, actionTop, 62, buttonGap);
        layout.validateButton = placeButton(actionCursor, actionTop, 72, buttonGap);
    } else {
        layout.title = RECT{ content.left + MaterialEditorPanelMetrics::Padding, content.top, content.right - MaterialEditorPanelMetrics::Padding, content.top + 38 };
        int previewCursor = content.left + MaterialEditorPanelMetrics::Padding;
        const int previewTop = content.top + 38;
        layout.infoButton = placeButton(previewCursor, previewTop, 42, 4);
        layout.previewPrimitiveButton = placeButton(previewCursor, previewTop, 52, 4);
        layout.previewSceneButton = placeButton(previewCursor, previewTop, 46, 4);
        layout.previewQualityButton = placeButton(previewCursor, previewTop, 46, 4);
        layout.previewNormalButton = placeButton(previewCursor, previewTop, 52, 4);
        layout.previewNodeButton = placeButton(previewCursor, previewTop, 42, 4);
        int actionCursor = content.left + MaterialEditorPanelMetrics::Padding;
        const int actionTop = content.top + 72;
        layout.applyButton = placeButton(actionCursor, actionTop, 88, 4);
        layout.saveButton = placeButton(actionCursor, actionTop, 46, 4);
        layout.revertButton = placeButton(actionCursor, actionTop, 54, 4);
        layout.validateButton = placeButton(actionCursor, actionTop, 62, 4);
    }

    layout.graphCanvas = RECT{ content.left, content.top + layout.headerHeight, content.right, content.bottom };
    const int overlayLeft = layout.graphCanvas.left + MaterialEditorPanelMetrics::Padding;
    const int overlayTop = layout.graphCanvas.top + MaterialEditorPanelMetrics::Padding;
    layout.previewFrame = RECT{ overlayLeft, overlayTop, overlayLeft + (layout.compactToolbar ? 116 : MaterialEditorPanelMetrics::PreviewWidth), overlayTop + (layout.compactToolbar ? 76 : MaterialEditorPanelMetrics::PreviewHeight) };
    layout.diagnosticsPanel = RECT{ std::max(layout.previewFrame.right + MaterialEditorPanelMetrics::Padding, layout.graphCanvas.right - 372), overlayTop, layout.graphCanvas.right - MaterialEditorPanelMetrics::Padding, std::min(static_cast<int>(layout.graphCanvas.bottom - MaterialEditorPanelMetrics::Padding), overlayTop + 132) };
    layout.detailsPanel = RECT{ layout.diagnosticsPanel.left, layout.diagnosticsPanel.bottom + MaterialEditorPanelMetrics::Padding, layout.diagnosticsPanel.right, layout.graphCanvas.bottom - MaterialEditorPanelMetrics::Padding };
    return layout;
}

MaterialEditorPanelCommand MaterialEditorPanelRenderer::CommandAt(const RECT& content, int x, int y) noexcept {
    const MaterialEditorPanelLayout layout = ResolveLayout(content);
    if (MaterialEditorPanelPointInRect(layout.infoButton, x, y)) return MaterialEditorPanelCommand::Info;
    if (MaterialEditorPanelPointInRect(layout.applyButton, x, y)) return MaterialEditorPanelCommand::ApplyToSelection;
    if (MaterialEditorPanelPointInRect(layout.previewPrimitiveButton, x, y)) return MaterialEditorPanelCommand::PreviewPrimitive;
    if (MaterialEditorPanelPointInRect(layout.previewSceneButton, x, y)) return MaterialEditorPanelCommand::PreviewScene;
    if (MaterialEditorPanelPointInRect(layout.previewQualityButton, x, y)) return MaterialEditorPanelCommand::PreviewQuality;
    if (MaterialEditorPanelPointInRect(layout.previewNormalButton, x, y)) return MaterialEditorPanelCommand::PreviewNormal;
    if (MaterialEditorPanelPointInRect(layout.previewNodeButton, x, y)) return MaterialEditorPanelCommand::PreviewNode;
    if (MaterialEditorPanelPointInRect(layout.saveButton, x, y)) return MaterialEditorPanelCommand::Save;
    if (MaterialEditorPanelPointInRect(layout.revertButton, x, y)) return MaterialEditorPanelCommand::Revert;
    if (MaterialEditorPanelPointInRect(layout.validateButton, x, y)) return MaterialEditorPanelCommand::Validate;
    return MaterialEditorPanelCommand::None;
}

} // namespace kb::editor
#endif
