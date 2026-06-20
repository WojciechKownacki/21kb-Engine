#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cstddef>

namespace kb::render {

class EcsRenderTransformResolver;

// H3 - single source of world transform for the render bridge.
//
// The batched scene transform hierarchy system computes every entity's world
// transform once per frame and clears `worldDirty`. This reader consumes that
// already-computed world transform directly (zero recomputation) and only falls
// back to a full recursive resolve when an entity has not been computed yet
// (`worldDirty == true`, e.g. an editor-only immediate edit before the system
// ran). Render is a consumer of the computed column, never a second owner of the
// transform math.
//
// Single responsibility: decide precomputed-vs-resolve and report which path was
// taken. It owns no storage and recurses into nothing on the fast path.
class SceneRenderWorldTransformReader {
public:
    explicit SceneRenderWorldTransformReader(EcsRenderTransformResolver& fallbackResolver) noexcept;

    [[nodiscard]] kb::scene::TransformComponent Read(
        kb::scene::SceneEntity entity,
        const kb::scene::TransformComponent& computed) const;

    [[nodiscard]] std::size_t PrecomputedReadCount() const noexcept {
        return precomputedReadCount_;
    }

    [[nodiscard]] std::size_t ResolvedFallbackCount() const noexcept {
        return resolvedFallbackCount_;
    }

private:
    EcsRenderTransformResolver& fallbackResolver_;
    mutable std::size_t precomputedReadCount_ = 0;
    mutable std::size_t resolvedFallbackCount_ = 0;
};

} // namespace kb::render
