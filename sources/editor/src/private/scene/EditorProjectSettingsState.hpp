#pragma once

#include <cstdint>

namespace kb::editor {

enum class ProjectSettingsTooltipKind : std::uint8_t {
    None,
    MappingContext,
    InputEnabled,
    RenderBackend,
    PostProcess,
    AntiAliasing,
    MsaaSamples,
    Bloom,
    Shadows,
    SelectionOutline,
    GpuDriven,
};

// Transient UI state for the Project Settings panel: which category is selected
// in the left sidebar and whether the Mapping Context dropdown is expanded.
// Grows as the panel gains more categories/settings.
class EditorProjectSettingsState {
public:
    [[nodiscard]] int SelectedCategory() const noexcept {
        return selectedCategory_;
    }

    // Returns true when the selection actually changed (so the caller repaints).
    bool SelectCategory(int category) noexcept {
        if (selectedCategory_ == category) {
            return false;
        }
        selectedCategory_ = category;
        mappingContextDropdownOpen_ = false; // Closing a stale dropdown on switch.
        hoveredOption_ = -1;
        tooltipKind_ = ProjectSettingsTooltipKind::None;
        return true;
    }

    [[nodiscard]] bool IsMappingContextDropdownOpen() const noexcept {
        return mappingContextDropdownOpen_;
    }

    void ToggleMappingContextDropdown() noexcept {
        mappingContextDropdownOpen_ = !mappingContextDropdownOpen_;
        hoveredOption_ = -1;
    }

    // Returns true when a dropdown was actually open (so the caller can repaint).
    bool CloseDropdowns() noexcept {
        const bool wasOpen = mappingContextDropdownOpen_;
        mappingContextDropdownOpen_ = false;
        hoveredOption_ = -1;
        return wasOpen;
    }

    // Index of the dropdown option currently under the cursor (-1 = none).
    [[nodiscard]] int HoveredOption() const noexcept {
        return hoveredOption_;
    }

    // Returns true when the hovered option changed (so the caller can repaint).
    bool SetHoveredOption(int option) noexcept {
        if (hoveredOption_ == option) {
            return false;
        }
        hoveredOption_ = option;
        return true;
    }

    [[nodiscard]] ProjectSettingsTooltipKind TooltipKind() const noexcept {
        return tooltipKind_;
    }

    [[nodiscard]] int TooltipX() const noexcept {
        return tooltipX_;
    }

    [[nodiscard]] int TooltipY() const noexcept {
        return tooltipY_;
    }

    bool SetTooltip(ProjectSettingsTooltipKind kind, int x, int y) noexcept {
        if (tooltipKind_ == kind && tooltipX_ == x && tooltipY_ == y) {
            return false;
        }
        tooltipKind_ = kind;
        tooltipX_ = x;
        tooltipY_ = y;
        return true;
    }

    bool ClearTooltip() noexcept {
        return SetTooltip(ProjectSettingsTooltipKind::None, 0, 0);
    }

private:
    int selectedCategory_ = 0;
    bool mappingContextDropdownOpen_ = false;
    int hoveredOption_ = -1;
    ProjectSettingsTooltipKind tooltipKind_ = ProjectSettingsTooltipKind::None;
    int tooltipX_ = 0;
    int tooltipY_ = 0;
};

} // namespace kb::editor
