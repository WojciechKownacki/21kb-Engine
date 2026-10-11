#pragma once

#include "engine/visual/VisualGraphTypes.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kb::script {

enum class ScriptLifecycleEvent : std::uint8_t {
    Created,
    Activated,
    Ready,
    FixedTick,
    Tick,
    LateTick,
    BeforeRender,
    AfterRender,
    Deactivated,
    Destroyed,
};

[[nodiscard]] const char* ToString(ScriptLifecycleEvent event) noexcept;
[[nodiscard]] kb::visual::VisualGraphLifecycleEvent ToVisualGraphLifecycleEvent(ScriptLifecycleEvent event) noexcept;

// A global function of a Lua behaviour named after another engine's lifecycle callback - Unity's
// Start/Update, Godot's _ready/_process, Unreal's BeginPlay - which 21kb never calls. `hook` is the
// 21kb hook that function meant.
struct ForeignLifecycleName {
    std::uint32_t line = 0U;
    std::string name;
    ScriptLifecycleEvent hook = ScriptLifecycleEvent::Tick;
};

// Every such function in `luaSource`, in source order. A script without them may still be fine
// (a library, event handlers only); one with them silently does nothing in Play.
[[nodiscard]] std::vector<ForeignLifecycleName> FindForeignLifecycleNames(std::string_view luaSource);

} // namespace kb::script
