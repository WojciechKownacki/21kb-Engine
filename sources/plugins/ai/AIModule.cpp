#include "AIModule.hpp"

#include "AIBrainSystem.hpp"
#include "engine/modules/EngineModuleExports.hpp"
#include "engine/modules/EngineModuleLoadingPhase.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneRuntime.hpp"

#include <exception>
#include <memory>
#include <stdexcept>

namespace kb::ai_plugin {

AIModule::~AIModule() {
    if (!sceneSystems_.empty()) std::terminate();
}

kb::modules::EngineModuleMetadata AIModule::Metadata() const {
    return kb::modules::EngineModuleMetadata{
        "Gameplay.AI",
        1U,
        {},
        kb::modules::EngineModuleLoadingPhase::Default,
    };
}

void AIModule::OnSceneAttach(kb::scene::Scene& scene) {
    if (sceneSystems_.contains(&scene)) return;
    const kb::scene::SceneSystemHandle handle = scene.Runtime().AddSceneSystem(std::make_unique<AIBrainSystem>());
    if (handle.IsValid()) sceneSystems_.emplace(&scene, handle);
}

void AIModule::OnSceneDetach(kb::scene::Scene& scene) {
    const auto iterator = sceneSystems_.find(&scene);
    if (iterator == sceneSystems_.end()) return;
    if (!scene.Runtime().RemoveSceneSystem(iterator->second) && scene.Runtime().HasSceneSystem(iterator->second)) {
        throw std::logic_error("AI scene system could not detach during active dispatch");
    }
    sceneSystems_.erase(iterator);
}

} // namespace kb::ai_plugin

#if !defined(KB_ENGINE_MODULE_STATIC_LINK)
extern "C" KB_ENGINE_MODULE_EXPORT std::uint32_t kb_engine_module_abi_version() {
    return kb::modules::kEngineModuleAbiVersion;
}

extern "C" KB_ENGINE_MODULE_EXPORT const char* kb_engine_module_name() {
    return "Gameplay.AI";
}

extern "C" KB_ENGINE_MODULE_EXPORT kb::modules::IEngineModule* kb_create_engine_module() {
    return new kb::ai_plugin::AIModule();
}

extern "C" KB_ENGINE_MODULE_EXPORT void kb_destroy_engine_module(kb::modules::IEngineModule* module) {
    delete module;
}
#endif
