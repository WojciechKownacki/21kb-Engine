#pragma once

#include "engine/scene/SceneTransforms.hpp"

#include "engine/ecs/World.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "scene/SceneState.hpp"
#include <mutex>
#include <bit>
#include <atomic>
#include "scene/transform/SceneTransformRootHotKernel.hpp"

namespace kb::scene {

class SceneComponentRegistry;

// LIB-089: this is the actual recompute pass behind
// kb::scene::SceneRuntime::SynchronizeTransforms() — see that method's own
// doc comment (engine/scene/SceneRuntime.hpp) for the full per-consumer
// (scripts/physics/renderer) timing contract for WHEN world* is fresh.
// This class itself is purely mechanical: given the current local* values
// and hierarchy topology in `state`, recompute every dirty entity's
// world* fields — it has no opinion on scheduling, that lives entirely in
// SceneRuntimeService::Update's call sites (SceneRuntime.cpp).
class SceneTransformHierarchySystem {
public:
    void Update(SceneState& state) const;
};

// Records a worker's run of composed rows: consecutive entities share a word, which is published once.
class UpdatedTransformBitWriter {
public:
    explicit UpdatedTransformBitWriter(SceneState& state, std::mutex& sparseMutex) noexcept : state_(state), sparseMutex_(sparseMutex) {}
    UpdatedTransformBitWriter(const UpdatedTransformBitWriter&) = delete;
    UpdatedTransformBitWriter& operator=(const UpdatedTransformBitWriter&) = delete;
    ~UpdatedTransformBitWriter() { Finish(); }

    // Publishes what was recorded so far.
    void Finish() noexcept {
        Flush();
        if (identityAffineCount_ != 0U) {
            std::atomic_ref<std::size_t>{ state_.lastTransformRenderProxyIdentityAffineFastPathCount }.fetch_add(identityAffineCount_, std::memory_order_relaxed);
            identityAffineCount_ = 0U;
        }
    }

    void Record(SceneEntity entity, const TransformComponent& transform) {
        const bool identityAffine = SceneTransformRootHotKernel::CanWriteIdentityAffineFastPath(transform);
        const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
        if (denseIndex == kb::ecs::kInvalidGeneratedEntityIndex || denseIndex / 64U >= state_.transformUpdatedBits.size()) {
            const std::lock_guard lock{ sparseMutex_ };
            state_.transformUpdatedSparseEntities.push_back(entity);
            identityAffineCount_ += identityAffine ? 1U : 0U;
            return;
        }
        if (denseIndex / 64U != word_) {
            Flush();
            word_ = denseIndex / 64U;
        }
        const std::uint64_t bit = std::uint64_t{ 1U } << (denseIndex % 64U);
        bits_ |= bit;
        identityAffineBits_ |= identityAffine ? bit : 0U;
    }

private:
    void Flush() noexcept {
        if (bits_ != 0U) {
            const std::uint64_t previous = std::atomic_ref<std::uint64_t>{ state_.transformUpdatedBits[word_] }.fetch_or(bits_, std::memory_order_relaxed);
            identityAffineCount_ += static_cast<std::size_t>(std::popcount(identityAffineBits_ & ~previous));
            bits_ = 0U;
            identityAffineBits_ = 0U;
        }
    }

    SceneState& state_;
    std::mutex& sparseMutex_;
    std::size_t word_ = 0U;
    std::uint64_t bits_ = 0U;
    std::uint64_t identityAffineBits_ = 0U;
    std::size_t identityAffineCount_ = 0U;
};

// Whether a written row may be composed where it is written: no parent or children, not a camera or light (the sync
// queues those for the renderer's proxies).
[[nodiscard]] bool SceneTransformComposesOnWrite(const SceneState& state, SceneEntity entity) noexcept;

// Composes a written row without parent or children as the sync's root lane does.
void ComposeSceneTransformRoot(TransformComponent& transform) noexcept;

// Records a composed row for the render-proxy updates, on the calling thread.
void RecordComposedSceneTransform(SceneState& state, SceneEntity entity, const TransformComponent& transform);

// Readies the update bits for rows composed outside the sync (BeginSceneTransformRenderProxyUpdates and sizing).
void PrepareSceneTransformUpdateRecording(SceneState& state);

// Forgets the transforms composed in the last frame, once: by the first transform pass after an Update or by the
// next Update.
void BeginSceneTransformRenderProxyUpdates(SceneState& state) noexcept;

// Transforms().ParallelForEachRoot.
TransformPassStats RunSceneTransformPass(SceneState& state, std::size_t grainRows, std::span<const kb::ecs::ComponentId> extraComponents,
    SceneTransforms::TransformRangeBody body, void* context);

// The sizes of the render-proxy lists, counted from this frame's update bits and the mesh, camera and light
// components without building the lists.
struct SceneTransformRenderProxyCounts {
    std::size_t updated = 0U;
    std::size_t meshRenderers = 0U;
    std::size_t visibleMeshRenderers = 0U;
    std::size_t cameras = 0U;
    std::size_t lights = 0U;
    std::size_t identityAffine = 0U;
};
[[nodiscard]] SceneTransformRenderProxyCounts CountSceneTransformRenderProxyUpdates(const SceneState& state);

// Builds the render-proxy lists (updated entities, their world affines, and the mesh, camera and light entries
// among them) from this frame's update bits, if a sync changed them since the last read.
void EnsureSceneTransformRenderProxyLists(const SceneState& state);

// Starts the scene's worker threads (shared by the transform passes and SceneRuntime::ParallelFor) if they are not running.
void EnsureSceneTransformWorkerPool(SceneState& state);

} // namespace kb::scene
