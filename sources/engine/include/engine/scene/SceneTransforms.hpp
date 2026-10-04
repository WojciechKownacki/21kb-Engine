#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneVisitors.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace kb::scene {

class Scene;

// The rows of one storage chunk handed to a ParallelForEachRoot body, in storage order.
class TransformRowRange {
public:
    static constexpr std::size_t kMaxExtraComponents = 4U;

    [[nodiscard]] std::size_t Count() const noexcept { return count_; }
    [[nodiscard]] SceneEntity Entity(std::size_t row) const noexcept { return SceneEntity{ entityIds_[row] }; }
    [[nodiscard]] const TransformComponent& Get(std::size_t row) const noexcept { return rows_[row]; }
    // Writes the row's local transform with the result of Transforms().Set followed by the transform sync: a row
    // without parent or children gets its world transform here; any other row is left to the next sync.
    void SetLocal(std::size_t row, const Vec3& position, const Quat& rotation, const Vec3& scale);
    // The rows of the pass's index-th Extra component, which lives in the same chunk. Mutable access declares
    // every row written: versions, dirty flags and OnSet are published after workers finish, even if the body
    // throws. Use Column<const T> or ReadColumn<T> for reads, and WriteColumn<T> for a partial write.
    // A mutable TransformComponent extra is a further local-transform write intent: after the body, its
    // declared rows pass through the normal setter/compose path using their final local TRS. Derived world
    // fields and version metadata are managed by the engine. Duplicate extra aliases declare one intent.
    template <typename T>
    [[nodiscard]] T* Column(std::size_t extraIndex = 0U) const noexcept {
        if constexpr (!std::is_const_v<T>) DeclareColumnWritable(extraIndex);
        return extraIndex < kMaxExtraComponents ? static_cast<T*>(extraColumns_[extraIndex]) : nullptr;
    }
    template <typename T>
    [[nodiscard]] const T* ReadColumn(std::size_t extraIndex = 0U) const noexcept {
        return Column<const T>(extraIndex);
    }
    // Returns the first row of the declared write span. Overlapping/repeated declarations publish each row
    // once with its final value. A zero-length span declares no write; an invalid span throws before access.
    template <typename T>
    [[nodiscard]] T* WriteColumn(std::size_t firstRow, std::size_t count, std::size_t extraIndex = 0U) const {
        static_assert(!std::is_const_v<T>, "WriteColumn requires a mutable component type");
        DeclareColumnRowsWritable(extraIndex, firstRow, count);
        T* rows = static_cast<T*>(extraColumns_[extraIndex]);
        return rows == nullptr ? nullptr : rows + firstRow;
    }

private:
    friend class TransformPassAccess;

    void DeclareColumnWritable(std::size_t extraIndex) const noexcept;
    void DeclareColumnRowsWritable(std::size_t extraIndex, std::size_t firstRow, std::size_t count) const;

    const std::uint64_t* entityIds_ = nullptr;
    TransformComponent* rows_ = nullptr;
    std::size_t count_ = 0U;
    std::array<void*, kMaxExtraComponents> extraColumns_{};
    void* pass_ = nullptr;
    std::size_t archetypeIndex_ = 0U;
    std::size_t chunkIndex_ = 0U;
    bool composes_ = false;
};

struct TransformPassStats {
    std::size_t rowsVisited = 0U;
    // Explicit SetLocal writes by the body (mutable TransformComponent extras are not counted); the ones the
    // next sync composes (a parent or children, a prefab node, a
    // camera or light, an observed transform) are counted in rowsDeferred too.
    std::size_t rowsWritten = 0U;
    std::size_t rowsDeferred = 0U;
    std::size_t ranges = 0U;
};

class SceneTransformQueries {
public:
    explicit SceneTransformQueries(const Scene& scene) noexcept;

    [[nodiscard]] TransformComponent Get(SceneObject object) const;
    [[nodiscard]] TransformComponent Get(SceneEntity entity) const;
    [[nodiscard]] const TransformComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] bool ReadNonAlloc(std::span<const SceneEntity> entities, std::span<TransformComponent> transforms) const noexcept;
    void ForEach(ConstTransformVisitor visitor, void* context = nullptr) const;

private:
    const Scene& scene_;
};

class SceneTransforms {
public:
    explicit SceneTransforms(Scene& scene) noexcept;

    [[nodiscard]] TransformComponent Get(SceneObject object) const;
    [[nodiscard]] TransformComponent Get(SceneEntity entity) const;
    [[nodiscard]] const TransformComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] TransformComponent* TryGet(SceneEntity entity) noexcept;
    [[nodiscard]] bool ReadNonAlloc(std::span<const SceneEntity> entities, std::span<TransformComponent> transforms) const noexcept;
    void Set(SceneObject object, const TransformComponent& transform);
    void Set(SceneEntity entity, const TransformComponent& transform);
    // Batch write: transforms[i] goes to entities[i] (same length required, std::invalid_argument otherwise).
    // Cheaper per entity than a loop of Set; dead entities are skipped.
    void SetMany(std::span<const SceneEntity> entities, std::span<const TransformComponent> transforms);
    void MarkModified(SceneEntity entity) noexcept;
    // Bulk dirty signal: marks many transforms modified in one batched pass.
    void MarkModified(std::span<const SceneEntity> entities) noexcept;
    // Whether SceneRuntime::InterpolatedTransform blends the entity's poses of the last two fixed steps. On by default for
    // entities with a Rigidbody, CharacterController or Joint; any other entity reports its current transform unless
    // this turns interpolation on for it.
    void SetInterpolated(SceneEntity entity, bool interpolated);
    [[nodiscard]] bool IsInterpolated(SceneEntity entity) const noexcept;

    void ForEach(ConstTransformVisitor visitor, void* context = nullptr) const;
    void ForEachMutable(MutableTransformVisitor visitor, void* context = nullptr);

    // Runs body(range) on the scene's worker threads over every chunk of the rows that have a transform and every
    // component of `extraComponents`, and returns when all are done, with the same result as Set of the written
    // rows followed by the transform sync. Mutable extra-column access also publishes its declared rows,
    // independently of SetLocal. Typed passes use access declarations, so read-only columns signal no write.
    // The body may only read and write its own range (rows and Column<T>
    // values) and memory of the application; it must not call the scene's API or change structure (that throws).
    // The first exception a body throws is rethrown here after the written rows were published. Call it from the
    // application's thread, not from a scene system or inside ParallelFor.
    using TransformRangeBody = void (*)(TransformRowRange& range, void* context);
    // The original callback entry point conservatively declares all Extra rows mutable in every visited range.
    // This also publishes writes from prebuilt callbacks whose inline Column<T> predates access declarations.
    TransformPassStats ParallelForEachRoot(std::size_t grainRows, std::span<const kb::ecs::ComponentId> extraComponents, TransformRangeBody body, void* context);
    // Access-aware callback entry point: respects Column/ReadColumn/WriteColumn declarations from this header.
    // Its callback must be compiled with the declaring accessors; the typed overload calls this automatically.
    TransformPassStats ParallelForEachRootDeclared(std::size_t grainRows, std::span<const kb::ecs::ComponentId> extraComponents, TransformRangeBody body, void* context);
    template <typename... Extra, typename Body>
    TransformPassStats ParallelForEachRoot(std::size_t grainRows, Body&& body) {
        static_assert(sizeof...(Extra) <= TransformRowRange::kMaxExtraComponents, "Too many extra components for a transform pass");
        const std::array<kb::ecs::ComponentId, sizeof...(Extra)> ids{ ExtraComponentId<Extra>()... };
        using BodyType = std::remove_reference_t<Body>;
        return ParallelForEachRootDeclared(grainRows, ids, [](TransformRowRange& range, void* context) {
            (*static_cast<BodyType*>(context))(range);
        }, const_cast<std::remove_const_t<BodyType>*>(&body));
    }

private:
    template <typename T>
    [[nodiscard]] kb::ecs::ComponentId ExtraComponentId() const noexcept;
    [[nodiscard]] const void* EcsWorldHandle() const noexcept;

    Scene& scene_;
};

} // namespace kb::scene

#include "engine/ecs/World.hpp"

template <typename T>
kb::ecs::ComponentId kb::scene::SceneTransforms::ExtraComponentId() const noexcept {
    return static_cast<const kb::ecs::World*>(EcsWorldHandle())->Component<T>();
}
