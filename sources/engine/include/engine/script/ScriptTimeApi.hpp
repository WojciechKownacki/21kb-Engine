#pragma once

namespace kb::scene { class Scene; }

namespace kb::script {

class ScriptRuntimeHost;

class ScriptTimeApi final {
public:
    ScriptTimeApi() = delete;

    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
    // The frame delta scripts see (Time.Delta, as a registered function and from Lua): `deltaSeconds` times the scene's
    // time scale, zero while the scene is paused; a missing scene leaves it unscaled.
    [[nodiscard]] static float ScaledDelta(const kb::scene::Scene* scene, float deltaSeconds) noexcept;
};

} // namespace kb::script
