#include "rendering/UserWidgetEditorPanelRenderer.hpp"

#if defined(_WIN32)

#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/ui/layout/UIPresentationLayout.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "scene/EditorSceneContext.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {
namespace {

constexpr int kToolbarHeight = 36;
constexpr int kButtonWidth = 66;
constexpr int kButtonGap = 6;
constexpr int kRowHeight = 20;
constexpr int kHierarchyWidth = 240;
constexpr int kDetailsWidth = 350;
constexpr int kDetailsHeaderHeight = 42;

struct HierarchyRow {
    kb::scene::UIElementId elementId = 0U;
    int depth = 0;
    RECT bounds{};
};

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] RECT ToolbarButton(const RECT& content, int index) noexcept {
    const int left = content.left + 8 + index * (kButtonWidth + kButtonGap);
    return RECT{left, content.top + 5, left + kButtonWidth, content.top + 31};
}

[[nodiscard]] RECT HierarchyRect(const RECT& content) noexcept {
    const int right = std::min(content.right, content.left + kHierarchyWidth);
    return RECT{content.left, content.top + kToolbarHeight, right, content.bottom};
}

[[nodiscard]] RECT DetailsRect(const RECT& content) noexcept {
    const RECT hierarchy = HierarchyRect(content);
    const int left = std::max(hierarchy.right, content.right - kDetailsWidth);
    return RECT{left, content.top + kToolbarHeight, content.right, content.bottom};
}

[[nodiscard]] RECT PreviewRect(const RECT& content) noexcept {
    const RECT hierarchy = HierarchyRect(content);
    const RECT details = DetailsRect(content);
    return RECT{hierarchy.right + 10, content.top + kToolbarHeight + 10,
                std::max(hierarchy.right + 10, details.left - 10), content.bottom - 10};
}

[[nodiscard]] kb::scene::UIPresentationSnapshot BuildPreview(const RECT& content,
                                                             const UserWidgetEditorDocument& editor) {
    kb::scene::UIPresentationSnapshot snapshot;
    const kb::scene::UIDocument* document = editor.Document();
    const RECT preview = PreviewRect(content);
    const LONG width = std::max<LONG>(0, preview.right - preview.left);
    const LONG height = std::max<LONG>(0, preview.bottom - preview.top);
    if (document != nullptr && width > 0 && height > 0) {
        kb::scene::UIPresentationLayout::Build(*document, static_cast<std::uint32_t>(width),
                                               static_cast<std::uint32_t>(height), snapshot);
    }
    return snapshot;
}

[[nodiscard]] RECT ItemRect(const RECT& preview, const kb::scene::UIPresentationRect& rect) noexcept {
    return RECT{
        preview.left + static_cast<LONG>(std::lround(rect.left)),
        preview.top + static_cast<LONG>(std::lround(rect.top)),
        preview.left + static_cast<LONG>(std::lround(rect.right)),
        preview.top + static_cast<LONG>(std::lround(rect.bottom)),
    };
}

[[nodiscard]] COLORREF UiColor(kb::math::Color color) noexcept {
    const auto channel = [](float value) {
        return static_cast<BYTE>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
    };
    return RGB(channel(color.r), channel(color.g), channel(color.b));
}

void DrawText(HDC dc, RECT rect, std::string_view text, COLORREF color, int pointSize = 11, int weight = FW_NORMAL);

void DrawFrame(HDC dc, RECT rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    if (brush == nullptr)
        return;
    FrameRect(dc, &rect, brush);
    DeleteObject(brush);
}

void DrawPreview(HDC dc, const RECT& preview, const kb::scene::UIPresentationSnapshot& snapshot,
                 kb::scene::UIElementId selectedElementId, const EditorTheme& theme) {
    GdiDrawing::DrawSharpFrame(dc, preview, GdiDrawing::ToColorRef(theme.background),
                               GdiDrawing::ToColorRef(theme.borderPanel));
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, preview.left, preview.top, preview.right, preview.bottom);
    for (const kb::scene::UIPresentationItem& item : snapshot.items) {
        RECT rect = ItemRect(preview, item.rect);
        if (rect.right <= rect.left || rect.bottom <= rect.top)
            continue;
        if (item.paint) {
            GdiDrawing::FillRectColor(dc, rect, UiColor(item.paint->backgroundColor));
            DrawFrame(dc, rect, UiColor(item.paint->borderColor));
        } else {
            FrameRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
        }
        if (item.image) {
            MoveToEx(dc, rect.left, rect.top, nullptr);
            LineTo(dc, rect.right, rect.bottom);
            MoveToEx(dc, rect.right, rect.top, nullptr);
            LineTo(dc, rect.left, rect.bottom);
        }
        if (!item.text.empty()) {
            DrawText(dc, RECT{rect.left + 4, rect.top + 2, rect.right - 4, rect.bottom - 2}, item.text,
                     item.textStyle ? UiColor(item.textStyle->color) : GdiDrawing::ToColorRef(theme.textPrimary));
        }
        if (item.elementId == selectedElementId) {
            DrawFrame(dc, rect, GdiDrawing::ToColorRef(theme.accent));
        }
    }
    RestoreDC(dc, saved);
}

void AppendRows(const kb::scene::UIDocument& document, kb::scene::UIElementId parentId, int depth,
                const RECT& hierarchy, std::vector<HierarchyRow>& rows) {
    std::vector<const kb::scene::UIDocumentElement*> children;
    for (const kb::scene::UIDocumentElement& element : document.elements) {
        if (element.parentId == parentId)
            children.push_back(&element);
    }
    std::ranges::sort(children, {}, [](const auto* element) { return element->siblingOrder; });
    for (const kb::scene::UIDocumentElement* element : children) {
        const int top = hierarchy.top + static_cast<int>(rows.size()) * kRowHeight;
        rows.push_back(HierarchyRow{
            .elementId = element->id,
            .depth = depth,
            .bounds = RECT{hierarchy.left, top, hierarchy.right, top + kRowHeight},
        });
        AppendRows(document, element->id, depth + 1, hierarchy, rows);
    }
}

[[nodiscard]] std::vector<HierarchyRow> BuildRows(const RECT& content, const UserWidgetEditorDocument& editor) {
    std::vector<HierarchyRow> rows;
    const kb::scene::UIDocument* document = editor.Document();
    if (document == nullptr)
        return rows;
    AppendRows(*document, 0U, 0, HierarchyRect(content), rows);
    return rows;
}

void DrawText(HDC dc, RECT rect, std::string_view text, COLORREF color, int pointSize, int weight) {
    ScopedFont font{pointSize, weight};
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text.data(), static_cast<int>(text.size()), &rect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void DrawButton(HDC dc, const RECT& rect, std::string_view label, bool enabled, const EditorTheme& theme) {
    GdiDrawing::DrawSharpFrame(dc, rect, GdiDrawing::ToColorRef(theme.chrome),
                               GdiDrawing::ToColorRef(theme.borderPanel));
    DrawText(dc, RECT{rect.left + 8, rect.top, rect.right - 8, rect.bottom}, label,
             GdiDrawing::ToColorRef(enabled ? theme.textPrimary : theme.textDisabled), 10, FW_SEMIBOLD);
}

void DrawProperty(HDC dc, const RECT& details, int& y, const UserWidgetEditorPropertyRow& row,
                  const EditorTheme& theme) {
    if (y + kRowHeight > details.bottom)
        return;
    DrawText(dc, RECT{details.left + 12, y, details.left + 154, y + kRowHeight}, row.label,
             GdiDrawing::ToColorRef(theme.textSecondary));
    DrawText(dc, RECT{details.left + 158, y, details.right - 12, y + kRowHeight}, row.value,
             GdiDrawing::ToColorRef(theme.textPrimary));
    y += kRowHeight;
}

void DrawDetails(HDC dc, const RECT& details, const kb::scene::UIDocumentElement& element,
                 const EditorSceneContext& sceneContext, const EditorTheme& theme) {
    int y = details.top + 8;
    DrawText(dc, RECT{details.left + 12, y, details.right - 12, y + 28}, element.name,
             GdiDrawing::ToColorRef(theme.textPrimary), 14, FW_SEMIBOLD);
    y += kDetailsHeaderHeight;
    for (const UserWidgetEditorPropertyRow& row : UserWidgetEditorPropertyAdapter::Rows(element)) {
        UserWidgetEditorPropertyRow display = row;
        std::uint64_t assetId = 0U;
        if (row.property == UserWidgetEditorProperty::Image && element.image) {
            assetId = element.image->imageAssetId;
        } else if (row.property == UserWidgetEditorProperty::TextStyle && element.textStyle) {
            assetId = element.textStyle->fontAssetId;
        }
        if (assetId != 0U) {
            const kb::assets::AssetMetadata* metadata =
                sceneContext.Scene().Assets().Manager().Registry().Find(kb::assets::AssetId{assetId});
            display.value = metadata != nullptr ? metadata->name + " | " + row.value
                                                : "Missing #" + std::to_string(assetId) + " | " + row.value;
        }
        DrawProperty(dc, details, y, display, theme);
    }
}

} // namespace

void UserWidgetEditorPanelRenderer::Paint(HDC dc, const RECT& content, const EditorTheme& theme,
                                          EditorSceneContext& sceneContext) const {
    const UserWidgetEditorDocument& editor = sceneContext.UserWidgetEditor();
    GdiDrawing::FillRectColor(dc, content, GdiDrawing::ToColorRef(theme.panel));
    GdiDrawing::FillRectColor(dc, RECT{content.left, content.top, content.right, content.top + kToolbarHeight},
                              GdiDrawing::ToColorRef(theme.strip));
    DrawButton(dc, ToolbarButton(content, 0), "Save", editor.HasOpenDocument(), theme);
    DrawButton(dc, ToolbarButton(content, 1), "Undo", editor.CanUndo(), theme);
    DrawButton(dc, ToolbarButton(content, 2), "Redo", editor.CanRedo(), theme);
    DrawButton(dc, ToolbarButton(content, 3), "Add", editor.HasOpenDocument(), theme);
    const bool canRemove = editor.SelectedElement() != nullptr && editor.SelectedElement()->parentId != 0U;
    DrawButton(dc, ToolbarButton(content, 4), "Remove", canRemove, theme);
    DrawButton(dc, ToolbarButton(content, 5), "Parent", canRemove, theme);

    if (!editor.HasOpenDocument()) {
        DrawText(dc, RECT{content.left + 16, content.top + 48, content.right - 16, content.bottom - 16},
                 "Create or open a .kbui asset from Project Files.", GdiDrawing::ToColorRef(theme.textSecondary), 12);
        return;
    }

    const RECT hierarchy = HierarchyRect(content);
    GdiDrawing::FillRectColor(dc, hierarchy, GdiDrawing::ToColorRef(theme.chrome));
    GdiDrawing::FillRectColor(dc, RECT{hierarchy.right - 1, hierarchy.top, hierarchy.right, hierarchy.bottom},
                              GdiDrawing::ToColorRef(theme.borderPanel));
    const std::vector<HierarchyRow> rows = BuildRows(content, editor);
    const kb::scene::UIDocument* document = editor.Document();
    for (const HierarchyRow& row : rows) {
        if (row.elementId == editor.SelectedElementId()) {
            GdiDrawing::FillRectColor(dc, row.bounds, GdiDrawing::ToColorRef(theme.accent));
        }
        const auto found = std::ranges::find(document->elements, row.elementId, &kb::scene::UIDocumentElement::id);
        if (found == document->elements.end())
            continue;
        const int indent = 12 + row.depth * 16;
        const std::string label = found->name + "  #" + std::to_string(found->id);
        DrawText(dc, RECT{row.bounds.left + indent, row.bounds.top, row.bounds.right - 8, row.bounds.bottom}, label,
                 GdiDrawing::ToColorRef(theme.textPrimary));
    }

    const RECT preview = PreviewRect(content);
    const kb::scene::UIPresentationSnapshot snapshot = BuildPreview(content, editor);
    DrawPreview(dc, preview, snapshot, editor.SelectedElementId(), theme);

    const RECT details = DetailsRect(content);
    GdiDrawing::FillRectColor(dc, details, GdiDrawing::ToColorRef(theme.panel));
    GdiDrawing::FillRectColor(dc, RECT{details.left, details.top, details.left + 1, details.bottom},
                              GdiDrawing::ToColorRef(theme.borderPanel));
    if (const kb::scene::UIDocumentElement* selected = editor.SelectedElement()) {
        DrawDetails(dc, details, *selected, sceneContext, theme);
    }
}

UserWidgetEditorPanelHit UserWidgetEditorPanelRenderer::HitTest(const RECT& content,
                                                                const EditorSceneContext& sceneContext, int x, int y) {
    constexpr UserWidgetEditorPanelAction actions[]{
        UserWidgetEditorPanelAction::Save, UserWidgetEditorPanelAction::Undo,   UserWidgetEditorPanelAction::Redo,
        UserWidgetEditorPanelAction::Add,  UserWidgetEditorPanelAction::Remove, UserWidgetEditorPanelAction::Reparent,
    };
    for (int index = 0; index < static_cast<int>(std::size(actions)); ++index) {
        if (Contains(ToolbarButton(content, index), x, y)) {
            return UserWidgetEditorPanelHit{.action = actions[index]};
        }
    }
    for (const HierarchyRow& row : BuildRows(content, sceneContext.UserWidgetEditor())) {
        if (Contains(row.bounds, x, y)) {
            return UserWidgetEditorPanelHit{
                .action = UserWidgetEditorPanelAction::Select,
                .elementId = row.elementId,
            };
        }
    }
    const RECT preview = PreviewRect(content);
    const kb::scene::UIPresentationSnapshot snapshot = BuildPreview(content, sceneContext.UserWidgetEditor());
    for (auto item = snapshot.items.rbegin(); item != snapshot.items.rend(); ++item) {
        if (Contains(ItemRect(preview, item->rect), x, y)) {
            return UserWidgetEditorPanelHit{
                .action = UserWidgetEditorPanelAction::Select,
                .elementId = item->elementId,
                .canvasScale = item->canvasScale,
                .fromPreview = true,
            };
        }
    }
    const UserWidgetEditorDocument& editor = sceneContext.UserWidgetEditor();
    if (const kb::scene::UIDocumentElement* selected = editor.SelectedElement()) {
        const RECT details = DetailsRect(content);
        int rowTop = details.top + 8 + kDetailsHeaderHeight;
        for (const UserWidgetEditorPropertyRow& row : UserWidgetEditorPropertyAdapter::Rows(*selected)) {
            const RECT bounds{details.left, rowTop, details.right, rowTop + kRowHeight};
            if (Contains(bounds, x, y)) {
                return UserWidgetEditorPanelHit{
                    .action = UserWidgetEditorPanelAction::EditProperty,
                    .elementId = selected->id,
                    .property = row.property,
                };
            }
            rowTop += kRowHeight;
            if (rowTop >= details.bottom)
                break;
        }
    }
    return {};
}

} // namespace kb::editor

#endif
