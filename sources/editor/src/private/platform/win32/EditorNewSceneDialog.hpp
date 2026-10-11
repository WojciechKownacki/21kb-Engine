#pragma once

#include "engine/scene/SceneTemplates.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <functional>
#include <optional>
#include <string>

namespace kb::editor {

struct EditorNewSceneChoice {
    kb::scene::SceneTemplate sceneTemplate = kb::scene::SceneTemplate::Basic;
    std::string name;
    std::string folder;
    bool setAsStartupScene = false;
};

#if defined(_WIN32)

struct EditorNewSceneDialogDescriptor {
    std::string name = "NewScene";
    std::string folder = "Assets/Scenes";
    // Why `name` in `folder` cannot be created (shown under the fields, the dialog stays open), or
    // an empty string when it can.
    std::function<std::string(const std::string& name, const std::string& folder)> validate;
};

// File > New Scene, like Unity's New Scene window: a template card (Basic or Empty), the name and
// folder of the new scene, "Set as startup scene", a named error under the fields, Cancel / Create.
// nullopt = cancelled.
class EditorNewSceneDialog final {
public:
    EditorNewSceneDialog() = delete;

    [[nodiscard]] static std::optional<EditorNewSceneChoice> Show(HWND owner, EditorNewSceneDialogDescriptor descriptor);
};

#endif

} // namespace kb::editor
