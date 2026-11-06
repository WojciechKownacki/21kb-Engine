#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/StatelessSceneSystem.hpp"

#include <cstdint>
#include <unordered_map>

namespace kb::scene {

// Keeps the water of every River: a child object that draws the stream and one for its waterfalls, made from
// the river's points whenever they change. The children are transient render objects, never saved with the
// scene; a scene loaded again gets them on its first update.
class RiverSceneSystem final : public SceneSystem, public IStatelessSceneSystem {
public:
    void OnUpdate(SceneSystemContext& context) override;
    void OnDestroy(SceneSystemContext& context) override;

private:
    // The river data the children were last built from, so an unchanged river costs a comparison.
    std::unordered_map<SceneEntity::IdType, std::uint64_t> builtKeys_;
};

} // namespace kb::scene
