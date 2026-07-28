#pragma once

namespace kb::script {

class ScriptRuntimeHost;

// LIB-167: script boundary over the scene-owned Animator runtime. All calls
// target the caller by default and accept an optional explicit entity.
struct ScriptAnimatorApi {
    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
