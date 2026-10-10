#include "engine/scene/SceneCreateMenu.hpp"

#include "engine/assets/BuiltInShapes.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/AmbientRadianceComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentAuthoring.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/WorldBackdropComponent.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <vector>

namespace kb::scene {
namespace {

constexpr std::string_view k3DObject = "3D Object";
constexpr std::string_view kLight = "Light";

constexpr std::array<SceneCreateItem, 17> kItems{ {
    { "Empty", "Create Empty", "Entity", {}, "Ctrl+Shift+N" },
    { "Cube", "Cube", "Cube", k3DObject, "1 m" },
    { "Sphere", "Sphere", "Sphere", k3DObject, "1 m" },
    { "Capsule", "Capsule", "Capsule", k3DObject, "2 m" },
    { "Cylinder", "Cylinder", "Cylinder", k3DObject, "2 m" },
    { "Cone", "Cone", "Cone", k3DObject, "1 m" },
    { "Plane", "Plane", "Plane", k3DObject, "10 x 10 m" },
    { "Quad", "Quad", "Quad", k3DObject, "1 x 1 m" },
    { "Directional Light", "Directional", "Directional Light", kLight, {} },
    { "Point Light", "Point", "Point Light", kLight, {} },
    { "Spot Light", "Spot", "Spot Light", kLight, {} },
    { "Area Light", "Area", "Area Light", kLight, {} },
    { "Camera", "Camera", "Camera", {}, {} },
    { "Environment", "Environment", "Environment", {}, "Sky + ambient" },
    { "Particle System", "Particle System", "Particle System", "Effects", {} },
    { "Audio Source", "Audio Source", "Audio Source", "Audio", {} },
    { "Audio Listener", "Audio Listener", "Audio Listener", "Audio", {} },
} };

[[nodiscard]] std::optional<LightKind> LightKindOf(std::string_view id) noexcept {
    if (id == "Directional Light") return LightKind::Directional;
    if (id == "Point Light") return LightKind::Point;
    if (id == "Spot Light") return LightKind::Spot;
    if (id == "Area Light") return LightKind::AreaRect;
    return std::nullopt;
}

// The components an item is made of, in the ids of SceneComponentAuthoring.
[[nodiscard]] std::vector<std::string_view> OwnComponents(std::string_view id) {
    if (kb::assets::FindBuiltInShape(id) != nullptr) return { "MeshRenderer" };
    if (LightKindOf(id).has_value()) return { "Light" };
    if (id == "Camera") return { "Camera" };
    if (id == "Environment") return { "WorldBackdrop", "Ambient Radiance" };
    if (id == "Particle System") return { "Particle Effect" };
    if (id == "Audio Source") return { "AudioSource" };
    if (id == "Audio Listener") return { "AudioListener" };
    return {};
}

// The collider Unity gives its primitive of the same shape: a capsule for the cylinder, and a
// thin box where Unity uses a mesh collider (plane, quad). The cone has no primitive collider.
[[nodiscard]] std::optional<ColliderComponent> PrimitiveCollider(kb::assets::BuiltInShape shape) noexcept {
    ColliderComponent collider{};
    switch (shape) {
    case kb::assets::BuiltInShape::Cube:
        collider.shape = ColliderShape::Box;
        collider.boxSize = Vec3{ 1.0F, 1.0F, 1.0F };
        return collider;
    case kb::assets::BuiltInShape::Sphere:
        collider.shape = ColliderShape::Sphere;
        collider.radius = 0.5F;
        return collider;
    case kb::assets::BuiltInShape::Capsule:
    case kb::assets::BuiltInShape::Cylinder:
        collider.shape = ColliderShape::Capsule;
        collider.radius = 0.5F;
        collider.height = 2.0F;
        return collider;
    case kb::assets::BuiltInShape::Plane:
        collider.shape = ColliderShape::Box;
        collider.boxSize = Vec3{ 10.0F, 0.02F, 10.0F };
        return collider;
    case kb::assets::BuiltInShape::Quad:
        collider.shape = ColliderShape::Box;
        collider.boxSize = Vec3{ 1.0F, 1.0F, 0.02F };
        return collider;
    case kb::assets::BuiltInShape::Cone:
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] bool Added(Scene& scene, SceneEntity entity, std::string_view component, const kb::project::ProjectDescriptor& project) {
    return SceneComponentAuthoring::Add(scene, entity, component, project).status == SceneComponentAddStatus::Added;
}

} // namespace

std::span<const SceneCreateItem> SceneCreateMenu::Items() noexcept {
    return kItems;
}

const SceneCreateItem* SceneCreateMenu::Find(std::string_view id) noexcept {
    const auto found = std::ranges::find_if(kItems, [id](const SceneCreateItem& item) { return item.id == id; });
    return found == kItems.end() ? nullptr : &*found;
}

SceneCreateResult SceneCreateMenu::Create(Scene& scene, std::string_view id, std::string name,
    const kb::project::ProjectDescriptor& project, SceneEntity parent) {
    const SceneCreateItem* item = Find(id);
    if (item == nullptr) {
        std::string error = "unknown Create item '" + std::string{ id } + "'. Items:";
        for (const SceneCreateItem& known : kItems) error += " '" + std::string{ known.id } + "'";
        return { {}, std::move(error) };
    }
    const std::vector<std::string_view> components = OwnComponents(item->id);
    for (const std::string_view component : components) {
        const SceneComponentKind* kind = SceneComponentAuthoring::Find(component);
        if (kind != nullptr && !SceneComponentAuthoring::PluginEnabled(project, *kind)) {
            return { {}, std::string{ item->id } + " needs the project plugin " + std::string{ kind->requiredPlugin } };
        }
    }

    SceneObjectDesc desc{};
    desc.name = std::move(name);
    const std::optional<LightKind> lightKind = LightKindOf(item->id);
    if (lightKind == LightKind::Directional) {
        // Unity's new Directional Light: the sun from above, at an angle.
        desc.transform.localRotation = kb::math::FromEulerDegrees(kb::math::Vec3{ 50.0F, -30.0F, 0.0F });
    }
    const SceneEntity entity = scene.Entities().CreateEntity(std::move(desc));
    if (!entity.IsValid()) return { {}, "the scene refused a new entity" };
    if (parent.IsValid() && !scene.Hierarchy().SetParent(entity, parent)) {
        scene.Entities().Destroy(entity);
        return { {}, "the new entity could not be parented" };
    }
    for (const std::string_view component : components) {
        if (!Added(scene, entity, component, project)) {
            scene.Entities().Destroy(entity);
            return { {}, std::string{ item->id } + ": " + std::string{ component } + " could not be added" };
        }
    }

    if (const kb::assets::BuiltInShapeDesc* shape = kb::assets::FindBuiltInShape(item->id); shape != nullptr) {
        MeshRendererComponent renderer{};
        renderer.meshAssetId = kb::assets::BuiltInShapeId(shape->shape).value;
        scene.Components().MeshRenderers().Set(entity, renderer);
        const SceneComponentKind* collider = SceneComponentAuthoring::Find("Collider");
        if (const std::optional<ColliderComponent> fitted = PrimitiveCollider(shape->shape);
            fitted.has_value() && SceneComponentAuthoring::PluginEnabled(project, *collider)) {
            if (!Added(scene, entity, "Collider", project)) {
                scene.Entities().Destroy(entity);
                return { {}, std::string{ item->id } + ": Collider could not be added" };
            }
            scene.Components().Colliders().Set(entity, *fitted);
        }
    } else if (lightKind.has_value()) {
        LightComponent light{};
        light.kind = *lightKind;
        scene.Components().Lights().Set(entity, light);
    } else if (item->id == "Environment") {
        WorldBackdropComponent backdrop{};
        backdrop.mode = WorldBackdropMode::ProceduralSky;
        scene.Components().WorldBackdrops().Set(entity, backdrop);
        AmbientRadianceComponent ambient{};
        ambient.mode = AmbientRadianceMode::ProceduralSky;
        scene.Components().AmbientRadiances().Set(entity, ambient);
    }
    return { entity, {} };
}

} // namespace kb::scene
