#pragma once

#include <string>
#include <vector>

namespace kb::scene {
class Scene;
}
namespace kb::script {
class ScriptModule;
}

namespace kb::game {

[[nodiscard]] std::vector<std::string> DrainGameRuntimeErrors(
    kb::scene::Scene& scene, kb::script::ScriptModule* script);

} // namespace kb::game
