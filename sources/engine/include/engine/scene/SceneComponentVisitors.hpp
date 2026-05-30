#pragma once

#include "engine/scene/SceneVisitors.hpp"

namespace kb::scene {

class Scene;

class SceneComponentVisitors {
public:
    explicit SceneComponentVisitors(const Scene& scene) noexcept;

    void ForEachCamera(CameraVisitor visitor, void* context = nullptr) const;
    void ForEachMeshRenderer(MeshRendererVisitor visitor, void* context = nullptr) const;
    void ForEachVisibleMeshRenderer(MeshRendererVisitor visitor, void* context = nullptr) const;
    void ForEachLight(LightVisitor visitor, void* context = nullptr) const;

private:
    const Scene& scene_;
};

} // namespace kb::scene
