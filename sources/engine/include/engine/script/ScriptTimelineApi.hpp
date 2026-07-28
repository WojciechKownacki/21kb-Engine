#pragma once

namespace kb::script {

class ScriptRuntimeHost;

struct ScriptTimelineApi {
    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);
};

} // namespace kb::script
