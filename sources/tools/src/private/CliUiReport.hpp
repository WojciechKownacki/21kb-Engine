#pragma once

#include "engine/core/JsonValue.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/script/ScriptRuntimeSceneSystem.hpp"
#include "engine/script/UISceneValidation.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::scene {
class Scene;
}

namespace kb::cli {

// One `run --click <widget path | x,y>@<frame>`: the pointer arrives on the frame before `frame`, presses on
// `frame` and lets go on the next one.
struct UiClick {
    std::string target;
    int frame = 0;
    std::optional<kb::math::Vec2> point;
    kb::scene::SceneEntity widget{};
    kb::math::Vec2 position{};
    // The widget under the pointer when it pressed; empty when none.
    std::string hit;
    bool placed = false;
    std::vector<kb::script::ScriptUIDispatch> dispatched;
};

[[nodiscard]] std::optional<UiClick> ParseUiClick(std::string_view text, std::string& error);
// Finds the widget in the laid-out UI and the pointer position on it: where a press on it lands.
[[nodiscard]] bool PlaceUiClick(kb::scene::Scene& scene, UiClick& click, std::string& error);
// One line: what was clicked, where, what was under the pointer and who answered.
[[nodiscard]] std::string DescribeUiClick(const kb::scene::Scene& scene, const UiClick& click);

// `run --report ui`: every laid-out widget with its rectangle and the scripts that answer its events,
// the clicks made and every widget event delivered (with the frame and how many behaviours answered).
[[nodiscard]] kb::core::JsonValue BuildUiReport(kb::scene::Scene& scene, const kb::script::UIScriptIndex& scripts,
    const std::vector<UiClick>& clicks, const std::vector<std::pair<int, kb::script::ScriptUIDispatch>>& events, bool succeeded);

} // namespace kb::cli
