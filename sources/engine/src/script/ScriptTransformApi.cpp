#include "engine/script/ScriptTransformApi.hpp"

#include "engine/math/EngineMath.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

const ScriptValue* FindArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) {
    for (const ScriptFunctionArgument& argument : arguments) {
        if (argument.name == name) {
            return &argument.value;
        }
    }
    return nullptr;
}

[[nodiscard]] kb::scene::SceneEntity EntityArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? kb::scene::SceneEntity{} : kb::scene::SceneEntity{ value->AsUInt64() };
}

[[nodiscard]] float FloatArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, float fallback = 0.0F) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? fallback : value->AsFloat(fallback);
}

[[nodiscard]] bool BoolArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, bool fallback = false) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? fallback : value->AsBool(fallback);
}

[[nodiscard]] int IntArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, int fallback = 0) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? fallback : value->AsInt(fallback);
}

[[nodiscard]] std::string StringArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, std::string fallback = {}) {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? std::move(fallback) : value->AsString();
}

[[nodiscard]] bool Alive(const ScriptFunctionCallContext& context, kb::scene::SceneEntity entity) noexcept {
    return context.scene != nullptr && entity.IsValid() && context.scene->Entities().IsAlive(entity);
}

ScriptFunctionCallResult PositionResult(bool found, kb::scene::Vec3 position) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "found", ScriptValue{ found } },
            ScriptFunctionArgument{ "x", ScriptValue{ position.x } },
            ScriptFunctionArgument{ "y", ScriptValue{ position.y } },
            ScriptFunctionArgument{ "z", ScriptValue{ position.z } },
        },
        .errors = {},
    };
}

ScriptFunctionCallResult BoolResult(std::string_view pin, bool value) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ std::string{ pin }, ScriptValue{ value } } },
        .errors = {},
    };
}

// LIB-085: Vec3/Quat decomposed into named scalar pins/outputs — the same
// convention kb::script::ScriptMathApi already established for Vec3/Quat
// (LIB-048/049's Vec3Pins/QuatPins) rather than a second convention
// invented here, since ScriptValue cannot carry a Vec3/Quat directly
// (LIB-032's "no references/aggregates across the script boundary" rule).
ScriptFunctionCallResult RotationResult(bool found, kb::scene::Quat rotation) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "found", ScriptValue{ found } },
            ScriptFunctionArgument{ "x", ScriptValue{ rotation.x } },
            ScriptFunctionArgument{ "y", ScriptValue{ rotation.y } },
            ScriptFunctionArgument{ "z", ScriptValue{ rotation.z } },
            ScriptFunctionArgument{ "w", ScriptValue{ rotation.w } },
        },
        .errors = {},
    };
}

ScriptFunctionCallResult PoseResult(bool found, kb::scene::Vec3 position, kb::scene::Quat rotation) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "found", ScriptValue{ found } },
            ScriptFunctionArgument{ "posX", ScriptValue{ position.x } },
            ScriptFunctionArgument{ "posY", ScriptValue{ position.y } },
            ScriptFunctionArgument{ "posZ", ScriptValue{ position.z } },
            ScriptFunctionArgument{ "rotX", ScriptValue{ rotation.x } },
            ScriptFunctionArgument{ "rotY", ScriptValue{ rotation.y } },
            ScriptFunctionArgument{ "rotZ", ScriptValue{ rotation.z } },
            ScriptFunctionArgument{ "rotW", ScriptValue{ rotation.w } },
        },
        .errors = {},
    };
}

// Same near-zero epsilon convention as kb::math::Normalize's zero-length
// guard (LIB-054). A parent scaled to (near) zero along an axis makes the
// local position along that axis genuinely underdetermined — this returns
// the un-divided value (as if that axis' parent scale were 1) rather than
// an Inf/NaN result, the same "defined, not fabricated-precise, not a
// crash" resolution LIB-054 already applies elsewhere in kb::math.
[[nodiscard]] float SafeDivide(float numerator, float denominator) noexcept {
    return std::fabs(denominator) <= 0.000001F ? numerator : numerator / denominator;
}

struct LocalPose {
    kb::math::Vec3 position{};
    kb::math::Quat rotation{};
};

// LIB-085/086: the world-to-local back-solve, extracted here so both
// Transform.SetWorldPose and Transform.SetParent's keepWorld branch share
// ONE implementation rather than two copies of the same inverse math.
// `parentTransform` must already be a FRESH read (the caller is
// responsible for calling SynchronizeTransforms() first) — this function
// itself never touches the scene, it is pure math.
[[nodiscard]] LocalPose WorldPoseToLocal(const kb::scene::TransformComponent& parentTransform, kb::math::Vec3 worldPosition, kb::math::Quat worldRotation) noexcept {
    const kb::math::Quat parentWorldRotationInverse = kb::math::Inverse(parentTransform.worldRotation);
    const kb::math::Vec3 worldDelta = worldPosition - parentTransform.worldPosition;
    const kb::math::Vec3 unrotatedDelta = kb::math::Rotate(parentWorldRotationInverse, worldDelta);
    return LocalPose{
        .position = kb::math::Vec3{
            SafeDivide(unrotatedDelta.x, parentTransform.worldScale.x),
            SafeDivide(unrotatedDelta.y, parentTransform.worldScale.y),
            SafeDivide(unrotatedDelta.z, parentTransform.worldScale.z),
        },
        .rotation = parentWorldRotationInverse * worldRotation,
    };
}

ScriptFunctionCallResult GetPosition(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!Alive(context, entity)) {
        return PositionResult(false, {});
    }
    const kb::scene::TransformComponent transform = context.scene->Transforms().Get(entity);
    return PositionResult(true, transform.localPosition);
}

ScriptFunctionCallResult SetPosition(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!Alive(context, entity)) {
        return BoolResult("moved", false);
    }
    kb::scene::TransformComponent transform = context.scene->Transforms().Get(entity);
    transform.localPosition = kb::scene::Vec3{
        FloatArg(arguments, "x", transform.localPosition.x),
        FloatArg(arguments, "y", transform.localPosition.y),
        FloatArg(arguments, "z", transform.localPosition.z),
    };
    context.scene->Transforms().Set(entity, transform);
    return BoolResult("moved", true);
}

ScriptFunctionCallResult Translate(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!Alive(context, entity)) {
        return BoolResult("moved", false);
    }
    kb::scene::TransformComponent transform = context.scene->Transforms().Get(entity);
    transform.localPosition.x += FloatArg(arguments, "x");
    transform.localPosition.y += FloatArg(arguments, "y");
    transform.localPosition.z += FloatArg(arguments, "z");
    context.scene->Transforms().Set(entity, transform);
    return BoolResult("moved", true);
}

ScriptFunctionCallResult GetLocalRotation(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!Alive(context, entity)) {
        return RotationResult(false, {});
    }
    const kb::scene::TransformComponent transform = context.scene->Transforms().Get(entity);
    return RotationResult(true, transform.localRotation);
}

ScriptFunctionCallResult SetLocalRotation(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!Alive(context, entity)) {
        return BoolResult("moved", false);
    }
    kb::scene::TransformComponent transform = context.scene->Transforms().Get(entity);
    transform.localRotation = kb::scene::Quat{
        FloatArg(arguments, "x", transform.localRotation.x),
        FloatArg(arguments, "y", transform.localRotation.y),
        FloatArg(arguments, "z", transform.localRotation.z),
        FloatArg(arguments, "w", transform.localRotation.w),
    };
    context.scene->Transforms().Set(entity, transform);
    return BoolResult("moved", true);
}

ScriptFunctionCallResult GetLocalScale(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!Alive(context, entity)) {
        return PositionResult(false, {});
    }
    const kb::scene::TransformComponent transform = context.scene->Transforms().Get(entity);
    return PositionResult(true, transform.localScale);
}

ScriptFunctionCallResult SetLocalScale(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!Alive(context, entity)) {
        return BoolResult("moved", false);
    }
    kb::scene::TransformComponent transform = context.scene->Transforms().Get(entity);
    transform.localScale = kb::scene::Vec3{
        FloatArg(arguments, "x", transform.localScale.x),
        FloatArg(arguments, "y", transform.localScale.y),
        FloatArg(arguments, "z", transform.localScale.z),
    };
    context.scene->Transforms().Set(entity, transform);
    return BoolResult("moved", true);
}

// Read-only: reports whatever this entity's TransformComponent currently
// has cached as worldPosition/worldRotation (SceneTransformHierarchySystem,
// dirty-flag driven, LIB-066's "leniwie zsynchronizowany" world transform)
// — same honesty convention as ScriptSceneComponentApi's own read-only
// worldPosition.x/y/z properties (LIB-077): this does not force a
// SynchronizeTransforms() pass before reading, so a value set earlier this
// same frame (local or world) is only guaranteed visible here after the
// scheduler's own sync point, UNLESS it was written via
// Transform.SetWorldPose below, which does force a sync itself.
ScriptFunctionCallResult GetWorldPose(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!Alive(context, entity)) {
        return PoseResult(false, {}, {});
    }
    const kb::scene::TransformComponent transform = context.scene->Transforms().Get(entity);
    return PoseResult(true, transform.worldPosition, transform.worldRotation);
}

// LIB-085: the genuinely new capability this task adds — every OTHER
// Transform.* setter here only ever touches localPosition/localRotation/
// localScale directly (kb::scene already stores and composes those without
// any inversion). Setting a WORLD-space pose instead requires back-solving
// the equivalent local pose relative to the parent's CURRENT world
// transform — the exact inverse of kb::scene::TransformMath::Compose's
// general path (TransformMath.cpp): worldPosition = parentWorldPosition +
// Rotate(parentWorldRotation, parentWorldScale (x) localPosition),
// worldRotation = parentWorldRotation * localRotation. No such inverse
// existed anywhere in the engine before this (kb::scene::TransformMath only
// had forward Compose*, and kb::math had no quaternion Inverse — both
// confirmed absent by research before writing this).
//
// For a ROOT entity (no parent), TransformMath::ComposeRoot copies local
// straight to world unchanged, so local IS the requested world pose
// directly — no inversion needed.
//
// SynchronizeTransforms() is called BEFORE reading the parent's world
// transform (so a parent moved earlier this same frame is accounted for,
// not stale) AND AFTER writing this entity's new local pose (LIB-066's
// established flush pattern), so a Transform.WorldPose() read immediately
// after this call reports the pose that was actually requested, not a
// value pending the next scheduler sync point.
ScriptFunctionCallResult SetWorldPose(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!Alive(context, entity)) {
        return BoolResult("moved", false);
    }

    kb::scene::TransformComponent transform = context.scene->Transforms().Get(entity);
    const kb::math::Vec3 desiredWorldPosition{
        FloatArg(arguments, "posX", transform.worldPosition.x),
        FloatArg(arguments, "posY", transform.worldPosition.y),
        FloatArg(arguments, "posZ", transform.worldPosition.z),
    };
    const kb::math::Quat desiredWorldRotation = kb::math::Normalize(kb::math::Quat{
        FloatArg(arguments, "rotX", transform.worldRotation.x),
        FloatArg(arguments, "rotY", transform.worldRotation.y),
        FloatArg(arguments, "rotZ", transform.worldRotation.z),
        FloatArg(arguments, "rotW", transform.worldRotation.w),
    });

    const kb::scene::SceneEntity parent = context.scene->Hierarchy().Parent(entity);
    if (!parent.IsValid()) {
        transform.localPosition = desiredWorldPosition;
        transform.localRotation = desiredWorldRotation;
    } else {
        context.scene->Runtime().SynchronizeTransforms();
        const kb::scene::TransformComponent parentTransform = context.scene->Transforms().Get(parent);
        const LocalPose localPose = WorldPoseToLocal(parentTransform, desiredWorldPosition, desiredWorldRotation);
        transform.localPosition = localPose.position;
        transform.localRotation = localPose.rotation;
    }
    context.scene->Transforms().Set(entity, transform);
    context.scene->Runtime().SynchronizeTransforms();
    return BoolResult("moved", true);
}

// LIB-086: read-only — the entity's current parent (kb::scene::SceneEntity{}
// / invalid if it is a root), mirroring the "found" = alive convention every
// other Get*/read function in this module already uses. A root entity is
// NOT an error (found=true, parent=invalid) — only a dead/unknown entity
// reports found=false.
ScriptFunctionCallResult GetParent(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!Alive(context, entity)) {
        return ScriptFunctionCallResult{
            .executed = true,
            .outputs = {
                ScriptFunctionArgument{ "found", ScriptValue{ false } },
                ScriptFunctionArgument{ "parent", ScriptValue{ kb::scene::SceneEntity{}.Id(), ScriptValueType::Entity } },
            },
            .errors = {},
        };
    }
    const kb::scene::SceneEntity parent = context.scene->Hierarchy().Parent(entity);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "found", ScriptValue{ true } },
            ScriptFunctionArgument{ "parent", ScriptValue{ parent.Id(), ScriptValueType::Entity } },
        },
        .errors = {},
    };
}

// LIB-086: reparents `entity` under `parent` (invalid/omitted `parent` ==
// un-parent to root). Cycle detection is NOT reimplemented here — it
// already lives at the kb::scene level
// (SceneHierarchyParenting::WouldCreateCycle, reached through
// Hierarchy().SetParent, the exact same path World.SetParent already
// uses) and simply surfaces as this function returning moved=false, same
// as any other rejected reparent (dead entity, entity==parent, no-op
// already-this-parent).
//
// `keepWorld` is the genuinely new behavior LIB-086 adds: WITHOUT it,
// SceneHierarchyParenting::SetParent only reassigns the parent
// relationship — it never touches localPosition/localRotation/localScale
// — so the entity's WORLD pose changes to (new parent's world) composed
// with the UNCHANGED local pose, a visual "jump". WITH keepWorld=true,
// this captures the entity's world pose BEFORE reparenting (forcing a
// sync first, so it reflects any pending changes from earlier this frame),
// then AFTER reparenting succeeds, back-solves a new local pose against
// the NEW parent's (freshly synced) world transform — reusing the exact
// same WorldPoseToLocal helper Transform.SetWorldPose uses — so the
// entity's world pose is unchanged across the reparent even though its
// local pose is not.
ScriptFunctionCallResult SetParent(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!Alive(context, entity)) {
        return BoolResult("moved", false);
    }
    const kb::scene::SceneEntity newParent = EntityArg(arguments, "parent");
    const bool keepWorld = BoolArg(arguments, "keepWorld", false);

    kb::math::Vec3 worldPositionBeforeReparent{};
    kb::math::Quat worldRotationBeforeReparent{};
    kb::math::Vec3 worldScaleBeforeReparent{ 1.0F, 1.0F, 1.0F };
    if (keepWorld) {
        context.scene->Runtime().SynchronizeTransforms();
        const kb::scene::TransformComponent beforeTransform = context.scene->Transforms().Get(entity);
        worldPositionBeforeReparent = beforeTransform.worldPosition;
        worldRotationBeforeReparent = beforeTransform.worldRotation;
        worldScaleBeforeReparent = beforeTransform.worldScale;
    }

    if (!context.scene->Hierarchy().SetParent(entity, newParent)) {
        return BoolResult("moved", false);
    }

    if (keepWorld) {
        kb::scene::TransformComponent transform = context.scene->Transforms().Get(entity);
        if (newParent.IsValid()) {
            context.scene->Runtime().SynchronizeTransforms();
            const kb::scene::TransformComponent parentTransform = context.scene->Transforms().Get(newParent);
            const LocalPose localPose = WorldPoseToLocal(parentTransform, worldPositionBeforeReparent, worldRotationBeforeReparent);
            transform.localPosition = localPose.position;
            transform.localRotation = localPose.rotation;
            // LIB-086: keepWorld must preserve the FULL world transform, not
            // just position+rotation. kb::scene composes world scale
            // component-wise (worldScale = parentWorldScale * localScale — the
            // same model WorldPoseToLocal's SafeDivide-by-worldScale relies
            // on), so the localScale that keeps the pre-reparent worldScale
            // under a differently-scaled parent is worldScaleBefore /
            // parentWorldScale, component-wise. Without this, reparenting onto
            // a scaled parent silently rescales the entity.
            transform.localScale = kb::scene::Vec3{
                SafeDivide(worldScaleBeforeReparent.x, parentTransform.worldScale.x),
                SafeDivide(worldScaleBeforeReparent.y, parentTransform.worldScale.y),
                SafeDivide(worldScaleBeforeReparent.z, parentTransform.worldScale.z),
            };
        } else {
            // New parent is root (none) — local IS world directly, same as
            // Transform.SetWorldPose's own root case.
            transform.localPosition = worldPositionBeforeReparent;
            transform.localRotation = worldRotationBeforeReparent;
            transform.localScale = worldScaleBeforeReparent;
        }
        context.scene->Transforms().Set(entity, transform);
        context.scene->Runtime().SynchronizeTransforms();
    }
    return BoolResult("moved", true);
}

ScriptFunctionCallResult ChildResult(bool found, kb::scene::SceneEntity child) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "found", ScriptValue{ found } },
            ScriptFunctionArgument{ "child", ScriptValue{ child.Id(), ScriptValueType::Entity } },
        },
        .errors = {},
    };
}

// LIB-087: reads a genuinely O(1) count/indexed-lookup out of kb::scene's
// hierarchy child storage (SceneHierarchyCache::ChildCount/ChildAt,
// exposed here through SceneHierarchyAccess) — NOT the allocating
// ChildEntities() (which materializes a fresh std::vector of every child
// on every call). No collection crosses the script boundary here (LIB-032)
// — the caller loops index=0..count-1, the same index-and-loop shape
// LIB-069's World.FindAllByTag already established for its own
// script-boundary iteration — but unlike that function's O(n) full-scene
// rescan PER CALL (tags aren't indexed by anything), each call here is a
// real O(1) lookup, since the underlying storage is already indexed by
// parent and incrementally maintained on every reparent (confirmed by
// research before writing this: SceneHierarchyCache's dense/sparse child
// lists are updated in Add/Move/Remove, never rebuilt from a scan).
ScriptFunctionCallResult GetChildCount(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!Alive(context, entity)) {
        return ScriptFunctionCallResult{
            .executed = true,
            .outputs = {
                ScriptFunctionArgument{ "found", ScriptValue{ false } },
                ScriptFunctionArgument{ "count", ScriptValue{ 0 } },
            },
            .errors = {},
        };
    }
    const std::size_t count = context.scene->Hierarchy().ChildCount(entity);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "found", ScriptValue{ true } },
            ScriptFunctionArgument{ "count", ScriptValue{ static_cast<int>(count) } },
        },
        .errors = {},
    };
}

ScriptFunctionCallResult GetChild(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!Alive(context, entity)) {
        return ChildResult(false, {});
    }
    const int index = IntArg(arguments, "index", 0);
    if (index < 0) {
        return ChildResult(false, {});
    }
    const kb::scene::SceneEntity child = context.scene->Hierarchy().ChildAt(entity, static_cast<std::size_t>(index));
    return ChildResult(child.IsValid(), child);
}

// FindChild: a linear scan over `entity`'s OWN child list (already
// O(1)-indexed, see GetChildCount/GetChild above) comparing each child's
// Name() — cost is O(children of entity), NOT O(all entities in the
// scene) like LIB-069's World.FindAllByTag (which rescans the whole world
// every call because tags aren't indexed). `skip` mirrors FindAllByTag's
// own convention for walking past duplicate names (clamped to >= 0, same
// as FindAllByTag's clamp).
ScriptFunctionCallResult FindChild(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!Alive(context, entity)) {
        return ChildResult(false, {});
    }
    const std::string name = StringArg(arguments, "name");
    const int skip = IntArg(arguments, "skip", 0);
    const int clampedSkip = skip < 0 ? 0 : skip;
    int matched = 0;
    const std::size_t count = context.scene->Hierarchy().ChildCount(entity);
    for (std::size_t index = 0; index < count; ++index) {
        const kb::scene::SceneEntity child = context.scene->Hierarchy().ChildAt(entity, index);
        if (!child.IsValid() || context.scene->Entities().Name(child) != name) {
            continue;
        }
        if (matched == clampedSkip) {
            return ChildResult(true, child);
        }
        ++matched;
    }
    return ChildResult(false, {});
}

// LIB-088: Transform.Translate ALREADY existed (above) and is unchanged —
// its reappearance in the LIB-088 task list is documentation grouping
// with these four genuinely new functions, not a real gap: Translate
// already adds a delta directly to localPosition, exactly the local-space
// semantics this task's remaining scope needs no different.

// Rotate: composes a DELTA rotation onto the entity's existing
// localRotation, in LOCAL space — `local * delta` (not `delta * local`)
// matches this file's/EngineMath.hpp's own documented Hamilton product
// convention ("lhs*rhs applies rhs first, then lhs" — the same
// parent-composes-child order Compose* already uses for world = parent *
// local), so the delta rotates the entity in its OWN current local frame,
// not the world's.
ScriptFunctionCallResult Rotate(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!Alive(context, entity)) {
        return BoolResult("moved", false);
    }
    kb::scene::TransformComponent transform = context.scene->Transforms().Get(entity);
    const kb::math::Quat delta = kb::math::Normalize(kb::math::Quat{
        FloatArg(arguments, "x", 0.0F),
        FloatArg(arguments, "y", 0.0F),
        FloatArg(arguments, "z", 0.0F),
        FloatArg(arguments, "w", 1.0F),
    });
    transform.localRotation = kb::math::Normalize(transform.localRotation * delta);
    context.scene->Transforms().Set(entity, transform);
    return BoolResult("moved", true);
}

// LookAt: a look target is naturally specified in WORLD space, so this
// computes a WORLD rotation via the existing kb::math::LookRotation
// (LIB-049) from the entity's CURRENT world position toward the target,
// then applies the exact same root-vs-parent branching
// Transform.SetWorldPose already established — reusing WorldPoseToLocal
// wholesale with the entity's OWN (freshly synced) world position as the
// position argument, so position round-trips unchanged and only rotation
// is genuinely back-solved. Both the entity's own world position and (if
// present) the parent's world transform are read AFTER a
// SynchronizeTransforms() call, so a pending change from earlier this same
// frame is accounted for rather than solving against a stale pose.
ScriptFunctionCallResult LookAt(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!Alive(context, entity)) {
        return BoolResult("moved", false);
    }
    // LIB-088: force a sync BEFORE reading worldPosition — for EVERY entity,
    // not just parented ones. LookAt computes the look direction from the
    // entity's own world position, and a SetPosition earlier this same frame
    // only writes localPosition (worldPosition is recomposed lazily). The old
    // code synced only when the entity had a parent, so a ROOT entity used a
    // stale worldPosition and aimed from its previous location. One upfront
    // sync makes the read fresh for root and parented entities alike.
    context.scene->Runtime().SynchronizeTransforms();
    kb::scene::TransformComponent transform = context.scene->Transforms().Get(entity);
    const kb::math::Vec3 target{
        FloatArg(arguments, "targetX", transform.worldPosition.x),
        FloatArg(arguments, "targetY", transform.worldPosition.y),
        FloatArg(arguments, "targetZ", transform.worldPosition.z),
    };
    const kb::math::Vec3 up{
        FloatArg(arguments, "upX", 0.0F),
        FloatArg(arguments, "upY", 1.0F),
        FloatArg(arguments, "upZ", 0.0F),
    };

    const kb::scene::SceneEntity parent = context.scene->Hierarchy().Parent(entity);
    const kb::math::Vec3 forward = kb::math::Normalize(target - transform.worldPosition);
    const kb::math::Quat desiredWorldRotation = kb::math::LookRotation(forward, up);

    if (!parent.IsValid()) {
        transform.localRotation = desiredWorldRotation;
    } else {
        const kb::scene::TransformComponent parentTransform = context.scene->Transforms().Get(parent);
        const LocalPose localPose = WorldPoseToLocal(parentTransform, transform.worldPosition, desiredWorldRotation);
        transform.localRotation = localPose.rotation;
    }
    context.scene->Transforms().Set(entity, transform);
    context.scene->Runtime().SynchronizeTransforms();
    return BoolResult("moved", true);
}

// TransformPoint: converts a point expressed in this entity's OWN local
// space into world space — the point-only forward half of
// kb::scene::TransformMath::Compose's formula (TransformMath.cpp),
// applied directly against the entity's own cached world transform
// (no parent lookup needed, unlike SetWorldPose/LookAt, since a point is
// relative to THIS entity, not to a parent). Same "does not force a
// SynchronizeTransforms() pass" honesty convention as the read-only
// Transform.WorldPose — may be stale until the next sync point.
ScriptFunctionCallResult TransformPoint(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!Alive(context, entity)) {
        return PositionResult(false, {});
    }
    const kb::scene::TransformComponent transform = context.scene->Transforms().Get(entity);
    const kb::math::Vec3 localPoint{
        FloatArg(arguments, "x", 0.0F),
        FloatArg(arguments, "y", 0.0F),
        FloatArg(arguments, "z", 0.0F),
    };
    const kb::math::Vec3 scaledLocalPoint{
        localPoint.x * transform.worldScale.x,
        localPoint.y * transform.worldScale.y,
        localPoint.z * transform.worldScale.z,
    };
    const kb::math::Vec3 worldPoint = transform.worldPosition + kb::math::Rotate(transform.worldRotation, scaledLocalPoint);
    return PositionResult(true, worldPoint);
}

// InverseTransformPoint: the inverse of TransformPoint above — reuses
// WorldPoseToLocal literally, treating the ENTITY ITSELF as its own
// reference frame (the same math SetWorldPose/SetParent's keepWorld
// branch already use for a PARENT, just substituting this entity's own
// world transform in that role). The returned LocalPose's rotation half
// is discarded: this converts a POINT, not a pose — a point has no
// orientation of its own to back-solve — the identity quaternion passed
// in is only there because WorldPoseToLocal's signature requires one.
ScriptFunctionCallResult InverseTransformPoint(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!Alive(context, entity)) {
        return PositionResult(false, {});
    }
    const kb::scene::TransformComponent transform = context.scene->Transforms().Get(entity);
    const kb::math::Vec3 worldPoint{
        FloatArg(arguments, "x", 0.0F),
        FloatArg(arguments, "y", 0.0F),
        FloatArg(arguments, "z", 0.0F),
    };
    const LocalPose localPose = WorldPoseToLocal(transform, worldPoint, kb::math::Quat{});
    return PositionResult(true, localPose.position);
}

bool RegisterFunction(
    ScriptRuntimeHost& host,
    std::string name,
    std::vector<ScriptFunctionPin> inputs,
    std::vector<ScriptFunctionPin> outputs,
    ScriptFunctionCallback callback) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = std::move(inputs);
    desc.signature.outputs = std::move(outputs);
    desc.callback = std::move(callback);
    return host.RegisterFunction(std::move(desc));
}

} // namespace

bool ScriptTransformApi::Register(ScriptRuntimeHost& host) {
    bool ok = true;
    ok = RegisterFunction(host, "Transform.GetPosition",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } },
        {
            ScriptFunctionPin{ "found", ScriptValueType::Bool, true },
            ScriptFunctionPin{ "x", ScriptValueType::Float, true },
            ScriptFunctionPin{ "y", ScriptValueType::Float, true },
            ScriptFunctionPin{ "z", ScriptValueType::Float, true },
        },
        &GetPosition) && ok;
    ok = RegisterFunction(host, "Transform.SetPosition",
        {
            ScriptFunctionPin{ "entity", ScriptValueType::Entity, true },
            ScriptFunctionPin{ "x", ScriptValueType::Float, true },
            ScriptFunctionPin{ "y", ScriptValueType::Float, true },
            ScriptFunctionPin{ "z", ScriptValueType::Float, true },
        },
        { ScriptFunctionPin{ "moved", ScriptValueType::Bool, true } },
        &SetPosition) && ok;
    ok = RegisterFunction(host, "Transform.Translate",
        {
            ScriptFunctionPin{ "entity", ScriptValueType::Entity, true },
            ScriptFunctionPin{ "x", ScriptValueType::Float, true },
            ScriptFunctionPin{ "y", ScriptValueType::Float, true },
            ScriptFunctionPin{ "z", ScriptValueType::Float, true },
        },
        { ScriptFunctionPin{ "moved", ScriptValueType::Bool, true } },
        &Translate) && ok;
    // LIB-085: LocalPosition/SetLocalPosition are the exact same operation
    // as the pre-existing GetPosition/SetPosition above (both already only
    // ever touched localPosition) — reused directly, not reimplemented
    // under the new name, so there is exactly one place that logic lives.
    ok = RegisterFunction(host, "Transform.LocalPosition",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } },
        {
            ScriptFunctionPin{ "found", ScriptValueType::Bool, true },
            ScriptFunctionPin{ "x", ScriptValueType::Float, true },
            ScriptFunctionPin{ "y", ScriptValueType::Float, true },
            ScriptFunctionPin{ "z", ScriptValueType::Float, true },
        },
        &GetPosition) && ok;
    ok = RegisterFunction(host, "Transform.SetLocalPosition",
        {
            ScriptFunctionPin{ "entity", ScriptValueType::Entity, true },
            ScriptFunctionPin{ "x", ScriptValueType::Float, true },
            ScriptFunctionPin{ "y", ScriptValueType::Float, true },
            ScriptFunctionPin{ "z", ScriptValueType::Float, true },
        },
        { ScriptFunctionPin{ "moved", ScriptValueType::Bool, true } },
        &SetPosition) && ok;
    ok = RegisterFunction(host, "Transform.LocalRotation",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } },
        {
            ScriptFunctionPin{ "found", ScriptValueType::Bool, true },
            ScriptFunctionPin{ "x", ScriptValueType::Float, true },
            ScriptFunctionPin{ "y", ScriptValueType::Float, true },
            ScriptFunctionPin{ "z", ScriptValueType::Float, true },
            ScriptFunctionPin{ "w", ScriptValueType::Float, true },
        },
        &GetLocalRotation) && ok;
    ok = RegisterFunction(host, "Transform.SetLocalRotation",
        {
            ScriptFunctionPin{ "entity", ScriptValueType::Entity, true },
            ScriptFunctionPin{ "x", ScriptValueType::Float, true },
            ScriptFunctionPin{ "y", ScriptValueType::Float, true },
            ScriptFunctionPin{ "z", ScriptValueType::Float, true },
            ScriptFunctionPin{ "w", ScriptValueType::Float, true },
        },
        { ScriptFunctionPin{ "moved", ScriptValueType::Bool, true } },
        &SetLocalRotation) && ok;
    ok = RegisterFunction(host, "Transform.LocalScale",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } },
        {
            ScriptFunctionPin{ "found", ScriptValueType::Bool, true },
            ScriptFunctionPin{ "x", ScriptValueType::Float, true },
            ScriptFunctionPin{ "y", ScriptValueType::Float, true },
            ScriptFunctionPin{ "z", ScriptValueType::Float, true },
        },
        &GetLocalScale) && ok;
    ok = RegisterFunction(host, "Transform.SetLocalScale",
        {
            ScriptFunctionPin{ "entity", ScriptValueType::Entity, true },
            ScriptFunctionPin{ "x", ScriptValueType::Float, true },
            ScriptFunctionPin{ "y", ScriptValueType::Float, true },
            ScriptFunctionPin{ "z", ScriptValueType::Float, true },
        },
        { ScriptFunctionPin{ "moved", ScriptValueType::Bool, true } },
        &SetLocalScale) && ok;
    ok = RegisterFunction(host, "Transform.WorldPose",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } },
        {
            ScriptFunctionPin{ "found", ScriptValueType::Bool, true },
            ScriptFunctionPin{ "posX", ScriptValueType::Float, true },
            ScriptFunctionPin{ "posY", ScriptValueType::Float, true },
            ScriptFunctionPin{ "posZ", ScriptValueType::Float, true },
            ScriptFunctionPin{ "rotX", ScriptValueType::Float, true },
            ScriptFunctionPin{ "rotY", ScriptValueType::Float, true },
            ScriptFunctionPin{ "rotZ", ScriptValueType::Float, true },
            ScriptFunctionPin{ "rotW", ScriptValueType::Float, true },
        },
        &GetWorldPose) && ok;
    ok = RegisterFunction(host, "Transform.SetWorldPose",
        {
            ScriptFunctionPin{ "entity", ScriptValueType::Entity, true },
            ScriptFunctionPin{ "posX", ScriptValueType::Float, true },
            ScriptFunctionPin{ "posY", ScriptValueType::Float, true },
            ScriptFunctionPin{ "posZ", ScriptValueType::Float, true },
            ScriptFunctionPin{ "rotX", ScriptValueType::Float, true },
            ScriptFunctionPin{ "rotY", ScriptValueType::Float, true },
            ScriptFunctionPin{ "rotZ", ScriptValueType::Float, true },
            ScriptFunctionPin{ "rotW", ScriptValueType::Float, true },
        },
        { ScriptFunctionPin{ "moved", ScriptValueType::Bool, true } },
        &SetWorldPose) && ok;
    ok = RegisterFunction(host, "Transform.Parent",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } },
        {
            ScriptFunctionPin{ "found", ScriptValueType::Bool, true },
            ScriptFunctionPin{ "parent", ScriptValueType::Entity, true },
        },
        &GetParent) && ok;
    ok = RegisterFunction(host, "Transform.SetParent",
        {
            ScriptFunctionPin{ "entity", ScriptValueType::Entity, true },
            ScriptFunctionPin{ "parent", ScriptValueType::Entity, false },
            ScriptFunctionPin{ "keepWorld", ScriptValueType::Bool, false },
        },
        { ScriptFunctionPin{ "moved", ScriptValueType::Bool, true } },
        &SetParent) && ok;
    ok = RegisterFunction(host, "Transform.ChildCount",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } },
        {
            ScriptFunctionPin{ "found", ScriptValueType::Bool, true },
            ScriptFunctionPin{ "count", ScriptValueType::Int, true },
        },
        &GetChildCount) && ok;
    ok = RegisterFunction(host, "Transform.GetChild",
        {
            ScriptFunctionPin{ "entity", ScriptValueType::Entity, true },
            ScriptFunctionPin{ "index", ScriptValueType::Int, true },
        },
        {
            ScriptFunctionPin{ "found", ScriptValueType::Bool, true },
            ScriptFunctionPin{ "child", ScriptValueType::Entity, true },
        },
        &GetChild) && ok;
    ok = RegisterFunction(host, "Transform.FindChild",
        {
            ScriptFunctionPin{ "entity", ScriptValueType::Entity, true },
            ScriptFunctionPin{ "name", ScriptValueType::String, true },
            ScriptFunctionPin{ "skip", ScriptValueType::Int, false },
        },
        {
            ScriptFunctionPin{ "found", ScriptValueType::Bool, true },
            ScriptFunctionPin{ "child", ScriptValueType::Entity, true },
        },
        &FindChild) && ok;
    ok = RegisterFunction(host, "Transform.Rotate",
        {
            ScriptFunctionPin{ "entity", ScriptValueType::Entity, true },
            ScriptFunctionPin{ "x", ScriptValueType::Float, true },
            ScriptFunctionPin{ "y", ScriptValueType::Float, true },
            ScriptFunctionPin{ "z", ScriptValueType::Float, true },
            ScriptFunctionPin{ "w", ScriptValueType::Float, true },
        },
        { ScriptFunctionPin{ "moved", ScriptValueType::Bool, true } },
        &Rotate) && ok;
    ok = RegisterFunction(host, "Transform.LookAt",
        {
            ScriptFunctionPin{ "entity", ScriptValueType::Entity, true },
            ScriptFunctionPin{ "targetX", ScriptValueType::Float, true },
            ScriptFunctionPin{ "targetY", ScriptValueType::Float, true },
            ScriptFunctionPin{ "targetZ", ScriptValueType::Float, true },
            ScriptFunctionPin{ "upX", ScriptValueType::Float, false },
            ScriptFunctionPin{ "upY", ScriptValueType::Float, false },
            ScriptFunctionPin{ "upZ", ScriptValueType::Float, false },
        },
        { ScriptFunctionPin{ "moved", ScriptValueType::Bool, true } },
        &LookAt) && ok;
    ok = RegisterFunction(host, "Transform.TransformPoint",
        {
            ScriptFunctionPin{ "entity", ScriptValueType::Entity, true },
            ScriptFunctionPin{ "x", ScriptValueType::Float, true },
            ScriptFunctionPin{ "y", ScriptValueType::Float, true },
            ScriptFunctionPin{ "z", ScriptValueType::Float, true },
        },
        {
            ScriptFunctionPin{ "found", ScriptValueType::Bool, true },
            ScriptFunctionPin{ "x", ScriptValueType::Float, true },
            ScriptFunctionPin{ "y", ScriptValueType::Float, true },
            ScriptFunctionPin{ "z", ScriptValueType::Float, true },
        },
        &TransformPoint) && ok;
    ok = RegisterFunction(host, "Transform.InverseTransformPoint",
        {
            ScriptFunctionPin{ "entity", ScriptValueType::Entity, true },
            ScriptFunctionPin{ "x", ScriptValueType::Float, true },
            ScriptFunctionPin{ "y", ScriptValueType::Float, true },
            ScriptFunctionPin{ "z", ScriptValueType::Float, true },
        },
        {
            ScriptFunctionPin{ "found", ScriptValueType::Bool, true },
            ScriptFunctionPin{ "x", ScriptValueType::Float, true },
            ScriptFunctionPin{ "y", ScriptValueType::Float, true },
            ScriptFunctionPin{ "z", ScriptValueType::Float, true },
        },
        &InverseTransformPoint) && ok;
    return ok;
}

} // namespace kb::script
