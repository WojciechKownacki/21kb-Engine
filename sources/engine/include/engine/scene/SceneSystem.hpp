#pragma once

#include <cstdint>

namespace kb::scene {

class SceneSystemContext;

// The variable update is split around the authoritative fixed-step loop.
// Device/input and other producer systems use PreFixed (the default), while
// script Tick/LateTick rendering callbacks use PostFixed so they observe the
// physics result produced by the same SceneRuntime::Update call.
enum class SceneUpdatePhase : std::uint8_t {
    PreFixed,
    PostFixed,
};

// Stable, registration-order-independent fixed-step phases. Script FixedTick
// runs in PreSimulation; physics plugins simulate in Simulation. This gives a
// command batch flushed by FixedTick one unambiguous visibility boundary:
// before the matching physics step.
enum class SceneFixedUpdatePhase : std::uint8_t {
    PreSimulation,
    Simulation,
    PostSimulation,
};

class SceneSystem {
public:
    virtual ~SceneSystem() = default;

    virtual void OnCreate(SceneSystemContext& context);
    virtual void OnFrameStart(SceneSystemContext& context);
    virtual void OnUpdate(SceneSystemContext& context);
    virtual void OnFixedUpdate(SceneSystemContext& context);
    virtual void OnDestroy(SceneSystemContext& context);

    [[nodiscard]] virtual SceneUpdatePhase UpdatePhase() const noexcept {
        return SceneUpdatePhase::PreFixed;
    }

    [[nodiscard]] virtual SceneFixedUpdatePhase FixedUpdatePhase() const noexcept {
        return SceneFixedUpdatePhase::Simulation;
    }

    // Systems that do work in OnFixedUpdate MUST return true so the scene runtime
    // runs the fixed-step substep loop (and its interpolation sampling) for them.
    // Scenes without any such system skip that loop entirely - a no-physics scene
    // pays nothing for fixed-step machinery.
    [[nodiscard]] virtual bool RequiresFixedStep() const {
        return false;
    }
};

} // namespace kb::scene
