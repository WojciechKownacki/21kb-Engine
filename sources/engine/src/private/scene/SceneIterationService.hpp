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
    static void ForEachUpdatedCameraRenderProxy(const Scene& scene, CameraRenderProxyVisitor visitor, void* context = nullptr);
    static void ForEachMeshRenderer(const Scene& scene, MeshRendererVisitor visitor, void* context = nullptr);
    static void ForEachVisibleMeshRenderer(const Scene& scene, MeshRendererVisitor visitor, void* context = nullptr);
    static void ForEachUpdatedMeshRendererRenderProxy(const Scene& scene, MeshRendererRenderProxyVisitor visitor, void* context = nullptr);
    static void ForEachVisibleUpdatedMeshRendererRenderProxy(const Scene& scene, MeshRendererRenderProxyVisitor visitor, void* context = nullptr);
    static void ForEachLight(const Scene& scene, LightVisitor visitor, void* context = nullptr);
    static void ForEachUpdatedLightRenderProxy(const Scene& scene, LightRenderProxyVisitor visitor, void* context = nullptr);
    static void ForEachPhysicsBody(const Scene& scene, PhysicsBodyVisitor visitor, void* context = nullptr);
};

} // namespace kb::scene
