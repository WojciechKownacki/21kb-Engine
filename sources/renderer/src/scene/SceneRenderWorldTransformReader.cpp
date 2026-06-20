#include "scene/SceneRenderWorldTransformReader.hpp"

#include "scene/EcsRenderTransformResolver.hpp"

namespace kb::render {

SceneRenderWorldTransformReader::SceneRenderWorldTransformReader(EcsRenderTransformResolver& fallbackResolver) noexcept
    : fallbackResolver_(fallbackResolver) {}

kb::scene::TransformComponent SceneRenderWorldTransformReader::Read(
    kb::scene::SceneEntity entity,
    const kb::scene::TransformComponent& computed) const {
    // Fast path: the batched transform hierarchy system already produced this
    // entity's world transform and cleared the dirty flag. Consume it directly -
    // no parent recursion, no per-frame cache, no recomputation.
    if (!computed.worldDirty) {
        ++precomputedReadCount_;
        return computed;
    }

    // Fallback: the world transform has not been computed yet (e.g. an immediate
    // edit before the system ran). Resolve it on demand to preserve correctness.
    ++resolvedFallbackCount_;
    return fallbackResolver_.Resolve(entity);
}

} // namespace kb::render
