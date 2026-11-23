#include "GameCaptureCamera.hpp"

#include "engine/math/EngineMath.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::game {
namespace {

using kb::math::Vec3;

[[nodiscard]] std::optional<Vec3> ParseVec3(std::string_view text) {
    std::array<float, 3> values{};
    std::size_t start = 0U;
    for (std::size_t index = 0U; index < 3U; ++index) {
        const std::size_t end = index == 2U ? text.size() : text.find(',', start);
        if (end == std::string_view::npos || end <= start) return std::nullopt;
        const std::string part{ text.substr(start, end - start) };
        char* stop = nullptr;
        values[index] = std::strtof(part.c_str(), &stop);
        if (stop != part.c_str() + part.size() || !std::isfinite(values[index])) return std::nullopt;
        start = end + 1U;
    }
    return Vec3{ values[0], values[1], values[2] };
}

[[nodiscard]] std::vector<kb::scene::SceneEntity> AllEntities(kb::scene::Scene& scene) {
    std::vector<kb::scene::SceneEntity> all;
    std::vector<kb::scene::SceneEntity> pending = scene.Hierarchy().RootEntities();
    while (!pending.empty()) {
        const kb::scene::SceneEntity entity = pending.back();
        pending.pop_back();
        all.push_back(entity);
        for (const kb::scene::SceneEntity child : scene.Hierarchy().ChildEntities(entity)) pending.push_back(child);
    }
    return all;
}

// The sphere around everything the scene draws as geometry: the middle of the objects and how far the farthest reaches.
struct Bounds {
    Vec3 centre{};
    float radius = 5.0F;
};

[[nodiscard]] Bounds DrawnBounds(kb::scene::Scene& scene, const std::vector<kb::scene::SceneEntity>& entities) {
    Vec3 low{ 1e30F, 1e30F, 1e30F };
    Vec3 high{ -1e30F, -1e30F, -1e30F };
    bool found = false;
    for (const kb::scene::SceneEntity entity : entities) {
        const auto components = scene.Components();
        if (!components.MeshRenderers().Has(entity) && !components.Sprites2D().Has(entity)) continue;
        const kb::math::DVec3 at = scene.Transforms().WorldTranslation(entity);
        const kb::scene::TransformComponent transform = scene.Transforms().Get(entity);
        const float reach = 0.6F * std::max({ std::abs(transform.worldScale.x), std::abs(transform.worldScale.y), std::abs(transform.worldScale.z), 0.1F });
        low = Vec3{ std::min(low.x, static_cast<float>(at.x) - reach), std::min(low.y, static_cast<float>(at.y) - reach), std::min(low.z, static_cast<float>(at.z) - reach) };
        high = Vec3{ std::max(high.x, static_cast<float>(at.x) + reach), std::max(high.y, static_cast<float>(at.y) + reach), std::max(high.z, static_cast<float>(at.z) + reach) };
        found = true;
    }
    Bounds bounds;
    if (!found) return bounds;
    bounds.centre = Vec3{ 0.5F * (low.x + high.x), 0.5F * (low.y + high.y), 0.5F * (low.z + high.z) };
    const Vec3 extent = high - low;
    bounds.radius = std::max(1.0F, 0.5F * kb::math::Length(extent));
    return bounds;
}

} // namespace

std::string ApplyCaptureCamera(kb::scene::Scene& scene, const CaptureCameraRequest& request) {
    if (request.Empty()) return {};
    const std::vector<kb::scene::SceneEntity> entities = AllEntities(scene);
    scene.Runtime().SynchronizeTransforms();

    std::vector<kb::scene::SceneEntity> cameras;
    for (const kb::scene::SceneEntity entity : entities)
        if (scene.Components().Cameras().Has(entity)) cameras.push_back(entity);

    // Only one camera draws: the others stop being primary, and the chosen one wins any tie.
    const auto choose = [&](kb::scene::SceneEntity chosen) {
        for (const kb::scene::SceneEntity entity : cameras) {
            kb::scene::CameraComponent camera = *scene.Components().Cameras().TryGet(entity);
            camera.primary = entity == chosen;
            camera.priority = entity == chosen ? 1000 : 0;
            camera.viewportId = 0U;
            scene.Components().Cameras().Set(entity, camera);
        }
    };

    if (!request.camera.empty()) {
        if (!request.view.empty() || !request.eye.empty() || !request.lookAt.empty()) return "choose a camera by name or a free view, not both";
        std::string names;
        for (const kb::scene::SceneEntity entity : cameras) {
            const std::string name = scene.Entities().Name(entity);
            if (name == request.camera) {
                choose(entity);
                return {};
            }
            names += " " + name;
        }
        const std::string text = "the scene has no camera named '" + request.camera + "'";
        if (names.empty()) return text + " (it has no camera at all)";
        return text + "; its cameras:" + names;
    }

    const Bounds bounds = DrawnBounds(scene, entities);
    Vec3 direction{ 1.0F, 0.8F, -1.0F };
    if (!request.view.empty()) {
        const std::string& view = request.view;
        if (view == "overview") direction = Vec3{ 1.0F, 0.8F, -1.0F };
        else if (view == "top") direction = Vec3{ 0.0F, 1.0F, -0.001F };
        else if (view == "front") direction = Vec3{ 0.0F, 0.15F, -1.0F };
        else if (view == "side") direction = Vec3{ 1.0F, 0.15F, 0.0F };
        else return "unknown view '" + view + "'; views: overview, top, front, side (or give --eye and --look-at)";
    }
    const float distance = bounds.radius * 2.6F + 1.0F;
    Vec3 target = bounds.centre;
    Vec3 eye = target + kb::math::Normalize(direction) * distance;
    if (!request.eye.empty() || !request.lookAt.empty()) {
        const std::optional<Vec3> parsedEye = request.eye.empty() ? std::nullopt : ParseVec3(request.eye);
        const std::optional<Vec3> parsedTarget = request.lookAt.empty() ? std::nullopt : ParseVec3(request.lookAt);
        if ((!request.eye.empty() && !parsedEye.has_value()) || (!request.lookAt.empty() && !parsedTarget.has_value()))
            return "--eye and --look-at take a point x,y,z";
        if (parsedTarget.has_value()) target = *parsedTarget;
        eye = parsedEye.has_value() ? *parsedEye : target + kb::math::Normalize(direction) * distance;
    }
    const Vec3 forward = target - eye;
    if (kb::math::Length(forward) < 1e-4F) return "the eye and the point to look at are the same place";
    // Straight down or up has no "up" from +Y; keep the top of the image toward +Z then.
    const bool vertical = std::abs(forward.y) > 0.999F * kb::math::Length(forward);
    const Vec3 up = vertical ? Vec3{ 0.0F, 0.0F, forward.y < 0.0F ? 1.0F : -1.0F } : Vec3{ 0.0F, 1.0F, 0.0F };

    kb::scene::SceneObjectDesc desc;
    desc.name = "CaptureView";
    desc.transform.localPosition = eye;
    desc.transform.localRotation = kb::math::LookRotation(forward, up);
    const kb::scene::SceneEntity viewEntity = scene.Entities().CreateEntity(desc);
    scene.Entities().SetTransient(viewEntity, true);
    kb::scene::CameraComponent camera;
    camera.primary = true;
    camera.priority = 1000;
    camera.farClip = std::max(1000.0F, distance * 4.0F);
    scene.Components().Cameras().Set(viewEntity, camera);
    cameras.push_back(viewEntity);
    choose(viewEntity);
    return {};
}

} // namespace kb::game
