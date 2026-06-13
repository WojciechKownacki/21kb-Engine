#pragma once

namespace kb::script {

class ScriptRuntimeHost;

class ScriptTransformApi final {
public:
    ScriptTransformApi() = delete;

    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
