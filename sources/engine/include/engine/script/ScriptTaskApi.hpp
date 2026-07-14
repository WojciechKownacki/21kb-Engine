#pragma once

namespace kb::script {

class ScriptRuntimeHost;

class ScriptTaskApi final {
public:
    ScriptTaskApi() = delete;

    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
