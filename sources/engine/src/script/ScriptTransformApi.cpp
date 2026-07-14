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
    if (keepWorld) {
        context.scene->Runtime().SynchronizeTransforms();
        const kb::scene::TransformComponent beforeTransform = context.scene->Transforms().Get(entity);
        worldPositionBeforeReparent = beforeTransform.worldPosition;
        worldRotationBeforeReparent = beforeTransform.worldRotation;
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
        } else {
            // New parent is root (none) — local IS world directly, same as
            // Transform.SetWorldPose's own root case.
            transform.localPosition = worldPositionBeforeReparent;
            transform.localRotation = worldRotationBeforeReparent;
        }
        context.scene->Transforms().Set(entity, transform);
        context.scene->Runtime().SynchronizeTransforms();
    }
    return BoolResult("moved", true);
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
    return ok;
}

} // namespace kb::script
