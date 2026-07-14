#pragma once

namespace kb::script {

class ScriptRuntimeHost;

class ScriptTimerApi final {
public:
    ScriptTimerApi() = delete;

    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
