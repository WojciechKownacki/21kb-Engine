#pragma once

namespace kb::script {

class ScriptRuntimeHost;

class ScriptSaveApi final {
public:
    ScriptSaveApi() = delete;

    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
