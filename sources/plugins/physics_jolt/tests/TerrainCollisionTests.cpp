#include "TestSupport.hpp"

#include "TerrainBrush.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/TerrainAssetIO.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

namespace {

using kb::assets::TerrainAsset;
using kb::scene::ColliderShape;
using kb::scene::Vec3;

constexpr float kStep = 1.0F / 60.0F;

kb::project::ProjectDescriptor Descriptor() {
    kb::project::ProjectDescriptor descriptor;
    descriptor.disableEnginePluginsByDefault = true;
    descriptor.plugins.push_back({ .name = "Physics.Jolt", .binaryPath = KB_PHYSICS_JOLT_PLUGIN_PATH, .enabled = true });
    return descriptor;
}

// A plane that falls in x and z, so a swapped axis or a shifted origin changes the height.
float PlaneHeight(float worldX, float worldZ) {
    return 4.0F + 0.1F * worldX + 0.05F * worldZ;
}

float WorldX(const TerrainAsset& terrain, std::uint32_t x) {
    return static_cast<float>(x) * terrain.worldSizeX / static_cast<float>(terrain.width - 1U) - terrain.worldSizeX * 0.5F;
}

float WorldZ(const TerrainAsset& terrain, std::uint32_t z) {
    return static_cast<float>(z) * terrain.worldSizeZ / static_cast<float>(terrain.height - 1U) - terrain.worldSizeZ * 0.5F;
}

TerrainAsset MakeSlope(std::uint32_t width, std::uint32_t height, float size, std::uint32_t collisionStep = 1U) {
    TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset(width, size, size);
    terrain.height = height;
    terrain.worldSizeZ = size * static_cast<float>(height - 1U) / static_cast<float>(width - 1U);
    terrain.heights.assign(std::size_t{ width } * height, 0.0F);
    terrain.holes.assign(std::size_t{ width - 1U } * (height - 1U), 0U);
    terrain.collisionStep = collisionStep;
    for (std::uint32_t z = 0U; z < height; ++z) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            terrain.heights[std::size_t{ z } * width + x] = PlaneHeight(WorldX(terrain, x), WorldZ(terrain, z));
        }
    }
    return terrain;
}

void Step(kb::scene::Scene& scene, int frames) {
    for (int frame = 0; frame < frames; ++frame) {
        kb::tests::Require(scene.Runtime().Update(kStep), "Physics update failed");
        const auto errors = scene.Runtime().DrainSceneSystemErrors();
        if (!errors.empty()) std::cerr << "Scene system error: " << errors.front() << '\n';
        kb::tests::Require(errors.empty(), "Physics reported an error");
    }
}

double StepMilliseconds(kb::scene::Scene& scene) {
    const auto started = std::chrono::steady_clock::now();
    Step(scene, 1);
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
}

// A terrain asset on disk, registered with a scene that has the Jolt plugin, and the object that carries its collider.
class TerrainWorld {
public:
    TerrainWorld(const std::string& name, TerrainAsset terrain)
        : scene_(Descriptor())
        , terrain_(std::move(terrain)) {
        const std::filesystem::path folder = std::filesystem::temp_directory_path() / "kb_terrain_collision_tests";
        std::filesystem::create_directories(folder);
        file_ = folder / (name + ".kbterrain");
        std::string error;
        kb::tests::Require(kb::assets::TerrainAssetIO::Save(file_, terrain_, &error), "The terrain asset was not saved");
        const std::string virtualPath = "/Game/" + name + ".kbterrain";
        id_ = kb::assets::MakeAssetId(kb::assets::NormalizeAssetPath(virtualPath) + ":RenderMesh");
        kb::tests::Require(scene_.Assets().Manager().RegisterAsset({ .id = id_, .type = "RenderMesh", .virtualPath = virtualPath,
            .physicalPath = file_, .sourceExtension = ".kbterrain", .contentHash = 1U }), "The terrain asset was not registered");
        kb::scene::PhysicsBackend::SetStepPipelining(scene_, false);
        kb::tests::Require(scene_.IsModuleActive("Physics.Jolt"), "The Jolt plugin is not active");
        AddGround();
    }

    ~TerrainWorld() {
        std::error_code error;
        std::filesystem::remove(file_, error);
    }

    void AddGround() {
        ground_ = scene_.Entities().CreateObject({ .name = "Ground" });
        scene_.Components().Colliders().Set(ground_.Entity(), { .shape = ColliderShape::Terrain, .meshAssetId = id_.value });
    }

    void RemoveGround() { scene_.Entities().Destroy(ground_); }

    kb::scene::Scene& Scene() { return scene_; }
    TerrainAsset& Terrain() { return terrain_; }
    kb::assets::AssetId Id() const { return id_; }

    // The asset on disk takes what the test changed, as the editor saves a stroke.
    void Save() {
        kb::tests::Require(kb::assets::TerrainAssetIO::Save(file_, terrain_), "The terrain asset was not saved");
    }

    // The height of the first thing under (x, z), if the ground has anything there.
    std::optional<float> GroundY(float x, float z) {
        std::array<kb::scene::PhysicsCastResult, 4> storage{};
        kb::library::ArrayNonAlloc<kb::scene::PhysicsCastResult> results(storage);
        kb::scene::RaycastAllNonAlloc(scene_, { x, 1000.0F, z }, { 0.0F, -1.0F, 0.0F }, 2000.0F, kb::scene::kPhysicsAllLayers, results);
        if (results.Count() == 0U) return std::nullopt;
        return results.GetAt(0)->point.y;
    }

    void RequireGround(float x, float z, float expected, float tolerance, const char* message) {
        const std::optional<float> y = GroundY(x, z);
        if (!y || std::abs(*y - expected) > tolerance) {
            std::cerr << message << ": at " << x << ", " << z << " expected " << expected << " got "
                      << (y ? std::to_string(*y) : std::string{ "no hit" }) << '\n';
            kb::tests::Require(false, message);
        }
    }

    void RequireNoGround(float x, float z, const char* message) {
        const std::optional<float> y = GroundY(x, z);
        if (y) std::cerr << message << ": at " << x << ", " << z << " hit " << *y << '\n';
        kb::tests::Require(!y, message);
    }

private:
    kb::scene::Scene scene_;
    TerrainAsset terrain_;
    std::filesystem::path file_;
    kb::assets::AssetId id_{};
    kb::scene::SceneObject ground_{};
};

void RunHeightsMatchTheAsset() {
    TerrainAsset terrain = MakeSlope(129U, 129U, 128.0F);
    for (std::uint32_t z = 40U; z <= 45U; ++z) {
        for (std::uint32_t x = 40U; x <= 45U; ++x) terrain.holes[std::size_t{ z } * 128U + x] = 1U;
    }
    TerrainWorld world("Heights", terrain);
    Step(world.Scene(), 2);
    for (const auto& [x, z] : { std::pair{ 0.0F, 0.0F }, { -63.9F, -63.9F }, { 63.9F, -63.9F }, { -63.9F, 63.9F }, { 63.9F, 63.9F },
             { 10.25F, -33.5F }, { -50.0F, 20.0F }, { 0.5F, 0.5F } }) {
        world.RequireGround(x, z, PlaneHeight(x, z), 0.01F, "Ground height differs from the terrain asset");
    }
    world.RequireNoGround(64.5F, 0.0F, "The ground continues past the terrain edge");
    world.RequireNoGround(-64.5F, 0.0F, "The ground continues past the terrain edge");
    world.RequireNoGround(0.0F, 64.5F, "The ground continues past the terrain edge");
    world.RequireNoGround(0.0F, -64.5F, "The ground continues past the terrain edge");
    // Cells 40 to 45 are the hole: x = 40 + 0.5 - 64 and so on.
    world.RequireNoGround(-23.5F, -23.5F, "A hole cell has ground");
    world.RequireNoGround(-18.5F, -18.5F, "A hole cell has ground");
    world.RequireGround(-24.5F, -23.5F, PlaneHeight(-24.5F, -23.5F), 0.01F, "The cell beside a hole lost its ground");
    world.RequireGround(-17.5F, -18.5F, PlaneHeight(-17.5F, -18.5F), 0.01F, "The cell beside a hole lost its ground");

    // A terrain that is not square, sampled every second height.
    TerrainWorld wide("Wide", MakeSlope(129U, 65U, 128.0F, 2U));
    Step(wide.Scene(), 2);
    for (const auto& [x, z] : { std::pair{ 0.0F, 0.0F }, { -63.9F, -31.9F }, { 63.9F, 31.9F }, { 30.0F, -20.0F } }) {
        wide.RequireGround(x, z, PlaneHeight(x, z), 0.01F, "Ground height of a coarse non-square terrain differs from the asset");
    }
    wide.RequireNoGround(0.0F, 32.5F, "A non-square terrain has ground past its short side");
    wide.RequireNoGround(64.5F, 0.0F, "A non-square terrain has ground past its long side");
}

void RunBodyLandsOnTheGround() {
    TerrainWorld world("Landing", MakeSlope(129U, 129U, 128.0F));
    const auto box = world.Scene().Entities().CreateObject({ .name = "Box", .transform = { .localPosition = { 10.0F, 20.0F, -20.0F } } });
    world.Scene().Components().Colliders().Set(box.Entity(), { .boxSize = { 1.0F, 1.0F, 1.0F } });
    world.Scene().Components().Rigidbodies().Set(box.Entity(), { .bodyType = kb::scene::RigidbodyBodyType::Dynamic });
    Step(world.Scene(), 240);
    const Vec3 at = world.Scene().Transforms().Get(box.Entity()).localPosition;
    const float expected = PlaneHeight(at.x, at.z) + 0.5F * std::sqrt(1.0F + 0.1F * 0.1F + 0.05F * 0.05F);
    if (std::abs(at.y - expected) > 0.05F) std::cerr << "Box rests at " << at.y << ", expected " << expected << '\n';
    kb::tests::Require(std::abs(at.y - expected) <= 0.05F, "A falling box did not land on the terrain surface");
}

void RunCollisionFollowsTheBrush() {
    TerrainWorld world("Brush", MakeSlope(129U, 129U, 128.0F));
    const auto box = world.Scene().Entities().CreateObject({ .name = "Box", .transform = { .localPosition = { -45.0F, 20.0F, -45.0F } } });
    world.Scene().Components().Colliders().Set(box.Entity(), { .boxSize = { 1.0F, 1.0F, 1.0F } });
    world.Scene().Components().Rigidbodies().Set(box.Entity(), { .bodyType = kb::scene::RigidbodyBodyType::Dynamic });
    Step(world.Scene(), 240);
    const Vec3 restedAt = world.Scene().Transforms().Get(box.Entity()).localPosition;

    TerrainAsset& terrain = world.Terrain();
    const TerrainAsset before = terrain;
    kb::terrain_editor::TerrainBrushSettings settings;
    settings.mode = kb::terrain_editor::TerrainBrushMode::Raise;
    settings.radius = 8.0F;
    settings.strength = 6.0F;
    kb::terrain_editor::TerrainBrushResult result{};
    for (int stamp = 0; stamp < 4; ++stamp) {
        // A stroke reports every stamp; the shape takes them together.
        const kb::terrain_editor::TerrainBrushResult one = kb::terrain_editor::ApplyTerrainBrush(terrain, settings, { 20.0F, 20.0F, 1.0F });
        kb::tests::Require(one.Changed(), "The Raise brush changed nothing");
        kb::tests::Require(kb::scene::PhysicsBackend::UpdateTerrain(world.Scene(), world.Id().value, terrain,
            { one.minX, one.minZ, one.maxX, one.maxZ }), "The physics world did not take the changed region");
        result.minX = std::min(result.minX, one.minX);
        result.minZ = std::min(result.minZ, one.minZ);
        result.maxX = std::max(result.maxX, one.maxX);
        result.maxZ = std::max(result.maxZ, one.maxZ);
    }
    world.Save();
    float raised = 0.0F;
    for (std::size_t index = 0U; index < terrain.heights.size(); ++index) raised = std::max(raised, terrain.heights[index] - before.heights[index]);
    std::cerr << "Raise brush: samples " << result.minX << ".." << result.maxX << " x " << result.minZ << ".." << result.maxZ
              << ", highest rise " << raised << " m\n";
    kb::tests::Require(raised > 0.2F, "The Raise brush raised the ground too little to tell");

    Step(world.Scene(), 1);
    // Every sample of the changed region now has the new height, a few samples past it the old one.
    std::size_t compared = 0U;
    for (std::uint32_t z = result.minZ; z <= result.maxZ; z += 3U) {
        for (std::uint32_t x = result.minX; x <= result.maxX; x += 3U) {
            const float wx = WorldX(terrain, x);
            const float wz = WorldZ(terrain, z);
            const std::optional<float> y = world.GroundY(wx + 0.001F, wz + 0.001F);
            kb::tests::Require(y.has_value(), "No ground in the changed region");
            const float expected = terrain.heights[std::size_t{ z } * terrain.width + x];
            if (std::abs(*y - expected) > 0.05F) std::cerr << "Sample " << x << ", " << z << " expected " << expected << " got " << *y << '\n';
            kb::tests::Require(std::abs(*y - expected) <= 0.05F, "Collision does not follow the brush");
            ++compared;
        }
    }
    kb::tests::Require(compared > 4U, "The brush test compared too few samples");
    world.RequireGround(-40.0F, -40.0F, PlaneHeight(-40.0F, -40.0F), 0.01F, "Ground outside the brush region changed");
    world.RequireGround(45.0F, 5.0F, PlaneHeight(45.0F, 5.0F), 0.01F, "Ground outside the brush region changed");

    // A body away from the changed region stays where it rests.
    Step(world.Scene(), 60);
    const Vec3 stayed = world.Scene().Transforms().Get(box.Entity()).localPosition;
    kb::tests::Require(std::abs(stayed.y - restedAt.y) < 0.01F && std::abs(stayed.x - restedAt.x) < 0.01F,
        "A body away from the changed region moved");

    // The same body resting where the ground rises, a little every step, is lifted with it.
    settings.mode = kb::terrain_editor::TerrainBrushMode::Raise;
    settings.radius = 6.0F;
    settings.strength = 0.01F;
    for (int round = 0; round < 120; ++round) {
        const kb::terrain_editor::TerrainBrushResult one = kb::terrain_editor::ApplyTerrainBrush(terrain, settings, { -45.0F, -45.0F, 1.0F });
        kb::tests::Require(one.Changed() && kb::scene::PhysicsBackend::UpdateTerrain(world.Scene(), world.Id().value, terrain,
            { one.minX, one.minZ, one.maxX, one.maxZ }), "A small raise was not taken");
        Step(world.Scene(), 1);
    }
    Step(world.Scene(), 120);
    const Vec3 lifted = world.Scene().Transforms().Get(box.Entity()).localPosition;
    const std::optional<float> under = world.GroundY(lifted.x, lifted.z + 3.0F);
    std::cerr << "Box rested at " << restedAt.y << ", lifted to " << lifted.y << '\n';
    kb::tests::Require(lifted.y > restedAt.y + 0.5F && lifted.y < restedAt.y + 3.0F && under, "A body on the raised ground was not lifted with it");

    // A hole is cut and filled again.
    settings.mode = kb::terrain_editor::TerrainBrushMode::CutHole;
    settings.radius = 6.0F;
    const kb::terrain_editor::TerrainBrushResult cut = kb::terrain_editor::ApplyTerrainBrush(terrain, settings, { -30.0F, -30.0F, 1.0F });
    kb::tests::Require(cut.Changed(), "The hole brush changed nothing");
    const kb::assets::TerrainSampleRegion cells{ cut.minX, cut.minZ, cut.maxX + 1U, cut.maxZ + 1U };
    kb::tests::Require(kb::scene::PhysicsBackend::UpdateTerrain(world.Scene(), world.Id().value, terrain, cells), "The hole was not taken");
    Step(world.Scene(), 1);
    world.RequireNoGround(-30.0F, -30.0F, "A cut hole still has ground");
    world.RequireGround(-30.0F, -45.0F, PlaneHeight(-30.0F, -45.0F), 0.01F, "Ground away from the hole changed");
    settings.mode = kb::terrain_editor::TerrainBrushMode::FillHole;
    const kb::terrain_editor::TerrainBrushResult fill = kb::terrain_editor::ApplyTerrainBrush(terrain, settings, { -30.0F, -30.0F, 1.0F });
    kb::tests::Require(kb::scene::PhysicsBackend::UpdateTerrain(world.Scene(), world.Id().value, terrain,
        { fill.minX, fill.minZ, fill.maxX + 1U, fill.maxZ + 1U }), "The filled hole was not taken");
    Step(world.Scene(), 1);
    world.RequireGround(-30.0F, -30.0F, terrain.heights[std::size_t{ 34U } * terrain.width + 34U], 0.5F, "A filled hole has no ground");

    // Ground raised past the height range the shape was made for: the shape is made again.
    for (std::uint32_t z = 90U; z < 100U; ++z) {
        for (std::uint32_t x = 90U; x < 100U; ++x) terrain.heights[std::size_t{ z } * terrain.width + x] = 400.0F;
    }
    kb::tests::Require(kb::scene::PhysicsBackend::UpdateTerrain(world.Scene(), world.Id().value, terrain, { 90U, 90U, 99U, 99U }),
        "The very high ground was not taken");
    Step(world.Scene(), 1);
    world.RequireGround(WorldX(terrain, 95U), WorldZ(terrain, 95U), 400.0F, 0.05F, "Ground raised past the shape's range is not there");
    world.RequireGround(-40.0F, -40.0F, PlaneHeight(-40.0F, -40.0F), 0.01F, "Ground changed when the shape was made again");
    // Without a physics world the same call costs nothing and takes nothing.
    kb::scene::Scene plain{ kb::project::ProjectDescriptor{} };
    kb::tests::Require(!kb::scene::PhysicsBackend::UpdateTerrain(plain, world.Id().value, terrain, { 0U, 0U, 1U, 1U }),
        "A scene without physics took a terrain update");
}

// Time of a terrain edit against making the whole shape again, on the two largest terrains.
void RunUpdateCost(std::uint32_t resolution) {
    TerrainAsset terrain = MakeSlope(resolution, resolution, static_cast<float>(resolution - 1U));
    for (std::uint32_t z = 0U; z < resolution; ++z) {
        for (std::uint32_t x = 0U; x < resolution; ++x) {
            terrain.heights[std::size_t{ z } * resolution + x] += 3.0F * std::sin(static_cast<float>(x) * 0.07F) * std::cos(static_cast<float>(z) * 0.05F);
        }
    }
    TerrainWorld world("Cost" + std::to_string(resolution), std::move(terrain));
    Step(world.Scene(), 3);
    TerrainAsset& edited = world.Terrain();
    double idle = 1.0e9;
    for (int frame = 0; frame < 5; ++frame) idle = std::min(idle, StepMilliseconds(world.Scene()));

    double rebuild = 1.0e9;
    for (int round = 0; round < 3; ++round) {
        world.RemoveGround();
        Step(world.Scene(), 1);
        world.AddGround();
        rebuild = std::min(rebuild, StepMilliseconds(world.Scene()));
    }
    const std::uint32_t middle = resolution / 2U;
    const auto edit = [&](std::uint32_t half) {
        const kb::assets::TerrainSampleRegion region{ middle - half, middle - half, middle + half, middle + half };
        for (std::uint32_t z = region.minZ; z <= region.maxZ; ++z) {
            for (std::uint32_t x = region.minX; x <= region.maxX; ++x) edited.heights[std::size_t{ z } * resolution + x] += 0.5F;
        }
        const auto started = std::chrono::steady_clock::now();
        kb::tests::Require(kb::scene::PhysicsBackend::UpdateTerrain(world.Scene(), world.Id().value, edited, region), "Edit not taken");
        const double prepare = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        return prepare + StepMilliseconds(world.Scene()) - idle;
    };
    double brush = 1.0e9;
    double large = 1.0e9;
    for (int round = 0; round < 3; ++round) {
        brush = std::min(brush, edit(8U));
        large = std::min(large, edit(64U));
    }
    const double whole = [&] {
        const auto started = std::chrono::steady_clock::now();
        kb::tests::Require(kb::scene::PhysicsBackend::UpdateTerrain(world.Scene(), world.Id().value, edited, { 0U, 0U, resolution - 1U, resolution - 1U }),
            "Edit not taken");
        const double prepare = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        return prepare + StepMilliseconds(world.Scene()) - idle;
    }();
    std::cerr << "Terrain " << resolution << "x" << resolution << ": make the shape again " << rebuild << " ms, edit 17x17 samples " << brush
              << " ms, edit 129x129 " << large << " ms, rewrite all samples in place " << whole << " ms (a step with no edit: " << idle << " ms)\n";
    kb::tests::Require(brush < rebuild, "Editing a brush region costs as much as making the shape again");
    world.RequireGround(WorldX(edited, middle), WorldZ(edited, middle), edited.heights[std::size_t{ middle } * resolution + middle], 0.05F,
        "Ground differs after the timed edits");
}

} // namespace

int main() {
    RunHeightsMatchTheAsset();
    RunBodyLandsOnTheGround();
    RunCollisionFollowsTheBrush();
    RunUpdateCost(1025U);
    RunUpdateCost(2049U);
    std::cout << "Terrain collision tests passed\n";
    return 0;
}
