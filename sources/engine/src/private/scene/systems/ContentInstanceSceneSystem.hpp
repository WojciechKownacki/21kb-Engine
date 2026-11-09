#pragma once

#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/StatelessSceneSystem.hpp"

namespace kb::scene {

class ContentInstanceSceneSystem final : public SceneSystem, public IStatelessSceneSystem {
public:
    void OnUpdate(SceneSystemContext& context) override;
    void OnDestroy(SceneSystemContext& context) override;
};

} // namespace kb::scene
