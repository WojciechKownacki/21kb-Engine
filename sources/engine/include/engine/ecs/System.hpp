#pragma once

#include "engine/ecs/SystemAccess.hpp"

#include <string_view>

namespace kb::ecs {

class World;

class System {
public:
    virtual ~System() = default;

    [[nodiscard]] virtual std::string_view Name() const noexcept;
    [[nodiscard]] virtual SystemAccess DeclareAccess(World& world) const = 0;

    virtual void OnCreate(World& world);
    virtual void OnUpdate(World& world, float deltaSeconds);
    virtual void OnDestroy(World& world);
};

} // namespace kb::ecs
