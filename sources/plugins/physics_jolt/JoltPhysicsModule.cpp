#include "JoltPhysicsModule.hpp"

#include "JoltPhysicsSceneSystem.hpp"
#include "engine/modules/EngineModuleExports.hpp"
#include "engine/modules/EngineModuleLoadingPhase.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneRuntime.hpp"

#include <memory>

namespace kb::physics_jolt {

kb::modules::EngineModuleMetadata JoltPhysicsModule::Metadata() const {
    return kb::modules::EngineModuleMetadata{
        "Physics.Jolt",
        1U,
        {},
        kb::modules::EngineModuleLoadingPhase::Default,
    };
}

void JoltPhysicsModule::OnSceneAttach(kb::scene::Scene& scene) {
    scene.Runtime().AddSceneSystem(std::make_unique<JoltPhysicsSceneSystem>());
}

} // namespace kb::physics_jolt

extern "C" KB_ENGINE_MODULE_EXPORT std::uint32_t kb_engine_module_abi_version() {
    return kb::modules::kEngineModuleAbiVersion;
}

extern "C" KB_ENGINE_MODULE_EXPORT const char* kb_engine_module_name() {
    return "Physics.Jolt";
}

extern "C" KB_ENGINE_MODULE_EXPORT kb::modules::IEngineModule* kb_create_engine_module() {
    return new kb::physics_jolt::JoltPhysicsModule();
}

extern "C" KB_ENGINE_MODULE_EXPORT void kb_destroy_engine_module(kb::modules::IEngineModule* module) {
    delete module;
}
