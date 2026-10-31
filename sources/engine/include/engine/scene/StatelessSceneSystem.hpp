#pragma once

namespace kb::scene {

// Explicit checkpoint contract for a system with no instance-owned mutable
// state. Scene-owned data it operates on is captured by the scene coordinator.
// Systems owning simulation state must implement IRuntimeStateProvider instead.
class IStatelessSceneSystem {
public:
    virtual ~IStatelessSceneSystem() = default;
};

} // namespace kb::scene
