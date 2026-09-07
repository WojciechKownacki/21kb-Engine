#pragma once

namespace kb::script {

class ScriptRuntimeHost;

class ScriptUIApi final {
public:
    ScriptUIApi() = delete;

    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
