#include "inspection/InspectorSceneColor.hpp"

#include "engine/scene/AmbientRadianceComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/WorldBackdropComponent.hpp"
#include "scene/EditorSceneContext.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace kb::editor {
namespace {

using Rgb = std::array<float, 3U>;

[[nodiscard]] Rgb ToRgb(const kb::scene::Vec3& value) noexcept {
    return { value.x, value.y, value.z };
}

[[nodiscard]] kb::scene::Vec3 ToVec3(const Rgb& value) noexcept {
    return kb::scene::Vec3{ value[0], value[1], value[2] };
}

// The colour field of a component a property names, or nullptr.
[[nodiscard]] kb::scene::Vec3* BackdropColor(kb::scene::WorldBackdropComponent& backdrop, InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::WorldBackdropColor: return &backdrop.color;
    case InspectorPropertyId::WorldBackdropHorizonColor: return &backdrop.horizonColor;
    case InspectorPropertyId::WorldBackdropZenithColor: return &backdrop.zenithColor;
    case InspectorPropertyId::WorldBackdropSkyTint: return &backdrop.skyTint;
    case InspectorPropertyId::WorldBackdropGroundColor: return &backdrop.groundColor;
    default: return nullptr;
    }
}

[[nodiscard]] kb::scene::Vec3* AmbientColor(kb::scene::AmbientRadianceComponent& ambient, InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::AmbientRadianceColor: return &ambient.color;
    case InspectorPropertyId::AmbientRadianceHorizonColor: return &ambient.horizonColor;
    case InspectorPropertyId::AmbientRadianceZenithColor: return &ambient.zenithColor;
    default: return nullptr;
    }
}

[[nodiscard]] const char* EditLabel(InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::LightColor: return "Edit Light Color";
    case InspectorPropertyId::CameraClearColor: return "Edit Camera Clear Color";
    case InspectorPropertyId::WorldBackdropColor:
    case InspectorPropertyId::WorldBackdropHorizonColor:
    case InspectorPropertyId::WorldBackdropZenithColor:
    case InspectorPropertyId::WorldBackdropSkyTint:
    case InspectorPropertyId::WorldBackdropGroundColor: return "Edit World Backdrop Color";
    default: return "Edit Ambient Radiance Color";
    }
}

// Runs `edit` on a copy inside one scene edit transaction; `store` writes the accepted copy.
template <typename Component, typename Edit, typename Store>
[[nodiscard]] bool EditComponent(EditorSceneContext& sceneContext, const Component* current, InspectorPropertyId property,
    Edit edit, Store store) {
    if (current == nullptr) return false;
    Component candidate = *current;
    if (!edit(candidate)) return false;
    if (!sceneContext.BeginSceneEditTransaction(EditLabel(property))) return false;
    store(candidate);
    static_cast<void>(sceneContext.CommitSceneEditTransaction());
    return true;
}

} // namespace

bool InspectorSceneColor::IsColorProperty(InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::LightColor:
    case InspectorPropertyId::CameraClearColor:
    case InspectorPropertyId::WorldBackdropColor:
    case InspectorPropertyId::WorldBackdropHorizonColor:
    case InspectorPropertyId::WorldBackdropZenithColor:
    case InspectorPropertyId::WorldBackdropSkyTint:
    case InspectorPropertyId::WorldBackdropGroundColor:
    case InspectorPropertyId::AmbientRadianceColor:
    case InspectorPropertyId::AmbientRadianceHorizonColor:
    case InspectorPropertyId::AmbientRadianceZenithColor:
        return true;
    default:
        return false;
    }
}

std::string_view InspectorSceneColor::Label(InspectorPropertyId property) noexcept {
    switch (property) {
    case InspectorPropertyId::LightColor:
    case InspectorPropertyId::WorldBackdropColor:
    case InspectorPropertyId::AmbientRadianceColor: return "Color";
    case InspectorPropertyId::CameraClearColor: return "Clear Color";
    case InspectorPropertyId::WorldBackdropHorizonColor:
    case InspectorPropertyId::AmbientRadianceHorizonColor: return "Horizon";
    case InspectorPropertyId::WorldBackdropZenithColor:
    case InspectorPropertyId::AmbientRadianceZenithColor: return "Zenith";
    case InspectorPropertyId::WorldBackdropSkyTint: return "Sky Tint";
    case InspectorPropertyId::WorldBackdropGroundColor: return "Ground";
    default: return {};
    }
}

std::optional<std::array<float, 3U>> InspectorSceneColor::Read(
    const EditorSceneContext& sceneContext, kb::scene::SceneEntity entity, InspectorPropertyId property) {
    const kb::scene::Scene& scene = sceneContext.Scene();
    if (property == InspectorPropertyId::LightColor) {
        const kb::scene::LightComponent* light = scene.Components().Lights().TryGet(entity);
        return light != nullptr ? std::optional<Rgb>{ ToRgb(light->color) } : std::nullopt;
    }
    if (property == InspectorPropertyId::CameraClearColor) {
        const kb::scene::CameraComponent* camera = scene.Components().Cameras().TryGet(entity);
        return camera != nullptr ? std::optional<Rgb>{ ToRgb(camera->clearColor) } : std::nullopt;
    }
    if (const kb::scene::WorldBackdropComponent* backdrop = scene.Components().WorldBackdrops().TryGet(entity); backdrop != nullptr) {
        kb::scene::WorldBackdropComponent copy = *backdrop;
        if (const kb::scene::Vec3* color = BackdropColor(copy, property)) return ToRgb(*color);
    }
    if (const kb::scene::AmbientRadianceComponent* ambient = scene.Components().AmbientRadiances().TryGet(entity); ambient != nullptr) {
        kb::scene::AmbientRadianceComponent copy = *ambient;
        if (const kb::scene::Vec3* color = AmbientColor(copy, property)) return ToRgb(*color);
    }
    return std::nullopt;
}

bool InspectorSceneColor::Apply(EditorSceneContext& sceneContext, kb::scene::SceneEntity entity,
    InspectorPropertyId property, const std::array<float, 3U>& linear) {
    if (!sceneContext.Scene().Entities().IsAlive(entity)) return false;
    const std::optional<Rgb> current = Read(sceneContext, entity, property);
    if (!current.has_value() || *current == linear) return false;
    kb::scene::SceneComponents components = sceneContext.Scene().Components();
    const kb::scene::Vec3 color = ToVec3(linear);
    switch (property) {
    case InspectorPropertyId::LightColor:
        return EditComponent(sceneContext, components.Lights().TryGet(entity), property,
            [&color](kb::scene::LightComponent& light) { light.color = color; return true; },
            [&](const kb::scene::LightComponent& light) { components.Lights().Set(entity, light); });
    case InspectorPropertyId::CameraClearColor:
        return EditComponent(sceneContext, components.Cameras().TryGet(entity), property,
            [&color](kb::scene::CameraComponent& camera) { camera.clearColor = color; return true; },
            [&](const kb::scene::CameraComponent& camera) { components.Cameras().Set(entity, camera); });
    case InspectorPropertyId::AmbientRadianceColor:
    case InspectorPropertyId::AmbientRadianceHorizonColor:
    case InspectorPropertyId::AmbientRadianceZenithColor:
        return EditComponent(sceneContext, components.AmbientRadiances().TryGet(entity), property,
            [&](kb::scene::AmbientRadianceComponent& ambient) {
                *AmbientColor(ambient, property) = color;
                return kb::scene::IsAmbientRadianceComponentValid(ambient);
            },
            [&](const kb::scene::AmbientRadianceComponent& ambient) { components.AmbientRadiances().Set(entity, ambient); });
    default:
        return EditComponent(sceneContext, components.WorldBackdrops().TryGet(entity), property,
            [&](kb::scene::WorldBackdropComponent& backdrop) {
                kb::scene::Vec3* field = BackdropColor(backdrop, property);
                if (field == nullptr) return false;
                *field = color;
                return kb::scene::IsWorldBackdropComponentValid(backdrop);
            },
            [&](const kb::scene::WorldBackdropComponent& backdrop) { components.WorldBackdrops().Set(entity, backdrop); });
    }
}

float InspectorSceneColor::LinearToSrgb(float channel) noexcept {
    channel = std::clamp(channel, 0.0F, 1.0F);
    return channel <= 0.0031308F ? channel * 12.92F : 1.055F * std::pow(channel, 1.0F / 2.4F) - 0.055F;
}

float InspectorSceneColor::SrgbToLinear(float channel) noexcept {
    channel = std::clamp(channel, 0.0F, 1.0F);
    return channel <= 0.04045F ? channel / 12.92F : std::pow((channel + 0.055F) / 1.055F, 2.4F);
}

} // namespace kb::editor
