#pragma once

#include "engine/modules/IEngineModule.hpp"
#include "engine/modules/EngineModuleMetadata.hpp"

namespace kb::basic_lighting {

class BasicLightingModule final : public kb::modules::IEngineModule {
public:
    [[nodiscard]] kb::modules::EngineModuleMetadata Metadata() const override;
    void OnSceneAttach(kb::scene::Scene& scene) override;
    void OnSceneDetach(kb::scene::Scene& scene) override;
};

} // namespace kb::basic_lighting
