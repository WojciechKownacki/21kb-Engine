#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <filesystem>
#include <string>

namespace kb::scene {

class Scene;

// The font a Text gets when it has none: shipped beside the running executable (Content/EditorShell/Fonts).
[[nodiscard]] std::filesystem::path BundledUIFontPath();

struct UIDependencyResult {
    bool succeeded = true;
    std::string error;
};

// Gives a UI widget what it needs to show, for the editor and kb_cli alike: a Text without a font gets the
// bundled font (imported into /Game/UI/Fonts once, then reused); a Rect Transform it lacks; and a Canvas
// above it - the root Canvas the scene has, else a new one the widget's top object moves under.
// A scene object without UI components is left alone.
[[nodiscard]] UIDependencyResult CompleteUIDependencies(Scene& scene, SceneEntity entity);

} // namespace kb::scene
