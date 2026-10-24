#pragma once

namespace kb::script {
class ScriptRuntimeHost;
}

namespace kb::game {

// Configure ScriptModule before its scene system locks the function registry.
void ConfigureGameScriptLog(kb::script::ScriptRuntimeHost& host);

} // namespace kb::game
