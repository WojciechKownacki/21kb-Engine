#pragma once

#include "engine/scene/SceneVisitors.hpp"

namespace kb::scene {

class Scene;

class SceneSystemQueryAccess {
public:
    explicit SceneSystemQueryAccess(Scene& scene) noexcept;

    void ForEachCamera(CameraVisitor visitor, void* context = nullptr) const;
    void ForEachMeshRenderer(MeshRendererVisitor visitor, void* context = nullptr) const;
    void ForEachVisibleMeshRenderer(MeshRendererVisitor visitor, void* context = nullptr) const;
    void ForEachLight(LightVisitor visitor, void* context = nullptr) const;
    void ForEachPhysicsBody(PhysicsBodyVisitor visitor, void* context = nullptr) const;

private:
    Scene& scene_;
};

} // namespace kb::scene
