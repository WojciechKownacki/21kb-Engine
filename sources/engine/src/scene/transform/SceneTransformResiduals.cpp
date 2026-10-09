#include "scene/transform/SceneTransformResiduals.hpp"

#include "engine/ecs/Entity.hpp"

#include <algorithm>
#include <atomic>

namespace kb::scene {

const SceneTransformResidual* SceneTransformResiduals::Find(SceneEntity entity) const noexcept {
    if (Empty() || !entity.IsValid()) return nullptr;
    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < slots_.size()) {
        const std::uint64_t word = std::atomic_ref<const std::uint64_t>{ bits_[denseIndex / 64U] }.load(std::memory_order_acquire);
        if ((word >> (denseIndex % 64U) & 1U) != 0U && slots_[denseIndex].entityId == entity.Id()) {
            return &slots_[denseIndex];
        }
    }
    return sparseCount_.load(std::memory_order_acquire) == 0U ? nullptr : FindSparse(entity.Id());
}

const SceneTransformResidual* SceneTransformResiduals::FindSparse(std::uint64_t entityId) const noexcept {
    const std::lock_guard lock{ sparseMutex_ };
    const auto found = sparse_.find(entityId);
    return found == sparse_.end() ? nullptr : &found->second;
}

SceneTransformResidual& SceneTransformResiduals::Acquire(SceneEntity entity) {
    const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
    if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex && denseIndex < slots_.size()) {
        SceneTransformResidual& slot = slots_[denseIndex];
        std::atomic_ref<std::uint64_t> word{ bits_[denseIndex / 64U] };
        const std::uint64_t bit = std::uint64_t{ 1U } << (denseIndex % 64U);
        if ((word.load(std::memory_order_acquire) & bit) != 0U && slot.entityId == entity.Id()) return slot;
        // A slot left by a destroyed entity with the same dense index is reused.
        if ((word.load(std::memory_order_relaxed) & bit) == 0U) count_.fetch_add(1U, std::memory_order_relaxed);
        slot = SceneTransformResidual{ .entityId = entity.Id() };
        word.fetch_or(bit, std::memory_order_release);
        return slot;
    }
    const std::lock_guard lock{ sparseMutex_ };
    auto [found, inserted] = sparse_.try_emplace(entity.Id(), SceneTransformResidual{ .entityId = entity.Id() });
    if (inserted) {
        sparseCount_.fetch_add(1U, std::memory_order_release);
        count_.fetch_add(1U, std::memory_order_relaxed);
    }
    return found->second;
}

void SceneTransformResiduals::Reserve(std::size_t denseCount) {
    if (denseCount > slots_.size()) {
        const std::size_t size = std::max(denseCount, slots_.size() + slots_.size() / 2U);
        slots_.resize(size);
        bits_.resize((size + 63U) / 64U, 0U);
    }
    if (sparseCount_.load(std::memory_order_relaxed) == 0U) return;
    // Entries an earlier parallel pass parked in the side table move to their dense slots.
    for (auto entry = sparse_.begin(); entry != sparse_.end();) {
        const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(SceneEntity{ entry->first });
        if (denseIndex == kb::ecs::kInvalidGeneratedEntityIndex || denseIndex >= slots_.size()) {
            ++entry;
            continue;
        }
        const std::uint64_t bit = std::uint64_t{ 1U } << (denseIndex % 64U);
        if ((bits_[denseIndex / 64U] & bit) != 0U) count_.fetch_sub(1U, std::memory_order_relaxed);
        slots_[denseIndex] = entry->second;
        bits_[denseIndex / 64U] |= bit;
        entry = sparse_.erase(entry);
        sparseCount_.fetch_sub(1U, std::memory_order_relaxed);
    }
}

void SceneTransformResiduals::Clear() noexcept {
    std::fill(bits_.begin(), bits_.end(), 0U);
    sparse_.clear();
    sparseCount_.store(0U, std::memory_order_relaxed);
    count_.store(0U, std::memory_order_relaxed);
}

kb::math::DVec3 SceneTransformResiduals::Local(SceneEntity entity, const TransformComponent& transform) const noexcept {
    const SceneTransformResidual* residual = Find(entity);
    return JoinTranslation(transform.localPosition, residual == nullptr ? Vec3{} : residual->local);
}

kb::math::DVec3 SceneTransformResiduals::World(SceneEntity entity, const TransformComponent& transform, bool hasParent) const noexcept {
    const SceneTransformResidual* residual = Find(entity);
    if (residual == nullptr) return kb::math::ToDVec3(transform.worldPosition);
    return JoinTranslation(transform.worldPosition, hasParent ? residual->world : residual->local);
}

} // namespace kb::scene
