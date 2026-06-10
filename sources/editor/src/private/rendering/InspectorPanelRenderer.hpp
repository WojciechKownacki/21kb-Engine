#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "inspection/InspectorPanelState.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

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
#endif
};

} // namespace kb::editor
