#pragma once

#include "engine/ecs/ComponentReflection.hpp"
#include "engine/ecs/ComponentSerialization.hpp"
#include "engine/ecs/Entity.hpp"

#include <vector>

namespace kb::ecs {

class World;

class SerializedEntityComponentApplier {
public:
    [[nodiscard]] static bool Apply(World& world, Entity entity, const SerializedComponent& component);

private:
    static void CopyExistingComponent(const World& world, Entity entity, const ComponentReflection& reflection, std::vector<std::byte>& componentBuffer);
};

} // namespace kb::ecs
