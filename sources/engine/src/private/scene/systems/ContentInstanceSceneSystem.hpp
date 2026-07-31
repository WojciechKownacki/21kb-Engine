#pragma once

#include "engine/scene/SceneSystem.hpp"

namespace kb::scene {

class ContentInstanceSceneSystem final : public SceneSystem {
public:
    void OnUpdate(SceneSystemContext& context) override;
    void OnDestroy(SceneSystemContext& context) override;
};

} // namespace kb::scene
