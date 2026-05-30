#include "engine/scene/SceneTransforms.hpp"

#include "scene/SceneIterationService.hpp"

namespace kb::scene {

void SceneTransformQueries::ForEach(ConstTransformVisitor visitor, void* context) const {
    SceneIterationService::ForEachTransform(scene_, visitor, context);
}

void SceneTransforms::ForEach(ConstTransformVisitor visitor, void* context) const {
    SceneIterationService::ForEachTransform(scene_, visitor, context);
}

void SceneTransforms::ForEachMutable(MutableTransformVisitor visitor, void* context) {
    SceneIterationService::ForEachMutableTransform(scene_, visitor, context);
}

} // namespace kb::scene
