#include "MiniaudioModule.hpp"

#include "MiniaudioSceneSystem.hpp"
#include "engine/modules/EngineModuleExports.hpp"
#include "engine/modules/EngineModuleLoadingPhase.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneRuntime.hpp"

#include <memory>
#include <stdexcept>

namespace kb::audio_miniaudio {

kb::modules::EngineModuleMetadata MiniaudioModule::Metadata() const {
    return kb::modules::EngineModuleMetadata{
        "Audio.Miniaudio",
        1U,
        {},
        kb::modules::EngineModuleLoadingPhase::Default,
    };
}

void MiniaudioModule::OnSceneAttach(kb::scene::Scene& scene) {
    if (sceneSystems_.contains(&scene)) {
        return;
    }
    const kb::scene::SceneSystemHandle handle = scene.Runtime().AddSceneSystem(std::make_unique<MiniaudioSceneSystem>());
    if (handle.IsValid()) {
        sceneSystems_.emplace(&scene, handle);
    }
}

void MiniaudioModule::OnSceneDetach(kb::scene::Scene& scene) {
    const auto iterator = sceneSystems_.find(&scene);
    if (iterator == sceneSystems_.end()) {
        return;
    }
    if (!scene.Runtime().RemoveSceneSystem(iterator->second)
        && scene.Runtime().HasSceneSystem(iterator->second)) {
        throw std::logic_error("audio scene system could not detach during active dispatch");
    }
    sceneSystems_.erase(iterator);
}

} // namespace kb::audio_miniaudio

#if !defined(KB_ENGINE_MODULE_STATIC_LINK)
extern "C" KB_ENGINE_MODULE_EXPORT std::uint32_t kb_engine_module_abi_version() {
    return kb::modules::kEngineModuleAbiVersion;
}

extern "C" KB_ENGINE_MODULE_EXPORT const char* kb_engine_module_name() {
    return "Audio.Miniaudio";
}

extern "C" KB_ENGINE_MODULE_EXPORT kb::modules::IEngineModule* kb_create_engine_module() {
    return new kb::audio_miniaudio::MiniaudioModule();
}

extern "C" KB_ENGINE_MODULE_EXPORT void kb_destroy_engine_module(kb::modules::IEngineModule* module) {
    delete module;
}
#endif
