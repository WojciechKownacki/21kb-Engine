#include "Physics2DRuntime.hpp"

#include "Map2DCollision.hpp"
#include "engine/ecs/World.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/Body2DComponent.hpp"
#include "engine/scene/CharacterController2DComponent.hpp"
#include "engine/scene/Collider2DComponent.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/SceneTilemapRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/TilemapColliderComponent.hpp"

#include <box2d/box2d.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace kb::world2d {
namespace {

using kb::scene::Body2DComponent;
using kb::scene::CharacterController2DComponent;
using kb::scene::Collider2DComponent;
using kb::scene::SceneEntity;

constexpr float kPi = 3.14159265358979F;
constexpr int kSubSteps = 4;
constexpr float kGravity = 9.81F;

struct Pose {
    float x = 0.0F;
    float y = 0.0F;
    float angle = 0.0F;
    bool operator==(const Pose&) const = default;
};

[[nodiscard]] std::uint64_t Bits(std::uint32_t layer) noexcept { return 1ULL << layer; }

// Ledges of a tilemap that can be jumped up through carry this category instead of a layer; a mask that takes layer 0 takes them too.
constexpr std::uint64_t kOneWayBit = 1ULL << 20U;
[[nodiscard]] std::uint64_t WithOneWay(std::uint64_t mask) noexcept { return (mask & 1ULL) != 0U ? (mask | kOneWayBit) : mask; }

[[nodiscard]] Pose ReadPose(kb::scene::Scene& scene, SceneEntity entity, bool sideView) {
    const auto transform = scene.Transforms().Get(entity);
    const kb::math::Vec3 euler = kb::math::ToEulerDegrees(transform.worldRotation);
    if (sideView) return { transform.worldPosition.x, transform.worldPosition.y, euler.z * kPi / 180.0F };
    return { transform.worldPosition.x, transform.worldPosition.z, -euler.y * kPi / 180.0F };
}

[[nodiscard]] kb::math::Vec2 ScaleOf(kb::scene::Scene& scene, SceneEntity entity, bool sideView) {
    const auto transform = scene.Transforms().Get(entity);
    return sideView ? kb::math::Vec2{ std::abs(transform.worldScale.x), std::abs(transform.worldScale.y) }
                    : kb::math::Vec2{ std::abs(transform.worldScale.x), std::abs(transform.worldScale.z) };
}

// Writes a pose in the plane back to the entity, keeping the position across the plane and the other turns, relative to its parent.
void WritePose(kb::scene::Scene& scene, SceneEntity entity, bool sideView, const Pose& pose) {
    auto transform = scene.Transforms().Get(entity);
    kb::math::Vec3 world = transform.worldPosition;
    kb::math::Vec3 euler = kb::math::ToEulerDegrees(transform.worldRotation);
    if (sideView) {
        world.x = pose.x;
        world.y = pose.y;
        euler.z = pose.angle * 180.0F / kPi;
    } else {
        world.x = pose.x;
        world.z = pose.y;
        euler.y = -pose.angle * 180.0F / kPi;
    }
    const kb::math::Quat rotation = kb::math::FromEulerDegrees(euler);
    const SceneEntity parent = scene.Hierarchy().Parent(entity);
    if (parent.IsValid()) {
        const auto parentTransform = scene.Transforms().Get(parent);
        const kb::math::Quat inverse = kb::math::Inverse(parentTransform.worldRotation);
        kb::math::Vec3 local = kb::math::Rotate(inverse, world - parentTransform.worldPosition);
        const auto divide = [](float value, float scale) { return std::abs(scale) > 0.00001F ? value / scale : value; };
        local = { divide(local.x, parentTransform.worldScale.x), divide(local.y, parentTransform.worldScale.y), divide(local.z, parentTransform.worldScale.z) };
        transform.localPosition = local;
        transform.localRotation = inverse * rotation;
    } else {
        transform.localPosition = world;
        transform.localRotation = rotation;
    }
    scene.Transforms().Set(entity, transform);
}

[[nodiscard]] b2Vec2 ToB2(float x, float y) noexcept { return b2Vec2{ x, y }; }

[[nodiscard]] void* UserData(SceneEntity entity) noexcept { return reinterpret_cast<void*>(static_cast<std::uintptr_t>(entity.Id())); }
[[nodiscard]] SceneEntity EntityOf(void* data) noexcept { return SceneEntity{ static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(data)) }; }

} // namespace

struct Physics2DRuntime::State {
    struct ShapeRec {
        b2ShapeId id = b2_nullShapeId;
        Collider2DComponent applied{};
        Pose local{};
        kb::math::Vec2 scale{ 1.0F, 1.0F };
    };
    struct BodyRec {
        b2BodyId id = b2_nullBodyId;
        bool sideView = true;
        bool hasComponent = false;
        Body2DComponent applied{};
        Pose lastWritten{};
        bool started = false;
        std::map<SceneEntity, ShapeRec> shapes;
    };
    struct CharacterRec {
        b2BodyId body = b2_nullBodyId;
        b2ShapeId shape = b2_nullShapeId;
        CharacterController2DComponent applied{};
        kb::math::Vec2 scale{ 1.0F, 1.0F };
        Pose pose{};
        Pose lastWritten{};
        bool started = false;
        float moveX = 0.0F;
        float moveY = 0.0F;
        float velocityX = 0.0F;
        float velocityY = 0.0F;
        bool grounded = false;
        bool sideView = true;
    };
    struct MapRec {
        b2BodyId body = b2_nullBodyId;
        bool sideView = true;
        const void* map = nullptr;
        std::uint64_t revision = 0U;
        Pose pose{};
        std::vector<b2ShapeId> shapes;
    };
    struct JointRec {
        b2JointId id = b2_nullJointId;
        bool sideView = true;
    };
    b2WorldId worlds[2] = { b2_nullWorldId, b2_nullWorldId };   // 0: side view (X, Y), 1: top down (X, Z)
    b2BodyId grounds[2] = { b2_nullBodyId, b2_nullBodyId };     // the world, for joints to it
    std::map<std::uint32_t, JointRec> joints;
    std::uint32_t nextJoint = 1U;
    std::map<SceneEntity, BodyRec> bodies;
    std::map<SceneEntity, CharacterRec> characters;
    std::map<SceneEntity, MapRec> maps;
    std::set<std::uint64_t> oneWayShapes;   // b2StoreShapeId of the ledges

    [[nodiscard]] b2WorldId World(bool sideView) {
        b2WorldId& world = worlds[sideView ? 0 : 1];
        if (!b2World_IsValid(world)) {
            b2WorldDef definition = b2DefaultWorldDef();
            definition.gravity = ToB2(0.0F, sideView ? -kGravity : 0.0F);
            world = b2CreateWorld(&definition);
            b2World_SetPreSolveCallback(world, &PreSolve, this);
        }
        return world;
    }
    [[nodiscard]] b2BodyId Ground(bool sideView) {
        b2BodyId& ground = grounds[sideView ? 0 : 1];
        if (!b2Body_IsValid(ground)) {
            const b2BodyDef definition = b2DefaultBodyDef();
            ground = b2CreateBody(World(sideView), &definition);
        }
        return ground;
    }
    void PurgeJoints() {
        for (auto it = joints.begin(); it != joints.end();) {
            if (b2Joint_IsValid(it->second.id)) { ++it; continue; }
            it = joints.erase(it);
        }
    }
    [[nodiscard]] b2WorldId WorldIfAny(bool sideView) const {
        const b2WorldId world = worlds[sideView ? 0 : 1];
        return b2World_IsValid(world) ? world : b2_nullWorldId;
    }

    void ReleaseBody(BodyRec& body) {
        if (b2Body_IsValid(body.id)) b2DestroyBody(body.id);
        body.id = b2_nullBodyId;
        body.shapes.clear();
    }
    // A ledge only stops what comes from above it.
    static bool PreSolve(b2ShapeId first, b2ShapeId second, b2Vec2 point, b2Vec2 normal, void* context) {
        static_cast<void>(point);
        const State& state = *static_cast<const State*>(context);
        const bool firstLedge = state.oneWayShapes.contains(b2StoreShapeId(first));
        const bool secondLedge = state.oneWayShapes.contains(b2StoreShapeId(second));
        if (!firstLedge && !secondLedge) return true;
        // The normal points from the first shape to the second.
        return (firstLedge ? normal.y : -normal.y) > 0.7F;
    }
    void ReleaseMap(MapRec& map) {
        for (const b2ShapeId shape : map.shapes) oneWayShapes.erase(b2StoreShapeId(shape));
        map.shapes.clear();
        if (b2Body_IsValid(map.body)) b2DestroyBody(map.body);
        map.body = b2_nullBodyId;
    }
    void ReleaseCharacter(CharacterRec& character) {
        if (b2Body_IsValid(character.body)) b2DestroyBody(character.body);
        character.body = b2_nullBodyId;
        character.shape = b2_nullShapeId;
    }
    void ReleaseAll() {
        for (auto& [entity, body] : bodies) { static_cast<void>(entity); ReleaseBody(body); }
        bodies.clear();
        for (auto& [entity, character] : characters) { static_cast<void>(entity); ReleaseCharacter(character); }
        characters.clear();
        for (auto& [entity, map] : maps) { static_cast<void>(entity); ReleaseMap(map); }
        maps.clear();
        oneWayShapes.clear();
        joints.clear();
        for (b2BodyId& ground : grounds) ground = b2_nullBodyId;
        for (b2WorldId& world : worlds) {
            if (b2World_IsValid(world)) b2DestroyWorld(world);
            world = b2_nullWorldId;
        }
    }
};

namespace {

[[nodiscard]] b2BodyType TypeOf(std::int32_t type) noexcept {
    return type == 0 ? b2_staticBody : (type == 1 ? b2_kinematicBody : b2_dynamicBody);
}

void ApplyBodySettings(b2BodyId id, const Body2DComponent& value, const Body2DComponent* previous) {
    if (previous == nullptr || previous->type != value.type) b2Body_SetType(id, TypeOf(value.type));
    b2Body_SetGravityScale(id, value.gravityScale);
    b2Body_SetLinearDamping(id, value.linearDamping);
    b2Body_SetAngularDamping(id, value.angularDamping);
    b2Body_SetBullet(id, value.bullet);
    b2Body_EnableSleep(id, value.allowSleep);
    b2MotionLocks locks{};
    locks.angularZ = value.fixedRotation;
    b2Body_SetMotionLocks(id, locks);
}

[[nodiscard]] b2ShapeDef MakeShapeDef(const Collider2DComponent& value, SceneEntity entity) {
    b2ShapeDef definition = b2DefaultShapeDef();
    definition.userData = UserData(entity);
    definition.density = value.density;
    definition.material.friction = value.friction;
    definition.material.restitution = value.restitution;
    definition.filter.categoryBits = Bits(static_cast<std::uint32_t>(value.layer));
    definition.filter.maskBits = WithOneWay(value.collidesWith);
    definition.isSensor = value.isTrigger;
    definition.enableSensorEvents = true;
    definition.enableContactEvents = true;
    return definition;
}

[[nodiscard]] b2ShapeId CreateShape(b2BodyId body, const Collider2DComponent& value, SceneEntity entity, const Pose& local, kb::math::Vec2 scale) {
    const b2ShapeDef definition = MakeShapeDef(value, entity);
    const float cosine = std::cos(local.angle);
    const float sine = std::sin(local.angle);
    const float offsetX = value.offsetX * scale.x;
    const float offsetY = value.offsetY * scale.y;
    const b2Vec2 center = ToB2(local.x + cosine * offsetX - sine * offsetY, local.y + sine * offsetX + cosine * offsetY);
    const float turn = local.angle + value.angleDegrees * kPi / 180.0F;
    if (value.shape == 1) {
        const b2Circle circle{ center, value.radius * std::max(scale.x, scale.y) };
        return b2CreateCircleShape(body, &definition, &circle);
    }
    if (value.shape == 2) {
        const float radius = value.radius * scale.x;
        const float half = std::max(0.0F, value.height * scale.y * 0.5F - radius);
        const b2Vec2 up = ToB2(-std::sin(turn), std::cos(turn));
        const b2Capsule capsule{ ToB2(center.x - up.x * half, center.y - up.y * half), ToB2(center.x + up.x * half, center.y + up.y * half), radius };
        return b2CreateCapsuleShape(body, &definition, &capsule);
    }
    const b2Polygon box = b2MakeOffsetBox(std::max(0.0001F, value.width * scale.x * 0.5F), std::max(0.0001F, value.height * scale.y * 0.5F), center, b2MakeRot(turn));
    return b2CreatePolygonShape(body, &definition, &box);
}

[[nodiscard]] b2Capsule CharacterCapsule(const CharacterController2DComponent& value, kb::math::Vec2 scale, float x, float y) {
    const float radius = value.radius * scale.x;
    const float half = std::max(0.0F, value.height * scale.y * 0.5F - radius);
    return b2Capsule{ ToB2(x, y - half), ToB2(x, y + half), radius };
}

} // namespace

Physics2DRuntime::Physics2DRuntime() : state_(std::make_unique<State>()) {}
Physics2DRuntime::~Physics2DRuntime() { state_->ReleaseAll(); }

void Physics2DRuntime::OnCreate(kb::scene::SceneSystemContext& context) {
    context.GetScene().Runtime().SetPhysics2DRuntime(this);
}

void Physics2DRuntime::OnDestroy(kb::scene::SceneSystemContext& context) {
    state_->ReleaseAll();
    auto runtime = context.GetScene().Runtime();
    if (runtime.Physics2D() == this) runtime.SetPhysics2DRuntime(nullptr);
}

std::size_t Physics2DRuntime::BodyCount() const noexcept { return state_->bodies.size(); }
std::size_t Physics2DRuntime::CharacterCount() const noexcept { return state_->characters.size(); }
std::size_t Physics2DRuntime::JointCount() const noexcept { return state_->joints.size(); }
std::size_t Physics2DRuntime::MapCount() const noexcept { return state_->maps.size(); }
std::size_t Physics2DRuntime::MapShapeCount() const noexcept {
    std::size_t count = 0U;
    for (const auto& [entity, map] : state_->maps) { static_cast<void>(entity); count += map.shapes.size(); }
    return count;
}
std::size_t Physics2DRuntime::ShapeCount() const noexcept {
    std::size_t count = 0U;
    for (const auto& [entity, body] : state_->bodies) { static_cast<void>(entity); count += body.shapes.size(); }
    return count + state_->characters.size();
}

// The simulation only steps while playing; the end of play is noticed here, where updates still run.
void Physics2DRuntime::OnUpdate(kb::scene::SceneSystemContext& context) {
    if (!context.GetScene().Runtime().IsPlaying()) state_->ReleaseAll();
}

void Physics2DRuntime::OnFixedUpdate(kb::scene::SceneSystemContext& context) {
    auto& scene = context.GetScene();
    State& state = *state_;
    if (!scene.Runtime().IsPlaying()) {
        state.ReleaseAll();
        return;
    }
    const float step = context.DeltaSeconds();
    const auto query = std::as_const(scene).Components();

    // What the scene holds now.
    struct BodyEntry { SceneEntity entity; Body2DComponent value; };
    struct ColliderEntry { SceneEntity entity; Collider2DComponent value; };
    struct CharacterEntry { SceneEntity entity; CharacterController2DComponent value; };
    std::vector<BodyEntry> bodyEntries;
    std::vector<ColliderEntry> colliderEntries;
    std::vector<CharacterEntry> characterEntries;
    context.EcsWorld().CreateQuery<Body2DComponent>().ForEach(
        [](SceneEntity entity, const Body2DComponent& value, void* output) { static_cast<std::vector<BodyEntry>*>(output)->push_back({ entity, value }); }, &bodyEntries);
    context.EcsWorld().CreateQuery<Collider2DComponent>().ForEach(
        [](SceneEntity entity, const Collider2DComponent& value, void* output) { static_cast<std::vector<ColliderEntry>*>(output)->push_back({ entity, value }); }, &colliderEntries);
    context.EcsWorld().CreateQuery<CharacterController2DComponent>().ForEach(
        [](SceneEntity entity, const CharacterController2DComponent& value, void* output) { static_cast<std::vector<CharacterEntry>*>(output)->push_back({ entity, value }); }, &characterEntries);

    // Who owns each collider: its own Body 2D, the nearest parent's, or a static body of its own.
    const auto bodyOf = [&](SceneEntity entity) -> SceneEntity {
        for (SceneEntity at = entity; at.IsValid(); at = scene.Hierarchy().Parent(at)) {
            if (query.Bodies2D().Has(at)) return at;
        }
        return entity;
    };

    std::set<SceneEntity> liveOwners;
    std::set<SceneEntity> liveCharacters;
    for (const CharacterEntry& entry : characterEntries) {
        if (entry.value.enabled && kb::scene::IsCharacterController2DComponentValid(entry.value) && scene.Entities().IsActive(entry.entity)) liveCharacters.insert(entry.entity);
    }

    // Bodies.
    for (const BodyEntry& entry : bodyEntries) {
        if (!entry.value.enabled || !kb::scene::IsBody2DComponentValid(entry.value) || !scene.Entities().IsActive(entry.entity) || liveCharacters.contains(entry.entity)) continue;
        liveOwners.insert(entry.entity);
        State::BodyRec& body = state.bodies[entry.entity];
        const bool sideView = entry.value.sideView;
        // A static body of a lone collider becomes a real body when the component is added.
        if (b2Body_IsValid(body.id) && (body.sideView != sideView || !body.hasComponent)) state.ReleaseBody(body);
        const Pose pose = ReadPose(scene, entry.entity, sideView);
        if (!b2Body_IsValid(body.id)) {
            b2BodyDef definition = b2DefaultBodyDef();
            definition.type = TypeOf(entry.value.type);
            definition.position = ToB2(pose.x, pose.y);
            definition.rotation = b2MakeRot(pose.angle);
            definition.linearVelocity = ToB2(entry.value.startVelocityX, entry.value.startVelocityY);
            definition.userData = UserData(entry.entity);
            body.id = b2CreateBody(state.World(sideView), &definition);
            body.sideView = sideView;
            body.hasComponent = true;
            body.applied = Body2DComponent{};
            body.started = false;
            ApplyBodySettings(body.id, entry.value, nullptr);
            body.applied = entry.value;
        } else if (!(body.applied == entry.value)) {
            ApplyBodySettings(body.id, entry.value, &body.applied);
            body.applied = entry.value;
        }
        // Moved from outside: the entity is not where the simulation left it.
        if (body.started && !(pose == body.lastWritten)) b2Body_SetTransform(body.id, ToB2(pose.x, pose.y), b2MakeRot(pose.angle));
        body.started = true;
    }

    // Colliders, onto their body (or a static body of their own).
    std::set<std::pair<SceneEntity, SceneEntity>> liveShapes;
    for (const ColliderEntry& entry : colliderEntries) {
        if (!entry.value.enabled || !kb::scene::IsCollider2DComponentValid(entry.value) || !scene.Entities().IsActive(entry.entity)) continue;
        const SceneEntity owner = bodyOf(entry.entity);
        const bool ownerHasBody = query.Bodies2D().Has(owner);
        if (liveCharacters.contains(entry.entity)) continue;
        if (ownerHasBody && !liveOwners.contains(owner)) continue;
        bool sideView = entry.value.sideView;
        if (ownerHasBody) sideView = state.bodies[owner].sideView;
        State::BodyRec& body = state.bodies[owner];
        if (!ownerHasBody) {
            liveOwners.insert(owner);
            // The body of a removed Body 2D is released; its colliders stay as static shapes.
            if (b2Body_IsValid(body.id) && (body.sideView != sideView || body.hasComponent)) state.ReleaseBody(body);
            if (!b2Body_IsValid(body.id)) {
                const Pose pose = ReadPose(scene, owner, sideView);
                b2BodyDef definition = b2DefaultBodyDef();
                definition.type = b2_staticBody;
                definition.position = ToB2(pose.x, pose.y);
                definition.rotation = b2MakeRot(pose.angle);
                definition.userData = UserData(owner);
                body.id = b2CreateBody(state.World(sideView), &definition);
                body.sideView = sideView;
                body.hasComponent = false;
                body.lastWritten = pose;
                body.started = true;
            } else {
                const Pose pose = ReadPose(scene, owner, sideView);
                if (!(pose == body.lastWritten)) {
                    b2Body_SetTransform(body.id, ToB2(pose.x, pose.y), b2MakeRot(pose.angle));
                    body.lastWritten = pose;
                }
            }
        }
        if (!b2Body_IsValid(body.id)) continue;
        // The shape's place on the body.
        Pose local{};
        if (entry.entity != owner) {
            const Pose bodyPose = ReadPose(scene, owner, sideView);
            const Pose shapePose = ReadPose(scene, entry.entity, sideView);
            const float cosine = std::cos(-bodyPose.angle);
            const float sine = std::sin(-bodyPose.angle);
            const float dx = shapePose.x - bodyPose.x;
            const float dy = shapePose.y - bodyPose.y;
            local = { cosine * dx - sine * dy, sine * dx + cosine * dy, shapePose.angle - bodyPose.angle };
        }
        const kb::math::Vec2 scale = ScaleOf(scene, entry.entity, sideView);
        liveShapes.insert({ owner, entry.entity });
        State::ShapeRec& shape = body.shapes[entry.entity];
        const bool same = b2Shape_IsValid(shape.id) && shape.applied == entry.value && shape.local == local && shape.scale.x == scale.x && shape.scale.y == scale.y;
        if (same) continue;
        if (b2Shape_IsValid(shape.id)) b2DestroyShape(shape.id, false);
        shape.id = CreateShape(body.id, entry.value, entry.entity, local, scale);
        shape.applied = entry.value;
        shape.local = local;
        shape.scale = scale;
        b2Body_ApplyMassFromShapes(body.id);
    }

    // Tilemaps: the collision of a map as static shapes of its plane, rebuilt when the cells, the cell size or the place change.
    std::set<SceneEntity> liveMaps;
    if (kb::scene::SceneTilemapRuntime* tilemaps = scene.Runtime().Tilemaps(); tilemaps != nullptr) {
        std::vector<SceneEntity> mapEntities;
        context.EcsWorld().CreateQuery<kb::scene::TilemapColliderComponent>().ForEach(
            [](SceneEntity entity, const kb::scene::TilemapColliderComponent& value, void* output) {
                if (value.enabled) static_cast<std::vector<SceneEntity>*>(output)->push_back(entity);
            }, &mapEntities);
        for (const SceneEntity entity : mapEntities) {
            if (!scene.Entities().IsActive(entity)) continue;
            const kb::assets::Map2DAsset* map = tilemaps->Map(entity);
            if (map == nullptr) continue;
            liveMaps.insert(entity);
            State::MapRec& record = state.maps[entity];
            const bool sideView = map->orientation == kb::assets::Map2DOrientation::Wall;
            const Pose pose = ReadPose(scene, entity, sideView);
            const std::uint64_t revision = tilemaps->Revision(entity);
            if (!b2Body_IsValid(record.body) || record.sideView != sideView || record.map != map || record.revision != revision) {
                state.ReleaseMap(record);
                std::vector<Map2DShape2D> pieces;
                std::string error;
                if (!BuildMap2DShapes2D(*map, pieces, &error)) {
                    context.ReportError("Physics2D: " + error);
                    record.map = map;
                    record.revision = revision;
                    record.sideView = sideView;
                    continue;
                }
                b2BodyDef definition = b2DefaultBodyDef();
                definition.type = b2_staticBody;
                definition.position = ToB2(pose.x, pose.y);
                definition.rotation = b2MakeRot(pose.angle);
                definition.userData = UserData(entity);
                record.body = b2CreateBody(state.World(sideView), &definition);
                for (const Map2DShape2D& piece : pieces) {
                    // A ledge needs a side view to mean anything.
                    if (piece.oneWay && !sideView) continue;
                    std::vector<b2Vec2> points;
                    points.reserve(piece.points.size());
                    for (const auto& point : piece.points) points.push_back(ToB2(point[0], point[1]));
                    const b2Hull hull = b2ComputeHull(points.data(), static_cast<int>(points.size()));
                    if (hull.count < 3) continue;
                    const b2Polygon polygon = b2MakePolygon(&hull, 0.0F);
                    b2ShapeDef shapeDefinition = b2DefaultShapeDef();
                    shapeDefinition.userData = UserData(entity);
                    shapeDefinition.material.friction = piece.friction;
                    shapeDefinition.material.restitution = piece.restitution;
                    shapeDefinition.filter.categoryBits = piece.oneWay ? kOneWayBit : Bits(0U);
                    shapeDefinition.filter.maskBits = ~0ULL;
                    shapeDefinition.isSensor = piece.trigger;
                    shapeDefinition.enableSensorEvents = true;
                    shapeDefinition.enableContactEvents = true;
                    shapeDefinition.enablePreSolveEvents = piece.oneWay;
                    const b2ShapeId shape = b2CreatePolygonShape(record.body, &shapeDefinition, &polygon);
                    record.shapes.push_back(shape);
                    if (piece.oneWay) state.oneWayShapes.insert(b2StoreShapeId(shape));
                }
                record.sideView = sideView;
                record.map = map;
                record.revision = revision;
                record.pose = pose;
            } else if (!(pose == record.pose)) {
                b2Body_SetTransform(record.body, ToB2(pose.x, pose.y), b2MakeRot(pose.angle));
                record.pose = pose;
            }
        }
    }
    for (auto it = state.maps.begin(); it != state.maps.end();) {
        if (liveMaps.contains(it->first) && scene.Entities().IsAlive(it->first)) { ++it; continue; }
        state.ReleaseMap(it->second);
        it = state.maps.erase(it);
    }

    // Shapes, bodies and characters that went away.
    for (auto it = state.bodies.begin(); it != state.bodies.end();) {
        State::BodyRec& body = it->second;
        for (auto shape = body.shapes.begin(); shape != body.shapes.end();) {
            if (liveShapes.contains({ it->first, shape->first })) { ++shape; continue; }
            if (b2Shape_IsValid(shape->second.id)) {
                b2DestroyShape(shape->second.id, false);
                if (b2Body_IsValid(body.id)) b2Body_ApplyMassFromShapes(body.id);
            }
            shape = body.shapes.erase(shape);
        }
        if (liveOwners.contains(it->first) && scene.Entities().IsAlive(it->first)) { ++it; continue; }
        state.ReleaseBody(body);
        it = state.bodies.erase(it);
    }
    for (auto it = state.characters.begin(); it != state.characters.end();) {
        if (liveCharacters.contains(it->first) && scene.Entities().IsAlive(it->first)) { ++it; continue; }
        state.ReleaseCharacter(it->second);
        it = state.characters.erase(it);
    }

    // Characters: a kinematic body the other bodies and triggers see, moved by the character mover.
    for (const CharacterEntry& entry : characterEntries) {
        if (!liveCharacters.contains(entry.entity)) continue;
        State::CharacterRec& character = state.characters[entry.entity];
        const CharacterController2DComponent& value = entry.value;
        const bool sideView = value.sideView;
        const kb::math::Vec2 scale = ScaleOf(scene, entry.entity, sideView);
        const bool changed = !b2Body_IsValid(character.body) || character.sideView != sideView || !(character.applied == value) ||
            character.scale.x != scale.x || character.scale.y != scale.y;
        const Pose current = ReadPose(scene, entry.entity, sideView);
        if (changed) {
            const Pose keep = character.started && b2Body_IsValid(character.body) && character.sideView == sideView ? character.pose : current;
            state.ReleaseCharacter(character);
            character.sideView = sideView;
            character.applied = value;
            character.scale = scale;
            character.pose = keep;
            b2BodyDef definition = b2DefaultBodyDef();
            definition.type = b2_kinematicBody;
            definition.position = ToB2(keep.x, keep.y);
            definition.userData = UserData(entry.entity);
            character.body = b2CreateBody(state.World(sideView), &definition);
            b2ShapeDef shapeDefinition = b2DefaultShapeDef();
            shapeDefinition.userData = UserData(entry.entity);
            shapeDefinition.filter.categoryBits = Bits(static_cast<std::uint32_t>(value.layer));
            shapeDefinition.filter.maskBits = value.collidesWith;
            shapeDefinition.enableSensorEvents = true;
            shapeDefinition.enableContactEvents = true;
            const b2Capsule capsule = CharacterCapsule(value, scale, 0.0F, 0.0F);
            character.shape = b2CreateCapsuleShape(character.body, &shapeDefinition, &capsule);
        }
        if (character.started && !(current == character.lastWritten)) {
            character.pose = current;
            character.velocityY = 0.0F;
        }
        character.started = true;
    }

    // The characters walk: collide and slide, as Box2D's character mover.
    for (auto& [entity, character] : state.characters) {
        const CharacterController2DComponent& value = character.applied;
        const b2WorldId world = state.WorldIfAny(character.sideView);
        if (!b2World_IsValid(world) || !b2Body_IsValid(character.body)) continue;
        const bool gravity = value.useGravity && character.sideView;
        if (gravity) character.velocityY -= kGravity * value.gravityScale * step;
        if (!character.sideView) character.velocityY = 0.0F;
        const float desiredX = character.moveX;
        const float desiredY = character.sideView ? character.velocityY : character.moveY;
        b2Body_Disable(character.body);
        // A ledge of a map stops a character that stands on it or falls onto it, never one that is rising through it or that
        // stands beside it lower than its top.
        bool throughLedges = !character.sideView || character.velocityY > 0.01F;
        if (!throughLedges) {
            const float radius = value.radius * character.scale.x;
            const float halfHeight = std::max(radius, value.height * character.scale.y * 0.5F);
            const float feet = character.pose.y - halfHeight;
            const float reach = std::abs(desiredX * step) + radius + 0.1F;
            const b2AABB area{ ToB2(character.pose.x - reach, feet - 0.3F), ToB2(character.pose.x + reach, character.pose.y + halfHeight) };
            struct Ledges { float feet = 0.0F; bool below = false; };
            Ledges ledges{ feet, false };
            b2World_OverlapAABB(world, area, b2QueryFilter{ Bits(static_cast<std::uint32_t>(value.layer)), kOneWayBit },
                [](b2ShapeId shape, void* context) {
                    auto* found = static_cast<Ledges*>(context);
                    if (b2Shape_GetAABB(shape).upperBound.y > found->feet + 0.08F) found->below = true;
                    return true;
                }, &ledges);
            throughLedges = ledges.below;
        }
        const b2QueryFilter collide{ Bits(static_cast<std::uint32_t>(value.layer)), throughLedges ? value.collidesWith : WithOneWay(value.collidesWith) };
        const float slopeCosine = std::cos(value.slopeLimitDegrees * kPi / 180.0F);
        struct Planes {
            std::vector<b2CollisionPlane> planes;
            bool walkable = false;
            float slopeCosine = 0.0F;
        };
        const auto gather = [](b2ShapeId shape, const b2PlaneResult* result, void* context) {
            static_cast<void>(shape);
            auto* planes = static_cast<Planes*>(context);
            if (planes->planes.size() < 16U) {
                planes->planes.push_back(b2CollisionPlane{ result->plane, FLT_MAX, 0.0F, true });
                if (result->plane.normal.y >= planes->slopeCosine) planes->walkable = true;
            }
            return true;
        };
        const auto run = [&](Pose& at, b2Vec2 target, bool sideViewPlane, Planes& planes) {
            for (int iteration = 0; iteration < 5; ++iteration) {
                planes.planes.clear();
                planes.walkable = false;
                const b2Capsule mover = CharacterCapsule(value, character.scale, at.x, at.y);
                b2World_CollideMover(world, &mover, collide, gather, &planes);
                const b2PlaneSolverResult solved = b2SolvePlanes(ToB2(target.x - at.x, target.y - at.y), planes.planes.data(), static_cast<int>(planes.planes.size()));
                const float fraction = b2World_CastMover(world, &mover, solved.translation, collide);
                const b2Vec2 delta = ToB2(solved.translation.x * fraction, solved.translation.y * fraction);
                at.x += delta.x;
                at.y += delta.y;
                if (delta.x * delta.x + delta.y * delta.y < 0.0001F) break;
            }
            static_cast<void>(sideViewPlane);
        };
        Planes planes;
        planes.slopeCosine = slopeCosine;
        Pose at = character.pose;
        const Pose before = at;
        run(at, ToB2(at.x + desiredX * step, at.y + desiredY * step), character.sideView, planes);
        // A ledge in the way: lift by the step height, move, and settle down again.
        const float wantedX = desiredX * step;
        if (character.sideView && std::abs(wantedX) > 0.0001F && value.stepHeight > 0.0F && std::abs(at.x - before.x) < std::abs(wantedX) * 0.5F && character.grounded) {
            Pose lifted = before;
            const b2Capsule probe = CharacterCapsule(value, character.scale, lifted.x, lifted.y);
            const float room = b2World_CastMover(world, &probe, ToB2(0.0F, value.stepHeight), collide) * value.stepHeight;
            lifted.y += room;
            Planes liftedPlanes;
            liftedPlanes.slopeCosine = slopeCosine;
            run(lifted, ToB2(lifted.x + wantedX, lifted.y), true, liftedPlanes);
            if (std::abs(lifted.x - before.x) > std::abs(at.x - before.x) + 0.001F) {
                const b2Capsule down = CharacterCapsule(value, character.scale, lifted.x, lifted.y);
                const float drop = b2World_CastMover(world, &down, ToB2(0.0F, -(room + 0.02F)), collide) * (room + 0.02F);
                lifted.y -= drop;
                at = lifted;
            }
        }
        // Standing: something walkable within a skin below.
        const b2Capsule feet = CharacterCapsule(value, character.scale, at.x, at.y);
        Planes ground;
        ground.slopeCosine = slopeCosine;
        const b2Capsule lowered = CharacterCapsule(value, character.scale, at.x, at.y - 0.03F);
        b2World_CollideMover(world, &lowered, collide, gather, &ground);
        static_cast<void>(feet);
        const bool wasGrounded = character.grounded;
        character.grounded = character.sideView && ground.walkable && character.velocityY <= 0.01F;
        if (character.grounded) character.velocityY = 0.0F;
        else if (character.sideView && !wasGrounded && !planes.planes.empty()) {
            // Hitting a ceiling stops the rise.
            for (const b2CollisionPlane& plane : planes.planes) {
                if (plane.plane.normal.y < -0.7F && character.velocityY > 0.0F) character.velocityY = 0.0F;
            }
        }
        character.pose = at;
        b2Body_Enable(character.body);
        b2Body_SetTransform(character.body, ToB2(at.x, at.y), b2MakeRot(0.0F));
        const float inverse = step > 0.0F ? 1.0F / step : 0.0F;
        b2Body_SetLinearVelocity(character.body, ToB2((at.x - before.x) * inverse, (at.y - before.y) * inverse));
        Pose written = ReadPose(scene, entity, character.sideView);
        written.x = at.x;
        written.y = at.y;
        WritePose(scene, entity, character.sideView, written);
        character.lastWritten = ReadPose(scene, entity, character.sideView);
        character.pose = character.lastWritten;
        character.pose.angle = 0.0F;
    }

    state.PurgeJoints();

    // Step the worlds.
    for (b2WorldId& world : state.worlds) {
        if (b2World_IsValid(world)) b2World_Step(world, step, kSubSteps);
    }

    // Bodies back to the transforms.
    for (auto& [entity, body] : state.bodies) {
        if (!b2Body_IsValid(body.id)) continue;
        const b2Vec2 position = b2Body_GetPosition(body.id);
        const float angle = b2Rot_GetAngle(b2Body_GetRotation(body.id));
        Pose pose{ position.x, position.y, angle };
        if (body.hasComponent && body.applied.type != 0) {
            WritePose(scene, entity, body.sideView, pose);
            body.lastWritten = ReadPose(scene, entity, body.sideView);
        } else {
            body.lastWritten = ReadPose(scene, entity, body.sideView);
        }
    }

    // Contacts and triggers to the scripts.
    const bool deliver = kb::scene::PhysicsBackend::HasCollisionEventConsumer(scene);
    const auto queue = [&](b2ShapeId first, b2ShapeId second, bool trigger, kb::scene::PhysicsContactPhase phase, b2Vec2 point, b2Vec2 normal, bool sideView) {
        if (!deliver || !b2Shape_IsValid(first) || !b2Shape_IsValid(second)) return;
        const SceneEntity a = EntityOf(b2Shape_GetUserData(first));
        const SceneEntity b = EntityOf(b2Shape_GetUserData(second));
        if (!a.IsValid() || !b.IsValid() || a == b) return;
        const auto place = [&](float x, float y) { return sideView ? kb::math::Vec3{ x, y, 0.0F } : kb::math::Vec3{ x, 0.0F, y }; };
        for (int side = 0; side < 2; ++side) {
            kb::scene::PendingCollisionEvent event;
            event.target = side == 0 ? a : b;
            event.other = side == 0 ? b : a;
            event.point = place(point.x, point.y);
            const float sign = side == 0 ? 1.0F : -1.0F;
            event.normal = place(normal.x * sign, normal.y * sign);
            event.worldPoint = kb::math::DVec3{ event.point.x, event.point.y, event.point.z };
            event.isTrigger = trigger;
            event.phase = phase;
            kb::scene::PhysicsBackend::QueueCollisionEvent(scene, event);
        }
    };
    for (int plane = 0; plane < 2; ++plane) {
        const b2WorldId world = state.worlds[plane];
        if (!b2World_IsValid(world)) continue;
        const bool sideView = plane == 0;
        const b2ContactEvents contacts = b2World_GetContactEvents(world);
        for (int index = 0; index < contacts.beginCount; ++index) {
            const b2ContactBeginTouchEvent& event = contacts.beginEvents[index];
            b2Vec2 point{ 0.0F, 0.0F };
            b2Vec2 normal{ 0.0F, 0.0F };
            if (b2Contact_IsValid(event.contactId)) {
                const b2ContactData data = b2Contact_GetData(event.contactId);
                normal = data.manifold.normal;
                if (data.manifold.pointCount > 0 && b2Shape_IsValid(event.shapeIdA)) {
                    const b2Vec2 origin = b2Body_GetPosition(b2Shape_GetBody(event.shapeIdA));
                    point = ToB2(origin.x + data.manifold.points[0].anchorA.x, origin.y + data.manifold.points[0].anchorA.y);
                }
            }
            queue(event.shapeIdA, event.shapeIdB, false, kb::scene::PhysicsContactPhase::Enter, point, normal, sideView);
        }
        for (int index = 0; index < contacts.endCount; ++index) {
            const b2ContactEndTouchEvent& event = contacts.endEvents[index];
            queue(event.shapeIdA, event.shapeIdB, false, kb::scene::PhysicsContactPhase::Exit, b2Vec2{ 0.0F, 0.0F }, b2Vec2{ 0.0F, 0.0F }, sideView);
        }
        const b2SensorEvents sensors = b2World_GetSensorEvents(world);
        for (int index = 0; index < sensors.beginCount; ++index) {
            queue(sensors.beginEvents[index].sensorShapeId, sensors.beginEvents[index].visitorShapeId, true, kb::scene::PhysicsContactPhase::Enter, b2Vec2{ 0.0F, 0.0F }, b2Vec2{ 0.0F, 0.0F }, sideView);
        }
        for (int index = 0; index < sensors.endCount; ++index) {
            queue(sensors.endEvents[index].sensorShapeId, sensors.endEvents[index].visitorShapeId, true, kb::scene::PhysicsContactPhase::Exit, b2Vec2{ 0.0F, 0.0F }, b2Vec2{ 0.0F, 0.0F }, sideView);
        }
    }
}

bool Physics2DRuntime::SetVelocity(SceneEntity entity, float x, float y) {
    const auto found = state_->bodies.find(entity);
    if (found == state_->bodies.end() || !b2Body_IsValid(found->second.id) || !found->second.hasComponent) return false;
    b2Body_SetLinearVelocity(found->second.id, ToB2(x, y));
    return true;
}

bool Physics2DRuntime::GetVelocity(SceneEntity entity, float& x, float& y) const {
    if (const auto found = state_->bodies.find(entity); found != state_->bodies.end() && b2Body_IsValid(found->second.id)) {
        const b2Vec2 velocity = b2Body_GetLinearVelocity(found->second.id);
        x = velocity.x;
        y = velocity.y;
        return true;
    }
    if (const auto found = state_->characters.find(entity); found != state_->characters.end()) {
        x = found->second.moveX;
        y = found->second.sideView ? found->second.velocityY : found->second.moveY;
        return true;
    }
    return false;
}

bool Physics2DRuntime::AddImpulse(SceneEntity entity, float x, float y) {
    const auto found = state_->bodies.find(entity);
    if (found == state_->bodies.end() || !b2Body_IsValid(found->second.id) || found->second.applied.type != 2) return false;
    b2Body_ApplyLinearImpulseToCenter(found->second.id, ToB2(x, y), true);
    return true;
}

bool Physics2DRuntime::AddForce(SceneEntity entity, float x, float y) {
    const auto found = state_->bodies.find(entity);
    if (found == state_->bodies.end() || !b2Body_IsValid(found->second.id) || found->second.applied.type != 2) return false;
    b2Body_ApplyForceToCenter(found->second.id, ToB2(x, y), true);
    return true;
}

kb::scene::Physics2DHit Physics2DRuntime::Raycast(bool sideView, float originX, float originY, float directionX, float directionY, float maxDistance,
    std::uint32_t layerMask) const {
    kb::scene::Physics2DHit result;
    const b2WorldId world = state_->WorldIfAny(sideView);
    if (!b2World_IsValid(world)) return result;
    const float length = std::sqrt(directionX * directionX + directionY * directionY);
    if (!(length > 0.0F)) return result;
    const b2Vec2 translation = ToB2(directionX / length * maxDistance, directionY / length * maxDistance);
    // The closest solid shape: a trigger does not stop a ray.
    struct Closest { b2ShapeId shape = b2_nullShapeId; b2Vec2 point{}; b2Vec2 normal{}; float fraction = 1.0F; bool hit = false; };
    Closest closest;
    b2World_CastRay(world, ToB2(originX, originY), translation, b2QueryFilter{ ~0ULL, WithOneWay(layerMask) },
        [](b2ShapeId shape, b2Vec2 point, b2Vec2 normal, float fraction, void* context) {
            if (b2Shape_IsSensor(shape)) return -1.0F;
            *static_cast<Closest*>(context) = Closest{ shape, point, normal, fraction, true };
            return fraction;
        }, &closest);
    if (!closest.hit || !b2Shape_IsValid(closest.shape)) return result;
    result.hit = true;
    result.entity = EntityOf(b2Shape_GetUserData(closest.shape));
    result.x = closest.point.x;
    result.y = closest.point.y;
    result.normalX = closest.normal.x;
    result.normalY = closest.normal.y;
    result.distance = closest.fraction * maxDistance;
    return result;
}

namespace {

struct Found {
    std::vector<SceneEntity> entities;
};

bool Collect(b2ShapeId shape, void* context) {
    auto* found = static_cast<Found*>(context);
    const SceneEntity entity = EntityOf(b2Shape_GetUserData(shape));
    if (entity.IsValid() && std::ranges::find(found->entities, entity) == found->entities.end()) found->entities.push_back(entity);
    return true;
}

} // namespace

std::vector<SceneEntity> Physics2DRuntime::OverlapCircle(bool sideView, float x, float y, float radius, std::uint32_t layerMask) const {
    Found found;
    const b2WorldId world = state_->WorldIfAny(sideView);
    if (!b2World_IsValid(world)) return found.entities;
    const b2Vec2 center = ToB2(x, y);
    const b2ShapeProxy proxy = b2MakeProxy(&center, 1, radius);
    b2World_OverlapShape(world, &proxy, b2QueryFilter{ ~0ULL, WithOneWay(layerMask) }, &Collect, &found);
    return found.entities;
}

std::vector<SceneEntity> Physics2DRuntime::OverlapBox(bool sideView, float x, float y, float halfWidth, float halfHeight, std::uint32_t layerMask) const {
    Found found;
    const b2WorldId world = state_->WorldIfAny(sideView);
    if (!b2World_IsValid(world)) return found.entities;
    const b2Vec2 corners[4] = { ToB2(x - halfWidth, y - halfHeight), ToB2(x + halfWidth, y - halfHeight), ToB2(x + halfWidth, y + halfHeight), ToB2(x - halfWidth, y + halfHeight) };
    const b2ShapeProxy proxy = b2MakeProxy(corners, 4, 0.0F);
    b2World_OverlapShape(world, &proxy, b2QueryFilter{ ~0ULL, WithOneWay(layerMask) }, &Collect, &found);
    return found.entities;
}

bool Physics2DRuntime::MoveCharacter(SceneEntity entity, float x, float y) {
    const auto found = state_->characters.find(entity);
    if (found == state_->characters.end()) return false;
    found->second.moveX = x;
    found->second.moveY = y;
    return true;
}

bool Physics2DRuntime::JumpCharacter(SceneEntity entity, float speed) {
    const auto found = state_->characters.find(entity);
    if (found == state_->characters.end() || !found->second.grounded || !found->second.sideView) return false;
    found->second.velocityY = speed;
    found->second.grounded = false;
    return true;
}

bool Physics2DRuntime::IsGrounded(SceneEntity entity) const {
    const auto found = state_->characters.find(entity);
    return found != state_->characters.end() && found->second.grounded;
}

std::uint32_t Physics2DRuntime::CreateJoint(const kb::scene::Physics2DJointDesc& desc, std::string& error) {
    State& state = *state_;
    using Type = kb::scene::Physics2DJointDesc::Type;
    const auto finite = [](std::initializer_list<float> values) {
        return std::ranges::all_of(values, [](float value) { return std::isfinite(value); });
    };
    if (!finite({ desc.x, desc.y, desc.x2, desc.y2, desc.axisX, desc.axisY, desc.lower, desc.upper, desc.motorSpeed, desc.maxMotor, desc.hertz, desc.dampingRatio, desc.length })) {
        error = "the joint numbers must be finite";
        return 0U;
    }
    if (desc.hertz < 0.0F || desc.dampingRatio < 0.0F || desc.maxMotor < 0.0F || desc.length < 0.0F) {
        error = "the spring, the motor limit and the length cannot be negative";
        return 0U;
    }
    // The body of an entity, in the simulation: a body or a character.
    struct Found { b2BodyId body = b2_nullBodyId; bool sideView = true; };
    const auto find = [&](SceneEntity entity) -> Found {
        if (const auto body = state.bodies.find(entity); body != state.bodies.end() && b2Body_IsValid(body->second.id)) return { body->second.id, body->second.sideView };
        if (const auto character = state.characters.find(entity); character != state.characters.end() && b2Body_IsValid(character->second.body)) return { character->second.body, character->second.sideView };
        return {};
    };
    const Found first = find(desc.first);
    if (!first.body.index1) {
        error = "the first entity has no 2D body in the simulation";
        return 0U;
    }
    Found second{};
    if (desc.second.IsValid()) {
        if (desc.second == desc.first) {
            error = "a joint needs two different bodies";
            return 0U;
        }
        second = find(desc.second);
        if (!second.body.index1) {
            error = "the second entity has no 2D body in the simulation";
            return 0U;
        }
        if (second.sideView != first.sideView) {
            error = "the two bodies are in different planes";
            return 0U;
        }
    } else {
        second = { state.Ground(first.sideView), first.sideView };
    }
    if (b2Body_GetType(first.body) != b2_dynamicBody && b2Body_GetType(second.body) != b2_dynamicBody) {
        error = "a joint needs at least one dynamic body";
        return 0U;
    }
    const b2Vec2 anchor = ToB2(desc.x, desc.y);
    const b2Vec2 anchorSecond = desc.type == Type::Distance ? ToB2(desc.x2, desc.y2) : anchor;
    float axisAngle = b2Rot_GetAngle(b2Body_GetRotation(first.body));
    if (desc.type == Type::Prismatic) {
        if (!(std::abs(desc.axisX) + std::abs(desc.axisY) > 0.0F)) {
            error = "a prismatic joint needs an axis";
            return 0U;
        }
        axisAngle = std::atan2(desc.axisY, desc.axisX);
    }
    const b2Rot frame = b2MakeRot(axisAngle);
    const auto connect = [&](b2JointDef& base) {
        base.bodyIdA = first.body;
        base.bodyIdB = second.body;
        base.localFrameA = b2Transform{ b2Body_GetLocalPoint(first.body, anchor), b2InvMulRot(b2Body_GetRotation(first.body), frame) };
        base.localFrameB = b2Transform{ b2Body_GetLocalPoint(second.body, anchorSecond), b2InvMulRot(b2Body_GetRotation(second.body), frame) };
        base.collideConnected = desc.collideConnected;
    };
    const b2WorldId world = state.World(first.sideView);
    b2JointId id = b2_nullJointId;
    switch (desc.type) {
    case Type::Distance: {
        b2DistanceJointDef definition = b2DefaultDistanceJointDef();
        connect(definition.base);
        const float current = std::sqrt((anchorSecond.x - anchor.x) * (anchorSecond.x - anchor.x) + (anchorSecond.y - anchor.y) * (anchorSecond.y - anchor.y));
        definition.length = std::max(0.005F, desc.length > 0.0F ? desc.length : current);
        definition.enableSpring = desc.hertz > 0.0F;
        definition.hertz = desc.hertz;
        definition.dampingRatio = desc.dampingRatio;
        if (desc.enableLimit) {
            if (!(desc.upper >= desc.lower)) { error = "the longest length is shorter than the shortest"; return 0U; }
            definition.enableLimit = true;
            definition.minLength = std::max(0.005F, desc.lower);
            definition.maxLength = std::max(definition.minLength, desc.upper);
        }
        id = b2CreateDistanceJoint(world, &definition);
        break;
    }
    case Type::Revolute: {
        b2RevoluteJointDef definition = b2DefaultRevoluteJointDef();
        connect(definition.base);
        definition.enableSpring = desc.hertz > 0.0F;
        definition.hertz = desc.hertz;
        definition.dampingRatio = desc.dampingRatio;
        if (desc.enableLimit) {
            if (!(desc.upper >= desc.lower) || desc.lower < -170.0F || desc.upper > 170.0F) { error = "a revolute limit is from -170 to 170 degrees, lower first"; return 0U; }
            definition.enableLimit = true;
            definition.lowerAngle = desc.lower * kPi / 180.0F;
            definition.upperAngle = desc.upper * kPi / 180.0F;
        }
        definition.enableMotor = desc.enableMotor;
        definition.motorSpeed = desc.motorSpeed * kPi / 180.0F;
        definition.maxMotorTorque = desc.maxMotor;
        id = b2CreateRevoluteJoint(world, &definition);
        break;
    }
    case Type::Prismatic: {
        b2PrismaticJointDef definition = b2DefaultPrismaticJointDef();
        connect(definition.base);
        definition.enableSpring = desc.hertz > 0.0F;
        definition.hertz = desc.hertz;
        definition.dampingRatio = desc.dampingRatio;
        if (desc.enableLimit) {
            if (!(desc.upper >= desc.lower)) { error = "the upper limit is lower than the lower"; return 0U; }
            definition.enableLimit = true;
            definition.lowerTranslation = desc.lower;
            definition.upperTranslation = desc.upper;
        }
        definition.enableMotor = desc.enableMotor;
        definition.motorSpeed = desc.motorSpeed;
        definition.maxMotorForce = desc.maxMotor;
        id = b2CreatePrismaticJoint(world, &definition);
        break;
    }
    case Type::Weld: {
        b2WeldJointDef definition = b2DefaultWeldJointDef();
        connect(definition.base);
        definition.linearHertz = desc.hertz;
        definition.angularHertz = desc.hertz;
        definition.linearDampingRatio = desc.dampingRatio;
        definition.angularDampingRatio = desc.dampingRatio;
        id = b2CreateWeldJoint(world, &definition);
        break;
    }
    }
    if (!b2Joint_IsValid(id)) {
        error = "the joint could not be made";
        return 0U;
    }
    const std::uint32_t handle = state.nextJoint++;
    state.joints[handle] = State::JointRec{ id, first.sideView };
    return handle;
}

bool Physics2DRuntime::DestroyJoint(std::uint32_t joint) {
    const auto found = state_->joints.find(joint);
    if (found == state_->joints.end()) return false;
    if (b2Joint_IsValid(found->second.id)) b2DestroyJoint(found->second.id, true);
    state_->joints.erase(found);
    return true;
}

bool Physics2DRuntime::SetJointMotor(std::uint32_t joint, float speed, float maxForce) {
    const auto found = state_->joints.find(joint);
    if (found == state_->joints.end() || !b2Joint_IsValid(found->second.id) || !std::isfinite(speed) || !std::isfinite(maxForce) || maxForce < 0.0F) return false;
    const b2JointId id = found->second.id;
    switch (b2Joint_GetType(id)) {
    case b2_revoluteJoint:
        b2RevoluteJoint_EnableMotor(id, true);
        b2RevoluteJoint_SetMotorSpeed(id, speed * kPi / 180.0F);
        b2RevoluteJoint_SetMaxMotorTorque(id, maxForce);
        return true;
    case b2_prismaticJoint:
        b2PrismaticJoint_EnableMotor(id, true);
        b2PrismaticJoint_SetMotorSpeed(id, speed);
        b2PrismaticJoint_SetMaxMotorForce(id, maxForce);
        return true;
    default:
        return false;
    }
}

bool Physics2DRuntime::JointExists(std::uint32_t joint) const {
    const auto found = state_->joints.find(joint);
    return found != state_->joints.end() && b2Joint_IsValid(found->second.id);
}

} // namespace kb::world2d
