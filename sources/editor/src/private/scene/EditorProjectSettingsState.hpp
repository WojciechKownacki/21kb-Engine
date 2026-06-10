#pragma once

namespace kb::editor {

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

private:
    int selectedCategory_ = 0;
    bool mappingContextDropdownOpen_ = false;
    int hoveredOption_ = -1;
};

} // namespace kb::editor
