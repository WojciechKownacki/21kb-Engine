#include "scene/systems/SceneSystemMutableTransformIteration.hpp"

#include "engine/scene/SceneSystemTransformAccess.hpp"
#include "scene/SceneIterationService.hpp"

namespace kb::scene {
namespace {

struct MutableTransformCallbackContext {
    SceneSystemTransformAccess* access = nullptr;
    MutableTransformVisitor visitor = nullptr;
    void* userContext = nullptr;
};

void MutableTransformCallback(SceneEntity entity, TransformComponent& transform, void* context) {
    auto* callbackContext = static_cast<MutableTransformCallbackContext*>(context);
    if (callbackContext == nullptr || callbackContext->visitor == nullptr || callbackContext->access == nullptr) {
        return;
    }

    callbackContext->visitor(entity, transform, callbackContext->userContext);
    callbackContext->access->MarkModified(entity);
}

} // namespace

void SceneSystemMutableTransformIteration::ForEach(Scene& scene, SceneSystemTransformAccess& access, MutableTransformVisitor visitor, void* context) {
    MutableTransformCallbackContext callbackContext{
        .access = &access,
        .visitor = visitor,
        .userContext = context,
    };
    SceneIterationService::ForEachMutableTransform(scene, &MutableTransformCallback, &callbackContext);
}

} // namespace kb::scene
