#pragma once

namespace kb::ecs {

class World;

class System {
public:
    virtual ~System() = default;

    virtual void OnCreate(World& world);
    virtual void OnUpdate(World& world, float deltaSeconds);
    virtual void OnDestroy(World& world);
};

} // namespace kb::ecs
