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
        return true;
    }

    [[nodiscard]] bool IsMappingContextDropdownOpen() const noexcept {
        return mappingContextDropdownOpen_;
    }

    void ToggleMappingContextDropdown() noexcept {
        mappingContextDropdownOpen_ = !mappingContextDropdownOpen_;
    }

    // Returns true when a dropdown was actually open (so the caller can repaint).
    bool CloseDropdowns() noexcept {
        const bool wasOpen = mappingContextDropdownOpen_;
        mappingContextDropdownOpen_ = false;
        return wasOpen;
    }

private:
    int selectedCategory_ = 0;
    bool mappingContextDropdownOpen_ = false;
};

} // namespace kb::editor
