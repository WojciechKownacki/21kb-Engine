#include "Map2DRuntime.hpp"
#include "Map2DView.hpp"
#include "Map2DCollision.hpp"
#include "Physics2DRuntime.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/Map2DAsset.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"

#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentAuthoring.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

using kb::scene::Body2DComponent;
using kb::scene::CharacterController2DComponent;
using kb::scene::Collider2DComponent;
using kb::scene::SceneEntity;

void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

[[nodiscard]] bool Near(float a, float b, float tolerance) { return std::abs(a - b) <= tolerance; }

struct World {
    kb::scene::Scene scene;
    kb::world2d::Physics2DRuntime* runtime = nullptr;
    kb::scene::SceneObject last;

    explicit World(bool withMaps = false) {
        if (withMaps) static_cast<void>(scene.Runtime().AddSceneSystem(std::make_unique<kb::world2d::Map2DRuntime>()));
        auto system = std::make_unique<kb::world2d::Physics2DRuntime>();
        runtime = system.get();
        static_cast<void>(scene.Runtime().AddSceneSystem(std::move(system)));
        scene.Runtime().SetPlaying(true);
        kb::scene::PhysicsBackend::SetCollisionEventConsumer(scene, true);
    }
    void Run(float seconds) {
        const int steps = static_cast<int>(std::ceil(seconds * 60.0F));
        for (int step = 0; step < steps; ++step) static_cast<void>(scene.Runtime().Update(1.0F / 60.0F));
    }
    [[nodiscard]] SceneEntity Make(const char* name, float x, float y, float z = 0.0F) {
        kb::scene::SceneObjectDesc desc{ .name = name };
        desc.transform.localPosition = { x, y, z };
        last = scene.Entities().CreateObject(desc);
        return last.Entity();
    }
    [[nodiscard]] kb::math::Vec3 At(SceneEntity entity) { return scene.Transforms().Get(entity).localPosition; }
};

[[nodiscard]] SceneEntity Ground(World& world, float y = 0.0F, float width = 20.0F) {
    const SceneEntity ground = world.Make("Ground", 0.0F, y);
    world.scene.Components().Colliders2D().Set(ground, Collider2DComponent{ .width = width, .height = 1.0F });
    return ground;
}

} // namespace

int main() {
    // A ball falls, lands on a static floor and rests; the floor never moves.
    {
        World world;
        const SceneEntity ground = Ground(world);
        const SceneEntity ball = world.Make("Ball", 0.0F, 3.0F);
        world.scene.Components().Bodies2D().Set(ball, Body2DComponent{});
        world.scene.Components().Colliders2D().Set(ball, Collider2DComponent{ .shape = 1, .radius = 0.5F, .restitution = 0.0F });
        world.Run(0.05F);
        Require(world.runtime->BodyCount() == 2U && world.runtime->ShapeCount() == 2U, "a body and a lone static collider are simulated");
        world.Run(2.5F);
        Require(Near(world.At(ball).y, 1.0F, 0.06F) && Near(world.At(ball).x, 0.0F, 0.05F), "a ball lands on the floor and rests on top of it");
        Require(Near(world.At(ground).y, 0.0F, 0.0001F), "a static floor never moves");
        float vx = 9.0F;
        float vy = 9.0F;
        Require(world.runtime->GetVelocity(ball, vx, vy) && std::abs(vy) < 0.1F, "a resting ball has no speed");
        // Not playing: nothing is simulated.
        world.scene.Runtime().SetPlaying(false);
        world.Run(0.05F);
        Require(world.runtime->BodyCount() == 0U, "nothing is simulated while editing");
    }

    // Gravity scale, start velocity, bodies of every type, and the script API for velocity and impulses.
    {
        World world;
        const SceneEntity floating = world.Make("Floating", 0.0F, 5.0F);
        world.scene.Components().Bodies2D().Set(floating, Body2DComponent{ .gravityScale = 0.0F, .startVelocityX = 3.0F });
        world.scene.Components().Colliders2D().Set(floating, Collider2DComponent{ .width = 0.5F, .height = 0.5F });
        const SceneEntity kinematic = world.Make("Kinematic", 0.0F, 8.0F);
        world.scene.Components().Bodies2D().Set(kinematic, Body2DComponent{ .type = 1 });
        world.scene.Components().Colliders2D().Set(kinematic, Collider2DComponent{ .width = 0.5F, .height = 0.5F });
        const SceneEntity staticBody = world.Make("Static", 4.0F, 8.0F);
        world.scene.Components().Bodies2D().Set(staticBody, Body2DComponent{ .type = 0 });
        world.scene.Components().Colliders2D().Set(staticBody, Collider2DComponent{ .width = 0.5F, .height = 0.5F });
        world.Run(0.05F);
        kb::script::ScriptRuntimeHost host{ world.scene };
        Require(host.Succeeded(), "the script host installs the 2D physics API");
        const auto call = [&](const char* name, std::vector<kb::script::ScriptFunctionArgument> arguments) {
            return host.Functions().Call(name, arguments, kb::script::ScriptFunctionCallContext{ .scene = &world.scene });
        };
        const auto entityArg = [](SceneEntity entity) { return kb::script::ScriptFunctionArgument{ "entity", kb::script::ScriptValue{ entity.Id(), kb::script::ScriptValueType::Entity } }; };
        const auto number = [](const char* name, float value) { return kb::script::ScriptFunctionArgument{ name, kb::script::ScriptValue{ value } }; };
        Require(call("Physics2D.SetVelocity", { entityArg(kinematic), number("x", 2.0F), number("y", 0.0F) }).Succeeded(), "a script sets a velocity");
        world.Run(1.0F);
        Require(Near(world.At(floating).x, 3.15F, 0.1F) && Near(world.At(floating).y, 5.0F, 0.01F), "a body without gravity keeps its start velocity");
        Require(Near(world.At(kinematic).x, 2.0F, 0.15F) && Near(world.At(kinematic).y, 8.0F, 0.01F), "a kinematic body moves by its velocity only");
        Require(Near(world.At(staticBody).x, 4.0F, 0.0001F) && Near(world.At(staticBody).y, 8.0F, 0.0001F), "a static body does not move");
        const auto velocity = call("Physics2D.GetVelocity", { entityArg(floating) });
        Require(velocity.Succeeded() && Near(velocity.Output("x")->AsFloat(), 3.0F, 0.01F), "a script reads a velocity");
        Require(call("Physics2D.AddImpulse", { entityArg(floating), number("x", 0.0F), number("y", 4.0F) }).Succeeded(), "a script gives a dynamic body an impulse");
        world.Run(0.1F);
        Require(world.At(floating).y > 5.0F, "an impulse moves a body");
        Require(!call("Physics2D.AddImpulse", { entityArg(staticBody), number("x", 1.0F), number("y", 0.0F) }).Succeeded(), "a static body takes no impulse");
        Require(!call("Physics2D.SetVelocity", { entityArg(floating), number("x", std::nanf("")), number("y", 0.0F) }).Succeeded(), "a script velocity must be finite");
        Require(!call("Physics2D.SetVelocity", { entityArg(world.Make("NoBody", 0.0F, 0.0F)), number("x", 1.0F), number("y", 0.0F) }).Succeeded(), "an entity without a body is refused");
    }

    // Layers: what a shape collides with is decided by its layer and mask, both ways.
    {
        World world;
        const SceneEntity ground = world.Make("Ground", 0.0F, 0.0F);
        world.scene.Components().Colliders2D().Set(ground, Collider2DComponent{ .width = 20.0F, .height = 1.0F, .layer = 0, .collidesWith = 0xFFFFU });
        const SceneEntity lands = world.Make("Lands", -2.0F, 3.0F);
        world.scene.Components().Bodies2D().Set(lands, Body2DComponent{});
        world.scene.Components().Colliders2D().Set(lands, Collider2DComponent{ .width = 1.0F, .height = 1.0F, .layer = 2, .collidesWith = 1U });
        const SceneEntity falls = world.Make("Falls", 2.0F, 3.0F);
        world.scene.Components().Bodies2D().Set(falls, Body2DComponent{});
        world.scene.Components().Colliders2D().Set(falls, Collider2DComponent{ .width = 1.0F, .height = 1.0F, .layer = 3, .collidesWith = 2U });
        const SceneEntity ignored = world.Make("Ignored", 6.0F, 3.0F);
        world.scene.Components().Bodies2D().Set(ignored, Body2DComponent{});
        world.scene.Components().Colliders2D().Set(ignored, Collider2DComponent{ .width = 1.0F, .height = 1.0F, .layer = 4, .collidesWith = 0xFFFFU });
        world.scene.Components().Colliders2D().Set(ground, Collider2DComponent{ .width = 20.0F, .height = 1.0F, .layer = 0, .collidesWith = 0xFFFFU & ~(1U << 4) });
        world.Run(3.0F);
        Require(Near(world.At(lands).y, 1.0F, 0.06F), "a shape whose mask includes the floor layer lands");
        Require(world.At(falls).y < -3.0F, "a shape whose mask leaves out the floor layer falls through");
        Require(world.At(ignored).y < -3.0F, "the floor must also accept the other shape: a floor that leaves out a layer lets that layer through");
    }

    // Triggers report entering and leaving to both sides; a trigger does not block.
    {
        World world;
        const SceneEntity zone = world.Make("Zone", 0.0F, 0.0F);
        world.scene.Components().Colliders2D().Set(zone, Collider2DComponent{ .width = 4.0F, .height = 2.0F, .isTrigger = true });
        const SceneEntity ball = world.Make("Ball", 0.0F, 4.0F);
        world.scene.Components().Bodies2D().Set(ball, Body2DComponent{});
        world.scene.Components().Colliders2D().Set(ball, Collider2DComponent{ .shape = 1, .radius = 0.4F });
        std::size_t enter = 0U;
        std::size_t exit = 0U;
        bool zoneSawBall = false;
        bool ballSawZone = false;
        for (int frame = 0; frame < 180; ++frame) {
            static_cast<void>(world.scene.Runtime().Update(1.0F / 60.0F));
            for (const auto& event : kb::scene::PhysicsBackend::DrainPendingCollisionEvents(world.scene)) {
                Require(event.isTrigger, "an overlap with a trigger is a trigger event");
                if (event.phase == kb::scene::PhysicsContactPhase::Enter) {
                    ++enter;
                    zoneSawBall = zoneSawBall || (event.target == zone && event.other == ball);
                    ballSawZone = ballSawZone || (event.target == ball && event.other == zone);
                }
                if (event.phase == kb::scene::PhysicsContactPhase::Exit) ++exit;
            }
        }
        Require(enter == 2U && exit == 2U && zoneSawBall && ballSawZone, "a trigger sends enter and exit to both sides");
        Require(world.At(ball).y < -5.0F, "a ball falls through a trigger");
    }

    // Contacts: a landing sends an enter event with a point and a normal.
    {
        World world;
        const SceneEntity ground = Ground(world);
        const SceneEntity crate = world.Make("Crate", 0.0F, 2.0F);
        world.scene.Components().Bodies2D().Set(crate, Body2DComponent{});
        world.scene.Components().Colliders2D().Set(crate, Collider2DComponent{ .width = 1.0F, .height = 1.0F });
        bool landed = false;
        for (int frame = 0; frame < 120 && !landed; ++frame) {
            static_cast<void>(world.scene.Runtime().Update(1.0F / 60.0F));
            for (const auto& event : kb::scene::PhysicsBackend::DrainPendingCollisionEvents(world.scene)) {
                if (event.phase == kb::scene::PhysicsContactPhase::Enter && !event.isTrigger && event.target == crate && event.other == ground) {
                    landed = true;
                    Require(Near(event.point.y, 0.5F, 0.1F) && Near(std::abs(event.normal.y), 1.0F, 0.1F), "the contact has the point and the normal of the landing");
                }
            }
        }
        Require(landed, "a landing sends a collision enter event");
    }

    // Queries and the script API: ray, circle and box.
    {
        World world;
        const SceneEntity ground = Ground(world);
        const SceneEntity wall = world.Make("Wall", 5.0F, 3.0F);
        world.scene.Components().Colliders2D().Set(wall, Collider2DComponent{ .width = 1.0F, .height = 4.0F, .layer = 5 });
        const SceneEntity zone = world.Make("Zone", -5.0F, 2.0F);
        world.scene.Components().Colliders2D().Set(zone, Collider2DComponent{ .width = 2.0F, .height = 2.0F, .isTrigger = true, .layer = 6 });
        world.Run(0.05F);
        kb::script::ScriptRuntimeHost host{ world.scene };
        const auto call = [&](const char* name, std::vector<kb::script::ScriptFunctionArgument> arguments) {
            return host.Functions().Call(name, arguments, kb::script::ScriptFunctionCallContext{ .scene = &world.scene });
        };
        const auto number = [](const char* name, float value) { return kb::script::ScriptFunctionArgument{ name, kb::script::ScriptValue{ value } }; };
        const auto integer = [](const char* name, int value) { return kb::script::ScriptFunctionArgument{ name, kb::script::ScriptValue{ value } }; };
        auto down = call("Physics2D.Raycast", { number("originX", 0.0F), number("originY", 10.0F), number("directionX", 0.0F), number("directionY", -1.0F) });
        Require(down.Succeeded() && down.Output("hit")->AsBool() && down.Output("entity")->AsUInt64() == ground.Id() && Near(down.Output("y")->AsFloat(), 0.5F, 0.01F) &&
                Near(down.Output("distance")->AsFloat(), 9.5F, 0.01F) && Near(down.Output("normalY")->AsFloat(), 1.0F, 0.01F),
            "a ray down hits the floor top with its entity, point, normal and distance");
        auto sideways = call("Physics2D.Raycast", { number("originX", 0.0F), number("originY", 3.0F), number("directionX", 1.0F), number("directionY", 0.0F), number("maxDistance", 20.0F) });
        Require(sideways.Output("hit")->AsBool() && sideways.Output("entity")->AsUInt64() == wall.Id() && Near(sideways.Output("distance")->AsFloat(), 4.5F, 0.01F), "a ray sideways hits the wall");
        auto filtered = call("Physics2D.Raycast", { number("originX", 0.0F), number("originY", 3.0F), number("directionX", 1.0F), number("directionY", 0.0F), integer("mask", 1) });
        Require(!filtered.Output("hit")->AsBool(), "a ray's layer mask leaves out the wall layer");
        auto short_ = call("Physics2D.Raycast", { number("originX", 0.0F), number("originY", 3.0F), number("directionX", 1.0F), number("directionY", 0.0F), number("maxDistance", 2.0F) });
        Require(!short_.Output("hit")->AsBool(), "a ray stops at its length");
        auto toTrigger = call("Physics2D.Raycast", { number("originX", -10.0F), number("originY", 2.0F), number("directionX", 1.0F), number("directionY", 0.0F), number("maxDistance", 20.0F) });
        Require(!toTrigger.Output("hit")->AsBool() || toTrigger.Output("entity")->AsUInt64() != zone.Id(), "a ray passes through a trigger");
        Require(!call("Physics2D.Raycast", { number("originX", 0.0F), number("originY", 0.0F), number("directionX", 0.0F), number("directionY", 0.0F) }).Succeeded(), "a ray needs a direction");
        auto circle = call("Physics2D.OverlapCircle", { number("x", 5.0F), number("y", 3.0F), number("radius", 0.5F) });
        Require(circle.Succeeded() && circle.Output("count")->AsInt() == 1 && circle.Output("entity")->AsUInt64() == wall.Id(), "a circle finds the wall it touches");
        auto none = call("Physics2D.OverlapCircle", { number("x", 0.0F), number("y", 5.0F), number("radius", 0.5F) });
        Require(none.Output("count")->AsInt() == 0, "a circle in the air finds nothing");
        auto box = call("Physics2D.OverlapBox", { number("x", 0.0F), number("y", 0.0F), number("halfWidth", 10.0F), number("halfHeight", 4.0F) });
        Require(box.Output("count")->AsInt() == 3, "a big box finds the floor, the wall and the trigger");
        auto masked = call("Physics2D.OverlapBox", { number("x", 0.0F), number("y", 0.0F), number("halfWidth", 10.0F), number("halfHeight", 4.0F), integer("mask", 1 << 6) });
        Require(masked.Output("count")->AsInt() == 1 && masked.Output("entity")->AsUInt64() == zone.Id(), "a box query finds a trigger and honours the layer mask");
    }

    // The top-down plane: X and Z, no gravity; a ball rolls into a wall along Z.
    {
        World world;
        const SceneEntity wall = world.Make("Wall", 0.0F, 0.0F, 6.0F);
        world.scene.Components().Colliders2D().Set(wall, Collider2DComponent{ .width = 10.0F, .height = 1.0F, .sideView = false });
        const SceneEntity ball = world.Make("Ball", 0.0F, 7.0F, 0.0F);
        world.scene.Components().Bodies2D().Set(ball, Body2DComponent{ .sideView = false, .startVelocityY = 2.0F });
        world.scene.Components().Colliders2D().Set(ball, Collider2DComponent{ .shape = 1, .radius = 0.5F, .sideView = false });
        world.Run(0.05F);
        Require(Near(world.At(ball).y, 7.0F, 0.0001F), "a top-down body keeps its height across the plane");
        world.Run(3.0F);
        Require(Near(world.At(ball).z, 5.0F, 0.1F) && Near(world.At(ball).x, 0.0F, 0.05F), "a top-down ball moves along Z and stops at the wall");
    }

    // A body with a child collider; removing the component removes the shape; removing the body removes the body.
    {
        World world;
        const SceneEntity ground = Ground(world);
        static_cast<void>(ground);
        const SceneEntity body = world.Make("Body", 0.0F, 4.0F);
        world.scene.Components().Bodies2D().Set(body, Body2DComponent{ .fixedRotation = true });
        kb::scene::SceneObjectDesc footDesc{ .name = "Foot", .parent = world.last };
        footDesc.transform.localPosition = { 0.0F, -1.0F, 0.0F };
        const auto child = world.scene.Entities().CreateObject(footDesc);
        world.scene.Components().Colliders2D().Set(child.Entity(), Collider2DComponent{ .width = 1.0F, .height = 0.5F });
        world.Run(3.0F);
        Require(Near(world.At(body).y, 1.75F, 0.08F), "a child shape offsets the body: its foot rests on the floor");
        Require(world.runtime->BodyCount() == 2U && world.runtime->ShapeCount() == 2U, "the child shape is on its parent's body");
        world.scene.Components().Colliders2D().Remove(child.Entity());
        world.Run(0.1F);
        Require(world.runtime->ShapeCount() == 1U, "removing a collider removes its shape");
        world.scene.Components().Bodies2D().Remove(body);
        world.Run(0.05F);
        Require(world.runtime->BodyCount() == 1U, "removing the body removes it");
    }

    // A character: lands, walks, is stopped by a wall, jumps, climbs a gentle slope and steps over a low ledge but not a high one.
    {
        World world;
        const SceneEntity floor = Ground(world, 0.0F, 60.0F);
        static_cast<void>(floor);
        const SceneEntity hero = world.Make("Hero", -10.0F, 3.0F);
        world.scene.Components().CharacterControllers2D().Set(hero, CharacterController2DComponent{});
        world.Run(0.05F);
        Require(world.runtime->CharacterCount() == 1U, "the character is in the simulation");
        world.Run(1.5F);
        Require(world.runtime->IsGrounded(hero) && Near(world.At(hero).y, 1.0F, 0.08F), "a character falls and stands on the floor");
        kb::script::ScriptRuntimeHost host{ world.scene };
        const auto call = [&](const char* name, std::vector<kb::script::ScriptFunctionArgument> arguments) {
            return host.Functions().Call(name, arguments, kb::script::ScriptFunctionCallContext{ .scene = &world.scene });
        };
        const auto entityArg = [](SceneEntity entity) { return kb::script::ScriptFunctionArgument{ "entity", kb::script::ScriptValue{ entity.Id(), kb::script::ScriptValueType::Entity } }; };
        const auto number = [](const char* name, float value) { return kb::script::ScriptFunctionArgument{ name, kb::script::ScriptValue{ value } }; };
        Require(call("Physics2D.IsGrounded", { entityArg(hero) }).Output("grounded")->AsBool(), "a script asks whether the character stands");
        Require(call("Physics2D.MoveCharacter", { entityArg(hero), number("x", 3.0F), number("y", 0.0F) }).Succeeded(), "a script walks the character");
        const float startX = world.At(hero).x;
        world.Run(1.0F);
        Require(Near(world.At(hero).x - startX, 3.0F, 0.2F) && Near(world.At(hero).y, 1.0F, 0.08F), "a character walks at the speed it was given along the floor");
        // A wall at x = -2.
        const SceneEntity wall = world.Make("Wall", -2.0F, 3.0F);
        world.scene.Components().Colliders2D().Set(wall, Collider2DComponent{ .width = 1.0F, .height = 6.0F });
        world.Run(3.0F);
        Require(world.At(hero).x < -2.0F - 0.5F - 0.2F + 0.05F && world.At(hero).x > -3.5F, "a wall stops a walking character");
        Require(call("Physics2D.MoveCharacter", { entityArg(hero), number("x", 0.0F), number("y", 0.0F) }).Succeeded(), "the character stops walking");
        world.Run(0.3F);
        const float stoppedX = world.At(hero).x;
        world.Run(0.3F);
        Require(Near(world.At(hero).x, stoppedX, 0.01F), "a character with no input stands still");
        // Jump.
        Require(call("Physics2D.JumpCharacter", { entityArg(hero), number("speed", 7.0F) }).Succeeded(), "a grounded character jumps");
        world.Run(0.25F);
        Require(world.At(hero).y > 1.6F && !world.runtime->IsGrounded(hero), "a jumping character is in the air");
        Require(!call("Physics2D.JumpCharacter", { entityArg(hero), number("speed", 7.0F) }).Succeeded(), "a character in the air cannot jump");
        world.Run(2.0F);
        Require(world.runtime->IsGrounded(hero) && Near(world.At(hero).y, 1.0F, 0.08F), "a character that jumped lands again");
    }

    // Slopes and ledges.
    {
        World world;
        static_cast<void>(Ground(world, 0.0F, 100.0F));
        // A ramp of 25 degrees starting at x = 0, long enough to climb 2 units.
        const SceneEntity ramp = world.Make("Ramp", 6.0F, 1.2F);
        world.scene.Components().Colliders2D().Set(ramp, Collider2DComponent{ .width = 12.0F, .height = 0.5F, .angleDegrees = 25.0F });
        const SceneEntity hero = world.Make("Hero", -4.0F, 1.5F);
        world.scene.Components().CharacterControllers2D().Set(hero, CharacterController2DComponent{ .slopeLimitDegrees = 40.0F });
        world.Run(1.0F);
        kb::script::ScriptRuntimeHost host{ world.scene };
        const auto call = [&](const char* name, std::vector<kb::script::ScriptFunctionArgument> arguments) {
            return host.Functions().Call(name, arguments, kb::script::ScriptFunctionCallContext{ .scene = &world.scene });
        };
        const auto entityArg = [](SceneEntity entity) { return kb::script::ScriptFunctionArgument{ "entity", kb::script::ScriptValue{ entity.Id(), kb::script::ScriptValueType::Entity } }; };
        const auto number = [](const char* name, float value) { return kb::script::ScriptFunctionArgument{ name, kb::script::ScriptValue{ value } }; };
        Require(call("Physics2D.MoveCharacter", { entityArg(hero), number("x", 3.0F), number("y", 0.0F) }).Succeeded(), "walk toward the ramp");
        world.Run(5.0F);
        Require(world.At(hero).x > 3.0F && world.At(hero).y > 2.0F && world.runtime->IsGrounded(hero), "a character walks up a slope under its limit");

        World steep;
        static_cast<void>(Ground(steep, 0.0F, 100.0F));
        const SceneEntity wallRamp = steep.Make("Steep", 6.0F, 2.0F);
        steep.scene.Components().Colliders2D().Set(wallRamp, Collider2DComponent{ .width = 12.0F, .height = 0.5F, .angleDegrees = 65.0F });
        const SceneEntity climber = steep.Make("Climber", -4.0F, 1.5F);
        steep.scene.Components().CharacterControllers2D().Set(climber, CharacterController2DComponent{ .slopeLimitDegrees = 40.0F });
        steep.Run(1.0F);
        kb::script::ScriptRuntimeHost steepHost{ steep.scene };
        Require(steepHost.Functions().Call("Physics2D.MoveCharacter", std::vector<kb::script::ScriptFunctionArgument>{ entityArg(climber), number("x", 3.0F), number("y", 0.0F) },
                    kb::script::ScriptFunctionCallContext{ .scene = &steep.scene }).Succeeded(), "walk toward the steep ramp");
        steep.Run(5.0F);
        Require(steep.At(climber).y < 2.0F, "a character does not climb a slope over its limit");

        // A low ledge is stepped onto; a high one blocks.
        for (const bool low : { true, false }) {
            World ledge;
            static_cast<void>(Ground(ledge, 0.0F, 100.0F));
            const float height = low ? 0.15F : 0.9F;
            const SceneEntity step = ledge.Make("Ledge", 5.0F, 0.5F + height * 0.5F);
            ledge.scene.Components().Colliders2D().Set(step, Collider2DComponent{ .width = 6.0F, .height = height });
            const SceneEntity walker = ledge.Make("Walker", 0.0F, 1.5F);
            ledge.scene.Components().CharacterControllers2D().Set(walker, CharacterController2DComponent{ .stepHeight = 0.25F });
            ledge.Run(1.0F);
            kb::script::ScriptRuntimeHost ledgeHost{ ledge.scene };
            Require(ledgeHost.Functions().Call("Physics2D.MoveCharacter", std::vector<kb::script::ScriptFunctionArgument>{ entityArg(walker), number("x", 3.0F), number("y", 0.0F) },
                        kb::script::ScriptFunctionCallContext{ .scene = &ledge.scene }).Succeeded(), "walk toward the ledge");
            ledge.Run(1.4F);
            if (low) Require(ledge.At(walker).x > 3.0F && ledge.At(walker).y > 1.0F + height - 0.1F, "a character steps onto a ledge lower than its step height");
            else Require(ledge.At(walker).x < 2.2F, "a ledge higher than the step height blocks a character");
        }
    }

    // The character is a body for the others: a ball rolls into it and a trigger sees it.
    {
        World world;
        static_cast<void>(Ground(world, 0.0F, 40.0F));
        const SceneEntity hero = world.Make("Hero", 0.0F, 1.1F);
        world.scene.Components().CharacterControllers2D().Set(hero, CharacterController2DComponent{});
        const SceneEntity zone = world.Make("Zone", 3.0F, 1.0F);
        world.scene.Components().Colliders2D().Set(zone, Collider2DComponent{ .width = 1.0F, .height = 2.0F, .isTrigger = true });
        world.Run(1.0F);
        kb::script::ScriptRuntimeHost host{ world.scene };
        const auto entityArg = [](SceneEntity entity) { return kb::script::ScriptFunctionArgument{ "entity", kb::script::ScriptValue{ entity.Id(), kb::script::ScriptValueType::Entity } }; };
        Require(host.Functions().Call("Physics2D.MoveCharacter", std::vector<kb::script::ScriptFunctionArgument>{ entityArg(hero), kb::script::ScriptFunctionArgument{ "x", kb::script::ScriptValue{ 3.0F } }, kb::script::ScriptFunctionArgument{ "y", kb::script::ScriptValue{ 0.0F } } },
                    kb::script::ScriptFunctionCallContext{ .scene = &world.scene }).Succeeded(), "walk into the zone");
        bool entered = false;
        for (int frame = 0; frame < 120; ++frame) {
            static_cast<void>(world.scene.Runtime().Update(1.0F / 60.0F));
            for (const auto& event : kb::scene::PhysicsBackend::DrainPendingCollisionEvents(world.scene)) {
                entered = entered || (event.isTrigger && event.phase == kb::scene::PhysicsContactPhase::Enter && event.target == zone && event.other == hero);
            }
        }
        Require(entered, "a trigger sees a character walk in");
    }

    // Joints: a hinge to the world swings a pendulum, a rope keeps its length, a slider keeps a line, a weld keeps a pose, a motor turns a wheel.
    {
        World world;
        kb::script::ScriptRuntimeHost host{ world.scene };
        const auto entityArg = [](const char* name, SceneEntity entity) { return kb::script::ScriptFunctionArgument{ name, kb::script::ScriptValue{ entity.Id(), kb::script::ScriptValueType::Entity } }; };
        const auto number = [](const char* name, float value) { return kb::script::ScriptFunctionArgument{ name, kb::script::ScriptValue{ value } }; };
        const auto integer = [](const char* name, int value) { return kb::script::ScriptFunctionArgument{ name, kb::script::ScriptValue{ value } }; };
        const auto flag = [](const char* name, bool value) { return kb::script::ScriptFunctionArgument{ name, kb::script::ScriptValue{ value } }; };
        const auto text = [](const char* name, const char* value) { return kb::script::ScriptFunctionArgument{ name, kb::script::ScriptValue{ std::string{ value } } }; };
        const auto call = [&](const char* name, std::vector<kb::script::ScriptFunctionArgument> arguments) {
            return host.Functions().Call(name, arguments, kb::script::ScriptFunctionCallContext{ .scene = &world.scene });
        };
        const auto bob = [&](const char* name, float x, float y) {
            const SceneEntity entity = world.Make(name, x, y);
            world.scene.Components().Bodies2D().Set(entity, Body2DComponent{});
            world.scene.Components().Colliders2D().Set(entity, Collider2DComponent{ .shape = 1, .radius = 0.25F, .layer = 7, .collidesWith = 0U });
            return entity;
        };
        const SceneEntity pendulum = bob("Pendulum", 2.0F, 10.0F);
        const SceneEntity rope = bob("Rope", 12.0F, 10.0F);
        const SceneEntity slider = bob("Slider", 22.0F, 10.0F);
        const SceneEntity partner = bob("Partner", 32.0F, 10.0F);
        const SceneEntity welded = bob("Welded", 33.0F, 10.0F);
        const SceneEntity wheel = world.Make("Wheel", 42.0F, 10.0F);
        world.scene.Components().Bodies2D().Set(wheel, Body2DComponent{ .gravityScale = 0.0F });
        world.scene.Components().Colliders2D().Set(wheel, Collider2DComponent{ .shape = 1, .radius = 0.5F, .layer = 7, .collidesWith = 0U });
        world.Run(0.05F);
        const auto hinge = call("Physics2D.CreateJoint", { text("type", "revolute"), entityArg("first", pendulum), number("x", 0.0F), number("y", 10.0F) });
        Require(hinge.Succeeded() && hinge.Output("joint")->AsInt() > 0, "a hinge to the world is made");
        const auto cord = call("Physics2D.CreateJoint", { text("type", "distance"), entityArg("first", rope), number("x", 12.0F), number("y", 10.0F), number("x2", 10.0F), number("y2", 10.0F) });
        Require(cord.Succeeded(), "a rope to the world is made");
        const auto rail = call("Physics2D.CreateJoint", { text("type", "prismatic"), entityArg("first", slider), number("x", 22.0F), number("y", 10.0F), number("axisX", 1.0F), number("axisY", 0.0F),
            flag("enableLimit", true), number("lower", -1.0F), number("upper", 1.0F) });
        Require(rail.Succeeded(), "a slider to the world is made");
        const auto glue = call("Physics2D.CreateJoint", { text("type", "weld"), entityArg("first", partner), entityArg("second", welded), number("x", 32.5F), number("y", 10.0F) });
        Require(glue.Succeeded(), "a weld between two bodies is made");
        const auto spin = call("Physics2D.CreateJoint", { text("type", "revolute"), entityArg("first", wheel), number("x", 42.0F), number("y", 10.0F), flag("enableMotor", true), number("motorSpeed", 90.0F), number("maxMotor", 1000.0F) });
        Require(spin.Succeeded(), "a motor hinge is made");
        Require(world.runtime->JointCount() == 5U, "five joints are in the simulation");
        world.Run(2.0F);
        const auto pendulumAt = world.At(pendulum);
        Require(Near(std::hypot(pendulumAt.x, pendulumAt.y - 10.0F), 2.0F, 0.05F) && pendulumAt.y < 9.9F, "a pendulum swings down on its arm");
        const auto ropeAt = world.At(rope);
        Require(Near(std::hypot(ropeAt.x - 10.0F, ropeAt.y - 10.0F), 2.0F, 0.05F), "a rope keeps its length");
        const auto sliderAt = world.At(slider);
        Require(Near(sliderAt.y, 10.0F, 0.02F) && sliderAt.x >= 21.0F - 0.05F && sliderAt.x <= 23.0F + 0.05F, "a slider stays on its line");
        const auto partnerAt = world.At(partner);
        const auto weldedAt = world.At(welded);
        Require(Near(weldedAt.x - partnerAt.x, 1.0F, 0.05F) && Near(weldedAt.y - partnerAt.y, 0.0F, 0.05F) && partnerAt.y < 9.0F, "welded bodies fall together and keep their offset");
        const int motorJoint = spin.Output("joint")->AsInt();
        Require(call("Physics2D.SetJointMotor", { integer("joint", motorJoint), number("speed", 0.0F), number("maxForce", 1000.0F) }).Succeeded(), "a script changes the speed of a motor");
        const int ropeJoint = cord.Output("joint")->AsInt();
        Require(!call("Physics2D.SetJointMotor", { integer("joint", ropeJoint), number("speed", 1.0F), number("maxForce", 1.0F) }).Succeeded(), "a rope has no motor");
        Require(call("Physics2D.JointExists", { integer("joint", ropeJoint) }).Output("exists")->AsBool(), "the rope exists");
        Require(call("Physics2D.DestroyJoint", { integer("joint", ropeJoint) }).Succeeded(), "a joint is destroyed");
        Require(!call("Physics2D.JointExists", { integer("joint", ropeJoint) }).Output("exists")->AsBool(), "a destroyed joint is gone");
        world.Run(1.0F);
        Require(world.At(rope).y < 5.0F, "a body whose rope was cut falls");
        Require(!call("Physics2D.CreateJoint", { text("type", "rope"), entityArg("first", rope), number("x", 0.0F), number("y", 0.0F) }).Succeeded(), "an unknown joint type is refused");
        Require(!call("Physics2D.CreateJoint", { text("type", "weld"), entityArg("first", rope), entityArg("second", rope), number("x", 0.0F), number("y", 0.0F) }).Succeeded(), "a joint needs two different bodies");
        Require(!call("Physics2D.CreateJoint", { text("type", "weld"), entityArg("first", world.Make("Nothing", 0.0F, 0.0F)), number("x", 0.0F), number("y", 0.0F) }).Succeeded(), "an entity without a body is refused");
        Require(!call("Physics2D.CreateJoint", { text("type", "revolute"), entityArg("first", rope), number("x", std::nanf("")), number("y", 0.0F) }).Succeeded(), "a joint anchor must be finite");
        // Removing a body takes its joints with it.
        world.scene.Components().Bodies2D().Remove(pendulum);
        world.Run(0.1F);
        Require(!call("Physics2D.JointExists", { integer("joint", hinge.Output("joint")->AsInt()) }).Output("exists")->AsBool(), "a joint ends with its body");
        Require(world.runtime->JointCount() == 3U, "three joints are left");
    }

    // The collision of a tilemap becomes static 2D shapes: floors, walls, slopes, ledges that can be jumped up through, triggers,
    // and they follow the map when a cell is changed while playing.
    {
        using kb::assets::Map2DTileKey;
        const auto root = std::filesystem::temp_directory_path() / "kb_world2d_physics_map_tests";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        auto map = kb::assets::MakeEmptyMap2DAsset(16U, 8U);
        map.background = false;
        map.orientation = kb::assets::Map2DOrientation::Wall;
        for (unsigned x = 0U; x < 16U; ++x) map.tiles[Map2DTileKey(x, 7U, 0)].solid = true;
        for (unsigned y = 4U; y < 7U; ++y) map.tiles[Map2DTileKey(12U, y, 0)].solid = true;
        for (unsigned x = 2U; x < 4U; ++x) map.tiles[Map2DTileKey(x, 4U, 0)].collision = kb::assets::Map2DCollision{ .kind = kb::assets::Map2DCollisionKind::OneWay };
        map.tiles[Map2DTileKey(8U, 6U, 0)].collision = kb::assets::Map2DCollision{ .kind = kb::assets::Map2DCollisionKind::SlopeUp };
        for (unsigned x = 9U; x < 12U; ++x) map.tiles[Map2DTileKey(x, 6U, 0)].solid = true;
        map.tiles[Map2DTileKey(14U, 6U, 0)].collision = kb::assets::Map2DCollision{ .kind = kb::assets::Map2DCollisionKind::Solid, .trigger = true };

        // The shapes themselves: where the cells are in the plane of the map.
        {
            std::vector<kb::world2d::Map2DShape2D> pieces;
            std::string error;
            Require(kb::world2d::BuildMap2DShapes2D(map, pieces, &error), error.c_str());
            // Two stretches of floor, one wall column, one ledge row, one slope triangle, one block and one trigger. The wall and the
            // block join down into the floor row, which splits it.
            Require(pieces.size() == 7U, "equal neighbouring cells join into one shape");
            const auto bounds = [](const kb::world2d::Map2DShape2D& piece) {
                std::array<float, 4> box{ 1e9F, 1e9F, -1e9F, -1e9F };
                for (const auto& point : piece.points) {
                    box[0] = std::min(box[0], point[0]); box[1] = std::min(box[1], point[1]);
                    box[2] = std::max(box[2], point[0]); box[3] = std::max(box[3], point[1]);
                }
                return box;
            };
            bool floor = false, ledge = false, slope = false, trigger = false;
            for (const auto& piece : pieces) {
                const auto box = bounds(piece);
                if (Near(box[0], -8.0F, 0.001F) && Near(box[2], 1.0F, 0.001F) && Near(box[1], 0.0F, 0.001F) && Near(box[3], 1.0F, 0.001F)) floor = true;
                if (piece.oneWay && Near(box[0], -6.0F, 0.001F) && Near(box[2], -4.0F, 0.001F) && Near(box[3], 4.0F, 0.001F)) ledge = true;
                if (piece.points.size() == 3U && Near(box[0], 0.0F, 0.001F) && Near(box[2], 1.0F, 0.001F) && Near(box[1], 1.0F, 0.001F) && Near(box[3], 2.0F, 0.001F)) slope = true;
                if (piece.trigger && Near(box[0], 6.0F, 0.001F) && Near(box[2], 7.0F, 0.001F)) trigger = true;
            }
            Require(floor && ledge && slope && trigger, "the floor, the ledge, the slope and the trigger are where their cells are");
        }

        World world(true);
        auto& manager = world.scene.Assets().Manager();
        Require(manager.Mounts().Mount("Game", root), "mount the map assets");
        Require(manager.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>()), "register the mesh loader");
        const auto file = root / "Level.kbmap2d";
        Require(kb::assets::Map2DAssetIO::Save(file, map), "save the map");
        const kb::assets::AssetId source{ 90021U };
        Require(manager.RegisterAsset({ .id = source, .type = "RenderMesh", .virtualPath = "/Game/Level.kbmap2d", .physicalPath = file, .contentHash = 1U }), "register the map");
        const SceneEntity level = world.scene.Entities().CreateEntity({ .name = "Level" });
        kb::world2d::SetUpMap2DComponents(world.scene, level, source.value, map);
        const SceneEntity ball = world.Make("Ball", -6.5F, 3.0F);
        world.scene.Components().Bodies2D().Set(ball, Body2DComponent{});
        world.scene.Components().Colliders2D().Set(ball, Collider2DComponent{ .shape = 1, .radius = 0.5F });
        const SceneEntity under = world.Make("UnderTheLedge", -5.0F, 2.0F);
        world.scene.Components().Bodies2D().Set(under, Body2DComponent{ .startVelocityY = 13.0F });
        world.scene.Components().Colliders2D().Set(under, Collider2DComponent{ .shape = 1, .radius = 0.4F });
        const SceneEntity inside = world.Make("IntoTheZone", 6.5F, 6.0F);
        world.scene.Components().Bodies2D().Set(inside, Body2DComponent{});
        world.scene.Components().Colliders2D().Set(inside, Collider2DComponent{ .shape = 1, .radius = 0.3F });
        const SceneEntity hero = world.Make("Hero", -1.0F, 1.6F);
        world.scene.Components().CharacterControllers2D().Set(hero, CharacterController2DComponent{});

        bool zoneEnter = false;
        for (int frame = 0; frame < 30; ++frame) {
            static_cast<void>(world.scene.Runtime().Update(1.0F / 60.0F));
            for (const auto& event : kb::scene::PhysicsBackend::DrainPendingCollisionEvents(world.scene)) {
                zoneEnter = zoneEnter || (event.isTrigger && event.phase == kb::scene::PhysicsContactPhase::Enter && event.other == inside && event.target == level);
            }
        }
        Require(world.scene.Runtime().DrainSceneSystemErrors().empty(), "the map and the physics start without errors");
        Require(world.runtime->MapCount() == 1U && world.runtime->MapShapeCount() == 7U, "the map is seven static shapes of the physics");
        const auto down = world.runtime->Raycast(true, -7.5F, 10.0F, 0.0F, -1.0F, 50.0F, 65535U);
        Require(down.hit && down.entity == level && Near(down.y, 1.0F, 0.01F), "a ray down finds the floor of the map, owned by the map");
        const auto ledgeHit = world.runtime->Raycast(true, -4.5F, 10.0F, 0.0F, -1.0F, 50.0F, 65535U);
        Require(ledgeHit.hit && Near(ledgeHit.y, 4.0F, 0.02F), "a ray down finds the top of a ledge");

        // Walk the character onto the slope and the block behind it.
        kb::script::ScriptRuntimeHost host{ world.scene };
        Require(host.Functions().Call("Physics2D.MoveCharacter", std::vector<kb::script::ScriptFunctionArgument>{ { "entity", kb::script::ScriptValue{ hero.Id(), kb::script::ScriptValueType::Entity } },
                    { "x", kb::script::ScriptValue{ 3.0F } }, { "y", kb::script::ScriptValue{ 0.0F } } }, kb::script::ScriptFunctionCallContext{ .scene = &world.scene }).Succeeded(), "walk");
        for (int frame = 0; frame < 60 * 3; ++frame) {
            static_cast<void>(world.scene.Runtime().Update(1.0F / 60.0F));
            for (const auto& event : kb::scene::PhysicsBackend::DrainPendingCollisionEvents(world.scene)) {
                zoneEnter = zoneEnter || (event.isTrigger && event.phase == kb::scene::PhysicsContactPhase::Enter && event.other == inside && event.target == level);
            }
        }
        Require(Near(world.At(ball).y, 1.5F, 0.06F) && Near(world.At(ball).x, -6.5F, 0.3F), "a ball lands on the floor of the map");
        Require(Near(world.At(under).y, 4.4F, 0.08F) && world.At(under).x > -6.0F && world.At(under).x < -4.0F, "a ball thrown up from under a ledge passes through it and lands on top");
        Require(zoneEnter, "a ball falling into a trigger cell reports it, to the map and to the ball");
        Require(world.At(hero).x > 1.5F && world.At(hero).y > 2.3F && world.runtime->IsGrounded(hero), "a character walks up the slope of the map onto the block");

        // A cell changed while playing changes the shapes: the floor under the ball goes away.
        const SceneEntity dropper = world.Make("Dropper", 7.5F, 3.0F);
        world.scene.Components().Bodies2D().Set(dropper, Body2DComponent{});
        world.scene.Components().Colliders2D().Set(dropper, Collider2DComponent{ .shape = 1, .radius = 0.3F });
        std::string editError;
        auto* tilemaps = world.scene.Runtime().Tilemaps();
        Require(tilemaps != nullptr && tilemaps->SetTile(level, 15U, 7U, 0, kb::assets::Map2DTile{}, &editError), editError.c_str());
        world.Run(2.0F);
        Require(world.At(dropper).y < -2.0F, "a ball over a cell that was erased while playing falls through");
        Require(world.runtime->MapShapeCount() == 7U, "the other shapes of the map are still there");
        std::filesystem::remove_all(root);
    }

    // A flat map is a top-down plane: its walls stop a body and a character that move across it.
    {
        using kb::assets::Map2DTileKey;
        const auto root = std::filesystem::temp_directory_path() / "kb_world2d_physics_flat_map_tests";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        auto map = kb::assets::MakeEmptyMap2DAsset(16U, 8U);
        map.background = false;
        map.orientation = kb::assets::Map2DOrientation::Flat;
        for (unsigned x = 4U; x < 12U; ++x) map.tiles[Map2DTileKey(x, 2U, 0)].solid = true;
        World world(true);
        auto& manager = world.scene.Assets().Manager();
        Require(manager.Mounts().Mount("Game", root), "mount the flat map assets");
        Require(manager.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>()), "register the mesh loader");
        const auto file = root / "Flat.kbmap2d";
        Require(kb::assets::Map2DAssetIO::Save(file, map), "save the flat map");
        const kb::assets::AssetId source{ 90022U };
        Require(manager.RegisterAsset({ .id = source, .type = "RenderMesh", .virtualPath = "/Game/Flat.kbmap2d", .physicalPath = file, .contentHash = 1U }), "register the flat map");
        const SceneEntity level = world.scene.Entities().CreateEntity({ .name = "FlatLevel" });
        kb::world2d::SetUpMap2DComponents(world.scene, level, source.value, map);
        const SceneEntity ball = world.Make("Ball", 0.0F, 0.0F, -2.0F);
        world.scene.Components().Bodies2D().Set(ball, Body2DComponent{ .sideView = false, .startVelocityY = 3.0F });
        world.scene.Components().Colliders2D().Set(ball, Collider2DComponent{ .shape = 1, .radius = 0.25F, .sideView = false });
        const SceneEntity hero = world.Make("Hero", 2.0F, 0.0F, -2.0F);
        world.scene.Components().CharacterControllers2D().Set(hero, CharacterController2DComponent{ .useGravity = false, .sideView = false });
        world.Run(0.1F);
        Require(world.scene.Runtime().DrainSceneSystemErrors().empty(), "the flat map and the physics start without errors");
        Require(world.runtime->MapCount() == 1U && world.runtime->MapShapeCount() == 1U, "the flat map is one shape in the top-down world");
        const auto ahead = world.runtime->Raycast(false, -1.0F, -5.0F, 0.0F, 1.0F, 20.0F, 65535U);
        Require(ahead.hit && ahead.entity == level && Near(ahead.y, 1.0F, 0.02F), "a ray across the plane finds the wall of the flat map");
        Require(!world.runtime->Raycast(true, -1.0F, -5.0F, 0.0F, 1.0F, 20.0F, 65535U).hit, "the side-view plane does not see the flat map");
        kb::script::ScriptRuntimeHost host{ world.scene };
        Require(host.Functions().Call("Physics2D.MoveCharacter", std::vector<kb::script::ScriptFunctionArgument>{ { "entity", kb::script::ScriptValue{ hero.Id(), kb::script::ScriptValueType::Entity } },
                    { "x", kb::script::ScriptValue{ 0.0F } }, { "y", kb::script::ScriptValue{ 3.0F } } }, kb::script::ScriptFunctionCallContext{ .scene = &world.scene }).Succeeded(), "walk across the plane");
        world.Run(3.0F);
        Require(Near(world.At(ball).z, 0.75F, 0.1F) && Near(world.At(ball).y, 0.0F, 0.0001F), "a body stops at the wall of a flat map and keeps its height");
        Require(Near(world.At(hero).z, 0.5F, 0.12F) && Near(world.At(hero).x, 2.0F, 0.05F), "a character is stopped by the wall of a flat map");
        std::filesystem::remove_all(root);
    }

    // The components through the authoring table and the scene file.
    {
        kb::scene::Scene scene;
        const auto object = scene.Entities().CreateObject({ .name = "Thing" });
        kb::project::ProjectDescriptor project;
        project.plugins.push_back({ .name = "World.Map2D", .enabled = true });
        for (const char* id : { "Body2D", "Collider2D", "CharacterController2D" }) {
            Require(kb::scene::SceneComponentAuthoring::Add(scene, object.Entity(), id, project).status == kb::scene::SceneComponentAddStatus::Added, "the shared table adds a 2D physics component");
        }
        const auto document = kb::scene::SceneDocumentService::Capture(scene, "Thing");
        const auto root = std::filesystem::temp_directory_path() / "kb_world2d_physics_tests";
        std::filesystem::create_directories(root);
        Require(kb::scene::SceneDocumentService::Save(document, root / "Thing.21kbscene"), "save the scene");
        const auto loaded = kb::scene::SceneDocumentService::Load(root / "Thing.21kbscene");
        const auto& components = loaded.document.worldPrefab.Nodes().front().components;
        Require(loaded.succeeded && components.body2D && components.collider2D && components.characterController2D, "the scene file keeps the 2D physics components");
        std::filesystem::remove_all(root);
    }

    std::puts("kb_world2d_physics_tests passed");
    return 0;
}
