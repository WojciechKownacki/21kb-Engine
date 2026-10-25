#pragma once
namespace kb::script {
class ScriptRuntimeHost;
class ScriptMap2DApi final {
public:
    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};
}
