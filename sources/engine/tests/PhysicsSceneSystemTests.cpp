#include "SceneSystemTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <utility>

namespace {

void RunPhysicsSceneSystemFallingBodyTest() {
    kb::project::ProjectDescriptor descriptor;
    descriptor.disableEnginePluginsByDefault = true;
    descriptor.plugins.push_back(kb::project::ProjectPluginReference{
        .name = "Physics.Jolt",
        .binaryPath = KB_PHYSICS_JOLT_PLUGIN_PATH,
        .enabled = true,
    });

    kb::scene::Scene scene{ std::move(descriptor) };

    kb::scene::SceneObject floor = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Floor",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, -0.5F, 0.0F },
        },
    });
    scene.Components().Rigidbodies().Set(floor.Entity(), kb::scene::RigidbodyComponent{
        .bodyType = kb::scene::RigidbodyBodyType::Static,
    });
    scene.Components().Colliders().Set(floor.Entity(), kb::scene::ColliderComponent{
        .shape = kb::scene::ColliderShape::Box,
        .boxSize = kb::scene::Vec3{ 10.0F, 1.0F, 10.0F },
    });

    kb::scene::SceneObject box = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Box",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, 4.0F, 0.0F },
        },
    });
    scene.Components().Rigidbodies().Set(box.Entity(), kb::scene::RigidbodyComponent{
        .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
        .mass = 1.0F,
    });
    scene.Components().Colliders().Set(box.Entity(), kb::scene::ColliderComponent{
        .shape = kb::scene::ColliderShape::Box,
        .boxSize = kb::scene::Vec3{ 1.0F, 1.0F, 1.0F },
    });

    for (int i = 0; i < 120; ++i) {
        [[maybe_unused]] const bool progressed = scene.Runtime().Update(1.0F / 60.0F);
    }

    const kb::scene::TransformComponent transform = scene.Transforms().Get(box);
    kb::tests::Require(transform.localPosition.y < 4.0F, "PhysicsSceneSystem did not move the dynamic body");
    kb::tests::Require(transform.localPosition.y > 0.35F, "PhysicsSceneSystem let the dynamic body tunnel through the floor");
}

} // namespace

namespace kb::tests {

void RunPhysicsSceneSystemTests() {
    RunPhysicsSceneSystemFallingBodyTest();
}

} // namespace kb::tests
