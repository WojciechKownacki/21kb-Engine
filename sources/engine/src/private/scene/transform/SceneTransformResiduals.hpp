#pragma once

#include "engine/math/DVec3.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace kb::scene {

// The parts of transform translations below float precision (docs/large_worlds.md). TransformComponent keeps
// float translations, so the stored transform row stays as small as it was; an entity whose translation needs
// more precision than a float has an entry here:
// - `local`: localTranslation - double(localPosition);
// - `world`: worldTranslation - double(worldPosition) of an entity with a parent, written by the transform sync
//   whenever it composes the entity. A root's world translation is its local translation, so a root reads
//   `local` for both.
// A residual counts only while it fits its float view (kb::math::FittingResidual): a float field written on its
// own since the residual was stored drops it once the value moved too far from it.
// Entities near the origin have no entry, and a scene without any entry pays one emptiness check per access.
struct SceneTransformResidual {
    std::uint64_t entityId = 0U;
    Vec3 local{};
    Vec3 world{};
};

class SceneTransformResiduals {
public:
    // Readable from several threads while others add entries for other entities.
    [[nodiscard]] bool Empty() const noexcept { return count_.load(std::memory_order_relaxed) == 0U; }
    [[nodiscard]] const SceneTransformResidual* Find(SceneEntity entity) const noexcept;
    // The entity's entry, created with zero residuals. Safe from several threads for different entities; an
    // entity with a dense index beyond the reserved range is kept in a locked side table until the next Reserve.
    [[nodiscard]] SceneTransformResidual& Acquire(SceneEntity entity);
    // Sizes the dense table for entities with dense indices below `denseCount`. Not concurrent with anything.
    void Reserve(std::size_t denseCount);
    void Clear() noexcept;

    [[nodiscard]] kb::math::DVec3 Local(SceneEntity entity, const TransformComponent& transform) const noexcept;
    // `hasParent` selects the composed world residual of a child or the local residual of a root.
    [[nodiscard]] kb::math::DVec3 World(SceneEntity entity, const TransformComponent& transform, bool hasParent) const noexcept;

private:
    [[nodiscard]] const SceneTransformResidual* FindSparse(std::uint64_t entityId) const noexcept;

    std::vector<std::uint64_t> bits_;
    std::vector<SceneTransformResidual> slots_;
    std::unordered_map<std::uint64_t, SceneTransformResidual> sparse_;
    mutable std::mutex sparseMutex_;
    std::atomic<std::size_t> sparseCount_{ 0U };
    std::atomic<std::size_t> count_{ 0U };
};

[[nodiscard]] constexpr kb::math::DVec3 JoinTranslation(Vec3 view, Vec3 residual) noexcept {
    return kb::math::DVec3{
        kb::math::JoinFloat(view.x, residual.x),
        kb::math::JoinFloat(view.y, residual.y),
        kb::math::JoinFloat(view.z, residual.z),
    };
}

// Splits a double-precision translation into its float view and the residual below it.
constexpr void SplitTranslation(const kb::math::DVec3& translation, Vec3& view, Vec3& residual) noexcept {
    const kb::math::SplitFloat x = kb::math::SplitDouble(translation.x);
    const kb::math::SplitFloat y = kb::math::SplitDouble(translation.y);
    const kb::math::SplitFloat z = kb::math::SplitDouble(translation.z);
    view = Vec3{ x.view, y.view, z.view };
    residual = Vec3{ x.residual, y.residual, z.residual };
}

[[nodiscard]] constexpr bool IsZero(Vec3 value) noexcept {
    return value.x == 0.0F && value.y == 0.0F && value.z == 0.0F;
}

using kb::math::FittingResidual;

} // namespace kb::scene
