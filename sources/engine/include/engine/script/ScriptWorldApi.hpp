#pragma once

namespace kb::script {

class ScriptRuntimeHost;

class ScriptWorldApi final {
public:
    ScriptWorldApi() = delete;

    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
