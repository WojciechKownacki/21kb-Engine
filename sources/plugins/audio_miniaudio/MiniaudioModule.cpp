#include "MiniaudioModule.hpp"

#include "MiniaudioSceneSystem.hpp"
#include "engine/modules/EngineModuleExports.hpp"
#include "engine/modules/EngineModuleLoadingPhase.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneRuntime.hpp"

#include <memory>

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
    scene.Runtime().AddSceneSystem(std::make_unique<MiniaudioSceneSystem>());
}

} // namespace kb::audio_miniaudio

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
