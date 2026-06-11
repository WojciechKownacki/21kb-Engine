#pragma once

#include "engine/modules/IEngineModule.hpp"

namespace kb::audio_miniaudio {

class MiniaudioModule final : public kb::modules::IEngineModule {
public:
    [[nodiscard]] kb::modules::EngineModuleMetadata Metadata() const override;
    void OnSceneAttach(kb::scene::Scene& scene) override;
};

} // namespace kb::audio_miniaudio
