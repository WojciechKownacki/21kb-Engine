#pragma once

#include "engine/modules/EngineModuleMetadata.hpp"
#include "engine/modules/IEngineModule.hpp"
#include "engine/scene/SceneSystemHandle.hpp"

#include <unordered_map>

namespace kb::scene { class Scene; }

namespace kb::ai_plugin {

// Gameplay.AI: runs every AI Brain of a scene (AIBrainComponent) while it plays.
class AIModule final : public kb::modules::IEngineModule {
public:
    ~AIModule() override;
    [[nodiscard]] kb::modules::EngineModuleMetadata Metadata() const override;
    void OnSceneAttach(kb::scene::Scene& scene) override;
    void OnSceneDetach(kb::scene::Scene& scene) override;

private:
    std::unordered_map<kb::scene::Scene*, kb::scene::SceneSystemHandle> sceneSystems_;
};

} // namespace kb::ai_plugin
