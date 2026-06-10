#pragma once

#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/InputComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/VisibilityComponent.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kb::scene {

struct ScenePrefabNodeComponents {
    std::optional<CameraComponent> camera;
    std::optional<MeshRendererComponent> meshRenderer;
    std::optional<LightComponent> light;
    std::optional<InputComponent> input;
};

struct ScenePrefabNodeDesc {
    static constexpr std::uint32_t NoParent = UINT32_MAX;

    std::string name;
    std::string nestedPrefabGuid;
    std::vector<ScenePrefabPropertyOverride> nestedPrefabOverrides;
    std::uint32_t parentNode = NoParent;
    TransformComponent transform{};
    VisibilityComponent visibility{};
    ScenePrefabNodeComponents components{};
};

} // namespace kb::scene
