#pragma once

namespace kb::script {

class ScriptRuntimeHost;

class ScriptMathApi final {
public:
    ScriptMathApi() = delete;

    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
