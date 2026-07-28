#pragma once

namespace kb::script {

class ScriptRuntimeHost;
}

namespace kb::scene {
class Scene;
}

namespace kb::script {

class ScriptWorldApi final {
public:
    ScriptWorldApi() = delete;

    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
    static void ReleaseDeadPrefabParameterSets(kb::scene::Scene& scene);
};

} // namespace kb::script
