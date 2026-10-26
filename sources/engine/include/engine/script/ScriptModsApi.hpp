#pragma once

namespace kb::script {

class ScriptRuntimeHost;

// Mods.*: which mods the running game has loaded (kb::mods::PublishedLoadedMods) and the scene each names as its entry.
// The game decides what to do with them, for example opening a mod's entry scene with Scene.Load.
class ScriptModsApi final {
public:
    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
