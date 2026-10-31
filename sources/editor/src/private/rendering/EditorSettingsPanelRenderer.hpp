#pragma once

#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <cstdint>

namespace kb::editor {

class EditorSceneContext;
#if defined(_WIN32)
enum class EditorSettingsHitKind : std::uint8_t {
    None,
    Toggle,
    Choice,
    Category,
    Language,
};

// The categories of the sidebar, in order.
enum class EditorSettingsCategory : std::uint8_t {
    General,
    Saving,
};

struct EditorSettingsHit {
    EditorSettingsHitKind kind = EditorSettingsHitKind::None;
    int row = -1;
    int option = -1;
};
#endif

class EditorSettingsPanelRenderer final {
public:
#if defined(_WIN32)
    void Paint(
        HDC dc,
        const RECT& content,
        const EditorTheme& theme,
        const EditorSceneContext& sceneContext) const;
    [[nodiscard]] static EditorSettingsHit HitTest(
        const RECT& content,
        int x,
        int y) noexcept;
    // The panel rectangle whose settings page lands exactly on `page`: its title bar and
    // category column fall outside, so another window can show just the page.
    [[nodiscard]] static RECT ContentForPage(const RECT& page) noexcept;
    // The language field, where the Language list opens below.
    [[nodiscard]] static RECT LanguageField(const RECT& content) noexcept;
    // The category shown on the right (the panel's own view state, not saved).
    [[nodiscard]] static EditorSettingsCategory SelectedCategory() noexcept;
    static void SelectCategory(EditorSettingsCategory category) noexcept;
#endif
};

} // namespace kb::editor
