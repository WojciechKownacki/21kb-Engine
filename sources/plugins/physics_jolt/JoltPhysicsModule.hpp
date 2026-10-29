#pragma once

#include "engine/modules/IEngineModule.hpp"
#include "engine/scene/SceneSystemHandle.hpp"

#include <unordered_map>

namespace kb::physics_jolt {

class JoltPhysicsModule final : public kb::modules::IEngineModule {
public:
    [[nodiscard]] kb::modules::EngineModuleMetadata Metadata() const override;
    void OnSceneAttach(kb::scene::Scene& scene) override;
    void OnSceneDetach(kb::scene::Scene& scene) override;

private:
    std::unordered_map<kb::scene::Scene*, kb::scene::SceneSystemHandle> sceneSystems_;
};

} // namespace kb::physics_jolt
