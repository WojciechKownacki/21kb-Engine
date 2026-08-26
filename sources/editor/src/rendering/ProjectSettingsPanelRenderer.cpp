#include "rendering/ProjectSettingsPanelRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/ProjectSettingsPanelLayout.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <filesystem>
#include <algorithm>
#include <string>
#include <vector>

namespace kb::editor {
namespace {

constexpr int kPadding = 16;
constexpr int kCategoryCount = static_cast<int>(ProjectSettingsCategory::Count);
constexpr int kTooltipWidth = 312;
constexpr int kTooltipPaddingX = 12;
constexpr int kTooltipPaddingY = 10;

[[nodiscard]] COLORREF Color(EditorColor color) noexcept {
    return GdiDrawing::ToColorRef(color);
}

[[nodiscard]] COLORREF Blend(COLORREF a, COLORREF b, int percentB) noexcept {
    const int percentA = 100 - percentB;
    return RGB(
        (GetRValue(a) * percentA + GetRValue(b) * percentB) / 100,
        (GetGValue(a) * percentA + GetGValue(b) * percentB) / 100,
        (GetBValue(a) * percentA + GetBValue(b) * percentB) / 100);
}

[[nodiscard]] const char* CategoryLabel(int index) noexcept {
    switch (static_cast<ProjectSettingsCategory>(index)) {
    case ProjectSettingsCategory::Inputs:
        return "Inputs";
    case ProjectSettingsCategory::Graphics:
        return "Graphics";
    case ProjectSettingsCategory::Physics:
        return "Physics";
    case ProjectSettingsCategory::Count:
    default:
        return "";
    }
}

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

void DrawText(HDC dc, RECT rect, const char* text, COLORREF color, int pointSize = 12, int weight = FW_NORMAL, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    ScopedFont font{ pointSize, weight };
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, static_cast<int>(flags | DT_NOPREFIX));
}

void DrawTooltipText(HDC dc, RECT rect, const char* text, COLORREF color, int pointSize = 11, int weight = FW_NORMAL, UINT flags = DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS) {
    ScopedFont font{ pointSize, weight };
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, static_cast<int>(flags | DT_NOPREFIX));
}

// "/Game/Input/Player.21kbinputcontext" -> "Player". Empty path -> "(None)".
[[nodiscard]] std::string MappingContextDisplayName(const std::string& virtualPath) {
    if (virtualPath.empty()) {
        return "(None)";
    }
    return std::filesystem::path{ virtualPath }.stem().string();
}

void DrawCategorySidebar(HDC dc, const ProjectSettingsPanelLayoutRects& rects, const EditorTheme& theme, int selectedCategory) {
    GdiDrawing::FillRectColor(dc, rects.sidebar, Color(theme.chrome));
    GdiDrawing::FillRectColor(dc, rects.divider, Color(theme.borderChrome));
    for (int index = 0; index < kCategoryCount; ++index) {
        const RECT row = ProjectSettingsPanelLayout::CategoryRow(rects.sidebar, index);
        const bool selected = index == selectedCategory;
        if (selected) {
            GdiDrawing::FillRectColor(dc, row, Blend(Color(theme.panel), Color(theme.accent), 18));
            GdiDrawing::FillRectColor(dc, RECT{ row.left, row.top, row.left + 3, row.bottom }, Color(theme.accent));
        }
        DrawText(dc, RECT{ row.left + 12, row.top, row.right - 8, row.bottom }, CategoryLabel(index), Color(selected ? theme.textPrimary : theme.textSecondary), 12, selected ? FW_SEMIBOLD : FW_NORMAL);
    }
}

void DrawSelectorBox(HDC dc, const RECT& box, const EditorTheme& theme, const std::string& display, bool isNone, bool open) {
    GdiDrawing::DrawSharpFrame(dc, box, Color(theme.chrome), Color(open ? theme.accent : theme.borderPanel));
    DrawText(dc, RECT{ box.left + 8, box.top, box.right - 22, box.bottom }, display.c_str(), Color(isNone ? theme.textDisabled : theme.textPrimary), 12);
    DrawText(dc, RECT{ box.right - 20, box.top, box.right - 4, box.bottom }, open ? "^" : "v", Color(theme.textSecondary), 11, FW_BOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawOptionButton(HDC dc, const RECT& rect, const EditorTheme& theme, const char* label, bool selected, bool enabled = true) {
    const COLORREF fill = selected ? Color(theme.accent) : Color(enabled ? theme.chrome : theme.background);
    const COLORREF border = selected ? Color(theme.accent) : Color(enabled ? theme.borderPanel : theme.borderChrome);
    const COLORREF text = Color(selected ? theme.textPrimary : (enabled ? theme.textSecondary : theme.textDisabled));
    GdiDrawing::DrawSharpFrame(dc, rect, fill, border);
    DrawText(dc, RECT{ rect.left + 8, rect.top, rect.right - 8, rect.bottom }, label, text, 12, selected ? FW_SEMIBOLD : FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawCheckbox(HDC dc, const RECT& rect, const EditorTheme& theme, bool checked) {
    const COLORREF boxFill = Color(checked ? theme.accent : theme.chrome);
    const COLORREF boxBorder = Color(checked ? theme.accent : theme.borderPanel);
    GdiDrawing::DrawSharpFrame(dc, rect, boxFill, boxBorder);
    if (checked) {
        DrawText(dc, rect, "x", Color(theme.textPrimary), 12, FW_BOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void DrawDropdownList(HDC dc, const RECT& fieldBox, const EditorTheme& theme, const std::vector<std::string>& options, const std::string& selected, int hoveredOption) {
    if (options.empty()) {
        return;
    }
    GdiDrawing::DrawSharpFrame(dc, ProjectSettingsPanelLayout::OptionListBounds(fieldBox, static_cast<int>(options.size())), Color(theme.chrome), Color(theme.accent));
    for (std::size_t index = 0; index < options.size(); ++index) {
        const RECT row = ProjectSettingsPanelLayout::OptionRow(fieldBox, static_cast<int>(index));
        const bool hovered = static_cast<int>(index) == hoveredOption;
        if (hovered) {
            GdiDrawing::FillRectColor(dc, GdiDrawing::Inset(row, 1), Blend(Color(theme.panel), Color(theme.accent), 24));
        } else if (options[index] == selected) {
            GdiDrawing::FillRectColor(dc, GdiDrawing::Inset(row, 1), Blend(Color(theme.panel), Color(theme.accent), 16));
        }
        const std::string label = MappingContextDisplayName(options[index]);
        DrawText(dc, RECT{ row.left + 8, row.top, row.right - 8, row.bottom }, label.c_str(), Color(options[index].empty() ? theme.textDisabled : theme.textPrimary), 12);
    }
}

struct TooltipContent {
    const char* title = "";
    const char* body = "";
};

[[nodiscard]] TooltipContent TooltipForKind(ProjectSettingsTooltipKind kind) noexcept {
    switch (kind) {
    case ProjectSettingsTooltipKind::MappingContext:
        return TooltipContent{
            "Mapping Context",
            "Selects the project-wide input mapping asset used by the runtime input system.",
        };
    case ProjectSettingsTooltipKind::PhysicsLayers:
        return TooltipContent{
            "Collision Layers Asset",
            "Selects the project-wide Physics Layers asset used to configure collision and query interactions at runtime.",
        };
    case ProjectSettingsTooltipKind::InputEnabled:
        return TooltipContent{
            "Input Enabled",
            "Enables or disables project input processing. Disable it for scenes that should ignore gameplay input.",
        };
    case ProjectSettingsTooltipKind::RenderBackend:
        return TooltipContent{
            "bgfx Backend",
            "Chooses the rendering backend for the editor viewport. Changing it recreates renderer resources.",
        };
    case ProjectSettingsTooltipKind::LightingPath:
        return TooltipContent{
            "Lighting Path",
            "Chooses Forward or Deferred scene lighting for opaque geometry. Transparent objects still render in the forward transparent pass.",
        };
    case ProjectSettingsTooltipKind::PostProcess:
        return TooltipContent{
            "Post FX",
            "Master switch for scene post-processing such as anti-aliasing, bloom, selection outline and final composition.",
        };
    case ProjectSettingsTooltipKind::AntiAliasing:
        return TooltipContent{
            "Anti-Aliasing",
            "Chooses exactly one AA path for the scene. TAA and FXAA run in post-process alongside your configured lighting path. "
            "MSAA multisamples the render target instead; on a Deferred-lit project it also forces per-sample Forward shading for "
            "this preview, since the G-buffer cannot be multisampled, so expect lower FPS than TAA/FXAA at the same resolution.",
        };
    case ProjectSettingsTooltipKind::MsaaSamples:
        return TooltipContent{
            "MSAA Samples",
            "Controls the hardware multisample count when Anti-Aliasing is set to MSAA. It is inactive for None, FXAA and TAA.",
        };
    case ProjectSettingsTooltipKind::Bloom:
        return TooltipContent{
            "Bloom",
            "Adds a soft glow around bright pixels in the post-process chain. It depends on Post FX being enabled.",
        };
    case ProjectSettingsTooltipKind::Shadows:
        return TooltipContent{
            "Shadows",
            "Enables shadow submission for scene lighting. Turning it off reduces rendering cost while previewing layout or materials.",
        };
    case ProjectSettingsTooltipKind::SelectionOutline:
        return TooltipContent{
            "Selection Outline",
            "Draws the editor outline around selected objects through the post-process chain. It depends on Post FX being enabled.",
        };
    case ProjectSettingsTooltipKind::GpuDriven:
        return TooltipContent{
            "GPU Driven",
            "Enables runtime GPU-driven scene submission where supported. The renderer falls back when the selected backend lacks required features.",
        };
    case ProjectSettingsTooltipKind::None:
    default:
        return {};
    }
}

void DrawTooltip(HDC dc, const RECT& content, const EditorTheme& theme, const EditorProjectSettingsState& state) {
    const TooltipContent tooltip = TooltipForKind(state.TooltipKind());
    if (tooltip.title[0] == '\0') {
        return;
    }

    constexpr int titleHeight = 19;
    constexpr int bodyHeight = 48;
    constexpr int gap = 5;
    const int width = kTooltipWidth;
    const int height = (kTooltipPaddingY * 2) + titleHeight + gap + bodyHeight;
    int left = state.TooltipX() + 16;
    int top = state.TooltipY() + 18;
    left = std::clamp(left, static_cast<int>(content.left) + 8, std::max(static_cast<int>(content.left) + 8, static_cast<int>(content.right) - width - 8));
    if (top + height > content.bottom - 8) {
        top = state.TooltipY() - height - 12;
    }
    top = std::clamp(top, static_cast<int>(content.top) + 8, std::max(static_cast<int>(content.top) + 8, static_cast<int>(content.bottom) - height - 8));

    const RECT popup{ left, top, left + width, top + height };
    GdiDrawing::DrawSharpFrame(dc, popup, Color(theme.chrome), Color(theme.borderPanel));
    GdiDrawing::FillRectColor(dc, RECT{ popup.left + 1, popup.top + 1, popup.right - 1, popup.top + 3 }, Color(theme.accent));

    RECT title{
        popup.left + kTooltipPaddingX,
        popup.top + kTooltipPaddingY,
        popup.right - kTooltipPaddingX,
        popup.top + kTooltipPaddingY + titleHeight,
    };
    DrawTooltipText(dc, title, tooltip.title, Color(theme.textPrimary), 12, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT body{
        title.left,
        title.bottom + gap,
        title.right,
        title.bottom + gap + bodyHeight,
    };
    DrawTooltipText(dc, body, tooltip.body, Color(theme.textSecondary));
}

void DrawInputsPage(HDC dc, const ProjectSettingsPanelLayoutRects& rects, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    const kb::project::ProjectDescriptor& project = sceneContext.Project();
    const bool dropdownOpen = sceneContext.ProjectSettings().IsMappingContextDropdownOpen();

    GdiDrawing::FillRectColor(dc, rects.sectionHeader, Color(theme.strip));
    DrawText(dc, RECT{ rects.sectionHeader.left + 8, rects.sectionHeader.top, rects.sectionHeader.right - 8, rects.sectionHeader.bottom }, "INPUTS", Color(theme.textSecondary), 11, FW_SEMIBOLD);

    // Enabled first so an open dropdown can overlap it.
    DrawText(dc, RECT{ rects.enabledLabel.left, rects.enabledLabel.top, rects.enabledLabel.right - 8, rects.enabledLabel.bottom }, "Enabled", Color(theme.textSecondary), 12);
    DrawCheckbox(dc, rects.enabledCheckbox, theme, project.inputEnabled);

    DrawText(dc, RECT{ rects.mappingLabel.left, rects.mappingLabel.top, rects.mappingLabel.right - 8, rects.mappingLabel.bottom }, "Mapping Context", Color(theme.textSecondary), 12);
    const RECT fieldBox = ProjectSettingsPanelLayout::MappingFieldBox(rects);
    DrawSelectorBox(dc, fieldBox, theme, MappingContextDisplayName(project.inputMappingContext), project.inputMappingContext.empty(), dropdownOpen);
    if (dropdownOpen) {
        DrawDropdownList(dc, fieldBox, theme, sceneContext.ProjectInputMappingContextOptions(), project.inputMappingContext, sceneContext.ProjectSettings().HoveredOption());
    }
}

void DrawGraphicsPage(
    HDC dc,
    const ProjectSettingsPanelLayoutRects& rects,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& renderBackendSettings) {
    GdiDrawing::FillRectColor(dc, rects.sectionHeader, Color(theme.strip));
    DrawText(dc, RECT{ rects.sectionHeader.left + 8, rects.sectionHeader.top, rects.sectionHeader.right - 8, rects.sectionHeader.bottom }, "GRAPHICS", Color(theme.textSecondary), 11, FW_SEMIBOLD);

    DrawText(dc, RECT{ rects.backendLabel.left, rects.backendLabel.top, rects.backendLabel.right - 8, rects.backendLabel.bottom }, "bgfx Backend", Color(theme.textSecondary), 12);
    const EditorRenderBackend backend = renderBackendSettings.Backend();
    DrawOptionButton(dc, rects.backendAutoButton, theme, "Auto", backend == EditorRenderBackend::Auto);
    DrawOptionButton(dc, rects.backendDx12Button, theme, "DX12", backend == EditorRenderBackend::DirectX12);
    DrawOptionButton(dc, rects.backendVulkanButton, theme, "Vulkan", backend == EditorRenderBackend::Vulkan);

    DrawText(dc, RECT{ rects.lightingPathLabel.left, rects.lightingPathLabel.top, rects.lightingPathLabel.right - 8, rects.lightingPathLabel.bottom }, "Lighting Path", Color(theme.textSecondary), 12);
    const kb::project::ProjectSceneLightingPath lightingPath = sceneContext.Project().sceneLightingPath;
    DrawOptionButton(dc, rects.lightingPathForwardButton, theme, "Forward", lightingPath == kb::project::ProjectSceneLightingPath::Forward);
    DrawOptionButton(dc, rects.lightingPathForwardPlusButton, theme, "Forward+", lightingPath == kb::project::ProjectSceneLightingPath::ForwardPlus);
    DrawOptionButton(dc, rects.lightingPathDeferredButton, theme, "Deferred", lightingPath == kb::project::ProjectSceneLightingPath::Deferred);

    DrawText(dc, rects.postProcessLabel, "Post FX", Color(theme.textSecondary), 12);
    DrawCheckbox(dc, rects.postProcessCheckbox, theme, renderBackendSettings.PostProcessEnabled());
    DrawText(dc, rects.antiAliasingLabel, "Anti-Aliasing", Color(theme.textSecondary), 12);
    const EditorAntiAliasingMode aaMode = renderBackendSettings.AntiAliasingMode();
    DrawOptionButton(dc, rects.antiAliasingNoneButton, theme, "None", aaMode == EditorAntiAliasingMode::None);
    DrawOptionButton(dc, rects.antiAliasingFxaaButton, theme, "FXAA", aaMode == EditorAntiAliasingMode::Fxaa);
    DrawOptionButton(dc, rects.antiAliasingTaaButton, theme, "TAA", aaMode == EditorAntiAliasingMode::Taa);
    DrawOptionButton(dc, rects.antiAliasingMsaaButton, theme, "MSAA", aaMode == EditorAntiAliasingMode::Msaa);
    const bool msaaActive = aaMode == EditorAntiAliasingMode::Msaa;
    DrawText(dc, rects.msaaLabel, "MSAA Samples", Color(msaaActive ? theme.textSecondary : theme.textDisabled), 12);
    DrawOptionButton(dc, rects.msaaOffButton, theme, "Off", msaaActive && renderBackendSettings.MsaaSamples() == 0U, msaaActive);
    DrawOptionButton(dc, rects.msaa2xButton, theme, "2x", msaaActive && renderBackendSettings.MsaaSamples() == 2U, msaaActive);
    DrawOptionButton(dc, rects.msaa4xButton, theme, "4x", msaaActive && renderBackendSettings.MsaaSamples() == 4U, msaaActive);
    DrawOptionButton(dc, rects.msaa8xButton, theme, "8x", msaaActive && renderBackendSettings.MsaaSamples() == 8U, msaaActive);
    DrawOptionButton(dc, rects.msaa16xButton, theme, "16x", msaaActive && renderBackendSettings.MsaaSamples() == 16U, msaaActive);
    DrawText(dc, rects.bloomLabel, "Bloom", Color(theme.textSecondary), 12);
    DrawCheckbox(dc, rects.bloomCheckbox, theme, renderBackendSettings.BloomEnabled());
    DrawText(dc, rects.shadowsLabel, "Shadows", Color(theme.textSecondary), 12);
    DrawCheckbox(dc, rects.shadowsCheckbox, theme, renderBackendSettings.ShadowsEnabled());
    DrawText(dc, rects.selectionOutlineLabel, "Selection Outline", Color(theme.textSecondary), 12);
    DrawCheckbox(dc, rects.selectionOutlineCheckbox, theme, renderBackendSettings.SelectionOutlineEnabled());
    DrawText(dc, rects.gpuDrivenLabel, "GPU Driven", Color(theme.textSecondary), 12);
    DrawCheckbox(dc, rects.gpuDrivenCheckbox, theme, renderBackendSettings.GpuDrivenEnabled());
}

void DrawPhysicsPage(HDC dc, const ProjectSettingsPanelLayoutRects& rects, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    const kb::project::ProjectDescriptor& project = sceneContext.Project();
    const bool dropdownOpen = sceneContext.ProjectSettings().IsPhysicsLayersDropdownOpen();

    GdiDrawing::FillRectColor(dc, rects.sectionHeader, Color(theme.strip));
    DrawText(dc, RECT{ rects.sectionHeader.left + 8, rects.sectionHeader.top, rects.sectionHeader.right - 8, rects.sectionHeader.bottom }, "PHYSICS", Color(theme.textSecondary), 11, FW_SEMIBOLD);
    DrawText(dc, RECT{ rects.mappingLabel.left, rects.mappingLabel.top, rects.mappingLabel.right - 8, rects.mappingLabel.bottom }, "Collision Layers Asset", Color(theme.textSecondary), 12);
    const RECT fieldBox = ProjectSettingsPanelLayout::MappingFieldBox(rects);
    DrawSelectorBox(dc, fieldBox, theme, MappingContextDisplayName(project.physicsLayersAsset), project.physicsLayersAsset.empty(), dropdownOpen);
    if (dropdownOpen) {
        DrawDropdownList(dc, fieldBox, theme, sceneContext.ProjectPhysicsLayersAssetOptions(), project.physicsLayersAsset, sceneContext.ProjectSettings().HoveredOption());
    }
}

} // namespace

void ProjectSettingsPanelRenderer::Paint(
    HDC dc,
    const RECT& content,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& renderBackendSettings) const {
    const ProjectSettingsPanelLayoutRects rects = ProjectSettingsPanelLayout::Resolve(content);
    const int selectedCategory = sceneContext.ProjectSettings().SelectedCategory();

    GdiDrawing::FillRectColor(dc, content, Color(theme.panel));

    // Title bar (full width).
    GdiDrawing::FillRectColor(dc, rects.header, Color(theme.strip));
    GdiDrawing::FillRectColor(dc, RECT{ rects.header.left, rects.header.top, rects.header.left + 3, rects.header.bottom }, Color(theme.accent));
    GdiDrawing::FillRectColor(dc, RECT{ rects.header.left, rects.header.bottom - 1, rects.header.right, rects.header.bottom }, Color(theme.borderChrome));
    DrawText(dc, RECT{ rects.header.left + kPadding, rects.header.top, rects.header.right - kPadding, rects.header.bottom }, "Project Settings", Color(theme.textPrimary), 14, FW_SEMIBOLD);

    DrawCategorySidebar(dc, rects, theme, selectedCategory);

    switch (static_cast<ProjectSettingsCategory>(selectedCategory)) {
    case ProjectSettingsCategory::Inputs:
        DrawInputsPage(dc, rects, theme, sceneContext);
        break;
    case ProjectSettingsCategory::Graphics:
        DrawGraphicsPage(dc, rects, theme, sceneContext, renderBackendSettings);
        break;
    case ProjectSettingsCategory::Physics:
        DrawPhysicsPage(dc, rects, theme, sceneContext);
        break;
    case ProjectSettingsCategory::Count:
    default:
        break;
    }

    DrawTooltip(dc, content, theme, sceneContext.ProjectSettings());
}

ProjectSettingsPanelRenderer::Hit ProjectSettingsPanelRenderer::HitTest(const RECT& content, const EditorSceneContext& sceneContext, int x, int y) {
    const ProjectSettingsPanelLayoutRects rects = ProjectSettingsPanelLayout::Resolve(content);

    // Left sidebar: category selection.
    for (int index = 0; index < kCategoryCount; ++index) {
        const RECT row = ProjectSettingsPanelLayout::CategoryRow(rects.sidebar, index);
        if (PointInRect(row, x, y)) {
            return Hit{ .kind = ProjectSettingsHitKind::CategoryItem, .index = index, .rect = row };
        }
    }

    const ProjectSettingsCategory category = static_cast<ProjectSettingsCategory>(sceneContext.ProjectSettings().SelectedCategory());
    if (category == ProjectSettingsCategory::Graphics) {
        for (int index = 0; index < 3; ++index) {
            const RECT button = ProjectSettingsPanelLayout::BackendOptionButton(rects, index);
            if (PointInRect(button, x, y)) {
                return Hit{ .kind = ProjectSettingsHitKind::RenderBackendOption, .index = index, .rect = button };
            }
        }
        for (int index = 0; index < 3; ++index) {
            const RECT button = ProjectSettingsPanelLayout::LightingPathOptionButton(rects, index);
            if (PointInRect(button, x, y)) {
                return Hit{ .kind = ProjectSettingsHitKind::LightingPathOption, .index = index, .rect = button };
            }
        }
        for (int index = 0; index < 5; ++index) {
            const RECT checkbox = ProjectSettingsPanelLayout::GraphicsToggleCheckbox(rects, index);
            const RECT label = ProjectSettingsPanelLayout::GraphicsToggleLabel(rects, index);
            if (PointInRect(checkbox, x, y) || PointInRect(label, x, y)) {
                return Hit{ .kind = ProjectSettingsHitKind::GraphicsToggle, .index = index, .rect = checkbox };
            }
        }
        for (int index = 0; index < 4; ++index) {
            const RECT button = ProjectSettingsPanelLayout::AntiAliasingModeButton(rects, index);
            if (PointInRect(button, x, y)) {
                return Hit{ .kind = ProjectSettingsHitKind::AntiAliasingMode, .index = index, .rect = button };
            }
        }
        for (int index = 0; index < 5; ++index) {
            const RECT button = ProjectSettingsPanelLayout::MsaaOptionButton(rects, index);
            if (PointInRect(button, x, y)) {
                return Hit{ .kind = ProjectSettingsHitKind::MsaaOption, .index = index, .rect = button };
            }
        }
        return Hit{};
    }

    if (category == ProjectSettingsCategory::Physics) {
        const RECT fieldBox = ProjectSettingsPanelLayout::MappingFieldBox(rects);
        if (sceneContext.ProjectSettings().IsPhysicsLayersDropdownOpen()) {
            const std::vector<std::string> options = sceneContext.ProjectPhysicsLayersAssetOptions();
            for (std::size_t index = 0; index < options.size(); ++index) {
                const RECT row = ProjectSettingsPanelLayout::OptionRow(fieldBox, static_cast<int>(index));
                if (PointInRect(row, x, y)) {
                    return Hit{ .kind = ProjectSettingsHitKind::PhysicsLayersOption, .index = static_cast<int>(index), .rect = row };
                }
            }
        }
        if (PointInRect(fieldBox, x, y)) {
            return Hit{ .kind = ProjectSettingsHitKind::PhysicsLayersField, .index = -1, .rect = fieldBox };
        }
        return Hit{};
    }

    // Remaining right pane controls belong to the Inputs category.
    if (category != ProjectSettingsCategory::Inputs) {
        return Hit{};
    }

    const RECT fieldBox = ProjectSettingsPanelLayout::MappingFieldBox(rects);
    if (sceneContext.ProjectSettings().IsMappingContextDropdownOpen()) {
        const std::vector<std::string> options = sceneContext.ProjectInputMappingContextOptions();
        for (std::size_t index = 0; index < options.size(); ++index) {
            const RECT row = ProjectSettingsPanelLayout::OptionRow(fieldBox, static_cast<int>(index));
            if (PointInRect(row, x, y)) {
                return Hit{ .kind = ProjectSettingsHitKind::MappingContextOption, .index = static_cast<int>(index), .rect = row };
            }
        }
    }

    if (PointInRect(fieldBox, x, y)) {
        return Hit{ .kind = ProjectSettingsHitKind::MappingContextField, .index = -1, .rect = fieldBox };
    }
    if (PointInRect(rects.enabledCheckbox, x, y) || PointInRect(rects.enabledLabel, x, y)) {
        return Hit{ .kind = ProjectSettingsHitKind::EnabledCheckbox, .index = -1, .rect = rects.enabledCheckbox };
    }
    return Hit{};
}

ProjectSettingsPanelRenderer::Hit ProjectSettingsPanelRenderer::TooltipHitTest(const RECT& content, const EditorSceneContext& sceneContext, int x, int y) {
    const Hit direct = HitTest(content, sceneContext, x, y);
    if (direct.kind != ProjectSettingsHitKind::None && direct.kind != ProjectSettingsHitKind::CategoryItem && direct.kind != ProjectSettingsHitKind::MappingContextOption && direct.kind != ProjectSettingsHitKind::PhysicsLayersOption) {
        return direct;
    }

    const ProjectSettingsPanelLayoutRects rects = ProjectSettingsPanelLayout::Resolve(content);
    const ProjectSettingsCategory category = static_cast<ProjectSettingsCategory>(sceneContext.ProjectSettings().SelectedCategory());
    if (category == ProjectSettingsCategory::Inputs) {
        if (PointInRect(rects.mappingLabel, x, y) || PointInRect(rects.mappingField, x, y)) {
            return Hit{ .kind = ProjectSettingsHitKind::MappingContextField, .index = -1, .rect = rects.mappingField };
        }
        if (PointInRect(rects.enabledLabel, x, y) || PointInRect(rects.enabledCheckbox, x, y)) {
            return Hit{ .kind = ProjectSettingsHitKind::EnabledCheckbox, .index = -1, .rect = rects.enabledCheckbox };
        }
        return direct;
    }

    if (category == ProjectSettingsCategory::Graphics) {
        if (PointInRect(rects.backendLabel, x, y)) {
            return Hit{ .kind = ProjectSettingsHitKind::RenderBackendOption, .index = -1, .rect = rects.backendLabel };
        }
        if (PointInRect(rects.lightingPathLabel, x, y)) {
            return Hit{ .kind = ProjectSettingsHitKind::LightingPathOption, .index = -1, .rect = rects.lightingPathLabel };
        }
        if (PointInRect(rects.antiAliasingLabel, x, y)) {
            return Hit{ .kind = ProjectSettingsHitKind::AntiAliasingMode, .index = -1, .rect = rects.antiAliasingLabel };
        }
        if (PointInRect(rects.msaaLabel, x, y)) {
            return Hit{ .kind = ProjectSettingsHitKind::MsaaOption, .index = -1, .rect = rects.msaaLabel };
        }
    }

    if (category == ProjectSettingsCategory::Physics && (PointInRect(rects.mappingLabel, x, y) || PointInRect(rects.mappingField, x, y))) {
        return Hit{ .kind = ProjectSettingsHitKind::PhysicsLayersField, .index = -1, .rect = rects.mappingField };
    }

    return direct;
}

} // namespace kb::editor

#endif
