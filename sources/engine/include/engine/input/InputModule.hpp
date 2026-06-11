#pragma once

#include "engine/modules/IEngineModule.hpp"
#include "engine/modules/EngineModuleMetadata.hpp"

namespace kb::input {

// Built-in engine module that owns a scene's input wiring. OnSceneAttach binds the
// scene input subsystem's asset resolvers to the scene asset manager and installs
// the polling system that re-evaluates actions every runtime tick. This logic used
// to live inline in Scene's constructor; routing it through EngineModuleHost lets a
// project enable or disable input like any other module.
class InputModule final : public kb::modules::IEngineModule {
public:
    [[nodiscard]] kb::modules::EngineModuleMetadata Metadata() const override;
    void OnSceneAttach(kb::scene::Scene& scene) override;
};

} // namespace kb::input
