#pragma once

namespace kb::script {

class ScriptRuntimeHost;

// LIB-058: the script-facing surface for the controlled collection types
// (kb::library::Array/Set/Map/Queue/Stack). A script never holds a
// collection value directly — ScriptValue is purely scalar (LIB-032) — so
// each collection lives natively in a per-host store and a script holds an
// opaque Hash handle to it, exactly the pattern Assets/Timer/Task handles
// already use. Register() wires Array.*/Set.*/Map.*/Queue.*/Stack.*
// functions into the one ScriptFunctionRegistry, so they are callable
// identically from Native, Lua, and Visual Graph.
class ScriptCollectionsApi final {
public:
    ScriptCollectionsApi() = delete;

    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
