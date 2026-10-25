#include "World2DModule.hpp"
#include "Map2DRuntime.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneRuntime.hpp"

#include "engine/modules/EngineModuleExports.hpp"
#include "engine/modules/EngineModuleLoadingPhase.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <flecs.h>

namespace kb::world2d {

void World2DModule::OnLoad(kb::modules::EngineModuleContext&) {
    // The provider statically links its ECS backend. Initialize this DLL's OS
    // hooks before it creates or destroys entities in a world owned by the host.
    ecs_os_init();
}

void World2DModule::OnUnload() {
    ecs_os_fini();
}

kb::modules::EngineModuleMetadata World2DModule::Metadata() const {
    return kb::modules::EngineModuleMetadata{
        "World.Map2D",
        1U,
        {},
        kb::modules::EngineModuleLoadingPhase::Default,
    };
}

void World2DModule::OnSceneAttach(kb::scene::Scene& scene) {
    if (scenes_.contains(&scene)) return;
    auto runtime = std::make_unique<Map2DRuntime>();
    auto* pointer = runtime.get();
    const auto handle = scene.Runtime().AddSceneSystem(std::move(runtime));
    if (handle.IsValid()) scenes_.emplace(&scene, Attached{ handle, pointer });
}

void World2DModule::OnSceneDetach(kb::scene::Scene& scene) {
    const auto found = scenes_.find(&scene);
    if (found == scenes_.end()) return;
    if (!scene.Runtime().RemoveSceneSystem(found->second.handle) && scene.Runtime().HasSceneSystem(found->second.handle))
        throw std::logic_error("Tilemap scene system could not detach during active dispatch");
    scenes_.erase(found);
}

Map2DRuntime* World2DModule::RuntimeFor(kb::scene::Scene& scene) const noexcept {
    const auto found = scenes_.find(&scene);
    return found == scenes_.end() ? nullptr : found->second.runtime;
}

} // namespace kb::world2d

#if !defined(KB_ENGINE_MODULE_STATIC_LINK)
extern "C" KB_ENGINE_MODULE_EXPORT std::uint32_t kb_engine_module_abi_version() {
    return kb::modules::kEngineModuleAbiVersion;
}

extern "C" KB_ENGINE_MODULE_EXPORT const char* kb_engine_module_name() {
    return "World.Map2D";
}

extern "C" KB_ENGINE_MODULE_EXPORT kb::modules::IEngineModule* kb_create_engine_module() {
    return new kb::world2d::World2DModule();
}

extern "C" KB_ENGINE_MODULE_EXPORT void kb_destroy_engine_module(kb::modules::IEngineModule* module) {
    delete module;
}
#endif
