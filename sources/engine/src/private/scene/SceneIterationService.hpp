#pragma once

#include "engine/scene/SceneVisitorTypes.hpp"

namespace kb::scene {

class Scene;

class SceneIterationService {
public:
    SceneIterationService() = delete;

    static void ForEachTransform(const Scene& scene, ConstTransformVisitor visitor, void* context = nullptr);
    static void ForEachMutableTransform(Scene& scene, MutableTransformVisitor visitor, void* context = nullptr);
    static void ForEachBehaviour(const Scene& scene, BehaviourVisitor visitor, void* context = nullptr);
    static void ForEachCamera(const Scene& scene, CameraVisitor visitor, void* context = nullptr);
    static void ForEachMeshRenderer(const Scene& scene, MeshRendererVisitor visitor, void* context = nullptr);
    static void ForEachVisibleMeshRenderer(const Scene& scene, MeshRendererVisitor visitor, void* context = nullptr);
    static void ForEachLight(const Scene& scene, LightVisitor visitor, void* context = nullptr);
};

} // namespace kb::scene
