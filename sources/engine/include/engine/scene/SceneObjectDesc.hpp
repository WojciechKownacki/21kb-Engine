#pragma once

#include "engine/scene/SceneObject.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/VisibilityComponent.hpp"

#include <string>

namespace kb::scene {

struct SceneObjectDesc {
    std::string name;
    SceneObject parent{};
    TransformComponent transform{};
    VisibilityComponent visibility{};
};

} // namespace kb::scene
