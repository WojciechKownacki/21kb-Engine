#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <string>
#include <vector>

namespace kb::scene {
class Scene;
}

namespace kb::editor {

// One entry of the Inspector "On Click" list: a Lua function a click on a widget can call, or a new
// function the click will get in a script.
struct UIClickChoice {
    kb::assets::AssetId script{};
    std::string scriptName;
    std::string function;
    // The function does not exist yet: choosing it appends a template to the script.
    bool createFunction = false;
};

// "On Click" of a button: which script function runs when it is clicked, in the words of a script author.
// It is stored as the widget Selectable event name (the function) and event target (the object that
// runs the script), so the engine event routing is unchanged.
class InspectorUIClickHandler final {
public:
    InspectorUIClickHandler() = delete;

    // Every function of the project Lua scripts a click can call - scripts in alphabetical order, each
    // with its functions and then a new function named after the widget (PlayButton -> PlayButtonClicked).
    [[nodiscard]] static std::vector<UIClickChoice> Choices(const kb::scene::Scene& scene, kb::scene::SceneEntity widget);
    // The row text: "MenuController.PlayClicked", "(none)", or the bare event name when no script defines it.
    [[nodiscard]] static std::string Describe(const kb::scene::Scene& scene, kb::scene::SceneEntity widget);
    // The Lua text appended to a script for a new function.
    [[nodiscard]] static std::string FunctionTemplate(const std::string& function);
    // The object whose script answers a click on `widget` once `script` is picked: the widget itself, an
    // object that already runs the script (an ancestor first), or none when the widget has to get the script.
    struct Target {
        bool found = false;
        kb::scene::SceneEntity entity{};     // invalid: the widget itself
        bool attachToWidget = false;
    };
    [[nodiscard]] static Target FindTarget(const kb::scene::Scene& scene, kb::scene::SceneEntity widget, kb::assets::AssetId script);
};

} // namespace kb::editor
