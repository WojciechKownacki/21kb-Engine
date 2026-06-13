#pragma once

namespace kb::script {

class ScriptRuntimeHost;

class ScriptPhysicsApi final {
public:
    ScriptPhysicsApi() = delete;

    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
