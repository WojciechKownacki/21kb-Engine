#include "rendering/ProjectSettingsPanelRenderer.hpp"
#include "localization/EditorLocalization.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/components/EditorDialogStyle.hpp"
#include "rendering/HeroIconKind.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/EditorSettingsPanelRenderer.hpp"
#include "rendering/PluginsPanelRenderer.hpp"
#include "rendering/ProjectSettingsPanelLayout.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "platform/win32/Utf8Gdi.hpp"

#include <array>
#include <cctype>
#include <filesystem>
#include <algorithm>
#include <optional>
#include <string_view>
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

[[nodiscard]] std::string_view CategoryLabel(int index) {
    switch (static_cast<ProjectSettingsCategory>(index)) {
    case ProjectSettingsCategory::Inputs:
        return EditorText("projectSettings.category.inputs", "Inputs");
    case ProjectSettingsCategory::Graphics:
        return EditorText("projectSettings.category.graphics", "Graphics");
    case ProjectSettingsCategory::Physics:
        return EditorText("projectSettings.category.physics", "Physics");
    case ProjectSettingsCategory::Lighting:
        return EditorText("projectSettings.category.lighting", "Lighting");
    case ProjectSettingsCategory::EditorGeneral:
        return EditorText("projectSettings.category.editorGeneral", "General");
    case ProjectSettingsCategory::EditorSaving:
        return EditorText("projectSettings.category.editorSaving", "Saving");
    case ProjectSettingsCategory::Plugins:
        return EditorText("projectSettings.category.plugins", "Installed plugins");
    case ProjectSettingsCategory::Count:
    default:
        return {};
    }
}

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

void DrawText(HDC dc, RECT rect, std::string_view text, COLORREF color, int pointSize = 12, int weight = FW_NORMAL, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    ScopedFont font{ pointSize, weight };
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    kb::editor::DrawTextUtf8(dc, text.data(), static_cast<int>(text.size()), &rect, static_cast<int>(flags | DT_NOPREFIX));
}

void DrawTooltipText(HDC dc, RECT rect, std::string_view text, COLORREF color, int pointSize = 11, int weight = FW_NORMAL, UINT flags = DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS) {
    ScopedFont font{ pointSize, weight };
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    kb::editor::DrawTextUtf8(dc, text.data(), static_cast<int>(text.size()), &rect, static_cast<int>(flags | DT_NOPREFIX));
}

// "/Game/Input/Player.21kbinputcontext" -> "Player". Empty path -> "(None)".
[[nodiscard]] std::string MappingContextDisplayName(const std::string& virtualPath) {
    if (virtualPath.empty()) {
        return std::string{ EditorText("projectSettings.noAsset", "(None)") };
    }
    return std::filesystem::path{ virtualPath }.stem().string();
}

void DrawCategorySidebar(HDC dc, const ProjectSettingsPanelLayoutRects& rects, const EditorTheme& theme, int selectedCategory,
    const EditorHierarchySearchState& search) {
    GdiDrawing::FillRectColor(dc, rects.sidebar, Color(theme.chrome));
    GdiDrawing::FillRectColor(dc, rects.divider, Color(theme.borderChrome));

    const std::string_view query = search.Query();
    GdiDrawing::DrawSharpFrame(dc, rects.searchField, Color(theme.panel), Color(search.IsFocused() ? theme.accent : theme.borderPanel));
    constexpr int iconSize = 12;
    const int iconTop = static_cast<int>(rects.searchField.top) + (static_cast<int>(rects.searchField.bottom - rects.searchField.top) - iconSize) / 2;
    HeroIconPainter::Draw(dc, RECT{ rects.searchField.left + 8, iconTop, rects.searchField.left + 8 + iconSize, iconTop + iconSize },
        HeroIconKind::MagnifyingGlass, Color(theme.textDisabled), 1);
    const RECT searchText{ rects.searchField.left + 26, rects.searchField.top, rects.searchField.right - 6, rects.searchField.bottom };
    if (query.empty() && !search.IsFocused()) {
        DrawText(dc, searchText, EditorText("settings.searchPlaceholder", "Search settings"), Color(theme.textDisabled), 12);
    } else {
        const std::string shown = std::string{ query } + (search.IsFocused() ? "|" : "");
        DrawText(dc, searchText, shown.c_str(), Color(theme.textPrimary), 12);
    }

    const std::array<std::string_view, 3> groups{
        EditorText("settings.group.project", "PROJECT"),
        EditorText("settings.group.editor", "EDITOR"),
        EditorText("settings.group.plugins", "PLUGINS"),
    };
    for (int group = 0; group < static_cast<int>(groups.size()); ++group) {
        DrawText(dc, ProjectSettingsPanelLayout::GroupCaption(rects.sidebar, group), groups[static_cast<std::size_t>(group)],
            Color(theme.textDisabled), 10, FW_SEMIBOLD, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    // Every category reads the same; only the open one stands out, bright and bold on its band.
    for (int index = 0; index < kCategoryCount; ++index) {
        const RECT row = ProjectSettingsPanelLayout::CategoryRow(rects.sidebar, index);
        const bool selected = index == selectedCategory;
        if (selected) {
            GdiDrawing::FillRectColor(dc, row, Blend(Color(theme.panel), Color(theme.accent), 18));
            GdiDrawing::FillRectColor(dc, RECT{ row.left, row.top, row.left + 3, row.bottom }, RGB(86, 172, 205));
        }
        DrawText(dc, RECT{ row.left + 12, row.top, row.right - 8, row.bottom }, CategoryLabel(index),
            Color(selected ? theme.textPrimary : theme.textDisabled), 12, selected ? FW_SEMIBOLD : FW_NORMAL);
    }
}

void DrawSelectorBox(HDC dc, const RECT& box, const EditorTheme& theme, const std::string& display, bool isNone, bool open) {
    GdiDrawing::DrawSharpFrame(dc, box, Color(theme.chrome), Color(open ? theme.accent : theme.borderPanel));
    DrawText(dc, RECT{ box.left + 8, box.top, box.right - 22, box.bottom }, display.c_str(), Color(isNone ? theme.textDisabled : theme.textPrimary), 12);
    constexpr int chevronSize = 12;
    const int chevronLeft = static_cast<int>(box.right) - 12 - chevronSize / 2;
    const int chevronTop = static_cast<int>(box.top) + (static_cast<int>(box.bottom - box.top) - chevronSize) / 2;
    HeroIconPainter::Draw(dc, RECT{ chevronLeft, chevronTop, chevronLeft + chevronSize, chevronTop + chevronSize },
        HeroIconKind::ChevronDown, Color(open ? theme.textPrimary : theme.textSecondary), 1);
}

void DrawOptionButton(HDC dc, const RECT& rect, const EditorTheme& theme, std::string_view label, bool selected, bool enabled = true) {
    EditorDialogStyle::PaintOptionButton(dc, rect, theme, label, selected, enabled);
}

void DrawCheckbox(HDC dc, const RECT& rect, const EditorTheme& theme, bool checked) {
    EditorDialogStyle::PaintSwitch(dc, rect, theme, checked);
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
    std::string_view title;
    std::string_view body;
};

// Rows of the Lighting page: shadows and global illumination, each for computers and for mobile devices.
[[nodiscard]] std::string_view QualityLabel(int row) {
    switch (row) {
    case 0:
        return EditorText("projectSettings.shadowQuality", "Shadows (Desktop)");
    case 1:
        return EditorText("projectSettings.shadowQualityMobile", "Shadows (Mobile)");
    case 2:
        return EditorText("projectSettings.giQuality", "Global Illumination (Desktop)");
    case 3:
        return EditorText("projectSettings.giQualityMobile", "Global Illumination (Mobile)");
    default:
        return {};
    }
}

[[nodiscard]] std::string_view QualityLevelLabel(kb::project::ProjectQualityLevel level) {
    switch (level) {
    case kb::project::ProjectQualityLevel::Low:
        return EditorText("projectSettings.quality.low", "Low");
    case kb::project::ProjectQualityLevel::Medium:
        return EditorText("projectSettings.quality.medium", "Medium");
    case kb::project::ProjectQualityLevel::High:
        return EditorText("projectSettings.quality.high", "High");
    case kb::project::ProjectQualityLevel::Ultra:
        return EditorText("projectSettings.quality.ultra", "Ultra");
    case kb::project::ProjectQualityLevel::Auto:
    default:
        return EditorText("projectSettings.quality.auto", "Auto");
    }
}

[[nodiscard]] kb::project::ProjectQualityLevel QualityOf(const kb::project::ProjectSettings& settings, int row) noexcept {
    switch (row) {
    case 0: return settings.shadowQuality;
    case 1: return settings.shadowQualityMobile;
    case 2: return settings.giQuality;
    default: return settings.giQualityMobile;
    }
}

[[nodiscard]] TooltipContent TooltipForKind(ProjectSettingsTooltipKind kind) {
    switch (kind) {
    case ProjectSettingsTooltipKind::MappingContext:
        return TooltipContent{
            EditorText("projectSettings.mappingContext", "Mapping Context"),
            EditorText("projectSettings.mappingContextTooltip",
            "Selects the project-wide input mapping asset used by the runtime input system."),
        };
    case ProjectSettingsTooltipKind::PhysicsLayers:
        return TooltipContent{
            EditorText("projectSettings.physicsLayers", "Collision Layers Asset"),
            EditorText("projectSettings.physicsLayersTooltip",
            "Selects the project-wide Physics Layers asset used to configure collision and query interactions at runtime."),
        };
    case ProjectSettingsTooltipKind::InputEnabled:
        return TooltipContent{
            EditorText("projectSettings.inputEnabled", "Input Enabled"),
            EditorText("projectSettings.inputEnabledTooltip",
            "Enables or disables project input processing. Disable it for scenes that should ignore gameplay input."),
        };
    case ProjectSettingsTooltipKind::RenderBackend:
        return TooltipContent{
            EditorText("projectSettings.renderBackend", "bgfx Backend"),
            EditorText("projectSettings.renderBackendTooltip",
            "Chooses the rendering backend for the editor viewport. Changing it recreates renderer resources."),
        };
    case ProjectSettingsTooltipKind::LightingPath:
        return TooltipContent{
            EditorText("projectSettings.lightingPath", "Lighting Path"),
            EditorText("projectSettings.lightingPathTooltip",
            "Chooses Forward or Deferred scene lighting for opaque geometry. Transparent objects still render in the forward transparent pass."),
        };
    case ProjectSettingsTooltipKind::PostProcess:
        return TooltipContent{
            EditorText("projectSettings.postProcess", "Post FX"),
            EditorText("projectSettings.postProcessTooltip",
            "Master switch for scene post-processing such as anti-aliasing, bloom, selection outline and final composition."),
        };
    case ProjectSettingsTooltipKind::AntiAliasing:
        return TooltipContent{
            EditorText("projectSettings.antiAliasing", "Anti-Aliasing"),
            EditorText("projectSettings.antiAliasingTooltip",
            "Chooses exactly one AA path for the scene. TAA and FXAA run in post-process alongside your configured lighting path. "
            "MSAA multisamples the render target instead; on a Deferred-lit project it also forces per-sample Forward shading for "
            "this preview, since the G-buffer cannot be multisampled, so expect lower FPS than TAA/FXAA at the same resolution."),
        };
    case ProjectSettingsTooltipKind::MsaaSamples:
        return TooltipContent{
            EditorText("projectSettings.msaaSamples", "MSAA Samples"),
            EditorText("projectSettings.msaaSamplesTooltip",
            "Controls the hardware multisample count when Anti-Aliasing is set to MSAA. It is inactive for None, FXAA and TAA."),
        };
    case ProjectSettingsTooltipKind::Bloom:
        return TooltipContent{
            EditorText("projectSettings.bloom", "Bloom"),
            EditorText("projectSettings.bloomTooltip",
            "Adds a soft glow around bright pixels in the post-process chain. It depends on Post FX being enabled."),
        };
    case ProjectSettingsTooltipKind::Shadows:
        return TooltipContent{
            EditorText("projectSettings.shadows", "Shadows"),
            EditorText("projectSettings.shadowsTooltip",
            "Enables shadow submission for scene lighting. Turning it off reduces rendering cost while previewing layout or materials."),
        };
    case ProjectSettingsTooltipKind::SelectionOutline:
        return TooltipContent{
            EditorText("projectSettings.selectionOutline", "Selection Outline"),
            EditorText("projectSettings.selectionOutlineTooltip",
            "Draws the editor outline around selected objects through the post-process chain. It depends on Post FX being enabled."),
        };
    case ProjectSettingsTooltipKind::GpuDriven:
        return TooltipContent{
            EditorText("projectSettings.gpuDriven", "GPU Driven"),
            EditorText("projectSettings.gpuDrivenTooltip",
            "Enables runtime GPU-driven scene submission where supported. The renderer falls back when the selected backend lacks required features."),
        };
    case ProjectSettingsTooltipKind::ShadowQuality:
        return TooltipContent{
            QualityLabel(0),
            EditorText("projectSettings.shadowQualityTooltip",
            "Low uses fewer cascades and a shorter range; Ultra the sharpest maps and the most local shadows."),
        };
    case ProjectSettingsTooltipKind::ShadowQualityMobile:
        return TooltipContent{
            QualityLabel(1),
            EditorText("projectSettings.shadowQualityMobileTooltip",
            "Phones draw few local shadows; Medium is the usual choice, Ultra costs a lot of bandwidth."),
        };
    case ProjectSettingsTooltipKind::GiQuality:
        return TooltipContent{
            QualityLabel(2),
            EditorText("projectSettings.giQualityTooltip",
            "Low: a coarser voxel grid, cheaper. Ultra: the finest grid, the most detail."),
        };
    case ProjectSettingsTooltipKind::GiQualityMobile:
        return TooltipContent{
            QualityLabel(3),
            EditorText("projectSettings.giQualityMobileTooltip",
            "Phones and tablets use a coarser grid than computers; Medium is the usual choice."),
        };
    case ProjectSettingsTooltipKind::None:
    default:
        return {};
    }
}

// The one line under each setting: what it does, in a few words. The tooltip keeps the long form.
[[nodiscard]] std::string_view DescriptionFor(ProjectSettingsTooltipKind kind) {
    switch (kind) {
    case ProjectSettingsTooltipKind::MappingContext:
        return EditorText("projectSettings.mappingContextDescription", "Input mapping asset the game reads its controls from.");
    case ProjectSettingsTooltipKind::PhysicsLayers:
        return EditorText("projectSettings.physicsLayersDescription", "Which layers collide and which queries see each other.");
    case ProjectSettingsTooltipKind::InputEnabled:
        return EditorText("projectSettings.inputEnabledDescription", "Turns project input on or off for every scene.");
    case ProjectSettingsTooltipKind::RenderBackend:
        return EditorText("projectSettings.renderBackendDescription", "Graphics API used by the editor viewport.");
    case ProjectSettingsTooltipKind::LightingPath:
        return EditorText("projectSettings.lightingPathDescription", "How lights are computed for opaque objects.");
    case ProjectSettingsTooltipKind::PostProcess:
        return EditorText("projectSettings.postProcessDescription", "Master switch for anti-aliasing, bloom and the outline.");
    case ProjectSettingsTooltipKind::AntiAliasing:
        return EditorText("projectSettings.antiAliasingDescription", "Smooths jagged edges; pick one method.");
    case ProjectSettingsTooltipKind::MsaaSamples:
        return EditorText("projectSettings.msaaSamplesDescription", "Samples per pixel when Anti-Aliasing is MSAA.");
    case ProjectSettingsTooltipKind::Bloom:
        return EditorText("projectSettings.bloomDescription", "Soft glow around bright areas.");
    case ProjectSettingsTooltipKind::Shadows:
        return EditorText("projectSettings.shadowsDescription", "Lights cast shadows; off renders faster.");
    case ProjectSettingsTooltipKind::SelectionOutline:
        return EditorText("projectSettings.selectionOutlineDescription", "Outline around selected objects in the editor.");
    case ProjectSettingsTooltipKind::GpuDriven:
        return EditorText("projectSettings.gpuDrivenDescription", "Lets the GPU cull and submit the scene where supported.");
    case ProjectSettingsTooltipKind::ShadowQuality:
        return EditorText("projectSettings.shadowQualityDescription", "Shadow sharpness and range on computers.");
    case ProjectSettingsTooltipKind::ShadowQualityMobile:
        return EditorText("projectSettings.shadowQualityMobileDescription", "Shadow quality on phones and tablets.");
    case ProjectSettingsTooltipKind::GiQuality:
        return EditorText("projectSettings.giQualityDescription", "Detail of indirect light on computers.");
    case ProjectSettingsTooltipKind::GiQualityMobile:
        return EditorText("projectSettings.giQualityMobileDescription", "Detail of indirect light on phones and tablets.");
    case ProjectSettingsTooltipKind::None:
    default:
        return {};
    }
}

[[nodiscard]] bool ContainsIgnoringCase(std::string_view text, std::string_view needle) noexcept {
    const auto lower = [](char character) noexcept {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    };
    return std::search(text.begin(), text.end(), needle.begin(), needle.end(),
        [&lower](char a, char b) noexcept { return lower(a) == lower(b); }) != text.end();
}

struct SearchableSetting {
    std::string_view label;
    std::string_view description;
};

// Everything the search box looks through, per category: the label and description of each
// setting. Project pages share their texts with the tooltips; Editor and Plugins pages name theirs.
[[nodiscard]] std::vector<SearchableSetting> SearchableSettings(ProjectSettingsCategory category) {
    using Kind = ProjectSettingsTooltipKind;
    const auto fromKinds = [](std::initializer_list<Kind> kinds) {
        std::vector<SearchableSetting> settings;
        for (const Kind kind : kinds) {
            settings.push_back({ TooltipForKind(kind).title, DescriptionFor(kind) });
        }
        return settings;
    };
    switch (category) {
    case ProjectSettingsCategory::Inputs:
        return fromKinds({ Kind::MappingContext, Kind::InputEnabled });
    case ProjectSettingsCategory::Graphics:
        return fromKinds({ Kind::RenderBackend, Kind::LightingPath, Kind::PostProcess, Kind::AntiAliasing, Kind::MsaaSamples,
            Kind::Bloom, Kind::Shadows, Kind::SelectionOutline, Kind::GpuDriven });
    case ProjectSettingsCategory::Physics:
        return fromKinds({ Kind::PhysicsLayers });
    case ProjectSettingsCategory::Lighting:
        return fromKinds({ Kind::ShadowQuality, Kind::ShadowQualityMobile, Kind::GiQuality, Kind::GiQualityMobile });
    case ProjectSettingsCategory::EditorGeneral:
        return { { EditorText("editorSettings.language", "Language"),
            EditorText("settings.languageDescription", "Language of the editor's menus and panels.") } };
    case ProjectSettingsCategory::EditorSaving:
        return { { EditorText("editorSettings.autosave", "Autosave"), EditorText("settings.autosaveDescription", "Saves open scenes on its own.") },
            { EditorText("editorSettings.interval", "Interval"), EditorText("settings.intervalDescription", "Minutes between automatic saves.") } };
    case ProjectSettingsCategory::Plugins:
        return { { EditorText("projectSettings.category.plugins", "Installed plugins"),
            EditorText("settings.pluginsDescription", "Turn engine plugins on or off for this project.") } };
    case ProjectSettingsCategory::Count:
    default:
        return {};
    }
}

// A setting's label with its description under it. While searching, a setting that does not
// match is dimmed, so the matching ones stand out on the page.
void DrawSetting(HDC dc, const ProjectSettingsPanelLayoutRects& rects, const RECT& label, std::string_view text,
    ProjectSettingsTooltipKind kind, const EditorTheme& theme, std::string_view query, bool enabled = true) {
    const std::string_view description = DescriptionFor(kind);
    const bool matches = query.empty() || ContainsIgnoringCase(text, query) || ContainsIgnoringCase(description, query) ||
        ContainsIgnoringCase(TooltipForKind(kind).title, query);
    const bool active = enabled && matches;
    DrawText(dc, RECT{ label.left, label.top, label.right - 8, label.bottom }, text,
        Color(active ? (query.empty() ? theme.textSecondary : theme.textPrimary) : theme.textDisabled), 12,
        !query.empty() && matches ? FW_SEMIBOLD : FW_NORMAL);
    DrawText(dc, ProjectSettingsPanelLayout::DescriptionRect(rects, label), description, Color(theme.textDisabled), 11);
}

void DrawTooltip(HDC dc, const RECT& content, const EditorTheme& theme, const EditorProjectSettingsState& state) {
    const TooltipContent tooltip = TooltipForKind(state.TooltipKind());
    if (tooltip.title.empty()) {
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
    const kb::project::ProjectSettings& settings = sceneContext.ProjectConfiguration();
    const bool dropdownOpen = sceneContext.ProjectSettings().IsMappingContextDropdownOpen();
    const std::string_view query = sceneContext.ProjectSettings().Search().Query();

    GdiDrawing::FillRectColor(dc, rects.sectionHeader, Color(theme.strip));
    DrawText(dc, RECT{ rects.sectionHeader.left + 8, rects.sectionHeader.top, rects.sectionHeader.right - 8, rects.sectionHeader.bottom }, EditorText("projectSettings.inputsSection", "INPUTS"), Color(theme.textSecondary), 11, FW_SEMIBOLD);

    // Enabled first so an open dropdown can overlap it.
    DrawSetting(dc, rects, rects.enabledLabel, EditorText("projectSettings.inputEnabledLabel", "Enabled"), ProjectSettingsTooltipKind::InputEnabled, theme, query);
    DrawCheckbox(dc, rects.enabledCheckbox, theme, settings.inputEnabled);

    DrawSetting(dc, rects, rects.mappingLabel, EditorText("projectSettings.mappingContext", "Mapping Context"), ProjectSettingsTooltipKind::MappingContext, theme, query);
    const RECT fieldBox = ProjectSettingsPanelLayout::MappingFieldBox(rects);
    DrawSelectorBox(dc, fieldBox, theme, MappingContextDisplayName(settings.inputMappingContext), settings.inputMappingContext.empty(), dropdownOpen);
    if (dropdownOpen) {
        DrawDropdownList(dc, fieldBox, theme, sceneContext.ProjectInputMappingContextOptions(), settings.inputMappingContext, sceneContext.ProjectSettings().HoveredOption());
    }
}

void DrawGraphicsPage(
    HDC dc,
    const ProjectSettingsPanelLayoutRects& rects,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& renderBackendSettings) {
    using Kind = ProjectSettingsTooltipKind;
    const std::string_view query = sceneContext.ProjectSettings().Search().Query();
    GdiDrawing::FillRectColor(dc, rects.sectionHeader, Color(theme.strip));
    DrawText(dc, RECT{ rects.sectionHeader.left + 8, rects.sectionHeader.top, rects.sectionHeader.right - 8, rects.sectionHeader.bottom }, EditorText("projectSettings.graphicsSection", "GRAPHICS"), Color(theme.textSecondary), 11, FW_SEMIBOLD);

    DrawSetting(dc, rects, rects.backendLabel, EditorText("projectSettings.renderBackend", "bgfx Backend"), Kind::RenderBackend, theme, query);
    const EditorRenderBackend backend = renderBackendSettings.Backend();
    DrawOptionButton(dc, rects.backendAutoButton, theme, EditorText("projectSettings.backendAuto", "Auto"), backend == EditorRenderBackend::Auto);
    DrawOptionButton(dc, rects.backendDx12Button, theme, "DX12", backend == EditorRenderBackend::DirectX12);
    DrawOptionButton(dc, rects.backendVulkanButton, theme, "Vulkan", backend == EditorRenderBackend::Vulkan);

    DrawSetting(dc, rects, rects.lightingPathLabel, EditorText("projectSettings.lightingPath", "Lighting Path"), Kind::LightingPath, theme, query);
    const kb::project::ProjectSceneLightingPath lightingPath = sceneContext.ProjectConfiguration().lightingPath;
    DrawOptionButton(dc, rects.lightingPathForwardButton, theme, EditorText("projectSettings.lightingForward", "Forward"), lightingPath == kb::project::ProjectSceneLightingPath::Forward);
    DrawOptionButton(dc, rects.lightingPathForwardPlusButton, theme, EditorText("projectSettings.lightingForwardPlus", "Forward+"), lightingPath == kb::project::ProjectSceneLightingPath::ForwardPlus);
    DrawOptionButton(dc, rects.lightingPathDeferredButton, theme, EditorText("projectSettings.lightingDeferred", "Deferred"), lightingPath == kb::project::ProjectSceneLightingPath::Deferred);

    DrawSetting(dc, rects, rects.postProcessLabel, EditorText("projectSettings.postProcess", "Post FX"), Kind::PostProcess, theme, query);
    DrawCheckbox(dc, rects.postProcessCheckbox, theme, renderBackendSettings.PostProcessEnabled());
    DrawSetting(dc, rects, rects.antiAliasingLabel, EditorText("projectSettings.antiAliasing", "Anti-Aliasing"), Kind::AntiAliasing, theme, query);
    const EditorAntiAliasingMode aaMode = renderBackendSettings.AntiAliasingMode();
    DrawOptionButton(dc, rects.antiAliasingNoneButton, theme, EditorText("projectSettings.antiAliasingNone", "None"), aaMode == EditorAntiAliasingMode::None);
    DrawOptionButton(dc, rects.antiAliasingFxaaButton, theme, "FXAA", aaMode == EditorAntiAliasingMode::Fxaa);
    DrawOptionButton(dc, rects.antiAliasingTaaButton, theme, "TAA", aaMode == EditorAntiAliasingMode::Taa);
    DrawOptionButton(dc, rects.antiAliasingMsaaButton, theme, "MSAA", aaMode == EditorAntiAliasingMode::Msaa);
    const bool msaaActive = aaMode == EditorAntiAliasingMode::Msaa;
    DrawSetting(dc, rects, rects.msaaLabel, EditorText("projectSettings.msaaSamples", "MSAA Samples"), Kind::MsaaSamples, theme, query, msaaActive);
    DrawOptionButton(dc, rects.msaaOffButton, theme, EditorText("projectSettings.msaaOff", "Off"), msaaActive && renderBackendSettings.MsaaSamples() == 0U, msaaActive);
    DrawOptionButton(dc, rects.msaa2xButton, theme, "2x", msaaActive && renderBackendSettings.MsaaSamples() == 2U, msaaActive);
    DrawOptionButton(dc, rects.msaa4xButton, theme, "4x", msaaActive && renderBackendSettings.MsaaSamples() == 4U, msaaActive);
    DrawOptionButton(dc, rects.msaa8xButton, theme, "8x", msaaActive && renderBackendSettings.MsaaSamples() == 8U, msaaActive);
    DrawOptionButton(dc, rects.msaa16xButton, theme, "16x", msaaActive && renderBackendSettings.MsaaSamples() == 16U, msaaActive);
    DrawSetting(dc, rects, rects.bloomLabel, EditorText("projectSettings.bloom", "Bloom"), Kind::Bloom, theme, query);
    DrawCheckbox(dc, rects.bloomCheckbox, theme, renderBackendSettings.BloomEnabled());
    DrawSetting(dc, rects, rects.shadowsLabel, EditorText("projectSettings.shadows", "Shadows"), Kind::Shadows, theme, query);
    DrawCheckbox(dc, rects.shadowsCheckbox, theme, renderBackendSettings.ShadowsEnabled());
    DrawSetting(dc, rects, rects.selectionOutlineLabel, EditorText("projectSettings.selectionOutline", "Selection Outline"), Kind::SelectionOutline, theme, query);
    DrawCheckbox(dc, rects.selectionOutlineCheckbox, theme, renderBackendSettings.SelectionOutlineEnabled());
    DrawSetting(dc, rects, rects.gpuDrivenLabel, EditorText("projectSettings.gpuVisibilityDiagnostics", "GPU Visibility Diagnostics"), Kind::GpuDriven, theme, query);
    DrawCheckbox(dc, rects.gpuDrivenCheckbox, theme, renderBackendSettings.GpuDrivenEnabled());
}

void DrawLightingPage(HDC dc, const ProjectSettingsPanelLayoutRects& rects, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    const kb::project::ProjectSettings& settings = sceneContext.ProjectConfiguration();
    const std::string_view query = sceneContext.ProjectSettings().Search().Query();
    constexpr std::array<ProjectSettingsTooltipKind, 4> kinds{ ProjectSettingsTooltipKind::ShadowQuality,
        ProjectSettingsTooltipKind::ShadowQualityMobile, ProjectSettingsTooltipKind::GiQuality, ProjectSettingsTooltipKind::GiQualityMobile };

    GdiDrawing::FillRectColor(dc, rects.sectionHeader, Color(theme.strip));
    DrawText(dc, RECT{ rects.sectionHeader.left + 8, rects.sectionHeader.top, rects.sectionHeader.right - 8, rects.sectionHeader.bottom }, EditorText("projectSettings.lightingSection", "LIGHTING QUALITY"), Color(theme.textSecondary), 11, FW_SEMIBOLD);
    for (int row = 0; row < 4; ++row) {
        const RECT label = rects.qualityLabel[row];
        DrawSetting(dc, rects, label, QualityLabel(row), kinds[static_cast<std::size_t>(row)], theme, query);
        const kb::project::ProjectQualityLevel current = QualityOf(settings, row);
        for (int level = 0; level < 5; ++level) {
            const auto levelValue = static_cast<kb::project::ProjectQualityLevel>(level);
            DrawOptionButton(dc, rects.qualityButton[row][level], theme, QualityLevelLabel(levelValue), current == levelValue);
        }
    }
}

void DrawPhysicsPage(HDC dc, const ProjectSettingsPanelLayoutRects& rects, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    const kb::project::ProjectSettings& settings = sceneContext.ProjectConfiguration();
    const bool dropdownOpen = sceneContext.ProjectSettings().IsPhysicsLayersDropdownOpen();
    const std::string_view query = sceneContext.ProjectSettings().Search().Query();

    GdiDrawing::FillRectColor(dc, rects.sectionHeader, Color(theme.strip));
    DrawText(dc, RECT{ rects.sectionHeader.left + 8, rects.sectionHeader.top, rects.sectionHeader.right - 8, rects.sectionHeader.bottom }, EditorText("projectSettings.physicsSection", "PHYSICS"), Color(theme.textSecondary), 11, FW_SEMIBOLD);
    DrawSetting(dc, rects, rects.mappingLabel, EditorText("projectSettings.physicsLayers", "Collision Layers Asset"), ProjectSettingsTooltipKind::PhysicsLayers, theme, query);
    const RECT fieldBox = ProjectSettingsPanelLayout::MappingFieldBox(rects);
    DrawSelectorBox(dc, fieldBox, theme, MappingContextDisplayName(settings.physicsLayersAsset), settings.physicsLayersAsset.empty(), dropdownOpen);
    if (dropdownOpen) {
        DrawDropdownList(dc, fieldBox, theme, sceneContext.ProjectPhysicsLayersAssetOptions(), settings.physicsLayersAsset, sceneContext.ProjectSettings().HoveredOption());
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
    DrawText(dc, RECT{ rects.header.left + kPadding, rects.header.top, rects.header.right - kPadding, rects.header.bottom }, EditorText("panel.settings", "Settings"), Color(theme.textPrimary), 14, FW_SEMIBOLD);

    DrawCategorySidebar(dc, rects, theme, selectedCategory, sceneContext.ProjectSettings().Search());

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
    case ProjectSettingsCategory::Lighting:
        DrawLightingPage(dc, rects, theme, sceneContext);
        break;
    case ProjectSettingsCategory::EditorGeneral:
    case ProjectSettingsCategory::EditorSaving: {
        // The Editor Settings page itself, without its own title bar and category column.
        EditorSettingsPanelRenderer::SelectCategory(static_cast<ProjectSettingsCategory>(selectedCategory) == ProjectSettingsCategory::EditorGeneral
            ? EditorSettingsCategory::General : EditorSettingsCategory::Saving);
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, rects.content.left, rects.content.top, rects.content.right, rects.content.bottom);
        EditorSettingsPanelRenderer{}.Paint(dc, EditorSettingsPanelRenderer::ContentForPage(rects.content), theme, sceneContext);
        RestoreDC(dc, saved);
        break;
    }
    case ProjectSettingsCategory::Plugins:
        PluginsPanelRenderer{}.Paint(dc, rects.content, theme, sceneContext);
        break;
    case ProjectSettingsCategory::Count:
    default:
        break;
    }

    DrawTooltip(dc, content, theme, sceneContext.ProjectSettings());
}

ProjectSettingsPanelRenderer::Hit ProjectSettingsPanelRenderer::HitTest(const RECT& content, const EditorSceneContext& sceneContext, int x, int y) {
    const ProjectSettingsPanelLayoutRects rects = ProjectSettingsPanelLayout::Resolve(content);
    if (PointInRect(rects.searchField, x, y)) {
        return Hit{ .kind = ProjectSettingsHitKind::SearchField, .index = -1, .rect = rects.searchField };
    }

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

    if (category == ProjectSettingsCategory::Lighting) {
        for (int row = 0; row < 4; ++row) {
            for (int level = 0; level < 5; ++level) {
                const RECT button = ProjectSettingsPanelLayout::QualityOptionButton(rects, row, level);
                if (PointInRect(button, x, y)) {
                    return Hit{ .kind = ProjectSettingsHitKind::QualityOption, .index = row * 5 + level, .rect = button };
                }
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

std::optional<RECT> ProjectSettingsPanelRenderer::EmbeddedPage(const RECT& content, const EditorSceneContext& sceneContext) {
    switch (static_cast<ProjectSettingsCategory>(sceneContext.ProjectSettings().SelectedCategory())) {
    case ProjectSettingsCategory::EditorGeneral:
    case ProjectSettingsCategory::EditorSaving:
    case ProjectSettingsCategory::Plugins:
        return ProjectSettingsPanelLayout::Resolve(content).content;
    default:
        return std::nullopt;
    }
}

bool ProjectSettingsPanelRenderer::CategoryMatches(int category, std::string_view query) {
    if (query.empty()) {
        return true;
    }
    if (ContainsIgnoringCase(CategoryLabel(category), query)) {
        return true;
    }
    for (const SearchableSetting& setting : SearchableSettings(static_cast<ProjectSettingsCategory>(category))) {
        if (ContainsIgnoringCase(setting.label, query) || ContainsIgnoringCase(setting.description, query)) {
            return true;
        }
    }
    return false;
}

std::optional<int> ProjectSettingsPanelRenderer::FirstMatchingCategory(std::string_view query) {
    for (int category = 0; category < kCategoryCount; ++category) {
        if (CategoryMatches(category, query)) {
            return category;
        }
    }
    return std::nullopt;
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

    if (category == ProjectSettingsCategory::Lighting) {
        for (int row = 0; row < 4; ++row) {
            if (PointInRect(rects.qualityLabel[row], x, y)) {
                return Hit{ .kind = ProjectSettingsHitKind::QualityOption, .index = row * 5, .rect = rects.qualityLabel[row] };
            }
        }
    }

    if (category == ProjectSettingsCategory::Physics && (PointInRect(rects.mappingLabel, x, y) || PointInRect(rects.mappingField, x, y))) {
        return Hit{ .kind = ProjectSettingsHitKind::PhysicsLayersField, .index = -1, .rect = rects.mappingField };
    }

    return direct;
}

} // namespace kb::editor

#endif
