#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/scene/RegionPortalComponent.hpp"
#include "engine/scene/RegionShapeComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePortalVisibility.hpp"
#include "engine/scene/SceneRegionPortalComponents.hpp"
#include "engine/scene/SceneRegionShapeComponents.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneVisibilityCellComponents.hpp"
#include "engine/scene/VisibilityCellComponent.hpp"

#include <array>

namespace kb::tests {
namespace {

using kb::math::Vec3;

// A 90 degree camera at `position` looking along +Z (or -Z), as a column-major world-to-clip transform.
[[nodiscard]] kb::scene::ScenePortalCamera Camera(Vec3 position, bool lookAlongPositiveZ = true) {
    const float facing = lookAlongPositiveZ ? 1.0F : -1.0F;
    kb::scene::ScenePortalCamera camera{ .position = position };
    camera.viewProjection = {
        facing, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, facing, facing,
        -facing * position.x, -position.y, -facing * position.z, -facing * position.z,
    };
    return camera;
}

[[nodiscard]] kb::scene::SceneEntity AddCell(kb::scene::Scene& scene, Vec3 center, kb::scene::VisibilityCellComponent cell = {}) {
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Cell", .transform = kb::scene::TransformComponent{ .localPosition = center } });
    scene.Components().RegionShapes().Set(object.Entity(), kb::scene::RegionShapeComponent{ .kind = kb::scene::RegionShapeKind::Box, .size = { 10.0F, 6.0F, 10.0F } });
    scene.Components().VisibilityCells().Set(object.Entity(), cell);
    return object.Entity();
}

[[nodiscard]] kb::scene::SceneEntity AddPortal(kb::scene::Scene& scene, Vec3 center, kb::scene::SceneEntity from, kb::scene::SceneEntity to,
    Vec3 opening = { 2.0F, 3.0F, 0.5F }) {
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Portal", .transform = kb::scene::TransformComponent{ .localPosition = center } });
    scene.Components().RegionShapes().Set(object.Entity(), kb::scene::RegionShapeComponent{ .kind = kb::scene::RegionShapeKind::Box, .size = opening });
    scene.Components().RegionPortals().Set(object.Entity(), kb::scene::SceneRegionPortalComponent{ .sourceCell = from, .targetCell = to, .enabled = true });
    return object.Entity();
}

void SetPortalEnabled(kb::scene::Scene& scene, kb::scene::SceneEntity portal, bool enabled) {
    kb::scene::SceneRegionPortalComponent value = *scene.Components().RegionPortals().TryGet(portal);
    value.enabled = enabled;
    scene.Components().RegionPortals().Set(portal, value);
}

// Cells A, B and C in a row along +Z, each 10 units deep, with openings A -> B and B -> C in the walls between.
struct Corridor {
    kb::scene::Scene scene;
    kb::scene::SceneEntity a{};
    kb::scene::SceneEntity b{};
    kb::scene::SceneEntity c{};
    kb::scene::SceneEntity ab{};
    kb::scene::SceneEntity bc{};

    Corridor() {
        a = AddCell(scene, { 0.0F, 0.0F, 0.0F });
        b = AddCell(scene, { 0.0F, 0.0F, 10.0F });
        c = AddCell(scene, { 0.0F, 0.0F, 20.0F });
        ab = AddPortal(scene, { 0.0F, 0.0F, 5.0F }, a, b);
        bc = AddPortal(scene, { 0.0F, 0.0F, 15.0F }, b, c);
        static_cast<void>(scene.Runtime().Update(0.0F));
    }
};

void TestOpenPortalsShowEveryCell() {
    Corridor corridor;
    Require(kb::scene::SceneHasVisibilityCells(corridor.scene), "The corridor must report its visibility cells");
    const kb::scene::ScenePortalVisibility visibility = kb::scene::ComputeScenePortalVisibility(corridor.scene, Camera({ 0.0F, 0.0F, -3.0F }));
    Require(visibility.Active() && visibility.CameraCells().size() == 1U && visibility.CameraCells()[0] == corridor.a,
        "The camera's cell must be found from its position");
    Require(visibility.IsCellVisible(corridor.a) && visibility.IsCellVisible(corridor.b) && visibility.IsCellVisible(corridor.c),
        "Cells seen through a chain of open portals must be visible");
    Require(!visibility.Hides({ 0.0F, 0.0F, 20.0F }) && !visibility.Hides({ 0.0F, 0.0F, 30.0F }),
        "Nothing in a visible cell, or outside every cell, may be culled");
}

void TestClosedPortalCullsFarCell() {
    Corridor corridor;
    SetPortalEnabled(corridor.scene, corridor.bc, false);
    const kb::scene::ScenePortalVisibility visibility = kb::scene::ComputeScenePortalVisibility(corridor.scene, Camera({ 0.0F, 0.0F, -3.0F }));
    Require(visibility.IsCellVisible(corridor.b) && !visibility.IsCellVisible(corridor.c),
        "A closed portal must hide the cell behind it");
    Require(visibility.Hides({ 0.0F, 0.0F, 20.0F }) && !visibility.Hides({ 0.0F, 0.0F, 10.0F }) && !visibility.Hides({ 0.0F, 0.0F, 0.0F }),
        "Entities in the cell behind a closed portal must be culled, the others kept");

    SetPortalEnabled(corridor.scene, corridor.ab, false);
    const kb::scene::ScenePortalVisibility sealed = kb::scene::ComputeScenePortalVisibility(corridor.scene, Camera({ 0.0F, 0.0F, -3.0F }));
    Require(!sealed.IsCellVisible(corridor.b) && !sealed.IsCellVisible(corridor.c) && sealed.Hides({ 0.0F, 0.0F, 10.0F }),
        "Closing the first portal must hide every cell beyond it");
}

void TestPortalsOutsideTheViewCullCells() {
    Corridor corridor;
    // Looking away from the opening: the portal is behind the camera.
    const kb::scene::ScenePortalVisibility away = kb::scene::ComputeScenePortalVisibility(corridor.scene, Camera({ 0.0F, 0.0F, -3.0F }, false));
    Require(away.Active() && !away.IsCellVisible(corridor.b) && !away.IsCellVisible(corridor.c) && away.Hides({ 0.0F, 0.0F, 20.0F }),
        "A portal behind the camera must not show the cells beyond it");

    // Openings at opposite sides of B: through the first, the second is out of the line of sight although
    // both are open and on screen.
    kb::scene::Scene scene;
    const kb::scene::SceneEntity a = AddCell(scene, { 0.0F, 0.0F, 0.0F });
    const kb::scene::SceneEntity b = AddCell(scene, { 0.0F, 0.0F, 10.0F });
    const kb::scene::SceneEntity c = AddCell(scene, { 0.0F, 0.0F, 20.0F });
    static_cast<void>(AddPortal(scene, { -4.0F, 0.0F, 5.0F }, a, b, { 1.0F, 1.0F, 0.5F }));
    const kb::scene::SceneEntity side = AddPortal(scene, { 4.0F, 0.0F, 15.0F }, b, c, { 1.0F, 1.0F, 0.5F });
    static_cast<void>(scene.Runtime().Update(0.0F));
    const kb::scene::ScenePortalVisibility narrowed = kb::scene::ComputeScenePortalVisibility(scene, Camera({ 0.0F, 0.0F, -3.0F }));
    Require(narrowed.IsCellVisible(b) && !narrowed.IsCellVisible(c),
        "The view through a portal must be clipped to its opening before the next portal is tested");

    // Lined up behind the first opening, the second one shows C.
    kb::scene::TransformComponent moved = scene.Transforms().Get(side);
    moved.localPosition = Vec3{ -6.0F, 0.0F, 15.0F };
    scene.Transforms().Set(side, moved);
    static_cast<void>(scene.Runtime().Update(0.0F));
    const kb::scene::ScenePortalVisibility lined = kb::scene::ComputeScenePortalVisibility(scene, Camera({ -1.6F, 0.0F, -3.0F }));
    Require(lined.IsCellVisible(c), "A portal seen through another portal must show the cell behind it");
}

void TestPortalDirectionOverridesAndMasks() {
    {
        // Portals lead from their source cell to their target cell only.
        kb::scene::Scene scene;
        const kb::scene::SceneEntity a = AddCell(scene, { 0.0F, 0.0F, 0.0F });
        const kb::scene::SceneEntity b = AddCell(scene, { 0.0F, 0.0F, 10.0F });
        static_cast<void>(AddPortal(scene, { 0.0F, 0.0F, 5.0F }, b, a));
        static_cast<void>(scene.Runtime().Update(0.0F));
        const kb::scene::ScenePortalVisibility visibility = kb::scene::ComputeScenePortalVisibility(scene, Camera({ 0.0F, 0.0F, -3.0F }));
        Require(!visibility.IsCellVisible(b), "A portal must not be looked through against its direction");
        const kb::scene::ScenePortalVisibility fromB = kb::scene::ComputeScenePortalVisibility(scene, Camera({ 0.0F, 0.0F, 13.0F }, false));
        Require(fromB.IsCellVisible(a) && fromB.CameraCells()[0] == b, "A portal must be looked through along its direction");
    }
    {
        Corridor corridor;
        kb::scene::VisibilityCellComponent forced = *corridor.scene.Components().VisibilityCells().TryGet(corridor.b);
        forced.visibilityOverride = kb::scene::VisibilityCellOverride::ForceHidden;
        corridor.scene.Components().VisibilityCells().Set(corridor.b, forced);
        const kb::scene::ScenePortalVisibility hidden = kb::scene::ComputeScenePortalVisibility(corridor.scene, Camera({ 0.0F, 0.0F, -3.0F }));
        Require(!hidden.IsCellVisible(corridor.b) && !hidden.IsCellVisible(corridor.c),
            "A force-hidden cell must be hidden and must not be looked through");
        kb::scene::VisibilityCellComponent shown = *corridor.scene.Components().VisibilityCells().TryGet(corridor.c);
        shown.visibilityOverride = kb::scene::VisibilityCellOverride::ForceVisible;
        corridor.scene.Components().VisibilityCells().Set(corridor.c, shown);
        const kb::scene::ScenePortalVisibility visible = kb::scene::ComputeScenePortalVisibility(corridor.scene, Camera({ 0.0F, 0.0F, -3.0F }));
        Require(visible.IsCellVisible(corridor.c) && !visible.Hides({ 0.0F, 0.0F, 20.0F }), "A force-visible cell must always be visible");
    }
    {
        Corridor corridor;
        SetPortalEnabled(corridor.scene, corridor.bc, false);
        // A cell for other layers does not take part; one excluded volume inside C is never culled.
        const kb::scene::SceneEntity excluded = AddCell(corridor.scene, { 0.0F, 0.0F, 22.0F },
            kb::scene::VisibilityCellComponent{ .membership = kb::scene::VisibilityCellMembership::Exclude });
        corridor.scene.Components().RegionShapes().Set(excluded,
            kb::scene::RegionShapeComponent{ .kind = kb::scene::RegionShapeKind::Box, .size = { 2.0F, 2.0F, 2.0F } });
        static_cast<void>(corridor.scene.Runtime().Update(0.0F));
        const kb::scene::ScenePortalVisibility visibility = kb::scene::ComputeScenePortalVisibility(corridor.scene, Camera({ 0.0F, 0.0F, -3.0F }));
        Require(visibility.Hides({ 0.0F, 0.0F, 18.0F }) && !visibility.Hides({ 0.0F, 0.0F, 22.0F }),
            "An Exclude cell must cut its volume out of the cell around it");
        kb::scene::VisibilityCellComponent layered = *corridor.scene.Components().VisibilityCells().TryGet(corridor.c);
        layered.membershipMask = 2U;
        corridor.scene.Components().VisibilityCells().Set(corridor.c, layered);
        const kb::scene::ScenePortalVisibility masked = kb::scene::ComputeScenePortalVisibility(corridor.scene, Camera({ 0.0F, 0.0F, -3.0F }));
        Require(masked.Hides({ 0.0F, 0.0F, 18.0F }, 2U) && !masked.Hides({ 0.0F, 0.0F, 18.0F }, 1U),
            "A cell must apply only to entities whose membership shares a bit with it");
    }
    {
        // Outside every cell the camera culls nothing.
        Corridor corridor;
        SetPortalEnabled(corridor.scene, corridor.bc, false);
        const kb::scene::ScenePortalVisibility outside = kb::scene::ComputeScenePortalVisibility(corridor.scene, Camera({ 0.0F, 0.0F, -30.0F }));
        Require(!outside.Active() && !outside.Hides({ 0.0F, 0.0F, 20.0F }), "A camera outside every cell must not cull by cell");
    }
}

} // namespace

void RunPortalVisibilityTests() {
    TestOpenPortalsShowEveryCell();
    TestClosedPortalCullsFarCell();
    TestPortalsOutsideTheViewCullCells();
    TestPortalDirectionOverridesAndMasks();
}

} // namespace kb::tests
