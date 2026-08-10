#include "rendering/SkeletalMeshEditorPanelRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/SkeletalMeshEditorBonePicker.hpp"
#include "rendering/SkeletalMeshEditorPanelLayout.hpp"
#include "rendering/SkeletalMeshEditorSceneLabelBuilder.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
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
constexpr int kTreeRowHeight = 20;
constexpr int kTreeAuxiliaryHeight = 76;

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
    const ScopedFont bodyFont{ 11, FW_NORMAL };
    const ScopedGdiObject selectedBodyFont(dc, bodyFont.handle);
    SetTextColor(dc, RGB(145, 155, 168));
    RECT filter{ rect.left + 14, rect.top + 31, rect.right - 12, rect.top + 49 };
    const std::string filterText = sceneContext.SkeletalMeshEditorTreeFilter().empty()
        ? "Search bones and sockets" : sceneContext.SkeletalMeshEditorTreeFilter();
    DrawTextA(dc, filterText.c_str(), -1, &filter, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    const std::vector<SkeletalMeshEditorTreeRow> rows = sceneContext.SkeletalMeshEditorTreeRows();
    for (std::size_t index = 0U; index < rows.size(); ++index) {
        RECT row{ rect.left + 1, rect.top + kTreeHeaderHeight + static_cast<int>(index) * kTreeRowHeight,
            rect.right - 1, rect.top + kTreeHeaderHeight + static_cast<int>(index + 1U) * kTreeRowHeight };
        if (row.top >= rect.bottom - kTreeAuxiliaryHeight) break;
        if (rows[index].selected) GdiDrawing::FillRectColor(dc, row, RGB(35, 75, 112));
        RECT label{ row.left + 10 + static_cast<int>(rows[index].depth) * 14, row.top, row.right - 6, row.bottom };
        SetTextColor(dc, rows[index].kind == SkeletalMeshEditorTreeItemKind::Socket ? RGB(120, 196, 176) : RGB(211, 217, 225));
        DrawTextA(dc, rows[index].label.c_str(), -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    const RECT morphPanel{ rect.left + 1, rect.bottom - kTreeAuxiliaryHeight, rect.right - 1, rect.bottom - 38 };
    const RECT curvesPanel{ rect.left + 1, rect.bottom - 38, rect.right - 1, rect.bottom - 1 };
    GdiDrawing::DrawSharpFrame(dc, morphPanel, RGB(28, 30, 34), RGB(53, 57, 64));
    GdiDrawing::DrawSharpFrame(dc, curvesPanel, RGB(28, 30, 34), RGB(53, 57, 64));
    RECT morphTitle{ morphPanel.left + 8, morphPanel.top + 3, morphPanel.right - 8, morphPanel.top + 18 };
    SetTextColor(dc, RGB(207, 214, 222));
    const bool skeletonDocument = sceneContext.IsSkeletalMeshEditorSkeletonDocument();
    const char* morphPanelTitle = skeletonDocument ? "Preview Geometry" : "Morph Targets";
    DrawTextA(dc, morphPanelTitle, -1, &morphTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    const std::vector<kb::scene::SkeletalMeshMorphTarget>& morphs = sceneContext.SkeletalMeshEditorMorphTargets();
    const std::string morphSummary = skeletonDocument
        ? (sceneContext.SkeletalMeshEditorAssetId().IsValid()
            ? "Compatible mesh is preview-only."
            : "None. Skeleton-only reference pose.")
        : (morphs.empty()
        ? "None"
        : morphs.front().name + " (LOD " + std::to_string(morphs.front().lodIndex) + ", " +
            std::to_string(morphs.front().deltas.size()) + " deltas)" +
            (morphs.size() > 1U ? " +" + std::to_string(morphs.size() - 1U) : ""));
    RECT morphValue{ morphPanel.left + 8, morphPanel.top + 19, morphPanel.right - 8, morphPanel.bottom - 3 };
    SetTextColor(dc, RGB(120, 196, 176));
    DrawTextA(dc, morphSummary.c_str(), -1, &morphValue, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    RECT curveTitle{ curvesPanel.left + 8, curvesPanel.top + 3, curvesPanel.right - 8, curvesPanel.top + 18 };
    SetTextColor(dc, RGB(207, 214, 222));
    DrawTextA(dc, "Curves", -1, &curveTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT curveValue{ curvesPanel.left + 8, curvesPanel.top + 18, curvesPanel.right - 8, curvesPanel.bottom - 2 };
    SetTextColor(dc, RGB(145, 155, 168));
    const char* curves = sceneContext.AnimationPreview().ClipAsset().IsValid() ? "Clip curve channels are available." : "Reference pose: no active clip.";
    DrawTextA(dc, curves, -1, &curveValue, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void PaintDetails(HDC dc, const RECT& rect, const EditorSceneContext& sceneContext) {
    GdiDrawing::FillRectColor(dc, rect, RGB(28, 30, 34));
    GdiDrawing::DrawSharpFrame(dc, rect, RGB(28, 30, 34), RGB(53, 57, 64));
    const SkeletalMeshEditorDetailsModel model = sceneContext.SkeletalMeshEditorDetails();
    const ScopedFont titleFont{ 13, FW_SEMIBOLD };
    const ScopedGdiObject selectedTitleFont(dc, titleFont.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(207, 214, 222));
    RECT title{ rect.left + 10, rect.top + 8, rect.right - 8, rect.top + 26 };
    const std::string titleText = (sceneContext.HasDirtySkeletalMeshEditorAssetEdit() ? "* " : "") +
        (model.title.empty() ? std::string{ "Asset Details" } : model.title);
    DrawTextA(dc, titleText.c_str(), -1, &title,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    const ScopedFont bodyFont{ 11, FW_NORMAL };
    const ScopedGdiObject selectedBodyFont(dc, bodyFont.handle);
    int y = rect.top + 30;
    for (const SkeletalMeshEditorDetailsSection& section : model.sections) {
        if (y + 18 > rect.bottom) break;
        RECT sectionRect{ rect.left + 8, y, rect.right - 8, y + 18 };
        SetTextColor(dc, RGB(139, 149, 161));
        DrawTextA(dc, section.title.c_str(), -1, &sectionRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        y += 18;
        for (const SkeletalMeshEditorDetailsField& field : section.fields) {
            if (y + 18 > rect.bottom) return;
            RECT label{ rect.left + 12, y, rect.left + 104, y + 18 };
            RECT value{ rect.left + 106, y, rect.right - 8, y + 18 };
            SetTextColor(dc, RGB(154, 164, 176));
            DrawTextA(dc, field.label.c_str(), -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SetTextColor(dc, RGB(211, 217, 225));
            DrawTextA(dc, field.value.c_str(), -1, &value, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            y += 18;
        }
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
    kb::scene::SkeletonBoneId selectedBone,
    const EditorViewportCameraState& camera,
    std::uint32_t viewportHeight) {
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
    SkeletalMeshEditorSceneLabelBuilder::Append(output, overlays.labels, camera, viewportHeight);
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
    const SkeletalMeshEditorPanelLayout layout = SkeletalMeshEditorPanelLayoutResolver::Resolve(content);
    PaintLinkedDocuments(dc, layout, sceneContext);
    PaintCommands(dc, layout, sceneContext);
    PaintAdvancedPreview(dc, layout.toolbox, sceneContext);
    PaintTree(dc, layout.skeletonTree, sceneContext);
    PaintDetails(dc, layout.assetDetails, sceneContext);
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

    const SkeletalMeshEditorPanelLayout layout = SkeletalMeshEditorPanelLayoutResolver::Resolve(content);
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
    settings.physicsDebugLines = BuildSkeletalPreviewLines(
        sceneContext.AnimationPreviewOverlays(), sceneContext.SelectedSkeletalMeshEditorBone(),
        sceneContext.AnimationPreviewCamera(), renderHeight);
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
    const SkeletalMeshEditorPanelLayout layout = SkeletalMeshEditorPanelLayoutResolver::Resolve(content);
    if (x < layout.skeletonTree.left || x >= layout.skeletonTree.right ||
        y < layout.skeletonTree.top + kTreeHeaderHeight || y >= layout.skeletonTree.bottom - kTreeAuxiliaryHeight) return std::nullopt;
    const std::size_t index = static_cast<std::size_t>((y - (layout.skeletonTree.top + kTreeHeaderHeight)) / kTreeRowHeight);
    const std::vector<SkeletalMeshEditorTreeRow> rows = sceneContext.SkeletalMeshEditorTreeRows();
    return index < rows.size() ? std::optional<SkeletalMeshEditorTreeRow>{ rows[index] } : std::nullopt;
}

bool SkeletalMeshEditorPanelRenderer::IsTreeSearchAt(const RECT& content, int x, int y) noexcept {
    const SkeletalMeshEditorPanelLayout layout = SkeletalMeshEditorPanelLayoutResolver::Resolve(content);
    const RECT search{ layout.skeletonTree.left + 8, layout.skeletonTree.top + 30,
        layout.skeletonTree.right - 8, layout.skeletonTree.top + 50 };
    return x >= search.left && x < search.right && y >= search.top && y < search.bottom;
}

std::optional<std::uint8_t> SkeletalMeshEditorPanelRenderer::AdvancedPreviewOverlayAt(
    const RECT& content, int x, int y) noexcept {
    const SkeletalMeshEditorPanelLayout layout = SkeletalMeshEditorPanelLayoutResolver::Resolve(content);
    if (x < layout.toolbox.left || x >= layout.toolbox.right || y < layout.toolbox.top + 32) return std::nullopt;
    const int index = (y - (layout.toolbox.top + 32)) / 20;
    return index >= 0 && index < 6 ? std::optional<std::uint8_t>{ static_cast<std::uint8_t>(index) } : std::nullopt;
}

std::optional<kb::scene::SkeletonBoneId> SkeletalMeshEditorPanelRenderer::BoneAt(
    const RECT& content, const EditorSceneContext& sceneContext, int x, int y) noexcept {
    const SkeletalMeshEditorPanelLayout layout = SkeletalMeshEditorPanelLayoutResolver::Resolve(content);
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

std::optional<SkeletalAssetDocument> SkeletalMeshEditorPanelRenderer::LinkedDocumentAt(
    const RECT& content, int x, int y) noexcept {
    const SkeletalMeshEditorPanelLayout layout = SkeletalMeshEditorPanelLayoutResolver::Resolve(content);
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
    const SkeletalMeshEditorPanelLayout layout = SkeletalMeshEditorPanelLayoutResolver::Resolve(content);
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
