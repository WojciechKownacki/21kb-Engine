#pragma once

#include "engine/modules/EngineModuleMetadata.hpp"

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::modules {

class EngineModuleContext;

// Engine-facing interface every subsystem (input, scripting, physics, audio, ...)
// implements to be driven by EngineModuleHost. Lifecycle, in order:
//   OnLoad        once, after the host decides the module is active (register components)
//   OnEnable      once, after every active module has loaded
//   OnSceneAttach for each scene the host activates (add scene systems)
//   OnSceneDetach when that scene is torn down
//   OnDisable     before unload
//   OnUnload      once, in reverse load order
// If OnLoad/OnEnable throws, the host calls OnUnload/OnDisable for that
// partially started step too, then rolls back earlier steps in reverse order.
// Cleanup must tolerate partial initialization and release owned registrations
// and callbacks before returning. It must not throw; cleanup failures are reported
// and do not prevent the host from rolling back the remaining modules.
// Default implementations are no-ops so a module only overrides what it needs.
class IEngineModule {
public:
    IEngineModule() = default;
    virtual ~IEngineModule() = default;

    IEngineModule(const IEngineModule&) = delete;
    IEngineModule& operator=(const IEngineModule&) = delete;
    IEngineModule(IEngineModule&&) = delete;
    IEngineModule& operator=(IEngineModule&&) = delete;

    [[nodiscard]] virtual EngineModuleMetadata Metadata() const = 0;

    virtual void OnLoad(EngineModuleContext&) {}
    virtual void OnEnable() {}
    virtual void OnSceneAttach(kb::scene::Scene&) {}
    virtual void OnSceneDetach(kb::scene::Scene&) {}
    virtual void OnDisable() {}
    virtual void OnUnload() {}
};

} // namespace kb::modules
