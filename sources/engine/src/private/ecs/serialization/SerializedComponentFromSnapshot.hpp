#pragma once

#include "engine/ecs/WorldSnapshot.hpp"
#include "engine/ecs/ComponentSerialization.hpp"

namespace kb::ecs {

class SerializedComponentFromSnapshot {
public:
    [[nodiscard]] static bool Serialize(const ComponentSnapshot& snapshot, const ComponentReflection& reflection, SerializedComponent& output);
};

} // namespace kb::ecs
