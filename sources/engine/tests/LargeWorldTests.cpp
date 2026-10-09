#include "TestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/library/EngineLibraryCollections.hpp"
#include "engine/math/DVec3.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocument.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

using kb::math::DVec3;
using kb::tests::Require;

// The worlds these tests place objects in: ten thousand kilometres from the origin on two axes, where neighbouring
// floats are a metre apart.
constexpr double kFar = 10'000'000.0;
// What "sub-millimetre" is checked against: a tenth of a millimetre.
constexpr double kTolerance = 0.0001;

[[nodiscard]] bool Near(const DVec3& lhs, const DVec3& rhs, double tolerance = kTolerance) noexcept {
    return kb::math::Distance(lhs, rhs) <= tolerance;
}

[[nodiscard]] kb::scene::Quat YawQuat(double radians) noexcept {
    return kb::scene::Quat{ 0.0F, static_cast<float>(std::sin(radians * 0.5)), 0.0F, static_cast<float>(std::cos(radians * 0.5)) };
}

// What the transform sync must produce for parent * local, computed independently in double precision.
[[nodiscard]] DVec3 Compose(const DVec3& parentWorld, kb::scene::Quat parentRotation, kb::scene::Vec3 parentScale, const DVec3& local) noexcept {
    const DVec3 scaled{ local.x * parentScale.x, local.y * parentScale.y, local.z * parentScale.z };
    return parentWorld + kb::math::RotateDouble(parentRotation, scaled);
}

[[nodiscard]] std::filesystem::path TempRoot() {
    return std::filesystem::temp_directory_path() / "21kb_large_world_tests";
}

void CleanTempRoot() {
    std::error_code error;
    std::filesystem::remove_all(TempRoot(), error);
    std::filesystem::create_directories(TempRoot(), error);
}

// A root at (1e7, 0, 1e7) keeps its translation to far below a millimetre, and the float fields show it rounded.
void RunFarRootTranslationTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Far Root" });
    const DVec3 target{ kFar + 0.123456789, 12.5, kFar - 0.987654321 };
    scene.Transforms().SetLocalTranslation(object.Entity(), target);
    scene.Runtime().SynchronizeTransforms();

    Require(Near(scene.Transforms().LocalTranslation(object.Entity()), target, 1e-6), "A far root lost its local translation");
    Require(Near(scene.Transforms().WorldTranslation(object.Entity()), target, 1e-6), "A far root lost its world translation");
    const kb::scene::TransformComponent transform = scene.Transforms().Get(object);
    Require(transform.localPosition.x == static_cast<float>(target.x) && transform.worldPosition.z == static_cast<float>(target.z),
        "The float translation of a far root is not its double translation rounded to float");

    // Two objects half a millimetre apart stay half a millimetre apart; in float they would coincide.
    const kb::scene::SceneObject neighbour = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Far Neighbour" });
    scene.Transforms().SetLocalTranslation(neighbour.Entity(), target + DVec3{ 0.0005, 0.0, 0.0 });
    scene.Runtime().SynchronizeTransforms();
    const double separation = scene.Transforms().WorldTranslation(neighbour.Entity()).x - scene.Transforms().WorldTranslation(object.Entity()).x;
    Require(std::abs(separation - 0.0005) <= 1e-6, "Two far objects half a millimetre apart lost their separation");
    Require(scene.Transforms().Get(neighbour).worldPosition.x == transform.worldPosition.x,
        "The test expects the two far objects to share their float translation");
}

// Children, grandchildren and a child far from its parent compose in double precision through rotated and scaled
// parents.
void RunFarHierarchyCompositionTest() {
    kb::scene::Scene scene;
    const DVec3 rootTranslation{ kFar + 0.3125, 4.0, kFar + 0.7 };
    const kb::scene::Quat rootRotation = YawQuat(0.5235987755982988);
    const kb::scene::SceneObject root = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Root", .transform = kb::scene::TransformComponent{ .localRotation = rootRotation } });
    scene.Transforms().SetLocalTranslation(root.Entity(), rootTranslation);
    const kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Child", .parent = root,
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 0.25F, 1.0F, -0.5F }, .localScale = kb::scene::Vec3{ 2.0F, 2.0F, 2.0F } } });
    const kb::scene::SceneObject grandchild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Grandchild", .parent = child,
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 0.001F, 0.0F, 0.0005F } } });
    scene.Runtime().SynchronizeTransforms();

    const kb::scene::TransformComponent rootTransform = scene.Transforms().Get(root);
    const kb::scene::TransformComponent childTransform = scene.Transforms().Get(child);
    const DVec3 expectedChild = Compose(rootTranslation, rootTransform.worldRotation, rootTransform.worldScale, kb::math::ToDVec3(kb::scene::Vec3{ 0.25F, 1.0F, -0.5F }));
    const DVec3 expectedGrandchild = Compose(expectedChild, childTransform.worldRotation, childTransform.worldScale, kb::math::ToDVec3(kb::scene::Vec3{ 0.001F, 0.0F, 0.0005F }));
    Require(Near(scene.Transforms().WorldTranslation(child.Entity()), expectedChild, 1e-6), "A child of a far root lost precision");
    Require(Near(scene.Transforms().WorldTranslation(grandchild.Entity()), expectedGrandchild, 1e-6), "A grandchild of a far root lost precision");
    // The millimetre between grandchild and child survives (doubled by the child's scale).
    const double offset = kb::math::Distance(scene.Transforms().WorldTranslation(grandchild.Entity()), scene.Transforms().WorldTranslation(child.Entity()));
    Require(std::abs(offset - 2.0 * std::sqrt(0.001 * 0.001 + 0.0005 * 0.0005)) <= 1e-6, "The offset of a far grandchild from its parent was lost");

    // A grouping object at the origin with a child placed far away (local translation = world translation).
    const kb::scene::SceneObject folder = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Folder" });
    const kb::scene::SceneObject member = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Member", .parent = folder });
    const DVec3 memberTranslation{ -kFar - 0.0421, 1.5, kFar + 0.0007 };
    scene.Transforms().SetLocalTranslation(member.Entity(), memberTranslation);
    scene.Runtime().SynchronizeTransforms();
    Require(Near(scene.Transforms().WorldTranslation(member.Entity()), memberTranslation, 1e-6), "A far child of an object at the origin lost precision");

    // A parent placed with float fields only (no part below float precision) still gives its children exact offsets.
    const kb::scene::SceneObject floatParent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Float Parent", .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 10000000.0F, 0.0F, 10000000.0F } } });
    const kb::scene::SceneObject floatChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Float Child", .parent = floatParent, .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 0.0001F, 0.0F, 0.0F } } });
    scene.Runtime().SynchronizeTransforms();
    Require(std::abs(scene.Transforms().WorldTranslation(floatChild.Entity()).x - (kFar + static_cast<double>(0.0001F))) <= 1e-9,
        "A tenth of a millimetre below a far float parent was lost");
}

// Float writes keep working: a fresh transform is exact, a residual survives a write that keeps the float translation
// (a rotation edit) and is dropped once the float translation moved away from it.
void RunFloatWritesNextToDoubleTranslationsTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Edited" });
    const DVec3 target{ kFar + 0.3, 0.0, 0.0 };
    scene.Transforms().SetLocalTranslation(object.Entity(), target);

    kb::scene::TransformComponent rotated = scene.Transforms().Get(object);
    rotated.localRotation = YawQuat(1.0);
    scene.Transforms().Set(object, rotated);
    Require(Near(scene.Transforms().LocalTranslation(object.Entity()), target, 1e-6), "A rotation edit through the float API dropped the double translation");

    scene.Transforms().Set(object, kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 5.0F, 0.0F, 0.0F } });
    scene.Runtime().SynchronizeTransforms();
    Require(scene.Transforms().LocalTranslation(object.Entity()) == DVec3{ 5.0, 0.0, 0.0 } &&
            scene.Transforms().WorldTranslation(object.Entity()) == DVec3{ 5.0, 0.0, 0.0 },
        "A float write moving an object near the origin kept a residual from far away");
}

// A transform pass writes double-precision translations from its workers.
void RunParallelPassDoubleTranslationTest() {
    kb::scene::Scene scene;
    std::vector<kb::scene::SceneEntity> entities;
    for (int index = 0; index < 4096; ++index) {
        entities.push_back(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Pass" }).Entity());
    }
    const auto expected = [](kb::scene::SceneEntity entity) {
        const double offset = static_cast<double>(entity.Id() % 1000U) * 0.0001;
        return DVec3{ kFar + offset, offset, -kFar - offset };
    };
    static_cast<void>(scene.Transforms().ParallelForEachRoot(256U, [&expected](kb::scene::TransformRowRange& range) {
        for (std::size_t row = 0U; row < range.Count(); ++row) {
            const kb::scene::TransformComponent& current = range.Get(row);
            range.SetLocal(row, expected(range.Entity(row)), current.localRotation, current.localScale);
        }
    }));
    scene.Runtime().SynchronizeTransforms();
    for (const kb::scene::SceneEntity entity : entities) {
        Require(Near(scene.Transforms().WorldTranslation(entity), expected(entity), 1e-6), "A transform pass lost a double-precision translation");
    }
}

class FarMover final : public kb::scene::SceneSystem {
public:
    explicit FarMover(kb::scene::SceneEntity entity) noexcept : entity_(entity) {}
    [[nodiscard]] bool RequiresFixedStep() const override { return true; }
    void OnFixedUpdate(kb::scene::SceneSystemContext& context) override {
        position_.x += 0.001;
        context.GetScene().Transforms().SetLocalTranslation(entity_, position_);
    }

private:
    kb::scene::SceneEntity entity_{};
    DVec3 position_{ kFar, 0.0, kFar };
};

// The interpolated pose between two fixed steps of an object far away moves by fractions of a millimetre.
void RunFarInterpolationTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Interpolated" });
    scene.Transforms().SetInterpolated(object.Entity(), true);
    scene.Transforms().SetLocalTranslation(object.Entity(), DVec3{ kFar, 0.0, kFar });
    static_cast<void>(scene.Runtime().AddSceneSystem(std::make_unique<FarMover>(object.Entity())));
    const float fixedDelta = scene.Runtime().FixedStepSettings().fixedDeltaSeconds;
    static_cast<void>(scene.Runtime().Update(fixedDelta * 3.5F));
    const std::size_t steps = scene.Runtime().LastFixedStepCount();
    Require(steps == 3U, "The interpolation test expects three fixed steps");
    const double alpha = static_cast<double>(scene.Runtime().FixedInterpolationAlpha());
    const std::optional<DVec3> interpolated = scene.Runtime().InterpolatedWorldTranslation(object.Entity());
    const double expected = kFar + 0.001 * (static_cast<double>(steps) - 1.0) + 0.001 * alpha;
    Require(interpolated.has_value() && std::abs(interpolated->x - expected) <= 1e-6 && std::abs(interpolated->z - kFar) <= 1e-6,
        "The interpolated pose of a far object lost precision");
}

// Duplicates, prefab assets and prefab instances keep double-precision translations.
void RunFarPrefabAndDuplicateTest() {
    CleanTempRoot();
    kb::scene::Scene scene;
    const kb::scene::SceneObject root = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Far Prefab Root" });
    const DVec3 rootTranslation{ kFar + 0.0123, 7.25, -kFar - 0.4567 };
    scene.Transforms().SetLocalTranslation(root.Entity(), rootTranslation);
    const kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Far Prefab Child", .parent = root });
    const DVec3 childTranslation{ 0.1, 0.000123, -0.2 };
    scene.Transforms().SetLocalTranslation(child.Entity(), childTranslation);

    const kb::scene::SceneObject duplicate = scene.Entities().Duplicate(root);
    Require(scene.Transforms().LocalTranslation(duplicate.Entity()) == scene.Transforms().LocalTranslation(root.Entity()),
        "A duplicate lost the double-precision translation of its source");

    const kb::scene::ScenePrefabHandle handle = scene.Prefabs().CaptureRegistered(root, "Far Prefab");
    Require(handle.IsValid(), "The far prefab could not be captured");
    const kb::scene::ScenePrefab captured = scene.Prefabs().Get(handle);
    Require(captured.Nodes().size() == 2U && captured.Nodes()[0].LocalTranslation() == scene.Transforms().LocalTranslation(root.Entity()),
        "A prefab capture lost the double-precision translation of its root");
    const std::filesystem::path path = TempRoot() / "FarPrefab.kbprefab";
    Require(scene.Prefabs().Save(handle, path), "The far prefab could not be saved");

    kb::scene::Scene target;
    const kb::scene::ScenePrefabHandle loaded = target.Prefabs().Load(path);
    Require(loaded.IsValid(), "The far prefab could not be loaded");
    const kb::scene::ScenePrefab loadedPrefab = target.Prefabs().Get(loaded);
    Require(loadedPrefab.Nodes()[0].LocalTranslation() == captured.Nodes()[0].LocalTranslation() &&
            loadedPrefab.Nodes()[1].LocalTranslation() == captured.Nodes()[1].LocalTranslation(),
        "A prefab asset did not round-trip its double-precision translations exactly");
    const kb::scene::ScenePrefabInstance instance = target.Prefabs().Instantiate(loaded);
    target.Runtime().SynchronizeTransforms();
    Require(target.Transforms().LocalTranslation(instance.ObjectAt(0U).Entity()) == captured.Nodes()[0].LocalTranslation(),
        "A prefab instance lost the double-precision translation of its root");
    const DVec3 expectedChild = Compose(rootTranslation, kb::scene::Quat{}, kb::scene::Vec3{ 1.0F, 1.0F, 1.0F }, childTranslation);
    Require(Near(target.Transforms().WorldTranslation(instance.ObjectAt(1U).Entity()), expectedChild, 1e-6),
        "A prefab instance child lost precision far from the origin");
    const std::vector<kb::scene::ScenePrefabInstance> many = target.Prefabs().InstantiateMany(loaded, 3U);
    target.Runtime().SynchronizeTransforms();
    for (const kb::scene::ScenePrefabInstance& copy : many) {
        Require(target.Transforms().LocalTranslation(copy.ObjectAt(0U).Entity()) == captured.Nodes()[0].LocalTranslation(),
            "A bulk prefab instance lost the double-precision translation of its root");
    }
}

// Scene files store translations in double precision from version 42.
void RunFarSceneDocumentRoundTripTest() {
    CleanTempRoot();
    kb::scene::Scene scene;
    const kb::scene::SceneObject root = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Far Scene Root" });
    const DVec3 rootTranslation{ kFar + 0.123456789012, -3.5, kFar + 0.000001 };
    scene.Transforms().SetLocalTranslation(root.Entity(), rootTranslation);
    const kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Far Scene Child", .parent = root });
    scene.Transforms().SetLocalTranslation(child.Entity(), DVec3{ 0.5, 0.0004, 0.25 });
    const DVec3 savedRoot = scene.Transforms().LocalTranslation(root.Entity());
    const DVec3 savedChild = scene.Transforms().LocalTranslation(child.Entity());

    const std::filesystem::path path = TempRoot() / "FarScene.21kbscene";
    Require(kb::scene::SceneDocumentService::Save(scene, path, "FarScene"), "The far scene could not be saved");
    const kb::scene::SceneDocumentLoadResult loaded = kb::scene::SceneDocumentService::Load(path);
    Require(loaded.succeeded && loaded.document.fileVersion == kb::scene::SceneDocument::CurrentFileVersion &&
            kb::scene::SceneDocument::CurrentFileVersion >= kb::scene::SceneDocument::DoubleTranslationFileVersion,
        "The far scene did not load as a current file");

    kb::scene::Scene target;
    Require(kb::scene::SceneDocumentService::LoadIntoScene(target, loaded.document), "The far scene could not be loaded into a scene");
    kb::scene::SceneEntity loadedRoot{};
    kb::scene::SceneEntity loadedChild{};
    for (const kb::scene::SceneEntity entity : target.Hierarchy().RootEntities()) {
        if (target.Entities().Name(entity) == "Far Scene Root") loadedRoot = entity;
    }
    Require(loadedRoot.IsValid(), "The far scene root was not loaded");
    for (const kb::scene::SceneEntity entity : target.Hierarchy().ChildEntities(loadedRoot)) loadedChild = entity;
    Require(loadedChild.IsValid(), "The far scene child was not loaded");
    Require(target.Transforms().LocalTranslation(loadedRoot) == savedRoot && target.Transforms().LocalTranslation(loadedChild) == savedChild,
        "A scene file did not round-trip double-precision translations exactly");
    target.Runtime().SynchronizeTransforms();
    Require(Near(target.Transforms().WorldTranslation(loadedRoot), rootTranslation, 1e-6), "A loaded far scene root lost precision");
}

[[nodiscard]] std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    return std::vector<std::uint8_t>{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

[[nodiscard]] bool WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(output);
}

void WriteLittleEndian(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value, std::size_t size) {
    Require(offset + size <= bytes.size(), "A legacy scene fixture write exceeded its buffer");
    for (std::size_t byte = 0U; byte < size; ++byte) bytes[offset + byte] = static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xFFU);
}

[[nodiscard]] std::uint32_t ReadUInt32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    Require(offset + 4U <= bytes.size(), "A legacy scene fixture read exceeded its buffer");
    std::uint32_t value = 0U;
    for (std::size_t byte = 0U; byte < 4U; ++byte) value |= static_cast<std::uint32_t>(bytes[offset + byte]) << (byte * 8U);
    return value;
}

// A version 41 file (float32 translations) loads unchanged and is written back as a current file.
void RunVersion41SceneMigrationTest() {
    CleanTempRoot();
    const kb::scene::Vec3 legacyTranslation{ 1234.5F, 2.25F, -7.75F };
    kb::scene::Scene scene;
    const kb::scene::SceneObject root = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Legacy Root", .transform = kb::scene::TransformComponent{ .localPosition = legacyTranslation } });
    static_cast<void>(root);
    const std::filesystem::path path = TempRoot() / "Version41.21kbscene";
    Require(kb::scene::SceneDocumentService::Save(scene, path, "Version41"), "The version 41 fixture could not be saved");

    // Rewrite the current file as version 41: the translation goes back to three float32 values.
    std::vector<std::uint8_t> bytes = ReadBytes(path);
    std::vector<std::uint8_t> doubles(24U);
    std::vector<std::uint8_t> floats(12U);
    const double components[3]{ legacyTranslation.x, legacyTranslation.y, legacyTranslation.z };
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        const std::uint64_t doubleBits = std::bit_cast<std::uint64_t>(components[axis]);
        const std::uint32_t floatBits = std::bit_cast<std::uint32_t>(static_cast<float>(components[axis]));
        std::memcpy(doubles.data() + axis * 8U, &doubleBits, 8U);
        std::memcpy(floats.data() + axis * 4U, &floatBits, 4U);
    }
    const auto found = std::search(bytes.begin(), bytes.end(), doubles.begin(), doubles.end());
    Require(found != bytes.end(), "The version 41 fixture did not hold the double translation");
    const auto at = bytes.erase(found, found + 24);
    bytes.insert(at, floats.begin(), floats.end());
    WriteLittleEndian(bytes, 8U, 41U, 4U);
    Require(WriteBytes(path, bytes), "The version 41 fixture could not be rewritten");

    // Its sidecar records the size, FNV-1a hash and CRC32 of the scene bytes.
    std::uint64_t hash = 14695981039346656037ULL;
    std::uint32_t checksum = 0xFFFFFFFFU;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
        checksum ^= byte;
        for (std::uint32_t bit = 0U; bit < 8U; ++bit) checksum = (checksum & 1U) != 0U ? (0xEDB88320U ^ (checksum >> 1U)) : (checksum >> 1U);
    }
    checksum ^= 0xFFFFFFFFU;
    if (hash == 0U) hash = 1099511628211ULL;
    const std::filesystem::path metaPath = path.parent_path() / "Version41.meta";
    std::vector<std::uint8_t> meta = ReadBytes(metaPath);
    std::size_t offset = 12U;
    for (std::uint32_t text = 0U; text < 4U; ++text) offset += 4U + ReadUInt32(meta, offset);
    WriteLittleEndian(meta, offset, bytes.size(), 8U);
    WriteLittleEndian(meta, offset + 8U, hash, 8U);
    WriteLittleEndian(meta, offset + 16U, checksum, 4U);
    Require(WriteBytes(metaPath, meta), "The version 41 sidecar could not be rewritten");

    const kb::scene::SceneDocumentLoadResult loaded = kb::scene::SceneDocumentService::Load(path);
    Require(loaded.succeeded && loaded.document.fileVersion == 41U, "A version 41 scene did not load");
    const std::span<const kb::scene::ScenePrefabNodeDesc> nodes = loaded.document.worldPrefab.Nodes();
    Require(nodes.size() == 1U && nodes[0].transform.localPosition.x == legacyTranslation.x && nodes[0].transform.localPosition.y == legacyTranslation.y &&
            nodes[0].transform.localPosition.z == legacyTranslation.z && nodes[0].LocalTranslation() == kb::math::ToDVec3(legacyTranslation),
        "A version 41 scene did not load its float translation unchanged");

    kb::scene::Scene target;
    Require(kb::scene::SceneDocumentService::LoadIntoScene(target, loaded.document), "A version 41 scene could not be loaded into a scene");
    const std::filesystem::path resaved = TempRoot() / "Resaved.21kbscene";
    Require(kb::scene::SceneDocumentService::Save(target, resaved, "Resaved"), "A version 41 scene could not be saved again");
    const kb::scene::SceneDocumentLoadResult reloaded = kb::scene::SceneDocumentService::Load(resaved);
    Require(reloaded.succeeded && reloaded.document.fileVersion == kb::scene::SceneDocument::CurrentFileVersion &&
            reloaded.document.worldPrefab.Nodes().size() == 1U &&
            reloaded.document.worldPrefab.Nodes()[0].LocalTranslation() == kb::math::ToDVec3(legacyTranslation),
        "A migrated version 41 scene changed its translation when saved as a current file");
}


// Jolt runs in double precision: a box falling onto a floor at (1e7, 0, 1e7) lands where it was dropped, reports its
// contact there, and rays find both with sub-millimetre hit points.
void RunFarPhysicsTest() {
    if (std::filesystem::path{ KB_PHYSICS_JOLT_PLUGIN_PATH }.empty()) {
        return;
    }
    kb::project::ProjectDescriptor descriptor;
    descriptor.disableEnginePluginsByDefault = true;
    descriptor.plugins.push_back(kb::project::ProjectPluginReference{ .name = "Physics.Jolt", .binaryPath = KB_PHYSICS_JOLT_PLUGIN_PATH, .enabled = true });
    kb::scene::Scene scene{ std::move(descriptor) };
    kb::scene::PhysicsBackend::SetStepPipelining(scene, false);
    kb::scene::PhysicsBackend::SetCollisionEventConsumer(scene, true);

    const kb::scene::SceneObject floor = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Far Floor" });
    scene.Transforms().SetLocalTranslation(floor.Entity(), DVec3{ kFar, -0.5, kFar });
    scene.Components().Rigidbodies().Set(floor.Entity(), kb::scene::RigidbodyComponent{ .bodyType = kb::scene::RigidbodyBodyType::Static });
    scene.Components().Colliders().Set(floor.Entity(), kb::scene::ColliderComponent{
        .shape = kb::scene::ColliderShape::Box, .boxSize = kb::scene::Vec3{ 10.0F, 1.0F, 10.0F }, .layer = 0x1U });

    const DVec3 drop{ kFar + 0.25, 4.0, kFar - 0.125 };
    const kb::scene::SceneObject box = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Far Box" });
    scene.Transforms().SetLocalTranslation(box.Entity(), drop);
    scene.Components().Rigidbodies().Set(box.Entity(), kb::scene::RigidbodyComponent{ .bodyType = kb::scene::RigidbodyBodyType::Dynamic, .mass = 1.0F });
    scene.Components().Colliders().Set(box.Entity(), kb::scene::ColliderComponent{
        .shape = kb::scene::ColliderShape::Box, .boxSize = kb::scene::Vec3{ 1.0F, 1.0F, 1.0F }, .layer = 0x2U });

    bool touched = false;
    DVec3 contact{};
    std::vector<kb::scene::PendingCollisionEvent> events;
    for (int step = 0; step < 150; ++step) {
        static_cast<void>(scene.Runtime().Update(1.0F / 60.0F));
        kb::scene::PhysicsBackend::DrainPendingCollisionEvents(scene, events);
        for (const kb::scene::PendingCollisionEvent& event : events) {
            if (!touched && event.target == box.Entity() && event.phase == kb::scene::PhysicsContactPhase::Enter) {
                touched = true;
                contact = event.worldPoint;
            }
        }
    }
    scene.Runtime().SynchronizeTransforms();
    const DVec3 rest = scene.Transforms().WorldTranslation(box.Entity());
    // Jolt keeps a resting box a couple of centimetres inside what it stands on (penetration slop).
    Require(std::abs(rest.y - 0.5) <= 0.05, "A far box did not come to rest on the far floor");
    Require(std::abs(rest.x - drop.x) <= 0.001 && std::abs(rest.z - drop.z) <= 0.001, "A far box drifted while falling straight down");
    Require(touched && std::abs(contact.y) <= 0.05 && std::abs(contact.x - drop.x) <= 0.5 + 0.001 && std::abs(contact.z - drop.z) <= 0.5 + 0.001,
        "The contact between the far box and the far floor was not reported where they touch");

    std::array<kb::scene::PhysicsCastResult, 4U> storage{};
    kb::library::ArrayNonAlloc<kb::scene::PhysicsCastResult> hits{ std::span<kb::scene::PhysicsCastResult>(storage) };
    const DVec3 boxRayOrigin{ rest.x + 0.0003, 10.0, rest.z - 0.0002 };
    kb::scene::RaycastAllNonAllocPrecise(scene, boxRayOrigin, kb::scene::Vec3{ 0.0F, -1.0F, 0.0F }, 20.0F, 0x7FFFFFFFU, hits);
    Require(hits.Count() == 2U && hits.GetAt(0U)->entity == box.Entity() && hits.GetAt(1U)->entity == floor.Entity(),
        "A ray at the far box did not hit the box and then the floor");
    const DVec3 boxHit = hits.GetAt(0U)->worldPoint;
    Require(std::abs(boxHit.x - boxRayOrigin.x) <= 1e-6 && std::abs(boxHit.z - boxRayOrigin.z) <= 1e-6 && std::abs(boxHit.y - (rest.y + 0.5)) <= 0.001,
        "A ray hit on the far box lost precision");
    const DVec3 floorRayOrigin{ kFar - 2.0004, 5.0, kFar + 3.0001 };
    kb::scene::RaycastAllNonAllocPrecise(scene, floorRayOrigin, kb::scene::Vec3{ 0.0F, -1.0F, 0.0F }, 20.0F, 0x1U, hits);
    Require(hits.Count() == 1U && std::abs(hits.GetAt(0U)->worldPoint.x - floorRayOrigin.x) <= 1e-6 &&
            std::abs(hits.GetAt(0U)->worldPoint.z - floorRayOrigin.z) <= 1e-6 && std::abs(hits.GetAt(0U)->worldPoint.y) <= 1e-4,
        "A ray hit on the far floor lost precision");
}
} // namespace

namespace kb::tests {

void RunLargeWorldTests() {
    RunFarRootTranslationTest();
    RunFarHierarchyCompositionTest();
    RunFloatWritesNextToDoubleTranslationsTest();
    RunParallelPassDoubleTranslationTest();
    RunFarInterpolationTest();
    RunFarPrefabAndDuplicateTest();
    RunFarSceneDocumentRoundTripTest();
    RunVersion41SceneMigrationTest();
    RunFarPhysicsTest();
}

} // namespace kb::tests
