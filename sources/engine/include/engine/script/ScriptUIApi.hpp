#pragma once

namespace kb::script {

class ScriptRuntimeHost;

// LIB-174: script boundary for the scene-owned deferred UI command queue.
// Calls target the caller's UIDocument by default and accept an explicit owner.
struct ScriptUIApi {
    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
