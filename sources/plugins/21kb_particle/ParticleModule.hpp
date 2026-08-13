#pragma once

#include "engine/modules/IEngineModule.hpp"
#include "engine/scene/SceneSystemHandle.hpp"

#include <unordered_map>

namespace kb::particle_plugin {

class ParticleModule final : public kb::modules::IEngineModule {
public:
    ~ParticleModule() override;
    [[nodiscard]] kb::modules::EngineModuleMetadata Metadata() const override;
    void OnSceneAttach(kb::scene::Scene& scene) override;
    void OnSceneDetach(kb::scene::Scene& scene) override;

private:
    std::unordered_map<kb::scene::Scene*, kb::scene::SceneSystemHandle> sceneSystems_;
};

} // namespace kb::particle_plugin
