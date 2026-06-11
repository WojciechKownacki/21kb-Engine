#include "MiniaudioModule.hpp"

#include "MiniaudioSceneSystem.hpp"
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

#if defined(_WIN32)
#define KB_AUDIO_MINIAUDIO_EXPORT __declspec(dllexport)
#else
#define KB_AUDIO_MINIAUDIO_EXPORT
#endif

extern "C" KB_AUDIO_MINIAUDIO_EXPORT kb::modules::IEngineModule* kbCreateEngineModule() {
    return new kb::audio_miniaudio::MiniaudioModule();
}

extern "C" KB_AUDIO_MINIAUDIO_EXPORT void kbDestroyEngineModule(kb::modules::IEngineModule* module) {
    delete module;
}
