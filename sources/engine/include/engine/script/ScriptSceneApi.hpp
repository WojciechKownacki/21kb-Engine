#pragma once

namespace kb::script {

class ScriptRuntimeHost;

class ScriptSceneApi final {
public:
    ScriptSceneApi() = delete;

    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
