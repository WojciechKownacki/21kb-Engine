#pragma once

#include "engine/modules/IEngineModule.hpp"

namespace kb::physics_jolt {

class JoltPhysicsModule final : public kb::modules::IEngineModule {
public:
    [[nodiscard]] kb::modules::EngineModuleMetadata Metadata() const override;
    void OnSceneAttach(kb::scene::Scene& scene) override;
};

} // namespace kb::physics_jolt
