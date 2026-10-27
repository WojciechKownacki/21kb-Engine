#include "GameRuntimeDiagnostics.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/script/ScriptModule.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <utility>

namespace kb::game {

std::vector<std::string> DrainGameRuntimeErrors(
    kb::scene::Scene& scene, kb::script::ScriptModule* script) {
    std::vector<std::string> errors = scene.Runtime().DrainSceneSystemErrors();
    for (std::string& error : errors) {
        error = "scene runtime error: " + std::move(error);
    }
    if (script != nullptr && script->Host() != nullptr) {
        for (std::string& error : script->Host()->DrainSceneSystemDiagnostics()) {
            errors.push_back("script error: " + std::move(error));
        }
    }
    return errors;
}

} // namespace kb::game
