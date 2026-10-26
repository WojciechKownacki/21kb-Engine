#include "engine/script/ScriptPhysics2DApi.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/ScenePhysics2DRuntime.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace kb::script {
namespace {

using Args = std::span<const ScriptFunctionArgument>;

const ScriptValue& Arg(Args args, std::string_view key) {
    static const ScriptValue empty;
    for (const auto& arg : args) {
        if (arg.name == key) return arg.value;
    }
    return empty;
}

ScriptFunctionCallResult Error(std::string message) {
    return { .executed = false, .errors = { std::move(message) } };
}

[[nodiscard]] kb::scene::ScenePhysics2DRuntime* Runtime(const ScriptFunctionCallContext& context) {
    return context.scene == nullptr ? nullptr : context.scene->Runtime().Physics2D();
}

[[nodiscard]] kb::scene::SceneEntity EntityArg(Args args) {
    return kb::scene::SceneEntity{ Arg(args, "entity").AsUInt64() };
}

constexpr const char* kNoRuntime = "the 2D physics is not running; World.Map2D must be active and the scene playing";

[[nodiscard]] bool Finite(Args args, std::initializer_list<std::string_view> names) {
    for (const std::string_view name : names) {
        if (!std::isfinite(Arg(args, name).AsFloat())) return false;
    }
    return true;
}

ScriptFunctionCallResult Applied(bool applied, const char* refusal) {
    if (!applied) return Error(refusal);
    return { .executed = true, .outputs = { { "applied", ScriptValue{ true } } } };
}

ScriptFunctionCallResult SetVelocity(const ScriptFunctionCallContext& context, Args args) {
    auto* runtime = Runtime(context);
    if (runtime == nullptr) return Error(kNoRuntime);
    if (!Finite(args, { "x", "y" })) return Error("the velocity must be finite");
    return Applied(runtime->SetVelocity(EntityArg(args), Arg(args, "x").AsFloat(), Arg(args, "y").AsFloat()), "the entity has no 2D body in the simulation");
}

ScriptFunctionCallResult GetVelocity(const ScriptFunctionCallContext& context, Args args) {
    auto* runtime = Runtime(context);
    if (runtime == nullptr) return Error(kNoRuntime);
    float x = 0.0F;
    float y = 0.0F;
    if (!runtime->GetVelocity(EntityArg(args), x, y)) return Error("the entity has no 2D body in the simulation");
    return { .executed = true, .outputs = { { "x", ScriptValue{ x } }, { "y", ScriptValue{ y } } } };
}

ScriptFunctionCallResult AddImpulse(const ScriptFunctionCallContext& context, Args args) {
    auto* runtime = Runtime(context);
    if (runtime == nullptr) return Error(kNoRuntime);
    if (!Finite(args, { "x", "y" })) return Error("the impulse must be finite");
    return Applied(runtime->AddImpulse(EntityArg(args), Arg(args, "x").AsFloat(), Arg(args, "y").AsFloat()), "the entity has no dynamic 2D body in the simulation");
}

ScriptFunctionCallResult AddForce(const ScriptFunctionCallContext& context, Args args) {
    auto* runtime = Runtime(context);
    if (runtime == nullptr) return Error(kNoRuntime);
    if (!Finite(args, { "x", "y" })) return Error("the force must be finite");
    return Applied(runtime->AddForce(EntityArg(args), Arg(args, "x").AsFloat(), Arg(args, "y").AsFloat()), "the entity has no dynamic 2D body in the simulation");
}

ScriptFunctionCallResult Raycast(const ScriptFunctionCallContext& context, Args args) {
    auto* runtime = Runtime(context);
    if (runtime == nullptr) return Error(kNoRuntime);
    if (!Finite(args, { "originX", "originY", "directionX", "directionY" })) return Error("the ray must be finite");
    const float directionX = Arg(args, "directionX").AsFloat();
    const float directionY = Arg(args, "directionY").AsFloat();
    if (directionX == 0.0F && directionY == 0.0F) return Error("the ray needs a direction");
    const float maxDistance = Arg(args, "maxDistance").AsFloat(1000.0F);
    if (!(maxDistance > 0.0F) || !std::isfinite(maxDistance)) return Error("the ray needs a positive length");
    const auto hit = runtime->Raycast(Arg(args, "sideView").AsBool(true), Arg(args, "originX").AsFloat(), Arg(args, "originY").AsFloat(), directionX, directionY,
        maxDistance, static_cast<std::uint32_t>(Arg(args, "mask").AsInt(65535)));
    return { .executed = true, .outputs = { { "hit", ScriptValue{ hit.hit } }, { "entity", ScriptValue{ hit.entity.Id(), ScriptValueType::Entity } },
        { "x", ScriptValue{ hit.x } }, { "y", ScriptValue{ hit.y } }, { "normalX", ScriptValue{ hit.normalX } }, { "normalY", ScriptValue{ hit.normalY } },
        { "distance", ScriptValue{ hit.distance } } } };
}

ScriptFunctionCallResult Overlapping(const std::vector<kb::scene::SceneEntity>& found) {
    return { .executed = true, .outputs = { { "count", ScriptValue{ static_cast<int>(found.size()) } },
        { "entity", ScriptValue{ found.empty() ? std::uint64_t{ 0U } : found.front().Id(), ScriptValueType::Entity } } } };
}

ScriptFunctionCallResult OverlapCircle(const ScriptFunctionCallContext& context, Args args) {
    auto* runtime = Runtime(context);
    if (runtime == nullptr) return Error(kNoRuntime);
    if (!Finite(args, { "x", "y", "radius" }) || !(Arg(args, "radius").AsFloat() > 0.0F)) return Error("the circle needs a finite position and a positive radius");
    return Overlapping(runtime->OverlapCircle(Arg(args, "sideView").AsBool(true), Arg(args, "x").AsFloat(), Arg(args, "y").AsFloat(), Arg(args, "radius").AsFloat(),
        static_cast<std::uint32_t>(Arg(args, "mask").AsInt(65535))));
}

ScriptFunctionCallResult OverlapBox(const ScriptFunctionCallContext& context, Args args) {
    auto* runtime = Runtime(context);
    if (runtime == nullptr) return Error(kNoRuntime);
    if (!Finite(args, { "x", "y", "halfWidth", "halfHeight" }) || !(Arg(args, "halfWidth").AsFloat() > 0.0F) || !(Arg(args, "halfHeight").AsFloat() > 0.0F)) {
        return Error("the box needs a finite position and positive half sizes");
    }
    return Overlapping(runtime->OverlapBox(Arg(args, "sideView").AsBool(true), Arg(args, "x").AsFloat(), Arg(args, "y").AsFloat(), Arg(args, "halfWidth").AsFloat(),
        Arg(args, "halfHeight").AsFloat(), static_cast<std::uint32_t>(Arg(args, "mask").AsInt(65535))));
}

ScriptFunctionCallResult MoveCharacter(const ScriptFunctionCallContext& context, Args args) {
    auto* runtime = Runtime(context);
    if (runtime == nullptr) return Error(kNoRuntime);
    if (!Finite(args, { "x", "y" })) return Error("the movement must be finite");
    return Applied(runtime->MoveCharacter(EntityArg(args), Arg(args, "x").AsFloat(), Arg(args, "y").AsFloat()), "the entity has no 2D character in the simulation");
}

ScriptFunctionCallResult JumpCharacter(const ScriptFunctionCallContext& context, Args args) {
    auto* runtime = Runtime(context);
    if (runtime == nullptr) return Error(kNoRuntime);
    if (!Finite(args, { "speed" })) return Error("the jump speed must be finite");
    return Applied(runtime->JumpCharacter(EntityArg(args), Arg(args, "speed").AsFloat()), "the entity has no grounded 2D character in the simulation");
}

ScriptFunctionCallResult IsGrounded(const ScriptFunctionCallContext& context, Args args) {
    auto* runtime = Runtime(context);
    if (runtime == nullptr) return Error(kNoRuntime);
    return { .executed = true, .outputs = { { "grounded", ScriptValue{ runtime->IsGrounded(EntityArg(args)) } } } };
}

[[nodiscard]] std::optional<kb::scene::Physics2DJointDesc::Type> JointTypeOf(std::string_view name) {
    using Type = kb::scene::Physics2DJointDesc::Type;
    if (name == "distance") return Type::Distance;
    if (name == "revolute" || name == "hinge") return Type::Revolute;
    if (name == "prismatic" || name == "slider") return Type::Prismatic;
    if (name == "weld" || name == "fixed") return Type::Weld;
    return std::nullopt;
}

ScriptFunctionCallResult CreateJoint(const ScriptFunctionCallContext& context, Args args) {
    auto* runtime = Runtime(context);
    if (runtime == nullptr) return Error(kNoRuntime);
    const auto type = JointTypeOf(Arg(args, "type").AsString());
    if (!type.has_value()) return Error("the joint type is distance, revolute, prismatic or weld");
    kb::scene::Physics2DJointDesc desc;
    desc.type = *type;
    desc.first = kb::scene::SceneEntity{ Arg(args, "first").AsUInt64() };
    desc.second = kb::scene::SceneEntity{ Arg(args, "second").AsUInt64() };
    desc.x = Arg(args, "x").AsFloat();
    desc.y = Arg(args, "y").AsFloat();
    desc.x2 = Arg(args, "x2").AsFloat(desc.x);
    desc.y2 = Arg(args, "y2").AsFloat(desc.y);
    desc.axisX = Arg(args, "axisX").AsFloat(1.0F);
    desc.axisY = Arg(args, "axisY").AsFloat(0.0F);
    desc.collideConnected = Arg(args, "collideConnected").AsBool(false);
    desc.enableLimit = Arg(args, "enableLimit").AsBool(false);
    desc.lower = Arg(args, "lower").AsFloat();
    desc.upper = Arg(args, "upper").AsFloat();
    desc.enableMotor = Arg(args, "enableMotor").AsBool(false);
    desc.motorSpeed = Arg(args, "motorSpeed").AsFloat();
    desc.maxMotor = Arg(args, "maxMotor").AsFloat();
    desc.hertz = Arg(args, "hertz").AsFloat();
    desc.dampingRatio = Arg(args, "dampingRatio").AsFloat(0.7F);
    desc.length = Arg(args, "length").AsFloat();
    std::string error;
    const std::uint32_t joint = runtime->CreateJoint(desc, error);
    if (joint == 0U) return Error(error);
    return { .executed = true, .outputs = { { "joint", ScriptValue{ static_cast<int>(joint) } } } };
}

ScriptFunctionCallResult DestroyJoint(const ScriptFunctionCallContext& context, Args args) {
    auto* runtime = Runtime(context);
    if (runtime == nullptr) return Error(kNoRuntime);
    return Applied(runtime->DestroyJoint(static_cast<std::uint32_t>(Arg(args, "joint").AsInt())), "there is no such joint");
}

ScriptFunctionCallResult SetJointMotor(const ScriptFunctionCallContext& context, Args args) {
    auto* runtime = Runtime(context);
    if (runtime == nullptr) return Error(kNoRuntime);
    if (!Finite(args, { "speed", "maxForce" })) return Error("the motor numbers must be finite");
    return Applied(runtime->SetJointMotor(static_cast<std::uint32_t>(Arg(args, "joint").AsInt()), Arg(args, "speed").AsFloat(), Arg(args, "maxForce").AsFloat()),
        "there is no such revolute or prismatic joint, or the force is negative");
}

ScriptFunctionCallResult JointExists(const ScriptFunctionCallContext& context, Args args) {
    auto* runtime = Runtime(context);
    if (runtime == nullptr) return Error(kNoRuntime);
    return { .executed = true, .outputs = { { "exists", ScriptValue{ runtime->JointExists(static_cast<std::uint32_t>(Arg(args, "joint").AsInt())) } } } };
}

} // namespace

bool ScriptPhysics2DApi::Register(ScriptRuntimeHost& host) {
    using T = ScriptValueType;
    const auto add = [&](std::string name, std::string description, std::vector<ScriptFunctionPin> inputs, std::vector<ScriptFunctionPin> outputs,
                         ScriptFunctionCallback callback) {
        ScriptFunctionDesc desc;
        desc.signature.name = "Physics2D." + name;
        desc.signature.description = std::move(description);
        desc.signature.inputs = std::move(inputs);
        desc.signature.outputs = std::move(outputs);
        desc.callback = callback;
        return host.RegisterFunction(std::move(desc));
    };
    const std::vector<ScriptFunctionPin> entity{ { "entity", T::Entity, true } };
    const std::vector<ScriptFunctionPin> entityXY{ { "entity", T::Entity, true }, { "x", T::Float, true }, { "y", T::Float, true } };
    const std::vector<ScriptFunctionPin> applied{ { "applied", T::Bool, true } };
    bool ok = add("SetVelocity", "Set the velocity of a 2D body, in units per second in the plane of its body (X and Y, or X and Z for a top-down body).", entityXY, applied, &SetVelocity);
    ok = add("GetVelocity", "Read the velocity of a 2D body, in units per second in the plane of its body.", entity, { { "x", T::Float, true }, { "y", T::Float, true } }, &GetVelocity) && ok;
    ok = add("AddImpulse", "Give a dynamic 2D body an instant push (mass times velocity change) in the plane of its body.", entityXY, applied, &AddImpulse) && ok;
    ok = add("AddForce", "Push a dynamic 2D body with a force for the next simulation step, in the plane of its body.", entityXY, applied, &AddForce) && ok;
    ok = add("Raycast", "Cast a ray in the 2D physics plane (X and Y for a side view, X and Z for a top-down view) and report the first shape it meets: its entity, the point, the surface normal and the distance. The mask selects layers (bit 0 is layer 0); triggers are skipped.",
        { { "originX", T::Float, true }, { "originY", T::Float, true }, { "directionX", T::Float, true }, { "directionY", T::Float, true }, { "maxDistance", T::Float, false }, { "mask", T::Int, false }, { "sideView", T::Bool, false } },
        { { "hit", T::Bool, true }, { "entity", T::Entity, true }, { "x", T::Float, true }, { "y", T::Float, true }, { "normalX", T::Float, true }, { "normalY", T::Float, true }, { "distance", T::Float, true } }, &Raycast) && ok;
    ok = add("OverlapCircle", "Count the shapes a circle touches in the 2D physics plane and name the first; the mask selects layers (bit 0 is layer 0); triggers are included.",
        { { "x", T::Float, true }, { "y", T::Float, true }, { "radius", T::Float, true }, { "mask", T::Int, false }, { "sideView", T::Bool, false } },
        { { "count", T::Int, true }, { "entity", T::Entity, true } }, &OverlapCircle) && ok;
    ok = add("OverlapBox", "Count the shapes an upright box touches in the 2D physics plane and name the first; the mask selects layers (bit 0 is layer 0); triggers are included.",
        { { "x", T::Float, true }, { "y", T::Float, true }, { "halfWidth", T::Float, true }, { "halfHeight", T::Float, true }, { "mask", T::Int, false }, { "sideView", T::Bool, false } },
        { { "count", T::Int, true }, { "entity", T::Entity, true } }, &OverlapBox) && ok;
    ok = add("MoveCharacter", "Set the velocity a 2D character walks with for the coming steps, along the ground; it slides along shapes, climbs slopes up to its limit and steps over small ledges. Gravity and jumps are the character's own.",
        entityXY, applied, &MoveCharacter) && ok;
    ok = add("JumpCharacter", "Make a 2D character that stands on the ground leave it with an upward speed, in units per second.",
        { { "entity", T::Entity, true }, { "speed", T::Float, true } }, applied, &JumpCharacter) && ok;
    ok = add("IsGrounded", "Whether a 2D character stands on something it can walk on.", entity, { { "grounded", T::Bool, true } }, &IsGrounded) && ok;
    ok = add("CreateJoint", "Join two 2D bodies as they are now, or one body to the world (leave second out): type distance (a rope or spring between the anchors x, y and x2, y2), revolute (a hinge at x, y), prismatic (a slider along axisX, axisY) or weld (rigid). Optional: collideConnected, enableLimit with lower and upper (degrees for revolute, units otherwise), enableMotor with motorSpeed and maxMotor, hertz and dampingRatio for a spring, length. Returns the joint number; joints are not saved with the scene.",
        { { "type", T::String, true }, { "first", T::Entity, true }, { "second", T::Entity, false }, { "x", T::Float, true }, { "y", T::Float, true }, { "x2", T::Float, false }, { "y2", T::Float, false },
          { "axisX", T::Float, false }, { "axisY", T::Float, false }, { "collideConnected", T::Bool, false }, { "enableLimit", T::Bool, false }, { "lower", T::Float, false }, { "upper", T::Float, false },
          { "enableMotor", T::Bool, false }, { "motorSpeed", T::Float, false }, { "maxMotor", T::Float, false }, { "hertz", T::Float, false }, { "dampingRatio", T::Float, false }, { "length", T::Float, false } },
        { { "joint", T::Int, true } }, &CreateJoint) && ok;
    ok = add("DestroyJoint", "Remove a joint made with Physics2D.CreateJoint.", { { "joint", T::Int, true } }, applied, &DestroyJoint) && ok;
    ok = add("SetJointMotor", "Drive a revolute joint (degrees per second, torque) or a prismatic joint (units per second, force) with a motor; speed 0 holds it.",
        { { "joint", T::Int, true }, { "speed", T::Float, true }, { "maxForce", T::Float, true } }, applied, &SetJointMotor) && ok;
    ok = add("JointExists", "Whether a joint is still there; it ends when it is destroyed or one of its bodies goes away.", { { "joint", T::Int, true } }, { { "exists", T::Bool, true } }, &JointExists) && ok;
    return ok;
}

} // namespace kb::script
