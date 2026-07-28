#pragma once

#include "engine/scene/SceneSystem.hpp"

namespace kb::scene {

class AnimatorSceneSystem final : public SceneSystem {
public:
    void OnUpdate(SceneSystemContext& context) override;
};

} // namespace kb::scene
