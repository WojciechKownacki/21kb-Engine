#pragma once

#include <span>
#include <string>
#include <string_view>

namespace kb::script {

// One value an event carries: a Lua script reads it as `event.args.<name>`.
struct ScriptEventHookArgument {
    std::string_view name;
    std::string_view type; // Entity, Hash, Int, Float, Bool or String (entities and hashes are integers in Lua)
    std::string_view meaning;
};

// A global Lua function the engine calls by name when something happens, and what it is called with. The list is
// what the runtime dispatches (ScriptRuntimeSceneSystem and the network, timeline and UI dispatchers): the API reference,
// the guides and `kb_cli validate` all read it, so a hook is described once.
struct ScriptEventHook {
    std::string_view name;      // "OnTriggerEnter"; "OnRpc_" names a family: OnRpc_<name> for each declared RPC
    std::string_view group;     // Lifecycle, Physics, Animation, Audio, Particles, Timeline, Gameplay, World, Network, UI
    std::string_view receives;  // "(self, dt)" for a lifecycle hook, "(self, event)" for the rest
    std::string_view when;      // when the engine calls it and on which object
    std::span<const ScriptEventHookArgument> args;
};

class ScriptEventHooks final {
public:
    ScriptEventHooks() = delete;

    [[nodiscard]] static std::span<const ScriptEventHook> All() noexcept;
    // The hook called `name` (exact spelling), or null. An OnRpc_<name> function is the OnRpc_ family.
    [[nodiscard]] static const ScriptEventHook* Find(std::string_view name) noexcept;
    // The hook whose name is nearest to `name`, the nearest overall when none is near.
    [[nodiscard]] static std::string_view Closest(std::string_view name);
    // The reference page for every hook with its arguments; `topic` EventHooks of `kb_cli api`.
    [[nodiscard]] static std::string Page();
    // The page of one hook (its arguments, when it is called and a short example).
    [[nodiscard]] static std::string HookPage(const ScriptEventHook& hook);
};

} // namespace kb::script
