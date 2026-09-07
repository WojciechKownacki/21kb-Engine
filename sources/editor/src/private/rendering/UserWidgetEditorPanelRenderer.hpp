#pragma once

#include "engine/scene/UIAssets.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/user_widget/UserWidgetEditorProperty.hpp"

#include <cstddef>
#include <optional>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class EditorSceneContext;

enum class UserWidgetEditorPanelAction {
    None,
    Save,
    Undo,
    Redo,
    Add,
    Remove,
    Reparent,
    Select,
    EditProperty,
};

struct UserWidgetEditorPanelHit {
    UserWidgetEditorPanelAction action = UserWidgetEditorPanelAction::None;
    kb::scene::UIElementId elementId = 0U;
    std::optional<UserWidgetEditorProperty> property;
    float canvasScale = 1.0F;
    bool fromPreview = false;
};

class UserWidgetEditorPanelRenderer final {
  public:
    void Paint(HDC dc, const RECT& content, const EditorTheme& theme, EditorSceneContext& sceneContext) const;

    [[nodiscard]] static UserWidgetEditorPanelHit HitTest(const RECT& content, const EditorSceneContext& sceneContext,
                                                          int x, int y);
};

#endif

} // namespace kb::editor
