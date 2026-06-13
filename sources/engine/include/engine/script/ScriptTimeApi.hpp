#pragma once

namespace kb::script {

class ScriptRuntimeHost;

class ScriptTimeApi final {
public:
    ScriptTimeApi() = delete;

    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
