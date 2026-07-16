#pragma once

namespace kb::script {

class ScriptRuntimeHost;

class ScriptAssetsApi final {
public:
    ScriptAssetsApi() = delete;

    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
