#pragma once

#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/VisibilityComponent.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace kb::scene {

struct ScenePrefabNodeComponents {
    std::optional<CameraComponent> camera;
    std::optional<MeshRendererComponent> meshRenderer;
    std::optional<LightComponent> light;
};

struct ScenePrefabNodeDesc {
    static constexpr std::uint32_t NoParent = UINT32_MAX;

    std::string name;
    std::uint32_t parentNode = NoParent;
    TransformComponent transform{};
    VisibilityComponent visibility{};
    ScenePrefabNodeComponents components{};
};

} // namespace kb::scene
