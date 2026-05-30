#include "engine/scene/SceneSystemTransformAccess.hpp"

#include "scene/SceneEntityService.hpp"
#include "scene/SceneIterationService.hpp"
#include "scene/SceneTransformService.hpp"

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

SceneSystemTransformAccess::SceneSystemTransformAccess(Scene& scene) noexcept
    : scene_(scene) {}

bool SceneSystemTransformAccess::IsAlive(SceneEntity entity) const noexcept {
    return SceneEntityService::IsAlive(scene_, entity);
}

TransformComponent SceneSystemTransformAccess::Get(SceneEntity entity) const {
    return SceneTransformService::Get(scene_, entity);
}

const TransformComponent* SceneSystemTransformAccess::TryGet(SceneEntity entity) const noexcept {
    return SceneTransformService::TryGet(scene_, entity);
}

TransformComponent* SceneSystemTransformAccess::TryGet(SceneEntity entity) noexcept {
    return SceneTransformService::TryGet(scene_, entity);
}

void SceneSystemTransformAccess::Set(SceneEntity entity, const TransformComponent& transform) {
    SceneTransformService::Set(scene_, entity, transform);
}

void SceneSystemTransformAccess::MarkModified(SceneEntity entity) noexcept {
    SceneTransformService::MarkModified(scene_, entity);
}

void SceneSystemTransformAccess::ForEach(ConstTransformVisitor visitor, void* context) const {
    SceneIterationService::ForEachTransform(scene_, visitor, context);
}

void SceneSystemTransformAccess::ForEachMutable(MutableTransformVisitor visitor, void* context) {
    MutableTransformCallbackContext callbackContext{
        .access = this,
        .visitor = visitor,
        .userContext = context,
    };
    SceneIterationService::ForEachMutableTransform(scene_, &MutableTransformCallback, &callbackContext);
}

} // namespace kb::scene
