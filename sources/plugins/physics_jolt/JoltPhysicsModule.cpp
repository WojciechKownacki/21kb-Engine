#include "JoltPhysicsModule.hpp"

#include "JoltPhysicsSceneSystem.hpp"
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

#if defined(_WIN32)
#define KB_PHYSICS_JOLT_EXPORT __declspec(dllexport)
#else
#define KB_PHYSICS_JOLT_EXPORT
#endif

extern "C" KB_PHYSICS_JOLT_EXPORT kb::modules::IEngineModule* kbCreateEngineModule() {
    return new kb::physics_jolt::JoltPhysicsModule();
}

extern "C" KB_PHYSICS_JOLT_EXPORT void kbDestroyEngineModule(kb::modules::IEngineModule* module) {
    delete module;
}
