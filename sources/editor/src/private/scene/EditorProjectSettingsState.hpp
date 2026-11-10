#pragma once

#include "scene/EditorHierarchySearchState.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace kb::editor {

enum class ProjectSettingsTooltipKind : std::uint8_t {
    None,
    MappingContext,
    PhysicsLayers,
    InputEnabled,
    RenderBackend,
    LightingPath,
    PostProcess,
    AntiAliasing,
    MsaaSamples,
    Bloom,
    Shadows,
    SelectionOutline,
    GpuDriven,
    ShadowQuality,
    ShadowQualityMobile,
    GiQuality,
    GiQualityMobile,
    WindowMode,
    WindowSize,
    Resizable,
    VSync,
    FrameRateLimit,
    FixedStep,
    Gravity,
    GravityPreset,
    TimeScale,
    MasterVolume,
    Tags,
};

// Transient UI state for the Project Settings panel: which category is selected
// in the left sidebar and whether the Mapping Context dropdown is expanded.
// Grows as the panel gains more categories/settings.
class EditorProjectSettingsState {
public:
    // The search box above the categories.
    [[nodiscard]] EditorHierarchySearchState& Search() noexcept {
        return search_;
    }
    [[nodiscard]] const EditorHierarchySearchState& Search() const noexcept {
        return search_;
    }
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
        physicsLayersDropdownOpen_ = false;
        EndNumberEdit();
        firstVisibleScene_ = 0;
        hoveredOption_ = -1;
        tooltipKind_ = ProjectSettingsTooltipKind::None;
        return true;
    }

    // Typing into a setting's cell: which cell (a ProjectSettingsNumber, kTagNameEdit for the new tag's name,
    // -1 for none) and the text typed so far. The text starts out selected, so the first key replaces the old value.
    static constexpr int kTagNameEdit = 1000;
    [[nodiscard]] bool IsNumberEditing() const noexcept {
        return editedNumber_ >= 0;
    }
    [[nodiscard]] bool IsTagNameEditing() const noexcept {
        return editedNumber_ == kTagNameEdit;
    }
    [[nodiscard]] int EditedNumber() const noexcept {
        return editedNumber_;
    }
    [[nodiscard]] EditorHierarchySearchState& NumberEdit() noexcept {
        return numberEdit_;
    }
    [[nodiscard]] const EditorHierarchySearchState& NumberEdit() const noexcept {
        return numberEdit_;
    }
    void BeginNumberEdit(int number, std::string text) {
        mappingContextDropdownOpen_ = false;
        physicsLayersDropdownOpen_ = false;
        search_.Focus(false);
        editedNumber_ = number;
        numberEdit_.SetQuery(std::move(text));
        numberEdit_.Focus(true);
        numberEdit_.SelectAll();
    }
    void BeginTagNameEdit() {
        BeginNumberEdit(kTagNameEdit, {});
    }
    void EndNumberEdit() noexcept {
        editedNumber_ = -1;
        numberEdit_.Clear();
        numberEdit_.Focus(false);
    }

    // The list of the Scenes page, or of the Tags page, starts at this row.
    [[nodiscard]] int FirstVisibleScene() const noexcept {
        return firstVisibleScene_;
    }
    void SetFirstVisibleScene(int index) noexcept {
        firstVisibleScene_ = index < 0 ? 0 : index;
    }

    [[nodiscard]] bool IsMappingContextDropdownOpen() const noexcept {
        return mappingContextDropdownOpen_;
    }

    void ToggleMappingContextDropdown() noexcept {
        mappingContextDropdownOpen_ = !mappingContextDropdownOpen_;
        physicsLayersDropdownOpen_ = false;
        hoveredOption_ = -1;
    }

    [[nodiscard]] bool IsPhysicsLayersDropdownOpen() const noexcept {
        return physicsLayersDropdownOpen_;
    }

    void TogglePhysicsLayersDropdown() noexcept {
        physicsLayersDropdownOpen_ = !physicsLayersDropdownOpen_;
        mappingContextDropdownOpen_ = false;
        hoveredOption_ = -1;
    }

    // Returns true when a dropdown was actually open (so the caller can repaint).
    bool CloseDropdowns() noexcept {
        const bool wasOpen = mappingContextDropdownOpen_ || physicsLayersDropdownOpen_;
        mappingContextDropdownOpen_ = false;
        physicsLayersDropdownOpen_ = false;
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
    EditorHierarchySearchState search_;
    EditorHierarchySearchState numberEdit_;
    int editedNumber_ = -1;
    int firstVisibleScene_ = 0;
    int selectedCategory_ = 0;
    bool mappingContextDropdownOpen_ = false;
    bool physicsLayersDropdownOpen_ = false;
    int hoveredOption_ = -1;
    ProjectSettingsTooltipKind tooltipKind_ = ProjectSettingsTooltipKind::None;
    int tooltipX_ = 0;
    int tooltipY_ = 0;
};

} // namespace kb::editor
