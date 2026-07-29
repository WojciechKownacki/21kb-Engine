#pragma once

namespace kb::script {

class ScriptRuntimeHost;

class ScriptLocalizationApi final {
public:
    ScriptLocalizationApi() = delete;
    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
