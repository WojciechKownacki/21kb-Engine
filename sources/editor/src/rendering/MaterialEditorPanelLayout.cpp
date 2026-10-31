#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "rendering/EditorAssetEditScope.hpp"
#include "rendering/gdi/EditorTextMeasure.hpp"

#include <initializer_list>
#include <string>
#include <string_view>

#if defined(_WIN32)
namespace kb::editor {

std::string_view MaterialEditorPanelRenderer::PreviewPrimitiveLabel(EditorMaterialPreviewPrimitiveKind kind) {
    switch (kind) {
    case EditorMaterialPreviewPrimitiveKind::Sphere: return EditorText("materialEditor.preview.sphere", "Sphere");
    case EditorMaterialPreviewPrimitiveKind::Cylinder: return EditorText("materialEditor.preview.cylinder", "Cylinder");
    case EditorMaterialPreviewPrimitiveKind::Cube: return EditorText("materialEditor.preview.cube", "Cube");
    case EditorMaterialPreviewPrimitiveKind::Plane: return EditorText("materialEditor.preview.plane", "Plane");
    case EditorMaterialPreviewPrimitiveKind::CustomMesh: return EditorText("materialEditor.preview.custom", "Custom");
    case EditorMaterialPreviewPrimitiveKind::Fallback: return EditorText("materialEditor.preview.fallback", "Fallback");
    }
    return EditorText("materialEditor.preview.preview", "Preview");
}

std::string_view MaterialEditorPanelRenderer::PreviewSceneLabel(EditorMaterialPreviewLightingPreset preset) {
    switch (preset) {
    case EditorMaterialPreviewLightingPreset::Studio: return EditorText("materialEditor.preview.studio", "Studio");
    case EditorMaterialPreviewLightingPreset::Neutral: return EditorText("materialEditor.preview.neutral", "Neutral");
    case EditorMaterialPreviewLightingPreset::HighContrast: return EditorText("materialEditor.preview.highContrast", "High");
    }
    return EditorText("materialEditor.preview.scene", "Scene");
}

std::string_view MaterialEditorPanelRenderer::PreviewQualityLabel(kb::render::RenderMaterialGraphQualityLevel qualityLevel) {
    switch (qualityLevel) {
    case kb::render::RenderMaterialGraphQualityLevel::Low: return EditorText("materialEditor.quality.low", "Low");
    case kb::render::RenderMaterialGraphQualityLevel::Medium: return EditorText("materialEditor.quality.medium", "Med");
    case kb::render::RenderMaterialGraphQualityLevel::High: return EditorText("materialEditor.quality.high", "High");
    case kb::render::RenderMaterialGraphQualityLevel::Epic: return EditorText("materialEditor.quality.epic", "Epic");
    }
    return EditorText("materialEditor.quality.quality", "Quality");
}

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
    // Each button is as wide as its widest label in the editor's language (11 pt, 8 px each side),
    // never narrower than the width it was drawn for in English.
    constexpr int kLabelPadding = 18;
    // The three preview choices carry a category icon before the value and a chevron after it.
    constexpr int kPreviewChoiceRoom = 30;
    const auto fit = [](int minimum, std::initializer_list<std::string_view> labels) noexcept {
        return FitTextWidth(minimum, labels, 11, FW_NORMAL, kLabelPadding);
    };
    using Primitive = EditorMaterialPreviewPrimitiveKind;
    using Lighting = EditorMaterialPreviewLightingPreset;
    using Quality = kb::render::RenderMaterialGraphQualityLevel;
    const std::initializer_list<std::string_view> primitiveLabels{
        PreviewPrimitiveLabel(Primitive::Sphere), PreviewPrimitiveLabel(Primitive::Cylinder),
        PreviewPrimitiveLabel(Primitive::Cube), PreviewPrimitiveLabel(Primitive::Plane),
        PreviewPrimitiveLabel(Primitive::CustomMesh), PreviewPrimitiveLabel(Primitive::Fallback) };
    const std::initializer_list<std::string_view> sceneLabels{
        PreviewSceneLabel(Lighting::Studio), PreviewSceneLabel(Lighting::Neutral), PreviewSceneLabel(Lighting::HighContrast) };
    const std::initializer_list<std::string_view> qualityLabels{
        PreviewQualityLabel(Quality::Low), PreviewQualityLabel(Quality::Medium), PreviewQualityLabel(Quality::High),
        PreviewQualityLabel(Quality::Epic) };
    const std::string_view infoLabel = EditorText("materialEditor.info", "Info");
    const std::string_view normalLabel = EditorText("materialEditor.normalDebug", "Normal");
    const std::string_view nodeLabel = EditorText("materialEditor.nodePreview", "Node");
    const std::string_view applyLabel = EditorText("materialEditor.applyToSelection", "Apply To Selection");
    const std::string_view applyShortLabel = EditorText("common.apply", "Apply");
    const std::string_view saveLabel = EditorText("common.save", "Save");
    const std::string_view revertLabel = EditorText("materialEditor.revert", "Revert");
    const std::string_view validateLabel = EditorText("materialEditor.validate", "Validate");
    // The title (14 pt semibold) and its unsaved mark.
    const int titleWidth = MeasureTextWidth(std::string{ EditorText("panel.materialEditor", "Material Editor") } + "  *", 14, FW_SEMIBOLD);

    if (width >= 1080) {
        const int buttonTop = content.top + 8;
        const int buttonBottom = buttonTop + 26;
        layout.validateButton = RECT{ content.right - MaterialEditorPanelMetrics::Padding - fit(72, { validateLabel }), buttonTop, content.right - MaterialEditorPanelMetrics::Padding, buttonBottom };
        layout.revertButton = RECT{ layout.validateButton.left - buttonGap - fit(62, { revertLabel }), buttonTop, layout.validateButton.left - buttonGap, buttonBottom };
        layout.saveButton = RECT{ layout.revertButton.left - buttonGap - fit(54, { saveLabel }), buttonTop, layout.revertButton.left - buttonGap, buttonBottom };
        layout.applyButton = RECT{ layout.saveButton.left - buttonGap - fit(118, { applyLabel }), buttonTop, layout.saveButton.left - buttonGap, buttonBottom };
        layout.previewNodeButton = RECT{ layout.applyButton.left - buttonGap - fit(58, { nodeLabel }), buttonTop, layout.applyButton.left - buttonGap, buttonBottom };
        layout.previewNormalButton = RECT{ layout.previewNodeButton.left - buttonGap - fit(70, { normalLabel }), buttonTop, layout.previewNodeButton.left - buttonGap, buttonBottom };
        layout.previewQualityButton = RECT{ layout.previewNormalButton.left - buttonGap - fit(62, qualityLabels) - kPreviewChoiceRoom, buttonTop, layout.previewNormalButton.left - buttonGap, buttonBottom };
        layout.previewSceneButton = RECT{ layout.previewQualityButton.left - buttonGap - fit(62, sceneLabels) - kPreviewChoiceRoom, buttonTop, layout.previewQualityButton.left - buttonGap, buttonBottom };
        layout.previewPrimitiveButton = RECT{ layout.previewSceneButton.left - buttonGap - fit(66, primitiveLabels) - kPreviewChoiceRoom, buttonTop, layout.previewSceneButton.left - buttonGap, buttonBottom };
        layout.infoButton = RECT{ layout.previewPrimitiveButton.left - buttonGap - fit(54, { infoLabel }), buttonTop, layout.previewPrimitiveButton.left - buttonGap, buttonBottom };
        layout.title = RECT{ content.left + MaterialEditorPanelMetrics::Padding, content.top, layout.infoButton.left - 10, content.top + layout.headerHeight };
    } else if (width >= 600) {
        const int previewTop = content.top + 8;
        int previewCursor = content.left + std::max(154, MaterialEditorPanelMetrics::Padding + titleWidth + 10);
        layout.title = RECT{ content.left + MaterialEditorPanelMetrics::Padding, content.top, previewCursor - 10, content.top + 42 };
        layout.infoButton = placeButton(previewCursor, previewTop, fit(48, { infoLabel }), buttonGap);
        layout.previewPrimitiveButton = placeButton(previewCursor, previewTop, fit(58, primitiveLabels) + kPreviewChoiceRoom, buttonGap);
        layout.previewSceneButton = placeButton(previewCursor, previewTop, fit(54, sceneLabels) + kPreviewChoiceRoom, buttonGap);
        layout.previewQualityButton = placeButton(previewCursor, previewTop, fit(54, qualityLabels) + kPreviewChoiceRoom, buttonGap);
        layout.previewNormalButton = placeButton(previewCursor, previewTop, fit(60, { normalLabel }), buttonGap);
        layout.previewNodeButton = placeButton(previewCursor, previewTop, fit(48, { nodeLabel }), buttonGap);
        int actionCursor = content.left + MaterialEditorPanelMetrics::Padding;
        const int actionTop = content.top + 42;
        layout.applyButton = placeButton(actionCursor, actionTop, fit(112, { applyLabel }), buttonGap);
        layout.saveButton = placeButton(actionCursor, actionTop, fit(54, { saveLabel }), buttonGap);
        layout.revertButton = placeButton(actionCursor, actionTop, fit(62, { revertLabel }), buttonGap);
        layout.validateButton = placeButton(actionCursor, actionTop, fit(72, { validateLabel }), buttonGap);
    } else {
        layout.title = RECT{ content.left + MaterialEditorPanelMetrics::Padding, content.top, content.right - MaterialEditorPanelMetrics::Padding, content.top + 38 };
        int previewCursor = content.left + MaterialEditorPanelMetrics::Padding;
        const int previewTop = content.top + 38;
        layout.infoButton = placeButton(previewCursor, previewTop, fit(42, { infoLabel }), 4);
        layout.previewPrimitiveButton = placeButton(previewCursor, previewTop, fit(52, primitiveLabels) + kPreviewChoiceRoom, 4);
        layout.previewSceneButton = placeButton(previewCursor, previewTop, fit(46, sceneLabels) + kPreviewChoiceRoom, 4);
        layout.previewQualityButton = placeButton(previewCursor, previewTop, fit(46, qualityLabels) + kPreviewChoiceRoom, 4);
        layout.previewNormalButton = placeButton(previewCursor, previewTop, fit(52, { normalLabel }), 4);
        layout.previewNodeButton = placeButton(previewCursor, previewTop, fit(42, { nodeLabel }), 4);
        int actionCursor = content.left + MaterialEditorPanelMetrics::Padding;
        const int actionTop = content.top + 72;
        layout.applyButton = placeButton(actionCursor, actionTop, fit(88, { applyShortLabel }), 4);
        layout.saveButton = placeButton(actionCursor, actionTop, fit(46, { saveLabel }), 4);
        layout.revertButton = placeButton(actionCursor, actionTop, fit(54, { revertLabel }), 4);
        layout.validateButton = placeButton(actionCursor, actionTop, fit(62, { validateLabel }), 4);
    }

    layout.headerHeight += kAssetEditScopeHeight;
    layout.header.bottom += kAssetEditScopeHeight;
    layout.graphCanvas = RECT{ content.left, content.top + layout.headerHeight, content.right, content.bottom };
    const int overlayLeft = layout.graphCanvas.left + MaterialEditorPanelMetrics::Padding;
    const int overlayTop = layout.graphCanvas.top + MaterialEditorPanelMetrics::Padding;
    const int previewDesiredWidth = layout.compactToolbar ? 176 : MaterialEditorPanelMetrics::PreviewWidth;
    const int previewDesiredHeight = layout.compactToolbar ? 140 : MaterialEditorPanelMetrics::PreviewHeight;
    // Clamp the preview to this panel's own content. The preview is a bgfx child surface parented to the
    // host window, NOT to this GDI panel, so a fixed-size frame that runs past the panel's right/bottom edge
    // would render OVER the neighbouring docked panel (a narrow Material Editor dock showed the sphere
    // spilling onto the Inspector). Never let it exceed the graph canvas bounds; if the panel is too narrow
    // to hold it, the frame collapses to empty and the preview simply isn't presented.
    const int previewRight = std::min(overlayLeft + previewDesiredWidth, static_cast<int>(layout.graphCanvas.right) - MaterialEditorPanelMetrics::Padding);
    const int previewBottom = std::min(overlayTop + previewDesiredHeight, static_cast<int>(layout.graphCanvas.bottom) - MaterialEditorPanelMetrics::Padding);
    layout.previewFrame = RECT{ overlayLeft, overlayTop, std::max(overlayLeft, previewRight), std::max(overlayTop, previewBottom) };
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
