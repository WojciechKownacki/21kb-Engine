#include "rendering/SkeletalMeshEditorPanelRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/SkeletalMeshEditorBonePicker.hpp"
#include "rendering/SkeletalMeshEditorPanelLayout.hpp"
#include "rendering/SkeletalMeshEditorSceneLabelBuilder.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedBrush.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "rendering/gdi/ScopedPen.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/SceneDepthPolicy.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include <bx/math.h>

namespace kb::editor {
namespace {

void PaintPanel(HDC dc, const RECT& rect, const char* title, const char* subtitle) {
    GdiDrawing::FillRectColor(dc, rect, RGB(28, 30, 34));
    GdiDrawing::DrawSharpFrame(dc, rect, RGB(28, 30, 34), RGB(53, 57, 64));
    const ScopedFont titleFont{ 13, FW_SEMIBOLD };
    const ScopedGdiObject selectedTitleFont(dc, titleFont.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(207, 214, 222));
    RECT titleRect{ rect.left + 10, rect.top + 8, rect.right - 8, rect.top + 28 };
    DrawTextA(dc, title, -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    const ScopedFont subtitleFont{ 11, FW_NORMAL };
    const ScopedGdiObject selectedSubtitleFont(dc, subtitleFont.handle);
    SetTextColor(dc, RGB(139, 149, 161));
    RECT subtitleRect{ rect.left + 10, rect.top + 34, rect.right - 8, rect.bottom - 8 };
    DrawTextA(dc, subtitle, -1, &subtitleRect, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
}

constexpr int kTreeHeaderHeight = 56;
constexpr int kTreeRowHeight = SkeletalMeshEditorPanelRenderer::TreeRowHeight;
constexpr int kTreeIndentWidth = 16;
constexpr int kTreeScrollbarWidth = 12;
constexpr int kTreeScrollbarInset = 3;
constexpr int kTreeScrollbarMinThumb = 24;
constexpr int kDetailsTabHeight = 25;
constexpr int kDetailsObjectHeaderHeight = 28;
constexpr int kDetailsCategoryHeight = 22;
constexpr int kDetailsFieldHeight = 22;

[[nodiscard]] int RectHeight(const RECT& rect) noexcept {
    return std::max(0L, rect.bottom - rect.top);
}

[[nodiscard]] SkeletalMeshEditorPanelLayout ResolvePanelLayout(
    const RECT& content, const EditorSceneContext& sceneContext) noexcept {
    return SkeletalMeshEditorPanelLayoutResolver::Resolve(
        content,
        sceneContext.SkeletalMeshEditorToolboxWidth(),
        sceneContext.SkeletalMeshEditorSkeletonTreeWidth(),
        sceneContext.SkeletalMeshEditorSkeletonTreeHeight());
}

[[nodiscard]] RECT TreeListRectForTree(const RECT& tree) noexcept {
    return RECT{
        tree.left + 1,
        tree.top + kTreeHeaderHeight,
        tree.right - 1,
        std::max(tree.top + kTreeHeaderHeight, tree.bottom - 1),
    };
}

void DrawTreeWire(HDC dc, int x1, int y1, int x2, int y2) {
    const ScopedPen pen{ 1, RGB(68, 72, 79) };
    const ScopedGdiObject selectedPen(dc, pen.handle);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
}

void DrawDisclosure(HDC dc, const RECT& rect, bool expanded) {
    POINT points[3]{};
    if (expanded) {
        points[0] = POINT{ rect.left + 3, rect.top + 5 };
        points[1] = POINT{ rect.right - 3, rect.top + 5 };
        points[2] = POINT{ (rect.left + rect.right) / 2, rect.bottom - 4 };
    } else {
        points[0] = POINT{ rect.left + 5, rect.top + 3 };
        points[1] = POINT{ rect.right - 4, (rect.top + rect.bottom) / 2 };
        points[2] = POINT{ rect.left + 5, rect.bottom - 3 };
    }
    const ScopedPen pen{ 1, RGB(170, 178, 188) };
    const ScopedBrush brush{ RGB(170, 178, 188) };
    const ScopedGdiObject selectedPen(dc, pen.handle);
    const ScopedGdiObject selectedBrush(dc, brush.handle);
    Polygon(dc, points, 3);
}

[[nodiscard]] RECT ScrollbarTrackForList(const RECT& list) noexcept {
    return RECT{
        list.right - kTreeScrollbarWidth,
        list.top + kTreeScrollbarInset,
        list.right - kTreeScrollbarInset,
        list.bottom - kTreeScrollbarInset,
    };
}

[[nodiscard]] RECT ScrollbarThumbForList(
    const RECT& list, int contentHeight, int offset) noexcept {
    const int viewportHeight = RectHeight(list);
    const RECT track = ScrollbarTrackForList(list);
    const int trackHeight = RectHeight(track);
    if (trackHeight <= 0 || contentHeight <= viewportHeight) return {};
    const int thumbHeight = std::clamp(
        (trackHeight * viewportHeight) / std::max(1, contentHeight),
        kTreeScrollbarMinThumb,
        trackHeight);
    const int maxOffset = std::max(1, contentHeight - viewportHeight);
    const int travel = std::max(0, trackHeight - thumbHeight);
    const int thumbTop = track.top +
        (travel * std::clamp(offset, 0, maxOffset)) / maxOffset;
    return RECT{ track.left + 2, thumbTop, track.right - 2, thumbTop + thumbHeight };
}

[[nodiscard]] RECT DetailsListRectForPanel(const RECT& panel) noexcept {
    return RECT{
        panel.left + 1,
        std::min(panel.bottom, panel.top + kDetailsTabHeight + kDetailsObjectHeaderHeight),
        panel.right - 1,
        std::max(panel.top + kDetailsTabHeight + kDetailsObjectHeaderHeight, panel.bottom - 1),
    };
}

[[nodiscard]] int DetailsContentHeight(const SkeletalMeshEditorDetailsModel& model) noexcept {
    int height = 0;
    for (const SkeletalMeshEditorDetailsSection& section : model.sections) {
        height += kDetailsCategoryHeight;
        if (section.expanded) {
            height += static_cast<int>(section.fields.size()) * kDetailsFieldHeight;
        }
    }
    return height;
}

[[nodiscard]] std::string AssetLabel(
    const EditorSceneContext& sceneContext,
    kb::assets::AssetId id,
    const char* missing) {
    if (!id.IsValid()) return missing;
    const kb::assets::AssetMetadata* metadata =
        sceneContext.Scene().Assets().Manager().Registry().Find(id);
    if (metadata == nullptr) return missing;
    const std::string filename = metadata->virtualPath.filename().string();
    return filename.empty() ? metadata->name : filename;
}

void PaintLinkedDocuments(
    HDC dc,
    const SkeletalMeshEditorPanelLayout& layout,
    const EditorSceneContext& sceneContext) {
    const RECT& rect = layout.documentBar;
    GdiDrawing::FillRectColor(dc, rect, RGB(24, 26, 30));
    GdiDrawing::DrawSharpFrame(dc, rect, RGB(24, 26, 30), RGB(53, 57, 64));
    SetBkMode(dc, TRANSPARENT);
    const ScopedFont labelFont{ 11, FW_SEMIBOLD };
    const ScopedGdiObject selectedLabelFont(dc, labelFont.handle);
    SetTextColor(dc, RGB(139, 149, 161));
    RECT familyLabel{ rect.left + 10, rect.top, std::min(rect.right, rect.left + 106), rect.bottom };
    DrawTextA(dc, "RELATED ASSETS", -1, &familyLabel,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    const std::array<RECT, 2U> buttons{ layout.meshDocument, layout.skeletonDocument };
    const bool skeletonActive = sceneContext.IsSkeletalMeshEditorSkeletonDocument();
    const bool meshAvailable = sceneContext.SkeletalMeshEditorAssetId().IsValid();
    const std::array<bool, 2U> active{ !skeletonActive, skeletonActive };
    const std::array<bool, 2U> enabled{ meshAvailable, true };
    const std::array<std::string, 2U> labels{
        "Skeletal Mesh  |  " + AssetLabel(
            sceneContext, sceneContext.SkeletalMeshEditorAssetId(), "No compatible mesh"),
        "Skeleton  |  " + AssetLabel(
            sceneContext, sceneContext.SkeletalMeshEditorSkeletonAssetId(), "Unavailable"),
    };
    const ScopedFont buttonFont{ 11, FW_NORMAL };
    const ScopedGdiObject selectedButtonFont(dc, buttonFont.handle);
    for (std::size_t index = 0U; index < buttons.size(); ++index) {
        const COLORREF fill = !enabled[index]
            ? RGB(31, 34, 39)
            : (active[index] ? RGB(35, 75, 112) : RGB(38, 41, 47));
        const COLORREF border = active[index] ? RGB(77, 143, 204) : RGB(59, 64, 72);
        GdiDrawing::DrawSharpFrame(dc, buttons[index], fill, border);
        SetTextColor(dc, !enabled[index] ? RGB(92, 99, 109) : RGB(211, 217, 225));
        RECT text{ buttons[index].left + 10, buttons[index].top,
            buttons[index].right - 8, buttons[index].bottom };
        DrawTextA(dc, labels[index].c_str(), -1, &text,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
}

struct CommandDescriptor {
    SkeletalAssetCommand command = SkeletalAssetCommand::Focus;
    std::string label;
    int width = 72;
    bool enabled = true;
    bool active = false;
};

[[nodiscard]] std::vector<CommandDescriptor> BuildCommands(const EditorSceneContext& sceneContext) {
    const bool skeleton = sceneContext.IsSkeletalMeshEditorSkeletonDocument();
    std::vector<CommandDescriptor> commands{
        { SkeletalAssetCommand::Save, "Save", 62, sceneContext.HasDirtySkeletalMeshEditorAssetEdit(), false },
        { SkeletalAssetCommand::Undo, "Undo", 62, sceneContext.CanUndoSkeletalMeshEditorAssetEdit() },
        { SkeletalAssetCommand::Redo, "Redo", 62, sceneContext.CanRedoSkeletalMeshEditorAssetEdit() },
        { SkeletalAssetCommand::Reload, "Reload", 70, sceneContext.CanReloadSkeletalMeshEditorAsset() },
    };
    if (skeleton) {
        commands.push_back({ SkeletalAssetCommand::PreviewMesh, "Preview Mesh...", 104, true });
        commands.push_back({ SkeletalAssetCommand::AddSocket, "Add Socket", 86,
            sceneContext.CanAddSkeletonEditorSocket() });
        commands.push_back({ SkeletalAssetCommand::DuplicateSocket, "Duplicate", 78,
            sceneContext.CanDuplicateSkeletonEditorSocket() });
        commands.push_back({ SkeletalAssetCommand::DeleteSocket, "Delete", 66,
            sceneContext.CanDeleteSkeletonEditorSocket() });
    } else {
        const bool fixedBounds = sceneContext.SkeletalMeshEditorBoundsMode() ==
            kb::scene::SkeletalMeshBoundsMode::Fixed;
        commands.push_back({ SkeletalAssetCommand::BoundsMode,
            fixedBounds ? "Bounds: Fixed" : "Bounds: Imported", 118, true, fixedBounds });
    }
    commands.push_back({ SkeletalAssetCommand::ReferencePose, "Reference Pose", 100, true,
        sceneContext.IsSkeletalMeshEditorReferencePose() });
    commands.push_back({ SkeletalAssetCommand::Focus, "Focus", 64, true });
    return commands;
}

[[nodiscard]] RECT CommandRect(const RECT& bar, const std::vector<CommandDescriptor>& commands, std::size_t index) {
    LONG left = bar.left + 10;
    for (std::size_t preceding = 0U; preceding < index; ++preceding) {
        left += commands[preceding].width + 5;
    }
    return RECT{ left, bar.top + 5, left + commands[index].width, bar.bottom - 5 };
}

void PaintCommands(HDC dc, const SkeletalMeshEditorPanelLayout& layout, const EditorSceneContext& sceneContext) {
    GdiDrawing::FillRectColor(dc, layout.commandBar, RGB(24, 26, 30));
    GdiDrawing::DrawSharpFrame(dc, layout.commandBar, RGB(24, 26, 30), RGB(53, 57, 64));
    const std::vector<CommandDescriptor> commands = BuildCommands(sceneContext);
    const ScopedFont font{ 11, FW_NORMAL };
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    for (std::size_t index = 0U; index < commands.size(); ++index) {
        const RECT button = CommandRect(layout.commandBar, commands, index);
        if (button.left >= layout.commandBar.right - 8) break;
        const RECT clipped{ button.left, button.top, std::min(button.right, layout.commandBar.right - 8), button.bottom };
        const COLORREF fill = !commands[index].enabled
            ? RGB(31, 34, 39)
            : (commands[index].active ? RGB(35, 75, 112) : RGB(38, 41, 47));
        const COLORREF border = commands[index].active ? RGB(77, 143, 204) : RGB(59, 64, 72);
        GdiDrawing::DrawSharpFrame(dc, clipped, fill, border);
        SetTextColor(dc, commands[index].enabled ? RGB(211, 217, 225) : RGB(92, 99, 109));
        RECT text{ clipped.left + 7, clipped.top, clipped.right - 7, clipped.bottom };
        DrawTextA(dc, commands[index].label.c_str(), -1, &text,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
}

[[nodiscard]] kb::render::SceneRenderCamera BuildPreviewCamera(
    const EditorViewportCameraState& camera,
    std::uint32_t width,
    std::uint32_t height) noexcept {
    const EditorViewportCameraAxes axes = camera.Axes();
    kb::render::SceneRenderCamera output{};
    bx::mtxLookAt(
        output.view.data(),
        bx::Vec3{ axes.position.x, axes.position.y, axes.position.z },
        bx::Vec3{
            axes.position.x + axes.forward.x,
            axes.position.y + axes.forward.y,
            axes.position.z + axes.forward.z,
        },
        bx::Vec3{ axes.up.x, axes.up.y, axes.up.z });
    const float aspect = height == 0U ? 1.0F :
        static_cast<float>(std::max(1U, width)) / static_cast<float>(height);
    kb::render::SceneDepthPolicy::MakePerspective(
        output.projection.data(), camera.VerticalFovDegrees(), aspect,
        camera.NearClip(), camera.FarClip(),
        kb::render::SceneDepthPolicy::HomogeneousDepth());
    return output;
}

void DrawAdvancedPreviewRow(HDC dc, int left, int right, int& y, const char* label, bool enabled) {
    RECT row{ left + 10, y, right - 8, y + 20 };
    GdiDrawing::FillRectColor(dc, RECT{ row.left, row.top + 3, row.left + 10, row.top + 13 },
        enabled ? RGB(74, 150, 106) : RGB(68, 72, 79));
    SetTextColor(dc, RGB(211, 217, 225));
    RECT text{ row.left + 16, row.top, row.right, row.bottom };
    DrawTextA(dc, label, -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    y += 20;
}

void PaintAdvancedPreview(HDC dc, const RECT& rect, const EditorSceneContext& sceneContext) {
    GdiDrawing::FillRectColor(dc, rect, RGB(28, 30, 34));
    GdiDrawing::DrawSharpFrame(dc, rect, RGB(28, 30, 34), RGB(53, 57, 64));
    const ScopedFont titleFont{ 13, FW_SEMIBOLD };
    const ScopedGdiObject selectedTitleFont(dc, titleFont.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(207, 214, 222));
    RECT title{ rect.left + 10, rect.top + 8, rect.right - 8, rect.top + 28 };
    DrawTextA(dc, "Advanced Preview", -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    const ScopedFont bodyFont{ 11, FW_NORMAL };
    const ScopedGdiObject selectedBodyFont(dc, bodyFont.handle);
    const AnimationPreviewOverlayState& overlays = sceneContext.AnimationPreview().Overlays();
    int y = rect.top + 32;
    DrawAdvancedPreviewRow(dc, rect.left, rect.right, y, "Bones", overlays.BonesVisible());
    DrawAdvancedPreviewRow(dc, rect.left, rect.right, y, "Bone Names", overlays.BoneNamesVisible());
    DrawAdvancedPreviewRow(dc, rect.left, rect.right, y, "Sockets", overlays.SocketsVisible());
    DrawAdvancedPreviewRow(dc, rect.left, rect.right, y, "Bounds", overlays.BoundsVisible());
    DrawAdvancedPreviewRow(dc, rect.left, rect.right, y, "LOD", overlays.LodVisible());
    DrawAdvancedPreviewRow(dc, rect.left, rect.right, y, "Normals", overlays.NormalsVisible());
}

void PaintTree(HDC dc, const RECT& rect, const EditorSceneContext& sceneContext) {
    GdiDrawing::FillRectColor(dc, rect, RGB(28, 30, 34));
    GdiDrawing::DrawSharpFrame(dc, rect, RGB(28, 30, 34), RGB(53, 57, 64));
    const ScopedFont titleFont{ 13, FW_SEMIBOLD };
    const ScopedGdiObject selectedTitleFont(dc, titleFont.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(207, 214, 222));
    RECT title{ rect.left + 10, rect.top + 8, rect.right - 8, rect.top + 26 };
    DrawTextA(dc, "Skeleton Tree", -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    GdiDrawing::DrawSharpFrame(dc, RECT{ rect.left + 8, rect.top + 30, rect.right - 8, rect.top + 50 }, RGB(23, 25, 28),
        sceneContext.IsSkeletalMeshEditorTreeSearchFocused() ? RGB(77, 143, 204) : RGB(63, 68, 76));
    const ScopedFont bodyFont{ 10, FW_NORMAL };
    const ScopedGdiObject selectedBodyFont(dc, bodyFont.handle);
    SetTextColor(dc, RGB(145, 155, 168));
    const RECT searchIcon{ rect.left + 13, rect.top + 34, rect.left + 25, rect.top + 46 };
    HeroIconPainter::Draw(dc, searchIcon, HeroIconKind::MagnifyingGlass, RGB(125, 135, 147), 1);
    RECT filter{ rect.left + 29, rect.top + 31, rect.right - 12, rect.top + 49 };
    const std::string filterText = sceneContext.SkeletalMeshEditorTreeFilter().empty()
        ? "Search Skeleton Tree..." : sceneContext.SkeletalMeshEditorTreeFilter();
    DrawTextA(dc, filterText.c_str(), -1, &filter, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    const std::vector<SkeletalMeshEditorTreeRow> rows = sceneContext.SkeletalMeshEditorTreeRows();
    const RECT list = TreeListRectForTree(rect);
    const int viewportHeight = RectHeight(list);
    const int contentHeight = static_cast<int>(rows.size()) * kTreeRowHeight;
    const int maxScroll = std::max(0, contentHeight - viewportHeight);
    const int scroll = std::clamp(sceneContext.SkeletalMeshEditorTreeScrollOffset(), 0, maxScroll);
    const bool hasScrollbar = contentHeight > viewportHeight;
    const int rowsRight = hasScrollbar ? list.right - kTreeScrollbarWidth : list.right;
    const std::size_t firstRow = static_cast<std::size_t>(scroll / kTreeRowHeight);
    const int firstRowOffset = scroll % kTreeRowHeight;
    const std::size_t visibleRows = static_cast<std::size_t>(
        (viewportHeight + firstRowOffset + kTreeRowHeight - 1) / kTreeRowHeight);
    const std::size_t lastRow = std::min(rows.size(), firstRow + visibleRows);
    const int savedDc = SaveDC(dc);
    IntersectClipRect(dc, list.left, list.top, list.right, list.bottom);
    int rowTop = list.top - firstRowOffset;
    for (std::size_t index = firstRow; index < lastRow; ++index, rowTop += kTreeRowHeight) {
        RECT row{ list.left, rowTop, rowsRight, rowTop + kTreeRowHeight };
        if (rows[index].selected) GdiDrawing::FillRectColor(dc, row, RGB(35, 75, 112));
        const int branchBase = row.left + 10;
        const int branchCenterY = (row.top + row.bottom) / 2;
        for (std::uint32_t depth = 0U; depth < rows[index].depth && depth < 64U; ++depth) {
            if ((rows[index].continuationMask & (std::uint64_t{ 1U } << depth)) != 0U) {
                const int wireX = branchBase + static_cast<int>(depth) * kTreeIndentWidth;
                DrawTreeWire(dc, wireX, row.top, wireX, row.bottom);
            }
        }
        const int itemBranchX = branchBase + static_cast<int>(rows[index].depth) * kTreeIndentWidth;
        if (rows[index].depth > 0U) {
            DrawTreeWire(
                dc, itemBranchX, row.top, itemBranchX,
                rows[index].lastSibling ? branchCenterY : row.bottom);
            DrawTreeWire(dc, itemBranchX, branchCenterY, itemBranchX + 7, branchCenterY);
        }
        const RECT disclosure{ itemBranchX + 1, row.top + 3, itemBranchX + 15, row.bottom - 3 };
        if (rows[index].hasChildren) DrawDisclosure(dc, disclosure, rows[index].expanded);
        const int itemLeft = itemBranchX + kTreeIndentWidth;
        const RECT itemIcon{ itemLeft, row.top + 3, itemLeft + 14, row.bottom - 3 };
        const bool socket = rows[index].kind == SkeletalMeshEditorTreeItemKind::Socket;
        const COLORREF itemColor = socket ? RGB(120, 196, 176) : RGB(171, 181, 194);
        HeroIconPainter::Draw(
            dc, itemIcon, socket ? HeroIconKind::Bolt : HeroIconKind::Skeleton, itemColor, 1);
        RECT label{ itemIcon.right + 4, row.top, row.right - 6, row.bottom };
        SetTextColor(dc, socket ? RGB(120, 196, 176) : RGB(211, 217, 225));
        DrawTextA(dc, rows[index].label.c_str(), -1, &label,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    RestoreDC(dc, savedDc);
    if (hasScrollbar) {
        const RECT track = ScrollbarTrackForList(list);
        const RECT thumb = ScrollbarThumbForList(list, contentHeight, scroll);
        GdiDrawing::DrawSharpFrame(dc, track, RGB(22, 24, 27), RGB(38, 42, 47));
        const bool dragging = sceneContext.IsSkeletalMeshEditorTreeScrollbarDragging();
        GdiDrawing::DrawSharpFrame(
            dc,
            thumb,
            dragging ? RGB(104, 116, 130) : RGB(76, 86, 98),
            dragging ? RGB(128, 142, 158) : RGB(94, 105, 118));
    }
}

void PaintDetails(HDC dc, const RECT& rect, const EditorSceneContext& sceneContext) {
    GdiDrawing::FillRectColor(dc, rect, RGB(28, 30, 34));
    GdiDrawing::DrawSharpFrame(dc, rect, RGB(28, 30, 34), RGB(53, 57, 64));
    const SkeletalMeshEditorDetailsModel model = sceneContext.SkeletalMeshEditorDetails();
    const RECT tabWell{ rect.left + 1, rect.top + 1, rect.right - 1,
        std::min(rect.bottom, rect.top + kDetailsTabHeight) };
    GdiDrawing::FillRectColor(dc, tabWell, RGB(22, 24, 28));
    const RECT detailsTab{ tabWell.left + 5, tabWell.top, std::min(tabWell.right, tabWell.left + 84), tabWell.bottom };
    GdiDrawing::DrawSharpFrame(dc, detailsTab, RGB(37, 40, 46), RGB(56, 61, 69));
    GdiDrawing::FillRectColor(dc, RECT{ detailsTab.left, detailsTab.bottom - 2, detailsTab.right, detailsTab.bottom },
        RGB(77, 143, 204));
    const ScopedFont titleFont{ 13, FW_SEMIBOLD };
    const ScopedGdiObject selectedTitleFont(dc, titleFont.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(207, 214, 222));
    RECT tabText{ detailsTab.left + 9, detailsTab.top, detailsTab.right - 6, detailsTab.bottom };
    DrawTextA(dc, "Details", -1, &tabText,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    RECT title{ rect.left + 10, tabWell.bottom, rect.right - 8,
        std::min(rect.bottom, tabWell.bottom + kDetailsObjectHeaderHeight) };
    const std::string titleText = (sceneContext.HasDirtySkeletalMeshEditorAssetEdit() ? "* " : "") +
        (model.title.empty() ? std::string{ "Asset Details" } : model.title);
    DrawTextA(dc, titleText.c_str(), -1, &title,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    const ScopedFont bodyFont{ 11, FW_NORMAL };
    const ScopedGdiObject selectedBodyFont(dc, bodyFont.handle);
    const RECT list = DetailsListRectForPanel(rect);
    const int contentHeight = DetailsContentHeight(model);
    const int viewportHeight = RectHeight(list);
    const int maximum = std::max(0, contentHeight - viewportHeight);
    const int scroll = std::clamp(sceneContext.SkeletalMeshEditorDetailsScrollOffset(), 0, maximum);
    const bool hasScrollbar = contentHeight > viewportHeight;
    const int rowsRight = hasScrollbar ? list.right - kTreeScrollbarWidth : list.right;
    const int savedDc = SaveDC(dc);
    IntersectClipRect(dc, list.left, list.top, list.right, list.bottom);
    int y = list.top - scroll;
    for (const SkeletalMeshEditorDetailsSection& section : model.sections) {
        RECT sectionRect{ list.left, y, rowsRight, y + kDetailsCategoryHeight };
        GdiDrawing::DrawSharpFrame(dc, sectionRect, RGB(36, 39, 44), RGB(51, 55, 62));
        DrawDisclosure(dc, RECT{ sectionRect.left + 4, sectionRect.top + 3,
            sectionRect.left + 18, sectionRect.bottom - 3 }, section.expanded);
        SetTextColor(dc, RGB(211, 217, 225));
        RECT sectionText{ sectionRect.left + 22, sectionRect.top, sectionRect.right - 7, sectionRect.bottom };
        DrawTextA(dc, section.title.c_str(), -1, &sectionText,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        y += kDetailsCategoryHeight;
        if (!section.expanded) continue;
        for (const SkeletalMeshEditorDetailsField& field : section.fields) {
            const int split = static_cast<int>(list.left) + std::max(
                104, static_cast<int>(rowsRight - list.left) * 45 / 100);
            RECT row{ list.left, y, rowsRight, y + kDetailsFieldHeight };
            GdiDrawing::FillRectColor(dc, row, ((y - list.top + scroll) / kDetailsFieldHeight) % 2 == 0
                ? RGB(28, 30, 34) : RGB(25, 27, 31));
            DrawTreeWire(dc, split, row.top, split, row.bottom);
            RECT label{ row.left + 10, row.top, split - 7, row.bottom };
            RECT value{ split + 6, row.top + 2, row.right - 6, row.bottom - 2 };
            SetTextColor(dc, RGB(154, 164, 176));
            DrawTextA(dc, field.label.c_str(), -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            const bool editable = field.action != SkeletalMeshEditorDetailsAction::None;
            if (editable) {
                GdiDrawing::DrawSharpFrame(dc, value, RGB(20, 22, 25), RGB(67, 73, 82));
                value.left += 6;
                value.right -= 16;
            }
            SetTextColor(dc, editable ? RGB(225, 230, 236) : RGB(182, 190, 201));
            DrawTextA(dc, field.value.c_str(), -1, &value,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            if (editable) {
                SetTextColor(dc, RGB(132, 143, 156));
                RECT affordance{ value.right + 3, row.top, row.right - 5, row.bottom };
                DrawTextA(dc,
                    field.action == SkeletalMeshEditorDetailsAction::SectionMaterial ||
                    field.action == SkeletalMeshEditorDetailsAction::PreviewLod ? "v" : "...",
                    -1, &affordance, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            y += kDetailsFieldHeight;
        }
    }
    RestoreDC(dc, savedDc);
    if (hasScrollbar) {
        const RECT track = ScrollbarTrackForList(list);
        const RECT thumb = ScrollbarThumbForList(list, contentHeight, scroll);
        GdiDrawing::DrawSharpFrame(dc, track, RGB(22, 24, 27), RGB(38, 42, 47));
        const bool dragging = sceneContext.IsSkeletalMeshEditorDetailsScrollbarDragging();
        GdiDrawing::DrawSharpFrame(dc, thumb,
            dragging ? RGB(104, 116, 130) : RGB(76, 86, 98),
            dragging ? RGB(128, 142, 158) : RGB(94, 105, 118));
    }
}

void AppendDebugSegment(
    std::vector<kb::render::PhysicsDebugLine>& output,
    kb::scene::Vec3 from,
    kb::scene::Vec3 to,
    kb::scene::Vec3 color,
    float alpha = 1.0F) {
    output.push_back(kb::render::PhysicsDebugLine{
        .from = { from.x, from.y, from.z },
        .to = { to.x, to.y, to.z },
        .color = { color.x, color.y, color.z },
        .alpha = alpha,
    });
}

void AppendBoneShape(
    std::vector<kb::render::PhysicsDebugLine>& output,
    const AnimationPreviewOverlayLine& bone,
    bool selected) {
    const kb::scene::Vec3 delta = bone.to - bone.from;
    const float length = kb::math::Length(delta);
    if (length <= 0.00001F) return;

    const kb::scene::Vec3 direction = delta * (1.0F / length);
    const kb::scene::Vec3 helper = std::abs(direction.y) < 0.9F
        ? kb::scene::Vec3{ 0.0F, 1.0F, 0.0F }
        : kb::scene::Vec3{ 1.0F, 0.0F, 0.0F };
    const kb::scene::Vec3 side = kb::math::Normalize(kb::math::Cross(direction, helper));
    const kb::scene::Vec3 up = kb::math::Normalize(kb::math::Cross(side, direction));
    const float radius = std::max(length * 0.09F, 0.0025F);
    const kb::scene::Vec3 ringCenter = bone.from + direction * (length * 0.22F);
    const std::array<kb::scene::Vec3, 4U> ring{
        ringCenter + side * radius,
        ringCenter + up * radius,
        ringCenter - side * radius,
        ringCenter - up * radius,
    };
    const kb::scene::Vec3 color = selected
        ? kb::scene::Vec3{ 1.0F, 0.82F, 0.18F }
        : kb::scene::Vec3{ 0.98F, 0.36F, 0.10F };
    for (std::size_t index = 0U; index < ring.size(); ++index) {
        const kb::scene::Vec3& current = ring[index];
        const kb::scene::Vec3& next = ring[(index + 1U) % ring.size()];
        AppendDebugSegment(output, bone.from, current, color);
        AppendDebugSegment(output, current, next, color);
        AppendDebugSegment(output, current, bone.to, color);
    }
    AppendDebugSegment(output, bone.from, bone.to, selected ? kb::scene::Vec3{ 1.0F, 0.95F, 0.55F } : color);
}

void AppendSelectedJointMarker(
    std::vector<kb::render::PhysicsDebugLine>& output,
    kb::scene::Vec3 position,
    float radius) {
    const kb::scene::Vec3 color{ 1.0F, 0.95F, 0.55F };
    AppendDebugSegment(output, position - kb::scene::Vec3{ radius, 0.0F, 0.0F },
        position + kb::scene::Vec3{ radius, 0.0F, 0.0F }, color);
    AppendDebugSegment(output, position - kb::scene::Vec3{ 0.0F, radius, 0.0F },
        position + kb::scene::Vec3{ 0.0F, radius, 0.0F }, color);
    AppendDebugSegment(output, position - kb::scene::Vec3{ 0.0F, 0.0F, radius },
        position + kb::scene::Vec3{ 0.0F, 0.0F, radius }, color);
}

[[nodiscard]] std::vector<kb::render::PhysicsDebugLine> BuildSkeletalPreviewLines(
    const AnimationPreviewOverlaySnapshot& overlays,
    kb::scene::SkeletonBoneId selectedBone) {
    std::vector<kb::render::PhysicsDebugLine> output;
    output.reserve(overlays.lines.size() * 16U);
    const bool selectedBoneHasIncomingShape = selectedBone != 0U &&
        std::ranges::any_of(overlays.lines, [selectedBone](const AnimationPreviewOverlayLine& line) {
            return line.boneId == selectedBone;
        });
    bool selectedRootMarkerEmitted = false;
    for (const AnimationPreviewOverlayLine& line : overlays.lines) {
        if (line.boneId != 0U) {
            AppendBoneShape(output, line, line.boneId == selectedBone);
            const float markerRadius = std::clamp(
                kb::math::Length(line.to - line.from) * 0.08F, 0.01F, 0.05F);
            if (line.boneId == selectedBone) {
                AppendSelectedJointMarker(output, line.to, markerRadius);
            } else if (!selectedBoneHasIncomingShape && !selectedRootMarkerEmitted &&
                line.fromBoneId == selectedBone) {
                AppendSelectedJointMarker(output, line.from, markerRadius);
                selectedRootMarkerEmitted = true;
            }
        } else {
            AppendDebugSegment(output, line.from, line.to, line.color);
        }
    }
    return output;
}

} // namespace

void SkeletalMeshEditorPanelRenderer::Paint(
    HDC dc,
    HWND host,
    const RECT& content,
    const DockPanel& panel,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& renderBackendSettings,
    EditorSceneBgfxViewport* sceneViewport) const {
    static_cast<void>(theme);
    if (!sceneContext.HasSkeletalMeshEditorAsset() ||
        sceneContext.SkeletalMeshEditorPreviewScene() == nullptr) {
        GdiDrawing::FillRectColor(dc, content, RGB(27, 29, 33));
        const ScopedFont font{ 15, FW_NORMAL };
        const ScopedGdiObject selectedFont(dc, font.handle);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(168, 178, 190));
        RECT text = content;
        const bool skeletonDocument = sceneContext.IsSkeletalMeshEditorSkeletonDocument();
        const char* message = sceneContext.HasPendingSkeletalMeshEditorOpen()
            ? (skeletonDocument ? "Loading Skeleton preview..." : "Loading Skeletal Mesh...")
            : (skeletonDocument ? "Open a Skeleton asset to begin editing." : "Open a Skeletal Mesh asset to begin editing.");
        DrawTextA(dc, message, -1, &text,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }
    const SkeletalMeshEditorPanelLayout layout = ResolvePanelLayout(content, sceneContext);
    PaintLinkedDocuments(dc, layout, sceneContext);
    PaintCommands(dc, layout, sceneContext);
    PaintAdvancedPreview(dc, layout.toolbox, sceneContext);
    PaintTree(dc, layout.skeletonTree, sceneContext);
    PaintDetails(dc, layout.assetDetails, sceneContext);
    const COLORREF splitterColor = RGB(58, 63, 71);
    const COLORREF activeSplitterColor = RGB(77, 143, 204);
    GdiDrawing::FillRectColor(dc,
        RECT{ layout.toolbox.right - 1, layout.toolbox.top, layout.toolbox.right + 1, layout.toolbox.bottom },
        sceneContext.IsSkeletalMeshEditorToolboxWidthDragging()
            ? activeSplitterColor
            : splitterColor);
    GdiDrawing::FillRectColor(dc,
        RECT{ layout.skeletonTree.left - 1, layout.skeletonTree.top,
            layout.skeletonTree.left + 1, layout.assetDetails.bottom },
        sceneContext.IsSkeletalMeshEditorSkeletonTreeWidthDragging()
            ? activeSplitterColor
            : splitterColor);
    GdiDrawing::FillRectColor(dc,
        RECT{ layout.skeletonTree.left, layout.skeletonTree.bottom - 1,
            layout.skeletonTree.right, layout.skeletonTree.bottom + 1 },
        sceneContext.IsSkeletalMeshEditorTreeDetailsHeightDragging()
            ? activeSplitterColor
            : splitterColor);
    if (sceneViewport != nullptr) {
        static_cast<void>(PresentViewport(
            *sceneViewport, host, content, panel, sceneContext, renderBackendSettings));
    }
}

bool SkeletalMeshEditorPanelRenderer::PresentViewport(
    EditorSceneBgfxViewport& sceneViewport,
    HWND host,
    const RECT& content,
    const DockPanel& panel,
    const EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& renderBackendSettings) {
    static_cast<void>(renderBackendSettings);
    const kb::scene::Scene* previewScene = sceneContext.SkeletalMeshEditorPreviewScene();
    if (host == nullptr || IsWindow(host) == 0 || IsWindowVisible(host) == 0 ||
        !sceneContext.HasSkeletalMeshEditorAsset() || previewScene == nullptr) {
        return false;
    }

    const SkeletalMeshEditorPanelLayout layout = ResolvePanelLayout(content, sceneContext);
    if (layout.viewport.right <= layout.viewport.left || layout.viewport.bottom <= layout.viewport.top) {
        return false;
    }
    const std::uint64_t revision = sceneContext.SkeletalMeshEditorPreviewRevision();
    const std::uint32_t renderWidth = static_cast<std::uint32_t>(layout.viewport.right - layout.viewport.left);
    const std::uint32_t renderHeight = static_cast<std::uint32_t>(layout.viewport.bottom - layout.viewport.top);
    EditorSceneBgfxViewport::PresentSettings settings{};
    settings.renderWidth = renderWidth;
    settings.renderHeight = renderHeight;
    settings.cameraOverride = BuildPreviewCamera(
        sceneContext.AnimationPreviewCamera(), renderWidth, renderHeight);
    settings.viewportKey = panel.id;
    settings.editorSceneOverlaysEnabled = true;
    const AnimationPreviewOverlaySnapshot overlays = sceneContext.AnimationPreviewOverlays();
    settings.physicsDebugLines = BuildSkeletalPreviewLines(
        overlays, sceneContext.SelectedSkeletalMeshEditorBone());
    SkeletalMeshEditorSceneLabelBuilder::Append(
        settings.viewportTextLabels, overlays.labels, sceneContext.AnimationPreviewCamera(),
        renderWidth, renderHeight, overlays.labelReferenceCameraDistance);
    settings.sceneRevision = revision;
    settings.sceneDirtyBaseRevision = revision;
    // The preview scene has its own monotonic revision. Re-synchronizing the complete 24 MB Y Bot
    // payload on every selection repaint stalls the UI thread while bgfx retires the upload.
    settings.sceneFullSyncRequired = false;
    settings.msaaSamples = 0U;
    settings.shadowPassEnabled = false;
    settings.postProcessEnabled = false;
    settings.selectionMaskEnabled = false;
    settings.selectionOutlineEnabled = false;
    settings.gpuDrivenRuntimeDispatchEnabled = false;
    sceneViewport.Present(host, layout.viewport, *previewScene, settings);
    return true;
}

std::optional<SkeletalMeshEditorTreeRow> SkeletalMeshEditorPanelRenderer::TreeRowAt(
    const RECT& content, const EditorSceneContext& sceneContext, int x, int y) {
    const RECT list = TreeListRect(content, sceneContext);
    if (x < list.left || x >= list.right || y < list.top || y >= list.bottom) {
        return std::nullopt;
    }
    if (TreeMaxScroll(content, sceneContext) > 0 && x >= list.right - kTreeScrollbarWidth) {
        return std::nullopt;
    }
    const std::vector<SkeletalMeshEditorTreeRow> rows = sceneContext.SkeletalMeshEditorTreeRows();
    const int scroll = std::clamp(
        sceneContext.SkeletalMeshEditorTreeScrollOffset(), 0, TreeMaxScroll(content, sceneContext));
    const std::size_t index = static_cast<std::size_t>(
        (y - list.top + scroll) / kTreeRowHeight);
    return index < rows.size() ? std::optional<SkeletalMeshEditorTreeRow>{ rows[index] } : std::nullopt;
}

std::optional<kb::scene::SkeletonBoneId> SkeletalMeshEditorPanelRenderer::TreeDisclosureAt(
    const RECT& content, const EditorSceneContext& sceneContext, int x, int y) {
    const std::optional<SkeletalMeshEditorTreeRow> row = TreeRowAt(content, sceneContext, x, y);
    if (!row.has_value() || row->kind != SkeletalMeshEditorTreeItemKind::Bone || !row->hasChildren) {
        return std::nullopt;
    }
    const RECT list = TreeListRect(content, sceneContext);
    const int disclosureLeft = list.left + 10 + static_cast<int>(row->depth) * kTreeIndentWidth;
    return x >= disclosureLeft - 3 && x < disclosureLeft + kTreeIndentWidth + 3
        ? std::optional<kb::scene::SkeletonBoneId>{ row->boneId }
        : std::nullopt;
}

RECT SkeletalMeshEditorPanelRenderer::TreeListRect(
    const RECT& content, const EditorSceneContext& sceneContext) noexcept {
    return TreeListRectForTree(ResolvePanelLayout(content, sceneContext).skeletonTree);
}

int SkeletalMeshEditorPanelRenderer::TreeMaxScroll(
    const RECT& content, const EditorSceneContext& sceneContext) {
    const RECT list = TreeListRect(content, sceneContext);
    const int contentHeight =
        static_cast<int>(sceneContext.SkeletalMeshEditorTreeRows().size()) * kTreeRowHeight;
    return std::max(0, contentHeight - RectHeight(list));
}

RECT SkeletalMeshEditorPanelRenderer::TreeScrollbarTrack(
    const RECT& content, const EditorSceneContext& sceneContext) noexcept {
    return ScrollbarTrackForList(TreeListRect(content, sceneContext));
}

RECT SkeletalMeshEditorPanelRenderer::TreeScrollbarThumb(
    const RECT& content, const EditorSceneContext& sceneContext) {
    const RECT list = TreeListRect(content, sceneContext);
    const int contentHeight =
        static_cast<int>(sceneContext.SkeletalMeshEditorTreeRows().size()) * kTreeRowHeight;
    return ScrollbarThumbForList(
        list, contentHeight, sceneContext.SkeletalMeshEditorTreeScrollOffset());
}

int SkeletalMeshEditorPanelRenderer::TreeScrollOffsetToRevealSelection(
    const RECT& content, const EditorSceneContext& sceneContext) {
    const std::vector<SkeletalMeshEditorTreeRow> rows = sceneContext.SkeletalMeshEditorTreeRows();
    const auto selected = std::ranges::find_if(rows, [](const SkeletalMeshEditorTreeRow& row) {
        return row.selected;
    });
    const int maxScroll = TreeMaxScroll(content, sceneContext);
    const int current = std::clamp(
        sceneContext.SkeletalMeshEditorTreeScrollOffset(), 0, maxScroll);
    if (selected == rows.end()) return current;
    const int rowTop = static_cast<int>(std::distance(rows.begin(), selected)) * kTreeRowHeight;
    const int rowBottom = rowTop + kTreeRowHeight;
    const int viewportHeight = RectHeight(TreeListRect(content, sceneContext));
    if (rowTop < current) return rowTop;
    if (rowBottom > current + viewportHeight) {
        return std::clamp(rowBottom - viewportHeight, 0, maxScroll);
    }
    return current;
}

bool SkeletalMeshEditorPanelRenderer::IsTreeSearchAt(
    const RECT& content, const EditorSceneContext& sceneContext, int x, int y) noexcept {
    const SkeletalMeshEditorPanelLayout layout = ResolvePanelLayout(content, sceneContext);
    const RECT search{ layout.skeletonTree.left + 8, layout.skeletonTree.top + 30,
        layout.skeletonTree.right - 8, layout.skeletonTree.top + 50 };
    return x >= search.left && x < search.right && y >= search.top && y < search.bottom;
}

std::optional<std::uint8_t> SkeletalMeshEditorPanelRenderer::AdvancedPreviewOverlayAt(
    const RECT& content, const EditorSceneContext& sceneContext, int x, int y) noexcept {
    const SkeletalMeshEditorPanelLayout layout = ResolvePanelLayout(content, sceneContext);
    if (x < layout.toolbox.left || x >= layout.toolbox.right || y < layout.toolbox.top + 32) return std::nullopt;
    const int index = (y - (layout.toolbox.top + 32)) / 20;
    return index >= 0 && index < 6 ? std::optional<std::uint8_t>{ static_cast<std::uint8_t>(index) } : std::nullopt;
}

std::optional<kb::scene::SkeletonBoneId> SkeletalMeshEditorPanelRenderer::BoneAt(
    const RECT& content, const EditorSceneContext& sceneContext, int x, int y) noexcept {
    const SkeletalMeshEditorPanelLayout layout = ResolvePanelLayout(content, sceneContext);
    if (x < layout.viewport.left || x >= layout.viewport.right || y < layout.viewport.top || y >= layout.viewport.bottom) return std::nullopt;
    const AnimationPreviewOverlaySnapshot overlays = sceneContext.AnimationPreviewOverlays();
    return SkeletalMeshEditorBonePicker::Pick(
        SkeletalMeshEditorBonePickViewport{
            .left = static_cast<float>(layout.viewport.left),
            .top = static_cast<float>(layout.viewport.top),
            .width = static_cast<float>(std::max(1L, layout.viewport.right - layout.viewport.left)),
            .height = static_cast<float>(std::max(1L, layout.viewport.bottom - layout.viewport.top)),
        },
        sceneContext.AnimationPreviewCamera(),
        overlays.lines,
        static_cast<float>(x),
        static_cast<float>(y));
}

std::optional<SkeletalMeshEditorDetailsHit> SkeletalMeshEditorPanelRenderer::DetailsHitAt(
    const RECT& content, const EditorSceneContext& sceneContext, int x, int y) {
    const RECT list = DetailsListRect(content, sceneContext);
    if (x < list.left || x >= list.right || y < list.top || y >= list.bottom) return std::nullopt;
    if (DetailsMaxScroll(content, sceneContext) > 0 && x >= list.right - kTreeScrollbarWidth) {
        return std::nullopt;
    }
    const SkeletalMeshEditorDetailsModel model = sceneContext.SkeletalMeshEditorDetails();
    const int localY = y - list.top + std::clamp(
        sceneContext.SkeletalMeshEditorDetailsScrollOffset(), 0,
        DetailsMaxScroll(content, sceneContext));
    int cursor = 0;
    for (const SkeletalMeshEditorDetailsSection& section : model.sections) {
        if (localY >= cursor && localY < cursor + kDetailsCategoryHeight) {
            return SkeletalMeshEditorDetailsHit{
                .kind = SkeletalMeshEditorDetailsHitKind::Section,
                .sectionTitle = section.title,
            };
        }
        cursor += kDetailsCategoryHeight;
        if (!section.expanded) continue;
        for (const SkeletalMeshEditorDetailsField& field : section.fields) {
            if (localY >= cursor && localY < cursor + kDetailsFieldHeight) {
                return field.action == SkeletalMeshEditorDetailsAction::None
                    ? std::nullopt
                    : std::optional<SkeletalMeshEditorDetailsHit>{ SkeletalMeshEditorDetailsHit{
                        .kind = SkeletalMeshEditorDetailsHitKind::Field,
                        .sectionTitle = section.title,
                        .field = field,
                    } };
            }
            cursor += kDetailsFieldHeight;
        }
    }
    return std::nullopt;
}

RECT SkeletalMeshEditorPanelRenderer::DetailsListRect(
    const RECT& content, const EditorSceneContext& sceneContext) noexcept {
    return DetailsListRectForPanel(ResolvePanelLayout(content, sceneContext).assetDetails);
}

int SkeletalMeshEditorPanelRenderer::DetailsMaxScroll(
    const RECT& content, const EditorSceneContext& sceneContext) {
    const RECT list = DetailsListRect(content, sceneContext);
    return std::max(0, DetailsContentHeight(sceneContext.SkeletalMeshEditorDetails()) - RectHeight(list));
}

RECT SkeletalMeshEditorPanelRenderer::DetailsScrollbarTrack(
    const RECT& content, const EditorSceneContext& sceneContext) noexcept {
    return ScrollbarTrackForList(DetailsListRect(content, sceneContext));
}

RECT SkeletalMeshEditorPanelRenderer::DetailsScrollbarThumb(
    const RECT& content, const EditorSceneContext& sceneContext) {
    const RECT list = DetailsListRect(content, sceneContext);
    return ScrollbarThumbForList(
        list,
        DetailsContentHeight(sceneContext.SkeletalMeshEditorDetails()),
        sceneContext.SkeletalMeshEditorDetailsScrollOffset());
}

std::optional<SkeletalAssetDocument> SkeletalMeshEditorPanelRenderer::LinkedDocumentAt(
    const RECT& content, const EditorSceneContext& sceneContext, int x, int y) noexcept {
    const SkeletalMeshEditorPanelLayout layout = ResolvePanelLayout(content, sceneContext);
    const std::array<RECT, 2U> buttons{ layout.meshDocument, layout.skeletonDocument };
    for (std::size_t index = 0U; index < buttons.size(); ++index) {
        if (x >= buttons[index].left && x < buttons[index].right &&
            y >= buttons[index].top && y < buttons[index].bottom) {
            return index == 0U ? SkeletalAssetDocument::Mesh : SkeletalAssetDocument::Skeleton;
        }
    }
    return std::nullopt;
}

std::optional<SkeletalAssetCommand> SkeletalMeshEditorPanelRenderer::CommandAt(
    const RECT& content, const EditorSceneContext& sceneContext, int x, int y) {
    const SkeletalMeshEditorPanelLayout layout = ResolvePanelLayout(content, sceneContext);
    if (x < layout.commandBar.left || x >= layout.commandBar.right ||
        y < layout.commandBar.top || y >= layout.commandBar.bottom) return std::nullopt;
    const std::vector<CommandDescriptor> commands = BuildCommands(sceneContext);
    for (std::size_t index = 0U; index < commands.size(); ++index) {
        const RECT button = CommandRect(layout.commandBar, commands, index);
        if (x >= button.left && x < button.right && y >= button.top && y < button.bottom) {
            return commands[index].enabled
                ? std::optional<SkeletalAssetCommand>{ commands[index].command }
                : std::nullopt;
        }
    }
    return std::nullopt;
}

} // namespace kb::editor

#endif
