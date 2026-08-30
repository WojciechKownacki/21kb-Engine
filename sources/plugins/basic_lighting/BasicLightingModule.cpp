#include "BasicLightingModule.hpp"

#include "engine/modules/EngineModuleExports.hpp"
#include "engine/modules/EngineModuleLoadingPhase.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneLightingAccess.hpp"

#include <cstdint>

namespace kb::basic_lighting {

kb::modules::EngineModuleMetadata BasicLightingModule::Metadata() const {
    return kb::modules::EngineModuleMetadata{
        "Rendering.BasicLighting",
        1U,
        {},
        kb::modules::EngineModuleLoadingPhase::Default,
    };
}

void BasicLightingModule::OnSceneAttach(kb::scene::Scene& scene) {
    kb::scene::SceneLightingAccess::SetBasicLightingEnabled(scene, true);
}

void BasicLightingModule::OnSceneDetach(kb::scene::Scene& scene) {
    kb::scene::SceneLightingAccess::SetBasicLightingEnabled(scene, false);
}

} // namespace kb::basic_lighting

#if !defined(KB_ENGINE_MODULE_STATIC_LINK)
extern "C" KB_ENGINE_MODULE_EXPORT std::uint32_t kb_engine_module_abi_version() {
    return kb::modules::kEngineModuleAbiVersion;
}

extern "C" KB_ENGINE_MODULE_EXPORT const char* kb_engine_module_name() {
    return "Rendering.BasicLighting";
}

extern "C" KB_ENGINE_MODULE_EXPORT kb::modules::IEngineModule* kb_create_engine_module() {
    return new kb::basic_lighting::BasicLightingModule();
}

extern "C" KB_ENGINE_MODULE_EXPORT void kb_destroy_engine_module(kb::modules::IEngineModule* module) {
    delete module;
}
#endif
