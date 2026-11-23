#include "engine/script/ScriptSandboxGlobals.hpp"

#include <algorithm>
#include <array>

namespace kb::script {
namespace {

constexpr std::array kGlobals{
    ScriptSandboxGlobal{ "Log", "function", "Log(value)", "Writes a value to the log.",
        R"md(Any value works: it is shown as text. `kb_cli run` prints it as `[log] <text>`, the editor shows it in the Console. It returns nothing.

```lua
Log("coins: " .. coins)
```

This is not `Math.Log` (the natural logarithm: topic `Math.Log`).)md" },
    ScriptSandboxGlobal{ "Emit", "function", "Emit(eventName, args)", "Sends a custom event to every script.",
        R"md(Every script that defines a function called `eventName` is called with `(self, event)`. `args` is an optional table; its entries arrive as `event.args.<key>`. It returns nothing.

```lua
Emit("CoinCollected", { amount = 1 })     -- elsewhere: function CoinCollected(self, event) ... event.args.amount
```

To reach one object only: `EmitTo`. This is not `Particles.Emit` (it emits particles: topic `Particles.Emit`). The hooks the engine itself calls: topic `EventHooks`.)md" },
    ScriptSandboxGlobal{ "EmitTo", "function", "EmitTo(entity, eventName, args)", "Sends a custom event to the script(s) of one object.",
        R"md(Like `Emit`, but only the scripts on `entity` (an entity id, for example from `World.FindByName("Score")`) are called. `args` is an optional table.

```lua
local counter = World.FindByName("CoinCounter")
EmitTo(counter, "CoinCollected", { amount = 1 })
```)md" },
    ScriptSandboxGlobal{ "Import", "function", "Import(moduleName)", "Loads a module script and returns what it returns.",
        R"md(A module is a `.lua` file that ends with `return { ... }`. Declare it at the top of the script with a comment, then import it by the same name (the folder and file name joined by dots: `Shared/Math.lua` is `Shared.Math`):

```lua
-- @import Shared.Math
local MathUtil = Import("Shared.Math")
Log(MathUtil.add(1, 2))
```

It returns the module table, or `nil` and an error text when the module is not declared or not found. The sandbox has no `require`.)md" },
    ScriptSandboxGlobal{ "SetShared", "function", "SetShared(key, value)", "Stores a number, text or true/false that every script can read.",
        R"md(`key` is a text name. `value` is a number, a string or a boolean (not a table). Returns `true` when it was stored. Read it with `GetShared`.

```lua
SetShared("coins", 3)
```

Shared values live as long as the scene runs and are visible to every script.)md" },
    ScriptSandboxGlobal{ "GetShared", "function", "GetShared(key)", "Reads a value stored with SetShared.",
        R"md(Returns the value, or `nil` when nothing is stored under `key`.

```lua
local coins = GetShared("coins") or 0
```)md" },
    ScriptSandboxGlobal{ "HasShared", "function", "HasShared(key)", "Whether a value is stored under a key.",
        "Returns `true` or `false`.\n\n```lua\nif not HasShared(\"coins\") then SetShared(\"coins\", 0) end\n```" },
    ScriptSandboxGlobal{ "RemoveShared", "function", "RemoveShared(key)", "Deletes a value stored with SetShared.",
        "Returns `true` when there was a value to remove.\n\n```lua\nRemoveShared(\"coins\")\n```" },
    ScriptSandboxGlobal{ "CallFunction", "function", "CallFunction(name, args)", "Calls an engine function by its registered name.",
        R"md(`name` is the function's full name (`"Transform.Rotate"`) and `args` a table of its inputs by name, as the function's reference page lists them. It returns nothing for a function with no output, the value for one output, or a table of the outputs; when the call fails it returns `nil` and the error text.

```lua
CallFunction("Transform.Rotate", { entity = self.entity, x = 0, y = 90, z = 0 })
```

Every function is also `Group.Name(inputs in order)` (`Transform.Rotate(self.entity, 0, 90, 0)`), which is the shorter way.)md" },
    ScriptSandboxGlobal{ "Events", "table", "Events.Subscribe(name, handler)", "Listens to events, sends them with a recipient filter.",
        R"md(- `Events.Subscribe(name, handler [, owner [, channel]])` calls `handler(event)` for every event called `name`; returns a handle (a number above 0). `event.args` holds the values.
- `Events.Unsubscribe(handle)` stops it; returns `true` when it was listening.
- `Events.Emit(name, args, target, filter)` delivers an event now; returns how many scripts got it.
- `Events.EmitDeferred(name, args, target, filter)` queues it for the end of the frame; returns `true` when queued.
- `Events.Broadcast(name, args, filter)` delivers to every script; returns how many got it.

`filter` is an optional table: `{ tag = "Enemy", component = "Collider", channel = "ui", scene = id, player = id }`.

```lua
function Created(self)
    Events.Subscribe("OnTimelineEvent", function(event) Log(event.args.name) end)
end
```

A global function named after the event does the same without subscribing: `function OnTimelineEvent(self, event)`.)md" },
    ScriptSandboxGlobal{ "Inspector", "table", "Inspector.name = default", "Declares a variable that is editable in the Inspector.",
        R"md(A top-level line `Inspector.speed = 5.0` declares the variable `speed` (its type comes from the default: 5.0 is a number, 5 an integer, `true` a boolean, a text a string). Read it with `Inspector.speed` or `self:GetVariable("speed")`; the value belongs to the object the script is on, so `kb_cli scene-attach ... --set speed=8` gives one object its own.

```lua
Inspector.speed = 5.0
function Tick(self, dt)
    Transform.Translate(self.entity, 0, 0, Inspector.speed * dt)
end
```

The older comment form `-- @expose speed Float = 5.0` declares the same.)md" },
    ScriptSandboxGlobal{ "assert", "function", "assert(condition, message)", "Fails the script with a message when a condition is false.",
        R"md(Standard Lua. When `condition` is `false` or `nil` it raises an error with `message` (default `assertion failed!`); otherwise it returns the condition. In a script it is the way to test logic without a window.

```lua
-- Tests.lua: attach to any object, then:  kb_cli run --project . --frames 2
local function double(n) return n * 2 end
function Ready(self, dt)
    assert(double(4) == 8, "double(4) must be 8")
    assert(Math.Clamp(5, 0, 1) == 1, "Clamp keeps the value inside the range")
end
```

A failed assert is a script error with the file and line: `run` prints `[script error] ... /Game/Scripts/Tests.lua:4: double(4) must be 8` (when `double(4)` is not 8), ends with `1 diagnostics` and **exits with code 1**. When every assert holds it ends with `0 diagnostics` and exits 0. With `--json` the errors are a list with `file`, `line`, `message` and `traceback`. Put the checks in `Ready` (once) or `Tick` (every frame).)md" },
    ScriptSandboxGlobal{ "error", "function", "error(message, level)", "Raises an error that stops the running function.",
        "Standard Lua. The script error shows in `run` with its file and line; `pcall` catches it.\n\n```lua\nif not target then error(\"no target\") end\n```" },
    ScriptSandboxGlobal{ "pcall", "function", "pcall(f, ...)", "Calls a function and catches its error.",
        "Standard Lua. Returns `true` and the results, or `false` and the error text.\n\n```lua\nlocal ok, err = pcall(function() Transform.Rotate(nil, 0, 0, 0) end)\nif not ok then Log(err) end\n```" },
    ScriptSandboxGlobal{ "xpcall", "function", "xpcall(f, handler, ...)", "Like pcall, with a function that handles the error.",
        "Standard Lua: when `f` fails, `handler` receives the error text and what it returns comes back after `false`." },
    ScriptSandboxGlobal{ "print", "function", "print(...)", "Standard Lua print; use Log instead.",
        "Use `Log(value)` to write to the log: its output is what `kb_cli run` and the editor show." },
    ScriptSandboxGlobal{ "type", "function", "type(value)", "The type of a value as text.",
        "Standard Lua: `\"nil\"`, `\"number\"`, `\"string\"`, `\"boolean\"`, `\"table\"`, `\"function\"`." },
    ScriptSandboxGlobal{ "tostring", "function", "tostring(value)", "A value as text.", "Standard Lua." },
    ScriptSandboxGlobal{ "tonumber", "function", "tonumber(value, base)", "Text as a number, or nil.", "Standard Lua." },
    ScriptSandboxGlobal{ "pairs", "function", "pairs(table)", "Walks every key and value of a table.", "Standard Lua; the order of keys is not defined." },
    ScriptSandboxGlobal{ "ipairs", "function", "ipairs(list)", "Walks a list from 1 up to the first nil.", "Standard Lua." },
    ScriptSandboxGlobal{ "next", "function", "next(table, key)", "The next key and value of a table.", "Standard Lua." },
    ScriptSandboxGlobal{ "select", "function", "select(n, ...)", "The n-th and later arguments, or the number of them with \"#\".", "Standard Lua." },
    ScriptSandboxGlobal{ "unpack", "function", "unpack(list, i, j)", "The elements of a list as separate values.",
        "Both script VMs have `unpack` (not `table.unpack`)." },
    ScriptSandboxGlobal{ "rawget", "function", "rawget(table, key)", "Reads a table without metamethods.", "Standard Lua." },
    ScriptSandboxGlobal{ "rawset", "function", "rawset(table, key, value)", "Writes a table without metamethods.", "Standard Lua." },
    ScriptSandboxGlobal{ "rawequal", "function", "rawequal(a, b)", "Compares two values without metamethods.", "Standard Lua." },
    ScriptSandboxGlobal{ "math", "library", "math.floor(x)", "Standard Lua maths.",
        "`abs acos asin atan atan2 ceil cos deg exp floor fmod huge log max min modf pi rad random randomseed sin sqrt tan`, the ones both script VMs have. The engine's own maths (vectors, clamping, interpolation) is the `Math` group: topic `Math`." },
    ScriptSandboxGlobal{ "string", "library", "string.format(format, ...)", "Standard Lua text functions.",
        "`byte char find format gmatch gsub len lower match rep reverse sub upper`; also as methods: `(\"%d coins\"):format(3)`." },
    ScriptSandboxGlobal{ "table", "library", "table.insert(list, value)", "Standard Lua list functions.", "`concat insert remove sort`. Use the global `unpack`, not `table.unpack`." },
    ScriptSandboxGlobal{ "coroutine", "library", "coroutine.yield()", "Standard Lua coroutines.",
        "`create resume running status wrap yield`. A lifecycle hook or event function may call `coroutine.yield()` (or the `Task` functions) to wait a frame." },
    ScriptSandboxGlobal{ "bit", "library", "bit.band(a, b)", "Bit operations (use these instead of & | ~ << >>).",
        "`tobit bnot band bor bxor lshift rshift arshift rol ror bswap tohex`." },
};

} // namespace

std::span<const ScriptSandboxGlobal> ScriptSandboxGlobals::All() noexcept {
    return kGlobals;
}

const ScriptSandboxGlobal* ScriptSandboxGlobals::Find(std::string_view name) noexcept {
    const auto found = std::ranges::find(kGlobals, name, &ScriptSandboxGlobal::name);
    return found == kGlobals.end() ? nullptr : &*found;
}

bool ScriptSandboxGlobals::IsKnown(std::string_view name) noexcept {
    return Find(name) != nullptr || name == "_G" || name == "rawlen" || name == "utf8";
}

std::string ScriptSandboxGlobals::Page(const ScriptSandboxGlobal& global) {
    std::string out = "# " + std::string{ global.name } + "\n\n";
    out += "`" + std::string{ global.signature } + "` - " + std::string{ global.summary } + "\n\n";
    out += std::string{ global.details } + "\n";
    // What the engine adds to a script is not there yet while the file loads; the standard Lua names are.
    constexpr std::array kOnlyInHooks{ std::string_view{ "Log" }, std::string_view{ "Emit" }, std::string_view{ "EmitTo" }, std::string_view{ "SetShared" },
        std::string_view{ "GetShared" }, std::string_view{ "HasShared" }, std::string_view{ "RemoveShared" }, std::string_view{ "CallFunction" } };
    if (std::ranges::find(kOnlyInHooks, global.name) != kOnlyInHooks.end()) {
        out += "\nCall it inside a hook (`Ready`, `Tick`, an event function), not on a line at the top level of the file: while the file loads it does not exist yet.\n";
    }
    return out;
}

std::string ScriptSandboxGlobals::IndexPage() {
    std::string out = "# Globals of a Lua script\n\n";
    out += "Names a script uses without defining them, besides the function groups (`Transform`, `Physics`, ...). ";
    out += "Ask `kb_cli api --topic <Name>` for one; the name spelled exactly like this wins over a function of a group with the same name.\n\n";
    out += "| Name | What it does |\n|---|---|\n";
    for (const ScriptSandboxGlobal& global : kGlobals) {
        out += "| [`" + std::string{ global.signature } + "`](globals/" + std::string{ global.name } + ".md) | " + std::string{ global.summary } + " |\n";
    }
    out += "\nThese and the function groups work only inside a hook (`Ready`, `Tick`, an event function): while the file loads they do not exist yet, ";
    out += "so a call on a line at the top level fails with `attempt to call global ... (a nil value)`. Only definitions and `Inspector.speed = 5.0` lines belong there.\n";
    out += "\nAlso in every script: `self` (the first argument of every hook: `self.entity`, `self:GetProperty(component, property)`, ";
    out += "`self:SetProperty(component, property, value)`, `self:HasComponent(component)`, `self:GetVariable(name)`, `self:SetVariable(name, value)`) ";
    out += "and the hooks the engine calls (topic `EventHooks`). There is no `io`, `os`, `require` or `load`.\n";
    return out;
}

} // namespace kb::script
