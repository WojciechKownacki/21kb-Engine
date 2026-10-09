#include "scene/transform/SceneTransformBranchUpdater.hpp"

#include "scene/SceneState.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"
#include "scene/transform/QuatMath.hpp"
#include "scene/transform/SceneTransformRootHotKernel.hpp"
#include "scene/transform/TransformMath.hpp"

#include <algorithm>
#include <cmath>

namespace kb::scene {
namespace {

// Below this distance from the origin a float translation is finer than a quarter of a millimetre, and the float
// composition is kept exactly as it always was.
constexpr float kFloatTranslationLimit = 2048.0F;

[[nodiscard]] float MaxAbs(Vec3 value) noexcept {
    return std::max({ std::fabs(value.x), std::fabs(value.y), std::fabs(value.z) });
}

// The double-precision world translation of a child the float composition just composed: the parent's precise
// world translation plus the child's precise local translation brought into the parent's frame, through the same
// path (translation only, scale, rotation) the float composition took. Children near the origin whose parent and
// local translation carry no residual keep the float result untouched.
void ComposeChildTranslation(SceneTransformBatchEntry& entry) {
    SceneTransformResiduals& residuals = entry.state->transformResiduals;
    TransformComponent& transform = *entry.transform;
    const SceneTransformResidual* own = residuals.Find(entry.entity);
    const SceneTransformResidual* parent = residuals.Find(entry.parentEntity);
    const Vec3 localResidual = own == nullptr ? Vec3{} : FittingResidual(transform.localPosition, own->local);
    Vec3 parentResidual{};
    if (parent != nullptr) {
        const bool parentHasParent = SceneHierarchyCache::Parent(*entry.state, entry.parentEntity).IsValid();
        parentResidual = FittingResidual(entry.parentTransform.worldPosition, parentHasParent ? parent->world : parent->local);
    }
    if (IsZero(localResidual) && IsZero(parentResidual) && MaxAbs(transform.worldPosition) < kFloatTranslationLimit) {
        if (own != nullptr && !IsZero(own->world)) residuals.Acquire(entry.entity).world = Vec3{};
        return;
    }
    const TransformComponent& parentTransform = entry.parentTransform;
    const kb::math::DVec3 local = JoinTranslation(transform.localPosition, localResidual);
    kb::math::DVec3 offset = local;
    if (!entry.translatedParentFastPath && !entry.unitScaleParentFastPath) {
        const Vec3 scale = entry.uniformScaleParentFastPath
            ? Vec3{ parentTransform.worldScale.x, parentTransform.worldScale.x, parentTransform.worldScale.x } : parentTransform.worldScale;
        offset = kb::math::DVec3{ offset.x * scale.x, offset.y * scale.y, offset.z * scale.z };
    }
    if (!entry.translatedParentFastPath && !entry.unrotatedParentFastPath) {
        offset = kb::math::RotateDouble(QuatMath::Normalize(parentTransform.worldRotation), offset);
    }
    Vec3 residual{};
    SplitTranslation(JoinTranslation(parentTransform.worldPosition, parentResidual) + offset, transform.worldPosition, residual);
    if (own != nullptr || !IsZero(residual)) residuals.Acquire(entry.entity).world = residual;
}

void UpdateEntries(std::span<SceneTransformBatchEntry> entries) noexcept {
    SceneTransformKernelBatch batch{ entries };
    SceneTransformHierarchyKernel{}(batch);
}

} // namespace

void UpdateSceneTransformBatchEntry(SceneTransformBatchEntry& entry) noexcept {
    if (entry.transform == nullptr) {
        return;
    }

    const bool shouldUpdate = entry.parentDirty || entry.transform->worldDirty || entry.transform->parentVersion != entry.parentWorldVersion;
    if (shouldUpdate) {
        if (entry.hasParent) {
            if (TransformMath::CanUseTranslatedParentFastPath(entry.parentTransform)) {
                *entry.transform = TransformMath::ComposeTranslatedParent(entry.parentTransform, *entry.transform);
                entry.translatedParentFastPath = true;
            } else if (TransformMath::CanUseUnrotatedParentFastPath(entry.parentTransform)) {
                *entry.transform = TransformMath::ComposeUnrotatedParent(entry.parentTransform, *entry.transform);
                entry.unrotatedParentFastPath = true;
            } else if (TransformMath::CanUseUnitScaleParentFastPath(entry.parentTransform)) {
                *entry.transform = TransformMath::ComposeUnitScaleParent(entry.parentTransform, *entry.transform);
                entry.unitScaleParentFastPath = true;
            } else if (TransformMath::CanUseUniformScaleParentFastPath(entry.parentTransform)) {
                *entry.transform = TransformMath::ComposeUniformScaleParent(entry.parentTransform, *entry.transform);
                entry.uniformScaleParentFastPath = true;
            } else if (TransformMath::CanUseStaticLocalRotationFastPath(*entry.transform)) {
                *entry.transform = TransformMath::ComposeStaticLocalRotationParent(entry.parentTransform, *entry.transform);
                entry.staticLocalRotationFastPath = true;
            } else {
                *entry.transform = TransformMath::Compose(entry.parentTransform, *entry.transform);
            }
            if (entry.state != nullptr) ComposeChildTranslation(entry);
        } else {
            if (SceneTransformRootHotKernel::CanApplyIdentityRotationFastPath(*entry.transform)) {
                SceneTransformRootHotKernel::ApplyIdentityRotationRoot(*entry.transform);
                entry.rootFastPath = true;
            } else {
                *entry.transform = TransformMath::ComposeRoot(*entry.transform);
            }
        }
    }
    entry.updated = shouldUpdate;
}

SceneTransformKernelBatch::SceneTransformKernelBatch(std::span<SceneTransformBatchEntry> entries) noexcept
    : entries_(entries) {}

std::size_t SceneTransformKernelBatch::Count() const noexcept {
    return entries_.size();
}

bool SceneTransformKernelBatch::Empty() const noexcept {
    return entries_.empty();
}

SceneTransformBatchEntry& SceneTransformKernelBatch::EntryAt(std::size_t index) const noexcept {
    return entries_[index];
}

std::span<SceneTransformBatchEntry> SceneTransformKernelBatch::Entries() const noexcept {
    return entries_;
}

void SceneTransformHierarchyKernel::operator()(SceneTransformKernelBatch& batch) const noexcept {
    for (SceneTransformBatchEntry& entry : batch.Entries()) {
        UpdateSceneTransformBatchEntry(entry);
    }
}

void SceneTransformBranchUpdater::UpdateBatch(kb::ecs::WorkerPool* workerPool, std::span<SceneTransformBatchEntry> entries, std::size_t grainSize) const {
    if (entries.empty()) {
        return;
    }
    if (workerPool == nullptr || !workerPool->Running() || entries.size() <= grainSize) {
        UpdateEntries(entries);
        return;
    }

    workerPool->ParallelForChunks(entries.size(), std::max<std::size_t>(1U, grainSize), [&entries](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
        UpdateEntries(entries.subspan(chunk.begin, chunk.count));
    });
}

} // namespace kb::scene
