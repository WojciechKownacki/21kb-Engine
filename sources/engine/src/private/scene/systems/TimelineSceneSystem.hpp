#pragma once

#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/StatelessSceneSystem.hpp"

namespace kb::scene {

class TimelineSceneSystem final : public SceneSystem, public IStatelessSceneSystem {
public:
    void OnUpdate(SceneSystemContext& context) override;
};

} // namespace kb::scene
