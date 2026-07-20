#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "inspection/InspectorPanelState.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <optional>

namespace kb::editor {

class InspectorPanelRenderer {
public:
#if defined(_WIN32)
    struct Hit {
        InspectorHitKind kind = InspectorHitKind::None;
        InspectorSectionId section = InspectorSectionId::None;
        InspectorPropertyId property = InspectorPropertyId::None;
        int index = -1; // Row index for dynamic lists (e.g. mapping context entries).
        RECT rect{};
    };

    void Paint(
        HDC dc,
        const RECT& content,
        const EditorTheme& theme,
        const EditorSceneContext& sceneContext) const;
    [[nodiscard]] static Hit HitTest(const RECT& content, const EditorSceneContext& sceneContext, int x, int y) noexcept;
    [[nodiscard]] static int ContentHeight(const RECT& content, const EditorSceneContext& sceneContext);
    [[nodiscard]] static int MaxScrollOffset(const RECT& content, const EditorSceneContext& sceneContext);
    [[nodiscard]] static std::optional<RECT> MaterialPreviewRect(const RECT& content, const EditorSceneContext& sceneContext) noexcept;
    [[nodiscard]] static RECT ScrollbarTrackRect(const RECT& content) noexcept;
    [[nodiscard]] static RECT ScrollbarThumbRect(const RECT& content, const EditorSceneContext& sceneContext) noexcept;

    // Geometry of the Add Component menu's internal scrollbar, in window (event)
    // space so the pointer controller can drag it. `active` is false when the menu
    // is closed or its list fits without scrolling.
    struct AddComponentScrollInfo {
        bool active = false;
        RECT track{};
        RECT thumb{};
        int maxScroll = 0;
    };
    [[nodiscard]] static AddComponentScrollInfo AddComponentScrollGeometry(const RECT& content, const EditorSceneContext& sceneContext);
    // True when (x, y) is inside the open menu's scrollable list (so the wheel
    // scrolls the menu instead of the whole inspector).
    [[nodiscard]] static bool AddComponentListContains(const RECT& content, const EditorSceneContext& sceneContext, int x, int y);
#endif
};

} // namespace kb::editor
