#pragma once

namespace kb::scene {

class SceneSystemContext;

class SceneSystem {
public:
    virtual ~SceneSystem() = default;

    virtual void OnCreate(SceneSystemContext& context);
    virtual void OnUpdate(SceneSystemContext& context);
    virtual void OnFixedUpdate(SceneSystemContext& context);
    virtual void OnDestroy(SceneSystemContext& context);

    // Systems that do work in OnFixedUpdate MUST return true so the scene runtime
    // runs the fixed-step substep loop (and its interpolation sampling) for them.
    // Scenes without any such system skip that loop entirely - a no-physics scene
    // pays nothing for fixed-step machinery.
    [[nodiscard]] virtual bool RequiresFixedStep() const {
        return false;
    }
};

} // namespace kb::scene
