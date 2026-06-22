#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <optional>

namespace kb::editor {

/// Dedicated Material Editor panel. Renders the live state of the Material Instance
/// asset currently selected for inspection (`EditorSceneContext::AssetBrowser().InspectorAsset()`).
///
/// This is a presentational (read-only) surface through KBMAT-0203. The top frame
/// is a live bgfx material sphere preview; interactive editing, drag/drop and undo arrive later. It is intentionally a
/// standalone component (not an extraction of the Inspector's material section, which is
/// coupled to Inspector-only interaction types).
class MaterialEditorPanelRenderer {
public:
#if defined(_WIN32)
    void Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext) const;

    /// Live material preview target rect (sphere) inside this panel's content area, or
    /// std::nullopt when no Material Instance is selected. Consumed by the editor frame
    /// loop to present the bgfx preview into this panel (mirrors the Inspector preview path).
    [[nodiscard]] static std::optional<RECT> MaterialPreviewRect(const RECT& content, const EditorSceneContext& sceneContext) noexcept;
#endif
};

} // namespace kb::editor
