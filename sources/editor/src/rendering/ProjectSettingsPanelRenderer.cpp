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
#include "rendering/gdi/CutTextAudit.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "platform/win32/Utf8Gdi.hpp"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <algorithm>
#include <optional>
#include <string_view>
#include <string>
#include <utility>
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
    case ProjectSettingsCategory::Display:
        return EditorText("projectSettings.category.display", "Display");
    case ProjectSettingsCategory::Audio:
        return EditorText("projectSettings.category.audio", "Audio");
    case ProjectSettingsCategory::Scenes:
        return EditorText("projectSettings.category.scenes", "Scenes");
    case ProjectSettingsCategory::Tags:
        return EditorText("projectSettings.category.tags", "Tags");
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
        // The hint is longer in some languages than the field is wide: it gets the largest size it fits at.
        const std::string_view hint = EditorText("settings.searchPlaceholder", "Search settings");
        DrawText(dc, searchText, hint, Color(theme.textDisabled),
            EditorDialogStyle::FittingSize(dc, hint, static_cast<int>(searchText.right - searchText.left), 12));
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

[[nodiscard]] std::string_view SettingLabel(ProjectSettingsTooltipKind kind) {
    switch (kind) {
    case ProjectSettingsTooltipKind::WindowMode:
        return EditorText("projectSettings.windowMode", "Window Mode");
    case ProjectSettingsTooltipKind::WindowSize:
        return EditorText("projectSettings.windowSize", "Window Size");
    case ProjectSettingsTooltipKind::Resizable:
        return EditorText("projectSettings.resizable", "Resizable");
    case ProjectSettingsTooltipKind::VSync:
        return "VSync";
    case ProjectSettingsTooltipKind::FrameRateLimit:
        return EditorText("projectSettings.frameRateLimit", "Frame Rate Limit");
    case ProjectSettingsTooltipKind::RenderScale:
        return EditorText("projectSettings.renderScale", "Render Scale");
    case ProjectSettingsTooltipKind::FixedStep:
        return EditorText("projectSettings.fixedStep", "Fixed Step (Hz)");
    case ProjectSettingsTooltipKind::Gravity:
        return EditorText("projectSettings.gravity", "Gravity");
    case ProjectSettingsTooltipKind::GravityPreset:
        return EditorText("projectSettings.gravityPreset", "Gravity Preset");
    case ProjectSettingsTooltipKind::TimeScale:
        return EditorText("projectSettings.timeScale", "Time Scale");
    case ProjectSettingsTooltipKind::MasterVolume:
        return EditorText("projectSettings.masterVolume", "Master Volume");
    case ProjectSettingsTooltipKind::Tags:
        return EditorText("projectSettings.category.tags", "Tags");
    default:
        return {};
    }
}

[[nodiscard]] std::string_view DescriptionFor(ProjectSettingsTooltipKind kind);

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
    case ProjectSettingsTooltipKind::WindowMode:
    case ProjectSettingsTooltipKind::WindowSize:
    case ProjectSettingsTooltipKind::Resizable:
    case ProjectSettingsTooltipKind::VSync:
    case ProjectSettingsTooltipKind::FrameRateLimit:
    case ProjectSettingsTooltipKind::RenderScale:
    case ProjectSettingsTooltipKind::FixedStep:
    case ProjectSettingsTooltipKind::Gravity:
    case ProjectSettingsTooltipKind::GravityPreset:
    case ProjectSettingsTooltipKind::TimeScale:
    case ProjectSettingsTooltipKind::MasterVolume:
    case ProjectSettingsTooltipKind::Tags:
        // These settings say all there is to say in their one-line description.
        return TooltipContent{ SettingLabel(kind), DescriptionFor(kind) };
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
    case ProjectSettingsTooltipKind::WindowMode:
        return EditorText("projectSettings.windowModeDescription", "How the game window opens: in a window or borderless fullscreen.");
    case ProjectSettingsTooltipKind::WindowSize:
        return EditorText("projectSettings.windowSizeDescription", "Size of the game window when it opens windowed.");
    case ProjectSettingsTooltipKind::Resizable:
        return EditorText("projectSettings.resizableDescription", "Lets the player resize the game window.");
    case ProjectSettingsTooltipKind::VSync:
        return EditorText("projectSettings.vsyncDescription", "Waits for the display refresh, which avoids tearing.");
    case ProjectSettingsTooltipKind::FrameRateLimit:
        return EditorText("projectSettings.frameRateLimitDescription", "Most frames per second the game draws; Off leaves it to VSync.");
    case ProjectSettingsTooltipKind::RenderScale:
        return EditorText("projectSettings.renderScaleDescription", "Share of the screen resolution the 3D scene renders at (0.5 to 1); the interface stays sharp.");
    case ProjectSettingsTooltipKind::FixedStep:
        return EditorText("projectSettings.fixedStepDescription", "Physics steps per second.");
    case ProjectSettingsTooltipKind::Gravity:
        return EditorText("projectSettings.gravityDescription", "Gravity of the scene in metres per second squared.");
    case ProjectSettingsTooltipKind::TimeScale:
        return EditorText("projectSettings.timeScaleDescription", "Speed of game time when a scene starts; 1 is normal.");
    case ProjectSettingsTooltipKind::MasterVolume:
        return EditorText("projectSettings.masterVolumeDescription", "Loudness of the whole game; 1 is full volume.");
    case ProjectSettingsTooltipKind::Tags:
        return EditorText("projectSettings.tagsDescription", "Tags for objects, shared by every scene, besides the built-in ones.");
    case ProjectSettingsTooltipKind::GravityPreset:
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
        return fromKinds({ Kind::PhysicsLayers, Kind::FixedStep, Kind::Gravity, Kind::GravityPreset, Kind::TimeScale });
    case ProjectSettingsCategory::Lighting:
        return fromKinds({ Kind::ShadowQuality, Kind::ShadowQualityMobile, Kind::GiQuality, Kind::GiQualityMobile });
    case ProjectSettingsCategory::Display:
        return fromKinds({ Kind::WindowMode, Kind::WindowSize, Kind::Resizable, Kind::VSync, Kind::FrameRateLimit, Kind::RenderScale });
    case ProjectSettingsCategory::Audio:
        return fromKinds({ Kind::MasterVolume });
    case ProjectSettingsCategory::Tags:
        return fromKinds({ Kind::Tags });
    case ProjectSettingsCategory::Scenes:
        return { { EditorText("projectSettings.scenesInBuild", "Scenes in Build"),
            EditorText("projectSettings.scenesInBuildDescription", "Scenes in the game package, in order. The first one starts the game.") } };
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

// How many rows the list of the open page has: the scenes of the build on the Scenes page, the tags on the Tags page.
[[nodiscard]] int ListedRowCount(const EditorSceneContext& sceneContext) {
    return static_cast<ProjectSettingsCategory>(sceneContext.ProjectSettings().SelectedCategory()) == ProjectSettingsCategory::Tags
        ? static_cast<int>(sceneContext.ProjectConfiguration().tags.size())
        : static_cast<int>(kb::project::ProjectBuildScenes(sceneContext.ProjectConfiguration()).size());
}

// A setting's label with its description under it. While searching, a setting that does not
// match is dimmed, so the matching ones stand out on the page.
void DrawSetting(HDC dc, const ProjectSettingsPanelLayoutRects& rects, const RECT& label, std::string_view text,
    ProjectSettingsTooltipKind kind, const EditorTheme& theme, std::string_view query, bool enabled = true) {
    const std::string_view description = DescriptionFor(kind);
    const bool matches = query.empty() || ContainsIgnoringCase(text, query) || ContainsIgnoringCase(description, query) ||
        ContainsIgnoringCase(TooltipForKind(kind).title, query);
    const bool active = enabled && matches;
    // The label is longer in some languages than its column is wide: it gets the largest size it fits at.
    const RECT room{ label.left, label.top, label.right - 8, label.bottom };
    const int weight = !query.empty() && matches ? FW_SEMIBOLD : FW_NORMAL;
    DrawText(dc, room, text, Color(active ? (query.empty() ? theme.textSecondary : theme.textPrimary) : theme.textDisabled),
        EditorDialogStyle::FittingSize(dc, text, static_cast<int>(room.right - room.left), 12, weight), weight);
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

// A setting's value in its cell: the number, or - while it is being typed - the text so far with a caret.
void DrawNumberCell(HDC dc, const RECT& cell, const EditorTheme& theme, const EditorSceneContext& sceneContext, ProjectSettingsNumber number) {
    const EditorProjectSettingsState& state = sceneContext.ProjectSettings();
    const bool editing = state.EditedNumber() == static_cast<int>(number);
    const std::string text = editing ? std::string{ state.NumberEdit().Query() } + "|"
                                     : ProjectSettingsPanelRenderer::NumberText(sceneContext.ProjectConfiguration(), number);
    GdiDrawing::DrawSharpFrame(dc, cell, Color(theme.chrome), Color(editing ? theme.accent : theme.borderPanel));
    DrawText(dc, GdiDrawing::Inset(cell, 2), text, Color(theme.textPrimary), 12, FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

// A value between a minus and a plus button.
void DrawStepper(HDC dc, const RECT& minus, const RECT& value, const RECT& plus, const EditorTheme& theme,
    const EditorSceneContext& sceneContext, ProjectSettingsNumber number) {
    DrawOptionButton(dc, minus, theme, "-", false);
    DrawOptionButton(dc, plus, theme, "+", false);
    DrawNumberCell(dc, value, theme, sceneContext, number);
}

void DrawDisplayPage(HDC dc, const ProjectSettingsPanelLayoutRects& rects, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    using Kind = ProjectSettingsTooltipKind;
    const kb::project::ProjectDisplaySettings& display = sceneContext.ProjectConfiguration().display;
    const std::string_view query = sceneContext.ProjectSettings().Search().Query();

    GdiDrawing::FillRectColor(dc, rects.sectionHeader, Color(theme.strip));
    DrawText(dc, RECT{ rects.sectionHeader.left + 8, rects.sectionHeader.top, rects.sectionHeader.right - 8, rects.sectionHeader.bottom }, EditorText("projectSettings.displaySection", "DISPLAY"), Color(theme.textSecondary), 11, FW_SEMIBOLD);

    DrawSetting(dc, rects, rects.displayLabel[0], SettingLabel(Kind::WindowMode), Kind::WindowMode, theme, query);
    DrawOptionButton(dc, rects.windowModeButton[0], theme, EditorText("projectSettings.windowModeWindowed", "Windowed"), display.windowMode == kb::project::ProjectWindowMode::Windowed);
    DrawOptionButton(dc, rects.windowModeButton[1], theme, EditorText("projectSettings.windowModeBorderless", "Borderless"), display.windowMode == kb::project::ProjectWindowMode::BorderlessFullscreen);

    DrawSetting(dc, rects, rects.displayLabel[1], SettingLabel(Kind::WindowSize), Kind::WindowSize, theme, query);
    for (std::size_t index = 0U; index < kProjectSettingsWindowSizes.size(); ++index) {
        const ProjectSettingsWindowSize& size = kProjectSettingsWindowSizes[index];
        const std::string text = std::to_string(size.width) + "x" + std::to_string(size.height);
        DrawOptionButton(dc, rects.windowSizeButton[index], theme, text, display.windowWidth == size.width && display.windowHeight == size.height);
    }
    DrawNumberCell(dc, rects.windowWidthValue, theme, sceneContext, ProjectSettingsNumber::WindowWidth);
    DrawNumberCell(dc, rects.windowHeightValue, theme, sceneContext, ProjectSettingsNumber::WindowHeight);

    DrawSetting(dc, rects, rects.displayLabel[2], SettingLabel(Kind::Resizable), Kind::Resizable, theme, query);
    DrawCheckbox(dc, rects.resizableCheckbox, theme, display.resizable);
    DrawSetting(dc, rects, rects.displayLabel[3], SettingLabel(Kind::VSync), Kind::VSync, theme, query);
    DrawCheckbox(dc, rects.vsyncCheckbox, theme, display.vsync);

    DrawSetting(dc, rects, rects.displayLabel[4], SettingLabel(Kind::FrameRateLimit), Kind::FrameRateLimit, theme, query);
    for (std::size_t index = 0U; index < kProjectSettingsFrameRates.size(); ++index) {
        const std::uint32_t rate = kProjectSettingsFrameRates[index];
        const std::string text = rate == 0U ? std::string{ EditorText("projectSettings.msaaOff", "Off") } : std::to_string(rate);
        DrawOptionButton(dc, rects.frameRateButton[index], theme, text, display.frameRateCap == rate);
    }
    DrawNumberCell(dc, rects.frameRateValue, theme, sceneContext, ProjectSettingsNumber::FrameRateCap);

    DrawSetting(dc, rects, rects.displayLabel[5], SettingLabel(Kind::RenderScale), Kind::RenderScale, theme, query);
    DrawNumberCell(dc, rects.renderScaleValue, theme, sceneContext, ProjectSettingsNumber::RenderScale);
}

void DrawAudioPage(HDC dc, const ProjectSettingsPanelLayoutRects& rects, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    const std::string_view query = sceneContext.ProjectSettings().Search().Query();
    GdiDrawing::FillRectColor(dc, rects.sectionHeader, Color(theme.strip));
    DrawText(dc, RECT{ rects.sectionHeader.left + 8, rects.sectionHeader.top, rects.sectionHeader.right - 8, rects.sectionHeader.bottom },
        EditorText("projectSettings.audioSection", "AUDIO"), Color(theme.textSecondary), 11, FW_SEMIBOLD);
    DrawSetting(dc, rects, rects.stepperLabel[5], SettingLabel(ProjectSettingsTooltipKind::MasterVolume), ProjectSettingsTooltipKind::MasterVolume, theme, query);
    DrawStepper(dc, rects.stepperButton[5][0], rects.stepperValue[5], rects.stepperButton[5][1], theme, sceneContext, ProjectSettingsNumber::MasterVolume);
}

// The cell that takes the name of a new tag: a hint while it is empty, the text so far with a caret while it is typed.
void DrawTagNameCell(HDC dc, const RECT& cell, const EditorTheme& theme, const EditorProjectSettingsState& state) {
    const bool editing = state.IsTagNameEditing();
    GdiDrawing::DrawSharpFrame(dc, cell, Color(theme.chrome), Color(editing ? theme.accent : theme.borderPanel));
    const RECT text{ cell.left + 8, cell.top, cell.right - 8, cell.bottom };
    if (editing) {
        const CutTextDataScope data;
        DrawText(dc, text, std::string{ state.NumberEdit().Query() } + "|", Color(theme.textPrimary), 12);
        return;
    }
    const std::string_view hint = EditorText("projectSettings.tagNameHint", "New tag name");
    DrawText(dc, text, hint, Color(theme.textDisabled),
        EditorDialogStyle::FittingSize(dc, hint, static_cast<int>(text.right - text.left), 12));
}

// The tags the project defines: a cell and a button that add one, and per tag a remove button. The list scrolls with
// the wheel when it is longer than the pane, like the Scenes page's.
void DrawTagsPage(HDC dc, const ProjectSettingsPanelLayoutRects& rects, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    const std::vector<std::string>& tags = sceneContext.ProjectConfiguration().tags;
    GdiDrawing::FillRectColor(dc, rects.sectionHeader, Color(theme.strip));
    DrawText(dc, RECT{ rects.sectionHeader.left + 8, rects.sectionHeader.top, rects.sectionHeader.right - 8, rects.sectionHeader.bottom },
        EditorText("projectSettings.tagsSection", "TAGS"), Color(theme.textSecondary), 11, FW_SEMIBOLD);
    DrawText(dc, rects.scenesIntro, DescriptionFor(ProjectSettingsTooltipKind::Tags), Color(theme.textDisabled), 11);
    DrawTagNameCell(dc, rects.tagNameField, theme, sceneContext.ProjectSettings());
    DrawOptionButton(dc, rects.tagAddButton, theme, EditorText("projectSettings.tagAdd", "Add tag"), false);

    const int capacity = ProjectSettingsPanelLayout::SceneRowCapacity(rects);
    const int first = ProjectSettingsPanelRenderer::FirstVisibleScene(sceneContext, rects);
    for (int visible = 0; visible < capacity && first + visible < static_cast<int>(tags.size()); ++visible) {
        const RECT row = ProjectSettingsPanelLayout::SceneRow(rects, visible);
        GdiDrawing::DrawSharpFrame(dc, row, Color(theme.chrome), Color(theme.borderPanel));
        // The tag's name is the project's data, not interface text: a long one ends in an ellipsis.
        const RECT label = ProjectSettingsPanelLayout::TagRowLabel(rects, visible);
        {
            const CutTextDataScope data;
            DrawText(dc, RECT{ row.left + 10, label.top, label.right, label.bottom }, tags[static_cast<std::size_t>(first + visible)], Color(theme.textPrimary), 12);
        }
        DrawOptionButton(dc, ProjectSettingsPanelLayout::SceneRemoveButton(rects, visible), theme, EditorText("projectSettings.sceneRemove", "Remove"), false);
    }
}

// The scenes a package holds, startup scene first: the list, a button that adds a scene, and per scene a move up
// and a remove button. The list scrolls with the wheel when it is longer than the pane.
void DrawScenesPage(HDC dc, const ProjectSettingsPanelLayoutRects& rects, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    const std::vector<std::string> scenes = kb::project::ProjectBuildScenes(sceneContext.ProjectConfiguration());
    const std::vector<std::string> projectScenes = sceneContext.ProjectScenePaths();

    GdiDrawing::FillRectColor(dc, rects.sectionHeader, Color(theme.strip));
    DrawText(dc, RECT{ rects.sectionHeader.left + 8, rects.sectionHeader.top, rects.sectionHeader.right - 8, rects.sectionHeader.bottom },
        EditorText("projectSettings.scenesSection", "SCENES IN BUILD"), Color(theme.textSecondary), 11, FW_SEMIBOLD);
    DrawText(dc, rects.scenesIntro, EditorText("projectSettings.scenesInBuildDescription", "Scenes in the game package, in order. The first one starts the game."),
        Color(theme.textDisabled), 11);
    DrawOptionButton(dc, rects.sceneAddButton, theme, EditorText("projectSettings.sceneAdd", "Add scene"), false);

    const int capacity = ProjectSettingsPanelLayout::SceneRowCapacity(rects);
    const int first = ProjectSettingsPanelRenderer::FirstVisibleScene(sceneContext, rects);
    for (int visible = 0; visible < capacity && first + visible < static_cast<int>(scenes.size()); ++visible) {
        const int index = first + visible;
        const std::string& scene = scenes[static_cast<std::size_t>(index)];
        const RECT row = ProjectSettingsPanelLayout::SceneRow(rects, visible);
        GdiDrawing::DrawSharpFrame(dc, row, Color(theme.chrome), Color(theme.borderPanel));
        // The first scene starts the game; the others show their place in the list.
        constexpr int kPlaceWidth = 96;
        const RECT place{ row.left + 10, row.top, row.left + kPlaceWidth, row.bottom };
        if (index == 0) {
            const std::string_view startup = EditorText("projectSettings.sceneStartup", "Startup");
            DrawText(dc, place, startup, Color(theme.accent), EditorDialogStyle::FittingSize(dc, startup, static_cast<int>(place.right - place.left), 12, FW_SEMIBOLD), FW_SEMIBOLD);
        } else {
            DrawText(dc, place, "#" + std::to_string(index + 1), Color(theme.textSecondary), 12);
        }
        // The scene's path is the project's data, not interface text: a long one ends in an ellipsis.
        const bool present = std::ranges::find(projectScenes, scene) != projectScenes.end();
        const RECT label = ProjectSettingsPanelLayout::SceneRowLabel(rects, visible);
        {
            const CutTextDataScope data;
            DrawText(dc, RECT{ row.left + kPlaceWidth, label.top, label.right, label.bottom }, std::filesystem::path{ scene }.replace_extension().generic_string(),
                Color(present ? theme.textPrimary : theme.textDisabled), 12);
        }
        DrawOptionButton(dc, ProjectSettingsPanelLayout::SceneMoveUpButton(rects, visible), theme, EditorText("projectSettings.sceneMoveUp", "Move up"), false, index > 0);
        DrawOptionButton(dc, ProjectSettingsPanelLayout::SceneRemoveButton(rects, visible), theme, EditorText("projectSettings.sceneRemove", "Remove"), false, scenes.size() > 1U);
    }
}

void DrawPhysicsPage(HDC dc, const ProjectSettingsPanelLayoutRects& rects, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    const kb::project::ProjectSettings& settings = sceneContext.ProjectConfiguration();
    const bool dropdownOpen = sceneContext.ProjectSettings().IsPhysicsLayersDropdownOpen();
    const std::string_view query = sceneContext.ProjectSettings().Search().Query();

    GdiDrawing::FillRectColor(dc, rects.sectionHeader, Color(theme.strip));
    DrawText(dc, RECT{ rects.sectionHeader.left + 8, rects.sectionHeader.top, rects.sectionHeader.right - 8, rects.sectionHeader.bottom }, EditorText("projectSettings.physicsSection", "PHYSICS"), Color(theme.textSecondary), 11, FW_SEMIBOLD);

    // The steppers, then the gravity presets, under the layers row so that its open list can cover them.
    const kb::project::ProjectSimulationSettings& simulation = settings.simulation;
    char value[32]{};
    for (int stepper = 0; stepper < 5; ++stepper) {
        using Kind = ProjectSettingsTooltipKind;
        const Kind kind = stepper == 0 ? Kind::FixedStep : (stepper == 4 ? Kind::TimeScale : Kind::Gravity);
        std::string label{ SettingLabel(kind) };
        if (kind == Kind::Gravity) {
            label += stepper == 1 ? " X" : (stepper == 2 ? " Y" : " Z");
        }
        DrawSetting(dc, rects, rects.stepperLabel[stepper], label, kind, theme, query);
        DrawStepper(dc, rects.stepperButton[stepper][0], rects.stepperValue[stepper], rects.stepperButton[stepper][1], theme,
            sceneContext, static_cast<ProjectSettingsNumber>(stepper));
    }
    DrawSetting(dc, rects, rects.gravityPresetLabel, SettingLabel(ProjectSettingsTooltipKind::GravityPreset), ProjectSettingsTooltipKind::GravityPreset, theme, query);
    for (std::size_t preset = 0U; preset < kProjectSettingsGravityPresets.size(); ++preset) {
        const std::array<float, 3U>& gravity = kProjectSettingsGravityPresets[preset];
        std::snprintf(value, sizeof(value), "0, %g, 0", static_cast<double>(gravity[1]));
        DrawOptionButton(dc, rects.gravityPresetButton[preset], theme, value, simulation.gravity == gravity);
    }

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
    case ProjectSettingsCategory::Display:
        DrawDisplayPage(dc, rects, theme, sceneContext);
        break;
    case ProjectSettingsCategory::Audio:
        DrawAudioPage(dc, rects, theme, sceneContext);
        break;
    case ProjectSettingsCategory::Scenes:
        DrawScenesPage(dc, rects, theme, sceneContext);
        break;
    case ProjectSettingsCategory::Tags:
        DrawTagsPage(dc, rects, theme, sceneContext);
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

    if (category == ProjectSettingsCategory::Display) {
        for (int index = 0; index < 2; ++index) {
            if (PointInRect(rects.windowModeButton[index], x, y)) {
                return Hit{ .kind = ProjectSettingsHitKind::WindowModeOption, .index = index, .rect = rects.windowModeButton[index] };
            }
        }
        for (int index = 0; index < 4; ++index) {
            if (PointInRect(rects.windowSizeButton[index], x, y)) {
                return Hit{ .kind = ProjectSettingsHitKind::WindowSizeOption, .index = index, .rect = rects.windowSizeButton[index] };
            }
        }
        for (int index = 0; index < 5; ++index) {
            if (PointInRect(rects.frameRateButton[index], x, y)) {
                return Hit{ .kind = ProjectSettingsHitKind::FrameRateOption, .index = index, .rect = rects.frameRateButton[index] };
            }
        }
        const std::array<std::pair<const RECT*, ProjectSettingsNumber>, 4U> cells{ {
            { &rects.windowWidthValue, ProjectSettingsNumber::WindowWidth },
            { &rects.windowHeightValue, ProjectSettingsNumber::WindowHeight },
            { &rects.frameRateValue, ProjectSettingsNumber::FrameRateCap },
            { &rects.renderScaleValue, ProjectSettingsNumber::RenderScale } } };
        for (const auto& [cell, number] : cells) {
            if (PointInRect(*cell, x, y)) {
                return Hit{ .kind = ProjectSettingsHitKind::NumberField, .index = static_cast<int>(number), .rect = *cell };
            }
        }
        const std::array<std::array<RECT, 2U>, 2U> toggles{ { { rects.resizableCheckbox, rects.displayLabel[2] }, { rects.vsyncCheckbox, rects.displayLabel[3] } } };
        for (int index = 0; index < 2; ++index) {
            const std::array<RECT, 2U>& toggle = toggles[static_cast<std::size_t>(index)];
            if (PointInRect(toggle[0], x, y) || PointInRect(toggle[1], x, y)) {
                return Hit{ .kind = ProjectSettingsHitKind::DisplayToggle, .index = index, .rect = toggle[0] };
            }
        }
        return Hit{};
    }

    if (category == ProjectSettingsCategory::Audio) {
        for (int direction = 0; direction < 2; ++direction) {
            if (PointInRect(rects.stepperButton[5][direction], x, y)) {
                return Hit{ .kind = ProjectSettingsHitKind::MasterVolumeStepper, .index = direction, .rect = rects.stepperButton[5][direction] };
            }
        }
        if (PointInRect(rects.stepperValue[5], x, y)) {
            return Hit{ .kind = ProjectSettingsHitKind::NumberField, .index = static_cast<int>(ProjectSettingsNumber::MasterVolume), .rect = rects.stepperValue[5] };
        }
        return Hit{};
    }

    if (category == ProjectSettingsCategory::Tags) {
        if (PointInRect(rects.tagAddButton, x, y)) {
            return Hit{ .kind = ProjectSettingsHitKind::TagAdd, .index = -1, .rect = rects.tagAddButton };
        }
        if (PointInRect(rects.tagNameField, x, y)) {
            return Hit{ .kind = ProjectSettingsHitKind::TagNameField, .index = -1, .rect = rects.tagNameField };
        }
        const int count = ListedRowCount(sceneContext);
        const int capacity = ProjectSettingsPanelLayout::SceneRowCapacity(rects);
        const int first = FirstVisibleScene(sceneContext, rects);
        for (int visible = 0; visible < capacity && first + visible < count; ++visible) {
            const RECT remove = ProjectSettingsPanelLayout::SceneRemoveButton(rects, visible);
            if (PointInRect(remove, x, y)) {
                return Hit{ .kind = ProjectSettingsHitKind::TagRemove, .index = first + visible, .rect = remove };
            }
        }
        return Hit{};
    }

    if (category == ProjectSettingsCategory::Scenes) {
        if (PointInRect(rects.sceneAddButton, x, y)) {
            return Hit{ .kind = ProjectSettingsHitKind::BuildSceneAdd, .index = -1, .rect = rects.sceneAddButton };
        }
        const std::size_t count = kb::project::ProjectBuildScenes(sceneContext.ProjectConfiguration()).size();
        const int capacity = ProjectSettingsPanelLayout::SceneRowCapacity(rects);
        const int first = FirstVisibleScene(sceneContext, rects);
        for (int visible = 0; visible < capacity && first + visible < static_cast<int>(count); ++visible) {
            const int index = first + visible;
            const RECT up = ProjectSettingsPanelLayout::SceneMoveUpButton(rects, visible);
            const RECT remove = ProjectSettingsPanelLayout::SceneRemoveButton(rects, visible);
            if (index > 0 && PointInRect(up, x, y)) {
                return Hit{ .kind = ProjectSettingsHitKind::BuildSceneMoveUp, .index = index, .rect = up };
            }
            if (count > 1U && PointInRect(remove, x, y)) {
                return Hit{ .kind = ProjectSettingsHitKind::BuildSceneRemove, .index = index, .rect = remove };
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
        for (int stepper = 0; stepper < 5; ++stepper) {
            for (int direction = 0; direction < 2; ++direction) {
                const RECT& button = rects.stepperButton[stepper][direction];
                if (PointInRect(button, x, y)) {
                    return Hit{ .kind = ProjectSettingsHitKind::SimulationStepper, .index = stepper * 2 + direction, .rect = button };
                }
            }
            if (PointInRect(rects.stepperValue[stepper], x, y)) {
                return Hit{ .kind = ProjectSettingsHitKind::NumberField, .index = stepper, .rect = rects.stepperValue[stepper] };
            }
        }
        for (int index = 0; index < 3; ++index) {
            if (PointInRect(rects.gravityPresetButton[index], x, y)) {
                return Hit{ .kind = ProjectSettingsHitKind::GravityPresetOption, .index = index, .rect = rects.gravityPresetButton[index] };
            }
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

int ProjectSettingsPanelRenderer::FirstVisibleScene(const EditorSceneContext& sceneContext, const ProjectSettingsPanelLayoutRects& rects) {
    const int count = ListedRowCount(sceneContext);
    const int last = std::max(0, count - ProjectSettingsPanelLayout::SceneRowCapacity(rects));
    return std::clamp(sceneContext.ProjectSettings().FirstVisibleScene(), 0, last);
}

int ProjectSettingsPanelRenderer::MaximumFirstVisibleScene(const RECT& content, const EditorSceneContext& sceneContext) {
    const int count = ListedRowCount(sceneContext);
    return std::max(0, count - ProjectSettingsPanelLayout::SceneRowCapacity(ProjectSettingsPanelLayout::Resolve(content)));
}

ProjectSettingsNumberLimits ProjectSettingsPanelRenderer::NumberLimits(ProjectSettingsNumber number) noexcept {
    switch (number) {
    case ProjectSettingsNumber::FixedStep:
        return { static_cast<double>(kb::project::kProjectFixedStepHzMin), static_cast<double>(kb::project::kProjectFixedStepHzMax), true };
    case ProjectSettingsNumber::GravityX:
    case ProjectSettingsNumber::GravityY:
    case ProjectSettingsNumber::GravityZ:
        return { -static_cast<double>(kb::project::kProjectGravityLimit), static_cast<double>(kb::project::kProjectGravityLimit), false };
    case ProjectSettingsNumber::TimeScale:
        return { 0.0, static_cast<double>(kb::project::kProjectTimeScaleMax), false };
    case ProjectSettingsNumber::WindowWidth:
    case ProjectSettingsNumber::WindowHeight:
        return { static_cast<double>(kb::project::kProjectWindowExtentMin), static_cast<double>(kb::project::kProjectWindowExtentMax), true };
    case ProjectSettingsNumber::MasterVolume:
        return { 0.0, 1.0, false };
    case ProjectSettingsNumber::RenderScale:
        return { static_cast<double>(kb::project::kProjectRenderScaleMin), static_cast<double>(kb::project::kProjectRenderScaleMax), false };
    case ProjectSettingsNumber::FrameRateCap:
    case ProjectSettingsNumber::Count:
    default:
        return { 0.0, static_cast<double>(kb::project::kProjectFrameRateCapMax), true };
    }
}

std::string ProjectSettingsPanelRenderer::NumberText(const kb::project::ProjectSettings& settings, ProjectSettingsNumber number) {
    const kb::project::ProjectSimulationSettings& simulation = settings.simulation;
    char text[32]{};
    switch (number) {
    case ProjectSettingsNumber::FixedStep:
        std::snprintf(text, sizeof(text), "%u", simulation.fixedStepHz);
        break;
    case ProjectSettingsNumber::GravityX:
    case ProjectSettingsNumber::GravityY:
    case ProjectSettingsNumber::GravityZ:
        std::snprintf(text, sizeof(text), "%g", static_cast<double>(simulation.gravity[static_cast<std::size_t>(number) - 1U]));
        break;
    case ProjectSettingsNumber::TimeScale:
        std::snprintf(text, sizeof(text), "%g", static_cast<double>(simulation.timeScale));
        break;
    case ProjectSettingsNumber::WindowWidth:
        std::snprintf(text, sizeof(text), "%u", settings.display.windowWidth);
        break;
    case ProjectSettingsNumber::WindowHeight:
        std::snprintf(text, sizeof(text), "%u", settings.display.windowHeight);
        break;
    case ProjectSettingsNumber::FrameRateCap:
        std::snprintf(text, sizeof(text), "%u", settings.display.frameRateCap);
        break;
    case ProjectSettingsNumber::MasterVolume:
        std::snprintf(text, sizeof(text), "%g", static_cast<double>(settings.masterVolume));
        break;
    case ProjectSettingsNumber::RenderScale:
        std::snprintf(text, sizeof(text), "%g", static_cast<double>(settings.display.renderScale));
        break;
    case ProjectSettingsNumber::Count:
    default:
        break;
    }
    return text;
}

kb::project::ProjectSettings ProjectSettingsPanelRenderer::WithNumber(
    kb::project::ProjectSettings settings, ProjectSettingsNumber number, double value) {
    const ProjectSettingsNumberLimits limits = NumberLimits(number);
    value = std::clamp(limits.whole ? std::round(value) : value, limits.minimum, limits.maximum);
    kb::project::ProjectSimulationSettings& simulation = settings.simulation;
    switch (number) {
    case ProjectSettingsNumber::FixedStep:
        simulation.fixedStepHz = static_cast<std::uint32_t>(value);
        break;
    case ProjectSettingsNumber::GravityX:
    case ProjectSettingsNumber::GravityY:
    case ProjectSettingsNumber::GravityZ:
        simulation.gravity[static_cast<std::size_t>(number) - 1U] = static_cast<float>(value);
        break;
    case ProjectSettingsNumber::TimeScale:
        simulation.timeScale = static_cast<float>(value);
        break;
    case ProjectSettingsNumber::WindowWidth:
        settings.display.windowWidth = static_cast<std::uint32_t>(value);
        break;
    case ProjectSettingsNumber::WindowHeight:
        settings.display.windowHeight = static_cast<std::uint32_t>(value);
        break;
    case ProjectSettingsNumber::FrameRateCap:
        settings.display.frameRateCap = static_cast<std::uint32_t>(value);
        break;
    case ProjectSettingsNumber::MasterVolume:
        settings.masterVolume = static_cast<float>(value);
        break;
    case ProjectSettingsNumber::RenderScale:
        settings.display.renderScale = static_cast<float>(value);
        break;
    case ProjectSettingsNumber::Count:
    default:
        break;
    }
    return settings;
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

    if (category == ProjectSettingsCategory::Display) {
        constexpr std::array<ProjectSettingsHitKind, 5> kinds{ ProjectSettingsHitKind::WindowModeOption, ProjectSettingsHitKind::WindowSizeOption,
            ProjectSettingsHitKind::DisplayToggle, ProjectSettingsHitKind::DisplayToggle, ProjectSettingsHitKind::FrameRateOption };
        if (PointInRect(rects.displayLabel[5], x, y)) {
            return Hit{ .kind = ProjectSettingsHitKind::NumberField, .index = static_cast<int>(ProjectSettingsNumber::RenderScale), .rect = rects.renderScaleValue };
        }
        for (int row = 0; row < 5; ++row) {
            if (PointInRect(rects.displayLabel[row], x, y)) {
                // A toggle says which one it is (0 Resizable, 1 VSync), as when it is clicked.
                return Hit{ .kind = kinds[static_cast<std::size_t>(row)], .index = row >= 2 && row <= 3 ? row - 2 : -1, .rect = rects.displayLabel[row] };
            }
        }
    }

    if (category == ProjectSettingsCategory::Audio && PointInRect(rects.stepperLabel[5], x, y)) {
        return Hit{ .kind = ProjectSettingsHitKind::MasterVolumeStepper, .index = -1, .rect = rects.stepperLabel[5] };
    }

    if (category == ProjectSettingsCategory::Physics) {
        if (PointInRect(rects.mappingLabel, x, y) || PointInRect(rects.mappingField, x, y)) {
            return Hit{ .kind = ProjectSettingsHitKind::PhysicsLayersField, .index = -1, .rect = rects.mappingField };
        }
        for (int stepper = 0; stepper < 5; ++stepper) {
            if (PointInRect(rects.stepperLabel[stepper], x, y) || PointInRect(rects.stepperValue[stepper], x, y)) {
                return Hit{ .kind = ProjectSettingsHitKind::SimulationStepper, .index = stepper * 2, .rect = rects.stepperLabel[stepper] };
            }
        }
        if (PointInRect(rects.gravityPresetLabel, x, y)) {
            return Hit{ .kind = ProjectSettingsHitKind::GravityPresetOption, .index = -1, .rect = rects.gravityPresetLabel };
        }
    }

    return direct;
}

} // namespace kb::editor

#endif
