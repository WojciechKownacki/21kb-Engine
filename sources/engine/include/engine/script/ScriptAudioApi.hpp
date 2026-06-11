#pragma once

namespace kb::script {

class ScriptRuntimeHost;

// Registers Audio.Play as a script function. The function creates an AudioSource
// entity with autoplay enabled; the active audio provider owns actual playback.
struct ScriptAudioApi {
    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
