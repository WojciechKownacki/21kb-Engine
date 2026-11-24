#include "engine/script/ScriptEventHooks.hpp"

#include "engine/scene/SceneUI.hpp"
#include "engine/script/ScriptNameSuggestion.hpp"

#include <array>
#include <vector>

namespace kb::script {
namespace {

using Arg = ScriptEventHookArgument;

constexpr std::array kContactArgs{
    Arg{ "other", "Entity", "the other object of the contact" },
    Arg{ "pointX", "Float", "world position of the contact, X" },
    Arg{ "pointY", "Float", "world position of the contact, Y" },
    Arg{ "pointZ", "Float", "world position of the contact, Z" },
    Arg{ "normalX", "Float", "contact normal pointing away from this object, X" },
    Arg{ "normalY", "Float", "contact normal, Y" },
    Arg{ "normalZ", "Float", "contact normal, Z" },
};
constexpr std::array kAnimationArgs{
    Arg{ "schemaMajor", "Int", "version of this event's layout (1)" },
    Arg{ "schemaMinor", "Int", "version of this event's layout (0)" },
    Arg{ "event", "Hash", "id of the event authored on the clip" },
    Arg{ "clip", "Hash", "id of the animation clip asset" },
    Arg{ "layer", "String", "animator layer (\"Timeline\" for a timeline clip)" },
    Arg{ "state", "String", "animator state (\"Clip\" for a timeline clip)" },
    Arg{ "normalizedTime", "Float", "0..1 position inside the clip when it fired" },
};
constexpr std::array kAudioMarkerArgs{
    Arg{ "voice", "Int", "the voice Audio.Play returned" },
    Arg{ "marker", "String", "the marker name given to Audio.AddMarker" },
    Arg{ "positionSeconds", "Float", "position in the sound when the marker fired" },
};
constexpr std::array kParticleArgs{
    Arg{ "instance", "Hash", "the particle instance that finished" },
    Arg{ "effect", "Hash", "id of its particle effect asset" },
};
constexpr std::array kTimelineMarkerArgs{
    Arg{ "schemaMajor", "Int", "version of this event's layout (1)" },
    Arg{ "schemaMinor", "Int", "version of this event's layout (0)" },
    Arg{ "instance", "Hash", "the playing timeline" },
    Arg{ "asset", "Hash", "id of the timeline asset" },
    Arg{ "marker", "Hash", "id of the marker" },
    Arg{ "time", "Float", "seconds into the timeline" },
};
constexpr std::array kTimelineEventArgs{
    Arg{ "schemaMajor", "Int", "version of this event's layout (1)" },
    Arg{ "schemaMinor", "Int", "version of this event's layout (0)" },
    Arg{ "instance", "Hash", "the playing timeline" },
    Arg{ "asset", "Hash", "id of the timeline asset" },
    Arg{ "marker", "Hash", "id of the key" },
    Arg{ "time", "Float", "seconds into the timeline" },
    Arg{ "name", "String", "the event name given to `kb_cli timeline event`" },
    Arg{ "payload", "String", "the text payload of the key (may be empty)" },
};
constexpr std::array kPrefabArgs{
    Arg{ "root", "Entity", "the root object of the new copy" },
    Arg{ "count", "Int", "how many objects the copy has" },
};
constexpr std::array kGameplayArgs{
    Arg{ "other", "Entity", "who caused it (the attacker, the healer, the target, the enemy)" },
    Arg{ "amount", "Float", "damage, healing or cost" },
    Arg{ "damageType", "String", "the damage type name (empty when none)" },
    Arg{ "ability", "Hash", "id of the ability used (0 when none)" },
};
constexpr std::array kTimerArgs{ Arg{ "timer", "Hash", "the id the Timer function returned" } };
constexpr std::array kTaskArgs{ Arg{ "task", "Hash", "the id the Task function returned" } };
constexpr std::array kSceneArgs{
    Arg{ "sceneId", "Hash", "id of the scene" },
    Arg{ "sceneName", "String", "name of the scene" },
};
constexpr std::array kNetPlayerArgs{ Arg{ "player", "Int", "the player (peer) id" } };
constexpr std::array kNetDisconnectedArgs{
    Arg{ "player", "Int", "the player (peer) id" },
    Arg{ "reason", "String", "why the connection ended" },
};
constexpr std::array kNetSpawnedArgs{
    Arg{ "entity", "Entity", "the replicated object" },
    Arg{ "owner", "Int", "the player that owns it" },
};
constexpr std::array kNetDespawnedArgs{ Arg{ "entity", "Entity", "the replicated object that went away" } };
constexpr std::array kNetVariableArgs{
    Arg{ "entity", "Entity", "the replicated object" },
    Arg{ "name", "String", "the declared variable name" },
    Arg{ "value", "Float", "the new value; Bool variables use 0 or 1" },
};
constexpr std::array kRpcArgs{
    Arg{ "sender", "Int", "the player that called it (0 from the server)" },
    Arg{ "b1, s1, e1, n1", "Bool, String, Entity, number", "the call's arguments, numbered per type in declaration order" },
};
constexpr std::array kUiArgs{
    Arg{ "entity", "Entity", "the widget the event is about" },
    Arg{ "pointerX", "Float", "pointer position in canvas pixels, X" },
    Arg{ "pointerY", "Float", "pointer position in canvas pixels, Y" },
    Arg{ "pointerAvailable", "Bool", "whether there was a pointer" },
    Arg{ "value", "Float", "a Slider, Scrollbar or Toggle value; the drag delta in OnUIDragged; the item index in OnUIItemBound" },
    Arg{ "value2", "Float", "second drag delta" },
    Arg{ "text", "String", "an Input Field's text, or the link of OnUILinkActivated" },
    Arg{ "action", "String", "the Selectable's eventName (empty when none)" },
    Arg{ "other", "Entity", "the dragged widget in OnUIDropped, the row in OnUIItemBound" },
};

constexpr std::string_view kLifecycleReceives = "(self, dt)";
constexpr std::string_view kEventReceives = "(self, event)";

constexpr std::array kLifecycleHooks{
    ScriptEventHook{ "Created", "Lifecycle", kLifecycleReceives, "once, when the object and its script are created", {} },
    ScriptEventHook{ "Activated", "Lifecycle", kLifecycleReceives, "when the script becomes active (the object is enabled)", {} },
    ScriptEventHook{ "Ready", "Lifecycle", kLifecycleReceives, "once, before the first Tick", {} },
    ScriptEventHook{ "FixedTick", "Lifecycle", kLifecycleReceives, "every fixed physics step (dt is the step)", {} },
    ScriptEventHook{ "Tick", "Lifecycle", kLifecycleReceives, "every frame (dt is the seconds since the last frame)", {} },
    ScriptEventHook{ "LateTick", "Lifecycle", kLifecycleReceives, "every frame, after every Tick", {} },
    ScriptEventHook{ "BeforeRender", "Lifecycle", kLifecycleReceives, "every frame, just before it is drawn", {} },
    ScriptEventHook{ "AfterRender", "Lifecycle", kLifecycleReceives, "every frame, just after it is drawn", {} },
    ScriptEventHook{ "Deactivated", "Lifecycle", kLifecycleReceives, "when the object is disabled", {} },
    ScriptEventHook{ "Destroyed", "Lifecycle", kLifecycleReceives, "when the object is removed or the scene ends", {} },
};

constexpr std::array kEventHooks{
    ScriptEventHook{ "OnTriggerEnter", "Physics", kEventReceives,
        "once when something starts overlapping this object's Collider that has `trigger=true`; the script of the other object gets it too", kContactArgs },
    ScriptEventHook{ "OnTriggerStay", "Physics", kEventReceives, "every fixed step while the overlap lasts", kContactArgs },
    ScriptEventHook{ "OnTriggerExit", "Physics", kEventReceives, "once when the overlap ends", kContactArgs },
    ScriptEventHook{ "OnCollisionEnter", "Physics", kEventReceives,
        "once when this object's solid Collider touches another; the script of the other object gets it too", kContactArgs },
    ScriptEventHook{ "OnCollisionStay", "Physics", kEventReceives, "every fixed step while they stay in contact", kContactArgs },
    ScriptEventHook{ "OnCollisionExit", "Physics", kEventReceives, "once when the contact ends", kContactArgs },
    ScriptEventHook{ "OnControllerColliderHit", "Physics", kEventReceives, "a CharacterController of this object bumped into a Collider while it moved", kContactArgs },
    ScriptEventHook{ "OnAnimationEvent", "Animation", kEventReceives,
        "an event authored on an animation clip played by this object's Animator or by a timeline", kAnimationArgs },
    ScriptEventHook{ "OnAudioMarker", "Audio", kEventReceives, "a marker added with Audio.AddMarker was reached by its voice", kAudioMarkerArgs },
    ScriptEventHook{ "OnParticleSystemFinished", "Particles", kEventReceives, "a particle instance of this object finished playing", kParticleArgs },
    ScriptEventHook{ "OnTimelineMarker", "Timeline", kEventReceives, "a timeline marker without a name was reached", kTimelineMarkerArgs },
    ScriptEventHook{ "OnTimelineEvent", "Timeline", kEventReceives, "a named timeline event (`kb_cli timeline event`) was reached", kTimelineEventArgs },
    ScriptEventHook{ "OnPrefabInstantiated", "World", kEventReceives, "World.InstantiatePrefab, called by this object, finished creating the copy", kPrefabArgs },
    ScriptEventHook{ "OnDamaged", "Gameplay", kEventReceives, "this combatant took damage (other = the attacker)", kGameplayArgs },
    ScriptEventHook{ "OnHealed", "Gameplay", kEventReceives, "this combatant was healed (other = the healer)", kGameplayArgs },
    ScriptEventHook{ "OnDeath", "Gameplay", kEventReceives, "this combatant died (other = the killer)", kGameplayArgs },
    ScriptEventHook{ "OnRevived", "Gameplay", kEventReceives, "this combatant came back to life", kGameplayArgs },
    ScriptEventHook{ "OnAbilityUsed", "Gameplay", kEventReceives, "this combatant used an ability (other = its target)", kGameplayArgs },
    ScriptEventHook{ "OnEnemySpotted", "Gameplay", kEventReceives, "this combatant saw an enemy (other = the enemy)", kGameplayArgs },
    ScriptEventHook{ "OnEnemyLost", "Gameplay", kEventReceives, "this combatant lost sight of an enemy (other = the enemy)", kGameplayArgs },
    ScriptEventHook{ "TimerFired", "World", kEventReceives, "a timer started with a Timer function ran out (to the object that owns it)", kTimerArgs },
    ScriptEventHook{ "TaskCompleted", "World", kEventReceives, "a task started with a Task function finished", kTaskArgs },
    ScriptEventHook{ "TaskFailed", "World", kEventReceives, "a task started with a Task function failed", kTaskArgs },
    ScriptEventHook{ "SceneLoaded", "World", kEventReceives, "a scene finished loading (every script gets it)", kSceneArgs },
    ScriptEventHook{ "SceneActivated", "World", kEventReceives, "a scene became the active scene (every script gets it)", kSceneArgs },
    ScriptEventHook{ "SceneUnloaded", "World", kEventReceives, "a scene was unloaded (every script gets it)", kSceneArgs },
    ScriptEventHook{ "OnNetConnected", "Network", kEventReceives, "a player connected (server) or the connection was made (client)", kNetPlayerArgs },
    ScriptEventHook{ "OnNetDisconnected", "Network", kEventReceives, "a connection ended", kNetDisconnectedArgs },
    ScriptEventHook{ "OnNetSpawned", "Network", kEventReceives, "a replicated object appeared on this client", kNetSpawnedArgs },
    ScriptEventHook{ "OnNetDespawned", "Network", kEventReceives, "a replicated object went away on this client", kNetDespawnedArgs },
    ScriptEventHook{ "OnNetAuthExpired", "Network", kEventReceives, "the login token of this client expired, so the server ended the session; log in again for a new token", {} },
    ScriptEventHook{ "OnNetVarChanged", "Network", kEventReceives, "a declared variable changed on a client after a state update", kNetVariableArgs },
    ScriptEventHook{ "OnRpc_", "Network", kEventReceives, "a remote call declared by the game arrived: the function is OnRpc_<name>", kRpcArgs },
};

[[nodiscard]] const std::vector<ScriptEventHook>& Hooks() {
    static const std::vector<ScriptEventHook> hooks = [] {
        std::vector<ScriptEventHook> all(kLifecycleHooks.begin(), kLifecycleHooks.end());
        all.insert(all.end(), kEventHooks.begin(), kEventHooks.end());
        for (const kb::scene::SceneUIEventDescriptor& descriptor : kb::scene::SceneUIEventCatalog()) {
            all.push_back(ScriptEventHook{ descriptor.callbackName, "UI", kEventReceives,
                "the player did this to a widget with a Selectable; only the script on that widget gets it (details: topic UIEvents)", kUiArgs });
        }
        return all;
    }();
    return hooks;
}

[[nodiscard]] std::string DisplayName(const ScriptEventHook& hook) {
    return std::string{ hook.name } + (hook.name == "OnRpc_" ? "<name>" : "");
}

[[nodiscard]] std::string ArgumentTable(const ScriptEventHook& hook) {
    std::string out;
    if (hook.args.empty()) return out;
    out += "| `event.args.` | Type | Meaning |\n|---|---|---|\n";
    for (const ScriptEventHookArgument& argument : hook.args) {
        out += "| `" + std::string{ argument.name } + "` | " + std::string{ argument.type } + " | " + std::string{ argument.meaning } + " |\n";
    }
    return out;
}

} // namespace

std::span<const ScriptEventHook> ScriptEventHooks::All() noexcept {
    return Hooks();
}

const ScriptEventHook* ScriptEventHooks::Find(std::string_view name) noexcept {
    for (const ScriptEventHook& hook : Hooks()) {
        if (hook.name == name) return &hook;
    }
    if (name.size() > 6U && name.starts_with("OnRpc_")) return Find("OnRpc_");
    return nullptr;
}

std::string_view ScriptEventHooks::Closest(std::string_view name) {
    std::vector<std::string_view> names;
    for (const ScriptEventHook& hook : Hooks()) {
        if (hook.group != "Lifecycle") names.push_back(hook.name);
    }
    const std::string nearest = ClosestScriptName(name, names);
    for (const std::string_view candidate : names) {
        if (candidate == nearest) return candidate;
    }
    return {};
}

std::string ScriptEventHooks::HookPage(const ScriptEventHook& hook) {
    const std::string display = DisplayName(hook);
    std::string out = "# " + display + "\n\n";
    out += "The engine calls `function " + display + std::string{ hook.receives } + "` of a Lua script.\n\nWhen: " + std::string{ hook.when } + ".\n\n";
    out += ArgumentTable(hook);
    if (hook.group == "Lifecycle") {
        out += "\n```lua\nfunction " + display + "(self, dt)\n    -- self.entity is the object this script is on\nend\n```\n";
    } else {
        out += "\n`event.name` is the hook name; `event.sender` and `event.target` are entities. Entities and ids are plain integers in Lua.\n";
        out += "\n```lua\nfunction " + std::string{ hook.name == "OnRpc_" ? "OnRpc_Hit" : hook.name } + "(self, event)\n";
        if (hook.args.empty()) {
            out += "    Log(event.name)\n";
        } else {
            const std::string_view first = hook.args.front().name;
            out += "    Log(tostring(event.args." + std::string{ first.substr(0U, first.find(',')) } + "))\n";
        }
        out += "end\n```\n";
        if (hook.group == "Physics" && hook.name != "OnControllerColliderHit") {
            out += "\nAt least one of the two objects must move (a Rigidbody or a CharacterController): two static Colliders never touch. ";
            out += "A pickup that a player collects by touching it: guide `recipe-pickup`.\n";
        }
    }
    out += "\nA misspelled hook name is never called, and `kb_cli validate` warns about it. All hooks: topic `EventHooks`.\n";
    return out;
}

std::string ScriptEventHooks::Page() {
    std::string out = "# Event hooks (EventHooks)\n\n";
    out += "The engine calls a global function of the script on an object by its name. Lifecycle hooks receive `(self, dt)`; every other hook ";
    out += "receives `(self, event)` and reads its values from `event.args` (`event.name`, `event.sender` and `event.target` are there too). ";
    out += "Define only the ones you need. A hook the engine does not have (a misspelled one, `OnStart`, `Update`) is never called, ";
    out += "and `kb_cli validate` warns about it.\n\n";
    out += "```lua\nfunction OnTriggerEnter(self, event)\n    Log(\"touched by \" .. tostring(event.args.other))\nend\n```\n\n";
    out += "Entities and ids (`Entity`, `Hash`) are plain integers in Lua. `kb_cli api --topic <HookName>` shows one hook with the types.\n\n";
    out += "## Hooks\n\n| Hook | Called | event.args |\n|---|---|---|\n";
    bool uiListed = false;
    for (const ScriptEventHook& hook : Hooks()) {
        if (hook.group == "UI") {
            if (uiListed) continue;
            uiListed = true;
            out += "| `OnUIClicked(self, event)`, `OnUIPressed`, `OnUIChanged`, ... | " + std::string{ hook.when } +
                " | `entity`, `pointerX`, `pointerY`, `value`, `text`, `action`, `other` |\n";
            continue;
        }
        std::string arguments;
        for (const ScriptEventHookArgument& argument : hook.args) {
            arguments += (arguments.empty() ? "`" : ", `") + std::string{ argument.name } + "`";
        }
        out += "| `" + DisplayName(hook) + std::string{ hook.receives } + "` | " + std::string{ hook.when } + " | " +
            (arguments.empty() ? std::string{ "-" } : arguments) + " |\n";
    }
    out += "\n## Touching things\n\n";
    out += "`OnTriggerEnter/Stay/Exit` (a Collider with `trigger=true`, nothing blocks) and `OnCollisionEnter/Stay/Exit` (solid contact) ";
    out += "go to the script of each object involved, with the other object in `event.args.other`. ";
    out += "At least one of the two must move (a Rigidbody or a CharacterController). A pickup: guide `recipe-pickup`.\n\n";
    out += "## Your own events\n\n";
    out += "`Emit(\"CoinCollected\", { amount = 1 })` sends an event to every script; `EmitTo(entity, \"CoinCollected\", { amount = 1 })` to one object. ";
    out += "A script answers with `function CoinCollected(self, event)` and reads `event.args.amount`. Topics `Emit` and `EmitTo`.\n";
    return out;
}

} // namespace kb::script
