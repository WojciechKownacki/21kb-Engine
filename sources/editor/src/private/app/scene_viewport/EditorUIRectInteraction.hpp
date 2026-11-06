#pragma once
#include "engine/scene/SceneUI.hpp"
#include <vector>
namespace kb::editor {
class EditorSceneContext;
class EditorUIRectInteraction {
  public:
    [[nodiscard]] static std::vector<kb::scene::SceneUIFrameElement>
    Overlays(const EditorSceneContext& context, float width, float height, float zoom = 1.0F);
    [[nodiscard]] static bool Begin(EditorSceneContext& context, float width, float height, float x, float y,
                                    float zoom = 1.0F);
    // `snap` pulls the dragged edges and the centre to the edges and centres of the other widgets and of the
    // canvas, within a few pixels, and leaves guides for the viewport to draw.
    [[nodiscard]] static bool Update(EditorSceneContext& context, float x, float y, bool preserveAspect = false,
                                     bool fromCenter = false, bool snap = true);
    [[nodiscard]] static bool End(EditorSceneContext& context, bool cancel = false);
};
} // namespace kb::editor
