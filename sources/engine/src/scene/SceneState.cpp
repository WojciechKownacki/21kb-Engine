#include "scene/SceneState.hpp"

namespace kb::scene {

SceneState::SceneState()
    : components(*world.NativeHandle())
    , componentStorage(world.NativeHandle(), components) {}

SceneState::~SceneState() = default;

} // namespace kb::scene
