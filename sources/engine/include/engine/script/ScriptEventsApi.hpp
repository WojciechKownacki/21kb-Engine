#pragma once

namespace kb::script {

class ScriptRuntimeHost;

// Fixed-pin gameplay event emission for every ScriptFunctionRegistry
// frontend, including Visual Graph. Callback-based Subscribe remains a
// native/Lua-only surface; graphs receive through typed CustomEvent nodes.
class ScriptEventsApi final {
public:
    ScriptEventsApi() = delete;

    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
