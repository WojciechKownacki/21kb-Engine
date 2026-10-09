#pragma once

namespace kb::script {

class ScriptRuntimeHost;

// Navigation.*: path queries, raycasts and nearest points on the scene's polygon navigation
// meshes (kb::scene::SceneNavigation), and area costs.
class ScriptNavigationApi final {
public:
    ScriptNavigationApi() = delete;

    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
