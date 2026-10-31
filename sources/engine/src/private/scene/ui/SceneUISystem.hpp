#pragma once

#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/StatelessSceneSystem.hpp"

namespace kb::scene {

class SceneUISystem final : public SceneSystem, public IStatelessSceneSystem {
  public:
    // Modal routing must be current before any input polling or gameplay update,
    // regardless of the order in which modules registered their systems.
    void OnFrameStart(SceneSystemContext& context) override;
};

} // namespace kb::scene
