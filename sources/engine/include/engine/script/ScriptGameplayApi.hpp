#pragma once

namespace kb::script {

class ScriptRuntimeHost;

// Script boundary over the scene's fights (kb::scene::SceneGameplay): damage, healing, abilities,
// health and mana for health bars, and what a combatant perceives. Calls target the caller by
// default and accept an explicit entity.
struct ScriptGameplayApi {
    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
