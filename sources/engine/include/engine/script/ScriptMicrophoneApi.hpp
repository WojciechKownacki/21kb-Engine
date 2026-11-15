#pragma once

namespace kb::script {

class ScriptRuntimeHost;

// Microphone.*: device list, start and stop of the capture of an input device, its recent level, and
// recordings kept as audio clips (kb::audio::AudioCapture).
class ScriptMicrophoneApi final {
public:
    ScriptMicrophoneApi() = delete;

    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
