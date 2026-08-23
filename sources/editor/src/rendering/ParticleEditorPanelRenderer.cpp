#include "rendering/ParticleEditorPanelRenderer.hpp"

#if defined(_WIN32)
#include "engine/scene/SceneAssets.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/ParticleEditorPanelLayout.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "scene/EditorSceneContext.hpp"

#include <bit>
#include <cstdint>
#include <string>

namespace kb::editor {
namespace {

enum class ParticleEditorButtonTone : std::uint8_t { Neutral, Primary, Selected, Toggle, Destructive };

void DrawButton(HDC dc, const RECT& rect, const char* label,
                ParticleEditorButtonTone tone = ParticleEditorButtonTone::Neutral, bool enabled = true) {
    COLORREF fill = RGB(45, 49, 57);
    COLORREF border = RGB(69, 75, 85);
    COLORREF textColor = RGB(225, 230, 237);
    if (!enabled) {
        fill = RGB(39, 42, 48);
        border = RGB(57, 61, 68);
        textColor = RGB(128, 136, 147);
    } else if (tone == ParticleEditorButtonTone::Primary) {
        fill = RGB(45, 104, 157);
        border = RGB(91, 157, 221);
    } else if (tone == ParticleEditorButtonTone::Selected) {
        fill = RGB(51, 82, 119);
        border = RGB(94, 156, 219);
    } else if (tone == ParticleEditorButtonTone::Toggle) {
        fill = RGB(43, 91, 74);
        border = RGB(83, 157, 125);
    } else if (tone == ParticleEditorButtonTone::Destructive) {
        fill = RGB(87, 52, 57);
        border = RGB(171, 91, 99);
    }
    GdiDrawing::FillRectColor(dc, rect, fill);
    const HBRUSH borderBrush = CreateSolidBrush(border);
    if (borderBrush != nullptr) {
        FrameRect(dc, &rect, borderBrush);
        DeleteObject(borderBrush);
    }
    SetTextColor(dc, textColor);
    RECT text = rect;
    DrawTextA(dc, label, -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void DrawSectionHeader(HDC dc, const RECT& rect, const char* label) {
    GdiDrawing::FillRectColor(dc, rect, RGB(34, 38, 45));
    const HBRUSH accent = CreateSolidBrush(RGB(82, 151, 214));
    if (accent != nullptr) {
        RECT rule{rect.left, rect.top + 5, rect.left + 2, rect.bottom - 5};
        FillRect(dc, &rule, accent);
        DeleteObject(accent);
    }
    RECT text = rect;
    text.left += 8;
    SetTextColor(dc, RGB(202, 213, 226));
    DrawTextA(dc, label, -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

[[nodiscard]] unsigned int WindowDpi(HWND window) noexcept {
    using GetDpiForWindowFunction = UINT(WINAPI*)(HWND);
    static_assert(sizeof(GetDpiForWindowFunction) == sizeof(FARPROC));
    static const GetDpiForWindowFunction getDpiForWindow = []() noexcept {
        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        const FARPROC address = user32 == nullptr ? nullptr : GetProcAddress(user32, "GetDpiForWindow");
        return address == nullptr ? nullptr : std::bit_cast<GetDpiForWindowFunction>(address);
    }();
    if (getDpiForWindow != nullptr) {
        const UINT dpi = getDpiForWindow(window);
        if (dpi != 0U)
            return dpi;
    }
    HDC dc = GetDC(window);
    if (dc == nullptr)
        return 96U;
    const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(window, dc);
    return dpi > 0 ? static_cast<unsigned int>(dpi) : 96U;
}
}

RECT ParticleEditorPanelRenderer::ViewportRect(const RECT& content, unsigned int dpi) noexcept {
    return ParticleEditorPanelLayoutResolver::Resolve(content, {}, 0, dpi).preview;
}

bool ParticleEditorPanelRenderer::PresentViewport(
    EditorSceneBgfxViewport& viewport,
    HWND host,
    const RECT& content,
    const DockPanel& panel,
    const EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& settings) {
    const kb::scene::Scene* preview = sceneContext.ParticleEditorPreviewScene();
    const RECT renderRect = ViewportRect(content, WindowDpi(host));
    if (preview == nullptr || renderRect.right <= renderRect.left || renderRect.bottom <= renderRect.top) return false;
    const std::uint64_t revision = sceneContext.ParticleEditorPreviewRevision();
    EditorSceneBgfxViewport::PresentSettings present{};
    present.viewportKey = panel.id;
    present.editorSceneOverlaysEnabled = false;
    present.sceneRevision = revision;
    present.sceneDirtyBaseRevision = revision;
    present.sceneFullSyncRequired = false;
    present.msaaSamples = settings.MsaaSamples();
    present.shadowPassEnabled = false;
    present.postProcessEnabled = true;
    present.selectionMaskEnabled = false;
    present.selectionOutlineEnabled = false;
    present.gpuDrivenRuntimeDispatchEnabled = settings.GpuDrivenEnabled();
    viewport.Present(host, renderRect, *preview, present);
    return true;
}

void ParticleEditorPanelRenderer::Paint(
    HDC dc,
    HWND host,
    const RECT& content,
    const DockPanel& panel,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& renderBackendSettings,
    EditorSceneBgfxViewport* sceneViewport) const {
    const unsigned int dpi = WindowDpi(host);
    const auto rows = sceneContext.ParticleEditorEmitterRows();
    const auto inspector = sceneContext.ParticleEditorInspector();
    const auto& workspace = sceneContext.ParticleEditorWorkspace();
    const ParticleEditorPanelLayout layout = ParticleEditorPanelLayoutResolver::Resolve(
        content, rows, workspace.ComposerScrollOffset(), dpi, &inspector);
    GdiDrawing::FillRectColor(dc, content, RGB(27, 29, 33));
    GdiDrawing::FillRectColor(dc, layout.toolbar, RGB(34, 37, 43));
    GdiDrawing::FillRectColor(dc, layout.composer, RGB(30, 33, 38));
    GdiDrawing::FillRectColor(dc, layout.composerHeader, RGB(38, 42, 49));
    GdiDrawing::FillRectColor(dc, layout.statusBar, RGB(24, 26, 30));
    const ScopedFont font{12, FW_SEMIBOLD};
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(219, 225, 233));
    RECT header{layout.toolbar.left + 8, layout.toolbar.top, layout.toolbar.right - 8, layout.toolbar.bottom};
    std::string title = "21kb Particle System";
    if (sceneContext.HasParticleEditorAsset()) {
        const auto* metadata = sceneContext.Scene().Assets().Manager().Registry().Find(sceneContext.ParticleEditorAssetId());
        if (metadata != nullptr) title = metadata->name.empty() ? metadata->virtualPath.filename().string() : metadata->name;
        if (sceneContext.ParticleEditorDirty()) title += " *";
    }
    DrawTextA(dc, title.c_str(), -1, &header, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (!sceneContext.HasParticleEditorAsset()) {
        SetTextColor(dc, RGB(168, 178, 190));
        RECT empty = layout.preview;
        DrawTextA(dc, "Open a .kbvfx asset to begin editing.", -1, &empty,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }
    RECT composerTitle = layout.composerHeader;
    composerTitle.left += 8;
    SetTextColor(dc, RGB(219, 225, 233));
    DrawTextA(dc, "Emitters", -1, &composerTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DrawButton(dc, layout.addEmitter, "+ Add Emitter", ParticleEditorButtonTone::Primary,
        rows.size() < kb::scene::kParticleEffectMaxEmitters);
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, layout.emitterList.left, layout.emitterList.top,
                      layout.emitterList.right, layout.emitterList.bottom);
    for (std::size_t index = 0U; index < layout.emitterRowCount; ++index) {
        const auto& rowLayout = layout.emitterRows[index];
        const auto& row = rows[index];
        GdiDrawing::FillRectColor(dc, rowLayout.bounds,
            row.selected ? RGB(51, 72, 98) : RGB(39, 43, 50));
        RECT grip = rowLayout.dragGrip;
        SetTextColor(dc, RGB(140, 151, 165));
        DrawTextA(dc, "::", -1, &grip, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT nameRect = rowLayout.name;
        nameRect.left += 3;
        SetTextColor(dc, row.enabled ? RGB(226, 231, 238) : RGB(135, 144, 156));
        const std::string& name = workspace.RenameActive() &&
                workspace.RenameEmitterId() == row.emitterId
            ? workspace.RenameText() : row.name;
        DrawTextA(dc, name.c_str(), -1, &nameRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        DrawButton(dc, rowLayout.enabledToggle, row.enabled ? "On" : "Off",
            row.enabled ? ParticleEditorButtonTone::Toggle : ParticleEditorButtonTone::Neutral);
        DrawButton(dc, rowLayout.moveUp, "^");
        DrawButton(dc, rowLayout.moveDown, "v");
        DrawButton(dc, rowLayout.remove, "x", ParticleEditorButtonTone::Destructive);
    }
    DrawSectionHeader(dc, layout.outputHeader, "Output");
    DrawButton(dc, layout.materialPicker, "Material");
    DrawButton(dc, layout.meshPicker, "Mesh");
    DrawButton(dc, layout.texturePicker, "Atlas");
    for (std::size_t index = 0U; index < layout.outputChoiceCount; ++index) {
        const auto& choice = inspector.outputChoices[index];
        const auto* asset = sceneContext.ParticleEditorWorkingAsset();
        const auto* emitter = asset == nullptr ? nullptr : kb::particle_editor::ParticleEmitterListModel::Find(*asset, inspector.emitterId);
        const bool active = emitter != nullptr && emitter->output.type == choice.type;
        DrawButton(dc, layout.outputChoices[index], choice.label.c_str(),
            active ? ParticleEditorButtonTone::Selected : ParticleEditorButtonTone::Neutral, choice.enabled);
        if (!choice.enabled) {
            SetTextColor(dc, RGB(161, 124, 124));
            RECT mark = layout.outputChoices[index]; mark.left = mark.right - 28;
            DrawTextA(dc, "N/A", -1, &mark, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }
    DrawSectionHeader(dc, layout.propertyHeader, "Properties");
    for (std::size_t index = 0U; index < layout.propertyRowCount; ++index) {
        const auto& property = inspector.properties[index];
        GdiDrawing::FillRectColor(dc, layout.propertyRows[index], RGB(36, 40, 46));
        RECT label = layout.propertyRows[index]; label.left += 3; label.right = (label.left + label.right) / 2;
        RECT value = layout.propertyRows[index]; value.left = label.right + 4; value.right -= 3;
        SetTextColor(dc, RGB(180, 190, 202));
        DrawTextA(dc, property.label.c_str(), -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SetTextColor(dc, property.editable ? RGB(226, 231, 238) : RGB(135, 144, 156));
        DrawTextA(dc, property.value.c_str(), -1, &value, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    DrawSectionHeader(dc, layout.moduleHeader, "Modules");
    DrawButton(dc, layout.addModule, "+ Add Module", ParticleEditorButtonTone::Primary,
        inspector.modules.size() < kb::scene::kParticleEffectMaxModulesPerEmitter);
    for (std::size_t index = 0U; index < layout.moduleRowCount; ++index) {
        const auto& module = inspector.modules[index];
        const auto& row = layout.moduleRows[index];
        GdiDrawing::FillRectColor(dc, row.bounds, module.selected ? RGB(51, 72, 98) : RGB(39, 43, 50));
        RECT grip = row.dragGrip; SetTextColor(dc, RGB(140, 151, 165));
        DrawTextA(dc, "::", -1, &grip, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT name = row.name; name.left += 3;
        const std::string text = module.label + "  " + module.summary;
        SetTextColor(dc, module.enabled ? RGB(226, 231, 238) : RGB(135, 144, 156));
        DrawTextA(dc, text.c_str(), -1, &name, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        DrawButton(dc, row.enabledToggle, module.enabled ? "On" : "Off",
            module.enabled ? ParticleEditorButtonTone::Toggle : ParticleEditorButtonTone::Neutral);
        DrawButton(dc, row.moveUp, "^"); DrawButton(dc, row.moveDown, "v");
        DrawButton(dc, row.remove, "x", ParticleEditorButtonTone::Destructive);
    }
    if (workspace.ModuleDragActive() && layout.moduleRowCount != 0U) {
        const std::uint32_t order = std::min<std::uint32_t>(workspace.ModuleDragTargetOrder(),
            static_cast<std::uint32_t>(layout.moduleRowCount - 1U));
        const RECT& target = layout.moduleRows[order].bounds;
        GdiDrawing::FillRectColor(dc, {target.left, target.top - 1, target.right, target.top + 2}, RGB(80, 157, 230));
    }
    DrawSectionHeader(dc, layout.dependencyHeader, "Dependencies");
    RECT dependencies = layout.dependencyHeader;
    const std::string dependencyTitle = "Dependencies (" + std::to_string(inspector.dependencies.size()) + ")";
    DrawTextA(dc, dependencyTitle.c_str(), -1, &dependencies, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    for (std::size_t index = 0U; index < layout.dependencyRowCount; ++index) {
        SetTextColor(dc, RGB(155, 194, 232));
        RECT row = layout.dependencyRows[index];
        DrawTextA(dc, inspector.dependencies[index].virtualPath.c_str(), -1, &row,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    DrawSectionHeader(dc, layout.diagnosticHeader, "Diagnostics");
    RECT diagnostics = layout.diagnosticHeader;
    const std::string diagnosticTitle = "Diagnostics (" + std::to_string(inspector.diagnostics.size()) + ")";
    DrawTextA(dc, diagnosticTitle.c_str(), -1, &diagnostics, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    for (std::size_t index = 0U; index < layout.diagnosticRowCount; ++index) {
        SetTextColor(dc, RGB(214, 143, 143));
        RECT row = layout.diagnosticRows[index];
        const std::string text = kb::scene::FormatParticleEffectDiagnostic(inspector.diagnostics[index]);
        DrawTextA(dc, text.c_str(), -1, &row, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    if (workspace.EmitterDragActive() && layout.emitterRowCount != 0U) {
        const std::uint32_t order = std::min<std::uint32_t>(
            workspace.DragTargetOrder(), static_cast<std::uint32_t>(layout.emitterRowCount - 1U));
        const RECT& target = layout.emitterRows[order].bounds;
        GdiDrawing::FillRectColor(dc,
            {target.left, target.top - 1, target.right, target.top + 2}, RGB(80, 157, 230));
    }
    RestoreDC(dc, saved);
    const bool dirty = sceneContext.ParticleEditorDirty();
    const HBRUSH statusIndicator = CreateSolidBrush(dirty ? RGB(221, 161, 78) : RGB(89, 184, 136));
    if (statusIndicator != nullptr) {
        RECT dot{layout.statusBar.left + 8, layout.statusBar.top + 7,
                 layout.statusBar.left + 14, layout.statusBar.bottom - 7};
        FillRect(dc, &dot, statusIndicator);
        DeleteObject(statusIndicator);
    }
    SetTextColor(dc, RGB(166, 177, 190));
    RECT status = layout.statusBar;
    status.left += 20;
    const std::string statusText = std::to_string(rows.size()) + "/" +
        std::to_string(kb::scene::kParticleEffectMaxEmitters) + " emitters" +
        (dirty ? "  Unsaved changes" : "  Saved");
    DrawTextA(dc, statusText.c_str(), -1, &status,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (sceneViewport != nullptr) {
        static_cast<void>(PresentViewport(*sceneViewport, host, content, panel, sceneContext, renderBackendSettings));
    }
    static_cast<void>(theme);
}

} // namespace kb::editor
#endif
