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
};

} // namespace kb::scene
