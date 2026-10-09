#include "TestSupport.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/IAssetLoader.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneLoadedContent.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTagCatalog.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/world/WorldCellBuilder.hpp"
#include "engine/world/WorldCellIndex.hpp"
#include "engine/world/WorldDescriptor.hpp"
#include "engine/world/WorldEditSession.hpp"
#include "engine/world/WorldObjectFile.hpp"
#include "engine/world/WorldPartitionGrid.hpp"
#include "engine/world/WorldPartitionRuntime.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <typeindex>
#include <vector>

namespace kb::tests {
namespace {

using namespace kb::world;
namespace scene = kb::scene;

void Check(bool condition, const std::string& message) {
    Require(condition, message.c_str());
}

[[nodiscard]] std::filesystem::path FreshDirectory(std::string_view label) {
    static std::atomic<unsigned> counter{ 0U };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("kb_world_" + std::string{ label } + "_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1U)));
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

struct NameSearch {
    scene::Scene* scene = nullptr;
    std::string_view name;
    bool found = false;
};

void VisitName(scene::SceneEntity entity, const scene::TransformComponent&, void* context) {
    auto* search = static_cast<NameSearch*>(context);
    search->found |= search->scene->Entities().Name(entity) == search->name;
}

[[nodiscard]] bool HasEntityNamed(scene::Scene& target, std::string_view name) {
    NameSearch search{ .scene = &target, .name = name, .found = false };
    target.Transforms().ForEach(&VisitName, &search);
    return search.found;
}

[[nodiscard]] std::vector<std::uint8_t> Bytes(const std::filesystem::path& path) {
    return scene::SceneAssetBinaryIO::ReadAllBytes(path);
}

[[nodiscard]] WorldObjectFile MakeObject(const std::string& key, const std::string& name, double x, double z,
    const std::string& layer = {}, bool alwaysLoaded = false, std::uint64_t mesh = 0U) {
    WorldObjectFile object;
    object.header.guid = MakeDeterministicWorldObjectGuid(key);
    object.header.name = name;
    object.header.position = { x, 2.0, z };
    object.header.dataLayer = layer;
    object.header.alwaysLoaded = alwaysLoaded;
    scene::ScenePrefabNodeDesc root;
    root.stableId = WorldObjectStableId(object.header.guid, 0U);
    root.name = name;
    root.transform.localPosition = { static_cast<float>(x), 2.0F, static_cast<float>(z) };
    if (mesh != 0U) {
        root.components.meshRenderer = scene::MeshRendererComponent{ .meshAssetId = mesh };
    }
    scene::ScenePrefabNodeDesc child;
    child.stableId = WorldObjectStableId(object.header.guid, 1U);
    child.name = name + "_part";
    child.parentNode = 0U;
    child.transform.localPosition = { 1.0F, 0.0F, 0.0F };
    if (mesh != 0U) {
        child.components.meshRenderer = scene::MeshRendererComponent{ .meshAssetId = mesh, .materialAssetId = 9U };
    }
    static_cast<void>(object.prefab.AddNode(std::move(root)));
    static_cast<void>(object.prefab.AddNode(std::move(child)));
    object.header.nodeCount = 2U;
    return object;
}

void WriteObject(const std::filesystem::path& directory, const WorldObjectFile& object) {
    std::string error;
    const std::vector<std::uint8_t> bytes = WorldObjectFileIO::Serialize(object, error);
    Check(!bytes.empty(), "world object must serialize: " + error);
    Check(WorldObjectFileIO::WriteBytes(directory / (object.header.guid + ".21kbobject"), bytes, error), "world object must write: " + error);
}

// Answers HLOD requests with a fixed proxy and remembers what it was asked.
class RecordingHlodBaker final : public IWorldHlodBaker {
public:
    [[nodiscard]] WorldHlodResult Bake(const WorldHlodRequest& request) override {
        requests.push_back(request);
        WorldHlodResult result;
        result.succeeded = true;
        result.objText = "v 0 0 0\nv 1 0 0\nv 0 0 1\nusemtl slot0\nf 1 2 3\n";
        result.materials = { 0xF00DF00DF00DF00DULL };
        result.triangleCount = 1U;
        result.sourceTriangleCount = static_cast<std::uint32_t>(request.instances.size()) * 12U;
        return result;
    }
    std::vector<WorldHlodRequest> requests;
};

// Lets an engine-only scene register HLOD meshes the way the renderer's loader would.
class StandInMeshLoader final : public kb::assets::IAssetLoader {
public:
    [[nodiscard]] std::string_view Type() const noexcept override { return "RenderMesh"; }
    [[nodiscard]] std::type_index PayloadType() const noexcept override { return typeid(int); }
    [[nodiscard]] std::vector<std::string> Extensions() const override { return { ".obj" }; }
    [[nodiscard]] kb::assets::AssetLoadResult Load(const kb::assets::AssetLoadRequest&) override {
        return { .asset = std::make_shared<int>(1), .error = {} };
    }
};

struct WorldFixture {
    std::filesystem::path root;
    std::filesystem::path descriptor;
    std::filesystem::path objects;
    std::vector<WorldObjectFile> placed;
};

// A 5x5 grid of 100 m cells (-2..2), one object per cell centre, plus a night-only
// object, an always-loaded object far away and two linked objects across a border.
[[nodiscard]] WorldFixture BuildFixtureWorld(std::string_view label, bool hlod) {
    WorldFixture fixture;
    fixture.root = FreshDirectory(label);
    fixture.descriptor = fixture.root / "Worlds" / "Forest.21kbworld";
    WorldDescriptor descriptor;
    descriptor.guid = "forest-world";
    descriptor.name = "Forest";
    descriptor.cellSize = 100.0;
    descriptor.objectsDirectory = "Forest.objects";
    descriptor.dataLayers = { { .name = "night", .initiallyActive = false }, { .name = "quest_done", .initiallyActive = true } };
    descriptor.hlod = { .enabled = hlod, .range = 500.0, .triangleRatio = 0.5 };
    std::string error;
    Check(WorldDescriptorIO::Write(fixture.descriptor, descriptor, error), "fixture world must write: " + error);
    fixture.objects = WorldPaths::ObjectsDirectory(fixture.descriptor, descriptor);
    for (int z = -2; z <= 2; ++z) {
        for (int x = -2; x <= 2; ++x) {
            fixture.placed.push_back(MakeObject("cell" + std::to_string(x) + "_" + std::to_string(z), "Tree " + std::to_string(x) + "," + std::to_string(z),
                x * 100.0 + 50.0, z * 100.0 + 50.0, {}, false, 77U));
        }
    }
    fixture.placed.push_back(MakeObject("lantern", "Lantern", 60.0, 60.0, "night"));
    fixture.placed.push_back(MakeObject("sun", "Sun", 5000.0, 5000.0, {}, true));
    WorldObjectFile anchor = MakeObject("anchor", "Anchor", 190.0, 20.0);
    WorldObjectFile swing = MakeObject("swing", "Swing", 210.0, 20.0);
    // The swing hangs from the anchor across the cell border at x = 200.
    scene::ScenePrefabNodeDesc* hook = swing.prefab.TryGetMutableNode(1U);
    hook->components.joint = scene::ScenePrefabJointComponent{ .connectedNodeStableId = WorldObjectStableId(anchor.header.guid, 0U) };
    swing.header.references = { anchor.header.guid };
    fixture.placed.push_back(anchor);
    fixture.placed.push_back(swing);
    for (const WorldObjectFile& object : fixture.placed) {
        WriteObject(fixture.objects, object);
    }
    return fixture;
}

void RunGridTests() {
    const WorldPartitionGrid grid{ 64.0 };
    Check(grid.CellOf(-0.5, 0.0) == WorldCellCoord{ -1, 0 }, "negative positions belong to negative cells");
    Check(grid.CellOf(63.999, 64.0) == WorldCellCoord{ 0, 1 }, "cell borders belong to the upper cell");
    // 10,000 km: far beyond any 32-bit cell index at 1 m cells.
    const WorldPartitionGrid metre{ 1.0 };
    const std::optional<WorldCellCoord> far = metre.CellOf(1.0e10, -1.0e10);
    Check(far.has_value() && far->x == 10'000'000'000LL && far->z == -10'000'000'000LL, "cell indices are 64-bit");
    Check(!metre.CellOf(std::numeric_limits<double>::infinity(), 0.0).has_value(), "non-finite positions have no cell");
    Check(!metre.CellOf(1.0e300, 0.0).has_value(), "positions beyond the serializable range have no cell");
    Check(grid.DistanceSquared({ 0, 0 }, 10.0, 10.0) == 0.0 && grid.DistanceSquared({ 1, 0 }, 10.0, 10.0) == 54.0 * 54.0,
        "cell distance is measured to the closest point");
    std::vector<WorldCellCoord> visited;
    grid.ForEachCellInRadius(32.0, 32.0, 50.0, [&visited](WorldCellCoord coord) { visited.push_back(coord); });
    Check(visited.size() == 9U && visited.front() == WorldCellCoord{ -1, -1 } && visited.back() == WorldCellCoord{ 1, 1 },
        "radius walks visit overlapping cells row by row");
    Check(!IsValidCellSize(0.5) && IsValidCellSize(256.0) && !IsValidCellSize(std::numeric_limits<double>::quiet_NaN()), "cell size bounds");
}

void RunFormatTests() {
    WorldDescriptor descriptor;
    descriptor.guid = "g";
    descriptor.name = "World";
    descriptor.cellSize = 256.0;
    descriptor.objectsDirectory = "World.objects";
    descriptor.dataLayers = { { .name = "night", .initiallyActive = false } };
    descriptor.tagDefinitions = { "Player", "Tree" };
    const std::string text = WorldDescriptorIO::Serialize(descriptor);
    const WorldDescriptorReadResult parsed = WorldDescriptorIO::Parse(text);
    Check(parsed.succeeded && WorldDescriptorIO::Serialize(parsed.descriptor) == text, "world descriptor round trips byte for byte");
    Check(parsed.descriptor.dataLayers.size() == 1U && !parsed.descriptor.dataLayers[0].initiallyActive && parsed.descriptor.tagDefinitions[1] == "Tree",
        "world descriptor keeps layers and tags");
    WorldDescriptor invalid = descriptor;
    invalid.dataLayers.push_back({ .name = "night", .initiallyActive = true });
    Check(!WorldDescriptorIO::Validate(invalid).empty(), "duplicate data layers are rejected");
    invalid = descriptor;
    invalid.dataLayers[0].name = "has space";
    Check(!WorldDescriptorIO::Validate(invalid).empty(), "data layer names are restricted");
    invalid = descriptor;
    invalid.objectsDirectory = "../escape";
    Check(!WorldDescriptorIO::Validate(invalid).empty(), "object directories stay inside the world folder");
    Check(!WorldDescriptorIO::Parse("{\"schema\":\"21kb.world/v2\"}").succeeded, "unknown world schemas are rejected");
    Check(WorldPaths::CellIndexVirtualPath("/Game/Worlds/Forest.21kbworld") == "/Game/Worlds/Forest.cells/Forest.21kbcells",
        "the cell index lives next to the world");

    WorldCellIndex index;
    index.worldGuid = "g";
    index.worldName = "World";
    index.cellSize = 256.0;
    index.hlodRange = 2048.0;
    index.dataLayers = descriptor.dataLayers;
    index.units.push_back({ .coord = { -9'000'000'000'000LL, 4 }, .persistent = false, .dataLayer = "night", .scene = "layer.night/c.21kbscene",
        .objectCount = 3U, .nodeCount = 7U, .estimatedBytes = 1234U });
    index.units.push_back({ .coord = {}, .persistent = true, .dataLayer = {}, .scene = "base/persistent.21kbscene", .objectCount = 1U,
        .nodeCount = 1U, .estimatedBytes = 10U });
    index.hlods.push_back({ .coord = { 1, 2 }, .mesh = "hlod/h.obj", .materials = { 0xFFFFFFFFFFFFFFF1ULL, 0U }, .triangleCount = 5U, .sourceTriangleCount = 50U });
    const WorldCellIndexReadResult cells = WorldCellIndexIO::Parse(WorldCellIndexIO::Serialize(index));
    Check(cells.succeeded, "cell index must parse: " + cells.error);
    Check(cells.index.units[0].coord.x == -9'000'000'000'000LL && cells.index.units[1].persistent &&
        cells.index.hlods[0].materials[0] == 0xFFFFFFFFFFFFFFF1ULL, "cell index keeps 64-bit coordinates and asset ids");
    index.units.push_back(index.units[0]);
    Check(!WorldCellIndexIO::Validate(index).empty(), "a cell index cannot list one unit twice");
    Check(ResolveWorldCellPath("/Game/W/F.cells/F.21kbcells", "base/c_0_0.21kbscene") == "/Game/W/F.cells/base/c_0_0.21kbscene",
        "cell paths resolve against the index");

    const std::filesystem::path directory = FreshDirectory("format");
    WorldObjectFile object = MakeObject("format-object", "Rock", 12.5, -3.25, "night", true, 5U);
    object.header.references = { MakeDeterministicWorldObjectGuid("other") };
    WriteObject(directory, object);
    const std::filesystem::path file = directory / (object.header.guid + ".21kbobject");
    const WorldObjectReadResult full = WorldObjectFileIO::Read(file);
    Check(full.succeeded, "world object must read: " + full.error);
    Check(full.object.header.name == "Rock" && full.object.header.dataLayer == "night" && full.object.header.alwaysLoaded &&
        full.object.header.position.x == 12.5 && full.object.header.position.z == -3.25 && full.object.header.references.size() == 1U &&
        full.object.prefab.NodeCount() == 2U && full.object.prefab.Nodes()[1].components.meshRenderer->meshAssetId == 5U,
        "world object round trips header and payload");
    const WorldObjectReadResult header = WorldObjectFileIO::Read(file, true);
    Check(header.succeeded && header.object.prefab.Empty() && header.object.header.nodeCount == 2U, "header-only reads skip the payload");
    std::filesystem::copy_file(file, directory / (MakeDeterministicWorldObjectGuid("renamed") + ".21kbobject"));
    Check(!WorldObjectFileIO::Read(directory / (MakeDeterministicWorldObjectGuid("renamed") + ".21kbobject")).succeeded,
        "a world object file must be named after its guid");
    std::vector<std::uint8_t> corrupt = Bytes(file);
    corrupt.back() ^= 0x5AU;
    corrupt.push_back(0U);
    Check(!WorldObjectFileIO::Parse(corrupt).succeeded, "trailing or corrupted payload bytes are rejected");
    WorldObjectFile twoRoots = object;
    scene::ScenePrefabNodeDesc extra;
    extra.stableId = 99U;
    extra.name = "Loose";
    static_cast<void>(twoRoots.prefab.AddNode(extra));
    std::string error;
    Check(WorldObjectFileIO::Serialize(twoRoots, error).empty(), "an object file holds a single root");
    Check(MakeDeterministicWorldObjectGuid("a") == MakeDeterministicWorldObjectGuid("a") && IsValidWorldObjectGuid(MakeWorldObjectGuid()) &&
        MakeWorldObjectGuid() != MakeWorldObjectGuid(), "object guids");
    std::filesystem::remove_all(directory);
}

[[nodiscard]] scene::SceneDocument MigrationScene() {
    scene::SceneDocument document;
    document.guid = "scene:migration";
    document.name = "Migration";
    document.tagDefinitions = { "Player", "Prop" };
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        scene::ScenePrefabNodeDesc root;
        root.name = "Root" + std::to_string(index);
        root.transform.localPosition = { static_cast<float>(index) * 300.0F, 0.0F, 10.0F };
        const std::uint32_t rootIndex = document.worldPrefab.AddNode(std::move(root));
        scene::ScenePrefabNodeDesc child;
        child.name = "Child" + std::to_string(index);
        child.parentNode = rootIndex;
        static_cast<void>(document.worldPrefab.AddNode(std::move(child)));
    }
    // Root3's child is jointed to Root0: the two must travel together.
    document.worldPrefab.TryGetMutableNode(7U)->components.joint =
        scene::ScenePrefabJointComponent{ .connectedNodeStableId = document.worldPrefab.Nodes()[0].stableId };
    document.worldPrefab.TryGetMutableNode(4U)->components.light = scene::LightComponent{ .kind = scene::LightKind::Directional };
    return document;
}

void RunMigrationAndEditTests() {
    const std::filesystem::path root = FreshDirectory("migration");
    const std::filesystem::path scenePath = root / "Level.21kbscene";
    Check(scene::SceneDocumentService::Save(MigrationScene(), scenePath), "migration scene must save");
    const std::vector<std::uint8_t> sceneBefore = Bytes(scenePath);
    const WorldMigrationResult migrated = WorldMigration::ConvertScene(scenePath, root / "Level.21kbworld", 256.0);
    Check(migrated.succeeded && migrated.objectCount == 4U, "a single-file scene converts to one object per root: " + migrated.error);
    Check(Bytes(scenePath) == sceneBefore && scene::SceneDocumentService::Load(scenePath).succeeded,
        "migration leaves the single-file scene untouched and loadable");
    Check(!WorldMigration::ConvertScene(scenePath, root / "Level.21kbworld", 256.0).succeeded, "migration never overwrites a world");
    const std::vector<std::filesystem::path> files = WorldObjectFileIO::List(root / "Level.objects");
    Check(files.size() == 4U, "migration writes one file per object");
    // Deterministic: converting again elsewhere produces identical files.
    const std::filesystem::path again = root / "again";
    std::filesystem::create_directories(again);
    Check(WorldMigration::ConvertScene(scenePath, again / "Level.21kbworld", 256.0).succeeded, "second migration");
    for (const std::filesystem::path& file : files) {
        Check(Bytes(file) == Bytes(again / "Level.objects" / file.filename()), "migration output is deterministic");
    }
    std::size_t linked = 0U;
    std::size_t persistent = 0U;
    for (const std::filesystem::path& file : files) {
        const WorldObjectReadResult object = WorldObjectFileIO::Read(file);
        Check(object.succeeded, "migrated object must read: " + object.error);
        linked += object.object.header.references.empty() ? 0U : 1U;
        persistent += object.object.header.alwaysLoaded ? 1U : 0U;
    }
    Check(linked == 1U && persistent == 1U, "migration records links and keeps directional lights always loaded");

    // Edit one region at a time.
    scene::Scene editor;
    WorldEditSession session;
    std::string error;
    Check(session.Open(editor, root / "Level.21kbworld", error), "world must open for editing: " + error);
    Check(session.LoadedObjectCount() == 1U && editor.Tags().Names().size() == 7U && editor.Tags().Names().back() == "Prop", "opening loads the always-loaded object and the world's tags: " + std::to_string(session.LoadedObjectCount()) + " " + std::to_string(editor.Tags().Names().size()));
    // Cell (1,0) holds Root1 only.
    Check(session.LoadRegion({ 1, 0 }, { 1, 0 }, error) == 1U && session.IsCellLoaded({ 1, 0 }), "a region loads its objects: " + error);
    // Cell (3,0) holds Root3, which pulls in its linked Root0.
    Check(session.LoadRegion({ 3, 0 }, { 3, 0 }, error) == 2U && session.LoadedObjectCount() == 4U, "linked objects load together: " + error);
    Check(session.Save(error) && session.LastSaveStats().written == 0U && session.LastSaveStats().unchanged == 4U,
        "saving unchanged objects writes no file: " + error);

    std::optional<WorldEditObjectInfo> root1;
    for (const WorldEditObjectInfo& object : session.Objects()) {
        if (object.name == "Root1") root1 = object;
    }
    Check(root1.has_value() && root1->loaded, "Root1 is loaded");
    const std::filesystem::path root1File = root / "Level.objects" / (root1->guid + ".21kbobject");
    const std::filesystem::path root3File = [&] {
        for (const WorldEditObjectInfo& object : session.Objects()) {
            if (object.name == "Root3") return root / "Level.objects" / (object.guid + ".21kbobject");
        }
        return std::filesystem::path{};
    }();
    const std::vector<std::uint8_t> root3Before = Bytes(root3File);
    scene::TransformComponent moved = editor.Transforms().Get(root1->root);
    moved.localPosition.x = 320.0F;
    editor.Transforms().Set(root1->root, moved);
    Check(session.SetObjectDataLayer(root1->root, "", error), "base layer assignment");
    // Unloading keeps the edit for the next save.
    Check(session.UnloadRegion({ 1, 0 }, { 1, 0 }, error) == 1U && session.LoadedObjectCount() == 3U, "region unload: " + error);
    const scene::SceneObject added = editor.Entities().CreateObject(scene::SceneObjectDesc{ .name = "NewRock" });
    const scene::SceneEntity root0 = [&] {
        for (const WorldEditObjectInfo& object : session.Objects()) {
            if (object.name == "Root0") return object.root;
        }
        return scene::SceneEntity{};
    }();
    Check(root0.IsValid(), "Root0 is loaded");
    const std::string root0Guid = session.FindObject(root0)->guid;
    // Deleting Root0 also drops the joint target; detach the joint first like an author would.
    for (const WorldEditObjectInfo& object : session.Objects()) {
        if (object.name == "Root3") {
            const scene::SceneEntity child = editor.Hierarchy().ChildAt(object.root, 0U);
            editor.Components().Joints().Remove(child);
        }
    }
    editor.Entities().Destroy(root0);
    Check(session.Save(error), "save after edits: " + error);
    const WorldSaveStats stats = session.LastSaveStats();
    Check(stats.written == 3U && stats.deleted == 1U, "save writes the moved, new and relinked objects and deletes the removed one");
    Check(Bytes(root3File) != root3Before, "the relinked object is rewritten");
    Check(!std::filesystem::exists(root / "Level.objects" / (root0Guid + ".21kbobject")), "deleted objects lose their file");
    const WorldObjectReadResult movedFile = WorldObjectFileIO::Read(root1File);
    Check(movedFile.succeeded && movedFile.object.header.position.x == 320.0, "an edit made before unloading reaches its file");
    Check(WorldObjectFileIO::List(root / "Level.objects").size() == 4U, "the new object has its own file");
    Check(session.FindObject(added.Entity()).has_value(), "new roots become world objects");

    // A reload that recreates every root in order (play mode restoring the edited
    // scene) keeps each root bound to its object: saving afterwards writes nothing.
    const std::vector<std::string> order = session.RootObjectGuids();
    const scene::SceneDocument snapshot = scene::SceneDocumentService::Capture(editor, "Level");
    Check(scene::SceneDocumentService::LoadIntoScene(editor, snapshot), "editor scene reload");
    Check(session.RebindRootObjects(order, error), "objects rebind after a reload: " + error);
    Check(session.Save(error) && session.LastSaveStats().written == 0U && session.LastSaveStats().deleted == 0U,
        "a rebound world saves without rewriting or deleting objects");
    Check(!session.RebindRootObjects({ order.front() }, error), "rebinding a different set of roots is refused");

    Check(session.DeclareDataLayer("night", false, error) && !session.DeclareDataLayer("night", true, error),
        "data layers are declared once");
    Check(session.Save(error), "save with a new data layer: " + error);
    const WorldDescriptorReadResult declared = WorldDescriptorIO::Read(root / "Level.21kbworld");
    Check(declared.succeeded && declared.descriptor.FindDataLayer("night") != nullptr && !declared.descriptor.FindDataLayer("night")->initiallyActive,
        "a declared data layer reaches the world file");
    session.Close();

    // Reopen: everything round trips.
    scene::Scene reopened;
    WorldEditSession second;
    Check(second.Open(reopened, root / "Level.21kbworld", error) && second.LoadAll(error) == 3U, "reopened world loads its objects: " + error);
    std::set<std::string> names;
    for (const scene::SceneEntity entity : reopened.Hierarchy().RootEntities()) names.insert(reopened.Entities().Name(entity));
    Check(names == std::set<std::string>{ "NewRock", "Root1", "Root2", "Root3" }, "the saved world holds exactly the edited objects");
    Check(!second.SetObjectDataLayer(*reopened.Hierarchy().RootEntities().begin(), "undeclared", error), "undeclared layers are refused");
    std::filesystem::remove_all(root);
}

void RunBuildTests() {
    WorldFixture fixture = BuildFixtureWorld("build", true);
    RecordingHlodBaker baker;
    const WorldBuildResult built = WorldCellBuilder::Build(fixture.descriptor, &baker);
    Check(built.succeeded, "world must build: " + built.error);
    const WorldCellIndexReadResult index = WorldCellIndexIO::Read(WorldPaths::CellIndexPath(fixture.descriptor));
    Check(index.succeeded, "cell index must read: " + index.error);
    // 25 grid cells, the night lantern, the persistent sun; anchor+swing share a unit.
    std::map<std::string, const WorldCellUnit*> units;
    for (const WorldCellUnit& unit : index.index.units) units[unit.scene] = &unit;
    Check(index.index.units.size() == 27U, "one unit per cell and layer: " + std::to_string(index.index.units.size()));
    Check(units.contains("layer.night/c_0_0.21kbscene") && units.contains("base/persistent.21kbscene") &&
        units.at("base/persistent.21kbscene")->persistent, "layers and always-loaded objects get their own units");
    const WorldObjectFile* anchor = nullptr;
    for (const WorldObjectFile& object : fixture.placed) if (object.header.name == "Anchor") anchor = &object;
    const std::string linkedScene = anchor->header.guid < MakeDeterministicWorldObjectGuid("swing") ? "base/c_1_0.21kbscene" : "base/c_2_0.21kbscene";
    Check(units.contains(linkedScene) && units.at(linkedScene)->objectCount >= 2U, "linked objects share their lead object's cell");
    for (const WorldCellUnit& unit : index.index.units) {
        const scene::SceneDocumentLoadResult cell = scene::SceneDocumentService::Load(WorldPaths::CellsDirectory(fixture.descriptor) / unit.scene);
        Check(cell.succeeded && cell.document.worldPrefab.NodeCount() == unit.nodeCount, "every cell scene loads: " + cell.error);
        Check(unit.estimatedBytes > unit.nodeCount * WorldCellIndex::EstimatedBytesPerNode, "cell memory estimates include the file size");
    }
    Check(baker.requests.size() == 25U && index.index.hlods.size() == 25U, "one HLOD per base cell with meshes");
    const auto centre = std::ranges::find_if(baker.requests, [](const WorldHlodRequest& request) { return request.coord == WorldCellCoord{ 0, 0 }; });
    Check(centre != baker.requests.end() && centre->instances.size() == 2U && std::abs(centre->instances[0].transform[9] - 50.0) < 1e-9 &&
        std::abs(centre->instances[1].transform[9] - 51.0) < 1e-9 && centre->instances[1].rendererMaterial == 9U,
        "HLOD instances carry cell-local transforms and their materials");
    Check(index.index.hlods.front().materials.front() == 0xF00DF00DF00DF00DULL, "HLOD materials reach the index");

    // A rebuild replaces the output: removed objects leave no stale cells.
    std::filesystem::remove(fixture.objects / (fixture.placed.front().header.guid + ".21kbobject"));
    Check(WorldCellBuilder::Build(fixture.descriptor, nullptr).succeeded, "rebuild without HLOD");
    const WorldCellIndexReadResult rebuilt = WorldCellIndexIO::Read(WorldPaths::CellIndexPath(fixture.descriptor));
    Check(rebuilt.succeeded && rebuilt.index.units.size() == 26U && rebuilt.index.hlods.empty() &&
        !std::filesystem::exists(WorldPaths::CellsDirectory(fixture.descriptor) / "base/c_-2_-2.21kbscene") &&
        !std::filesystem::exists(WorldPaths::CellsDirectory(fixture.descriptor) / "hlod"), "stale cells and proxies are removed");

    // Broken links and undeclared layers fail the build with a clear message.
    WorldObjectFile dangling = MakeObject("dangling", "Dangling", 0.0, 0.0);
    dangling.prefab.TryGetMutableNode(1U)->components.joint = scene::ScenePrefabJointComponent{ .connectedNodeStableId = 12345U };
    WriteObject(fixture.objects, dangling);
    const WorldBuildResult broken = WorldCellBuilder::Build(fixture.descriptor, nullptr);
    Check(!broken.succeeded && broken.error.find("Dangling") != std::string::npos, "a link to a missing object fails the build");
    std::filesystem::remove(fixture.objects / (dangling.header.guid + ".21kbobject"));
    WriteObject(fixture.objects, MakeObject("bad-layer", "Stray", 0.0, 0.0, "nowhere"));
    Check(!WorldCellBuilder::Build(fixture.descriptor, nullptr).succeeded, "objects in undeclared layers fail the build");
    std::filesystem::remove_all(fixture.root);
}

struct StreamingHarness {
    std::unique_ptr<scene::Scene> scene;
    scene::SceneEntity world{};
    WorldPartitionRuntime Runtime() { return WorldPartitionRuntime{ *scene }; }
};

[[nodiscard]] StreamingHarness MountWorld(const WorldFixture& fixture, bool standInMeshes) {
    StreamingHarness harness;
    harness.scene = std::make_unique<scene::Scene>();
    kb::assets::AssetManager& manager = harness.scene->Assets().Manager();
    if (standInMeshes) {
        Check(manager.RegisterLoader(std::make_unique<StandInMeshLoader>()), "stand-in mesh loader");
    }
    Check(manager.Mounts().Mount("Game", fixture.root) && manager.DiscoverMountedAssets() > 0U, "world project must mount");
    const kb::assets::AssetMetadata* world = manager.Registry().FindByPath("/Game/Worlds/Forest.21kbworld");
    Check(world != nullptr && world->type == "World", "the world asset is registered");
    const kb::assets::AssetMetadata* index = manager.Registry().FindByPath("/Game/Worlds/Forest.cells/Forest.21kbcells");
    Check(index != nullptr && std::ranges::find(world->dependencies, index->id) != world->dependencies.end(),
        "the world depends on its cell index");
    Check(index->dependencies.size() >= 27U, "the cell index depends on every cell scene");
    const scene::SceneObject owner = harness.scene->Entities().CreateObject(scene::SceneObjectDesc{ .name = "World" });
    harness.scene->Components().ContentInstances().Set(owner.Entity(), scene::ContentInstanceComponent{
        .assetId = world->id.value, .kind = scene::ContentInstanceKind::PartitionedWorld, .lifetime = scene::ContentInstanceLifetime::Owner, .active = true });
    harness.world = owner.Entity();
    return harness;
}

// Updates until no cell is loading or unloading and no request was issued for
// two frames. Calls `each` after every frame.
template <typename Each>
void Settle(StreamingHarness& harness, Each&& each) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 30 };
    int quiet = 0;
    while (quiet < 3) {
        if (std::chrono::steady_clock::now() >= deadline) {
            const WorldStreamingStats stats = harness.Runtime().Stats();
            const std::vector<WorldInstanceInfo> worlds = harness.Runtime().Worlds();
            Check(false, "world streaming did not settle: worlds=" + std::to_string(worlds.size()) +
                (worlds.empty() ? std::string{} : " ready=" + std::to_string(worlds[0].ready) + " error=" + worlds[0].error +
                    " failure=" + worlds[0].lastFailure) +
                " loaded=" + std::to_string(stats.loadedUnits) + " loading=" + std::to_string(stats.loadingUnits) +
                " unloading=" + std::to_string(stats.unloadingUnits) + " failed=" + std::to_string(stats.failedUnits) +
                " requests=" + std::to_string(stats.requestsThisFrame));
        }
        static_cast<void>(harness.scene->Runtime().Update(1.0F / 60.0F));
        each();
        const WorldStreamingStats stats = harness.Runtime().Stats();
        const std::vector<WorldInstanceInfo> worlds = harness.Runtime().Worlds();
        const bool resolved = std::ranges::all_of(worlds, [](const WorldInstanceInfo& world) { return world.ready || !world.error.empty(); });
        quiet = resolved && stats.loadingUnits == 0U && stats.unloadingUnits == 0U && stats.requestsThisFrame == 0U ? quiet + 1 : 0;
        std::this_thread::yield();
    }
}

void Settle(StreamingHarness& harness) {
    Settle(harness, [] {});
}

[[nodiscard]] std::set<WorldCellCoord> Square(std::int64_t minX, std::int64_t maxX, std::int64_t minZ, std::int64_t maxZ) {
    std::set<WorldCellCoord> cells;
    for (std::int64_t z = minZ; z <= maxZ; ++z) {
        for (std::int64_t x = minX; x <= maxX; ++x) cells.insert({ x, z });
    }
    return cells;
}

[[nodiscard]] std::set<WorldCellCoord> Loaded(StreamingHarness& harness) {
    const std::vector<WorldCellCoord> cells = harness.Runtime().LoadedCells(harness.world);
    return { cells.begin(), cells.end() };
}

void RunStreamingTests() {
    WorldFixture fixture = BuildFixtureWorld("stream", true);
    RecordingHlodBaker baker;
    Check(WorldCellBuilder::Build(fixture.descriptor, &baker).succeeded, "streaming world must build");
    StreamingHarness harness = MountWorld(fixture, true);
    WorldPartitionRuntime runtime = harness.Runtime();

    // Without a source only the persistent unit loads.
    Settle(harness);
    Check(runtime.Worlds().size() == 1U && runtime.Worlds()[0].ready, "the placed world resolves its index: " +
        (runtime.Worlds().empty() ? std::string{} : runtime.Worlds()[0].error));
    Check(runtime.PersistentState(harness.world) == WorldCellState::Loaded && Loaded(harness).empty(),
        "always-loaded objects stream in with the world");
    Check(HasEntityNamed(*harness.scene, "Sun"), "persistent content is in the scene");

    const std::uint64_t source = runtime.AddSource({ .position = { 50.0, 0.0, 50.0 }, .loadRadius = 120.0, .unloadRadius = 180.0, .priority = 0 });
    Settle(harness);
    Check(Loaded(harness) == Square(-1, 1, -1, 1), "cells within the load radius stream in");
    Check(HasEntityNamed(*harness.scene, "Tree 0,0") && !HasEntityNamed(*harness.scene, "Tree 2,0"),
        "cell content appears only for loaded cells");
    Check(runtime.CellState(harness.world, { 0, 0 }, "night") == WorldCellState::Unloaded && !HasEntityNamed(*harness.scene, "Lantern"),
        "an inactive data layer stays unloaded");
    Check(!runtime.IsHlodVisible(harness.world, { 0, 0 }) && runtime.IsHlodVisible(harness.world, { 2, 2 }) &&
        HasEntityNamed(*harness.scene, "HLOD 2,2"), "proxies stand in for unloaded cells within range");

    // Hysteresis: (-1,0) is outside the load radius but inside the unload radius.
    Check(runtime.UpdateSource(source, { .position = { 130.0, 0.0, 50.0 }, .loadRadius = 120.0, .unloadRadius = 180.0, .priority = 0 }), "move source");
    Settle(harness);
    std::set<WorldCellCoord> expected = Square(-1, 2, -1, 1);
    Check(Loaded(harness) == expected, "loaded cells stay until they leave the unload radius");
    Check(runtime.UpdateSource(source, { .position = { 350.0, 0.0, 50.0 }, .loadRadius = 120.0, .unloadRadius = 180.0, .priority = 0 }), "move source far");
    Settle(harness);
    expected = Square(1, 2, -1, 1);
    Check(Loaded(harness) == expected, "cells beyond the unload radius stream out");
    Check(!HasEntityNamed(*harness.scene, "Tree -1,0") && runtime.IsHlodVisible(harness.world, { -1, 0 }) &&
        !runtime.IsHlodVisible(harness.world, { 2, 0 }), "an unloaded cell swaps back to its proxy");

    // Data layers switch at runtime, here through the gameplay script API.
    kb::script::ScriptRuntimeHost host{ *harness.scene };
    const kb::script::ScriptFunctionCallContext call{ .scene = harness.scene.get() };
    const kb::script::ScriptFunctionCallResult activated = host.Functions().Call("Scene.SetDataLayerActive",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "layer", .value = kb::script::ScriptValue{ std::string{ "night" } } },
            { .name = "active", .value = kb::script::ScriptValue{ true } } }, call);
    Check(activated.Succeeded() && runtime.IsDataLayerActive("night"), "Scene.SetDataLayerActive switches a data layer");
    const kb::script::ScriptFunctionCallResult queried = host.Functions().Call("Scene.IsDataLayerActive",
        std::vector<kb::script::ScriptFunctionArgument>{ { .name = "layer", .value = kb::script::ScriptValue{ std::string{ "night" } } } }, call);
    Check(queried.Succeeded() && queried.Output("active").has_value() && queried.Output("active")->AsBool(), "Scene.IsDataLayerActive reports the layer");
    const kb::script::ScriptFunctionCallResult rejected = host.Functions().Call("Scene.SetDataLayerActive",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "layer", .value = kb::script::ScriptValue{ std::string{ "not a layer" } } },
            { .name = "active", .value = kb::script::ScriptValue{ true } } }, call);
    Check(!rejected.Succeeded(), "invalid data layer names are rejected");
    Check(runtime.UpdateSource(source, { .position = { 50.0, 0.0, 50.0 }, .loadRadius = 120.0, .unloadRadius = 180.0, .priority = 0 }), "move back");
    Settle(harness);
    Check(runtime.CellState(harness.world, { 0, 0 }, "night") == WorldCellState::Loaded && HasEntityNamed(*harness.scene, "Lantern"),
        "activating a layer streams its cells in");
    runtime.SetDataLayerActive("night", false);
    Settle(harness);
    Check(runtime.CellState(harness.world, { 0, 0 }, "night") == WorldCellState::Unloaded && !HasEntityNamed(*harness.scene, "Lantern") &&
        runtime.CellState(harness.world, { 0, 0 }) == WorldCellState::Loaded, "deactivating a layer unloads only that layer");

    // A stream focus entity is a source too.
    Check(runtime.RemoveSource(source), "remove source");
    const scene::SceneObject player = harness.scene->Entities().CreateObject(scene::SceneObjectDesc{ .name = "Player" });
    scene::TransformComponent transform = harness.scene->Transforms().Get(player);
    transform.localPosition = { -150.0F, 0.0F, -150.0F };
    harness.scene->Transforms().Set(player, transform);
    harness.scene->Components().StreamFocuses().Set(player.Entity(), scene::StreamFocusComponent{
        .innerRadius = 60.0F, .outerRadius = 80.0F, .priority = 0, .loadMask = scene::StreamLoadMask::WorldFragment, .enabled = true });
    Settle(harness);
    Check(Loaded(harness) == Square(-2, -1, -2, -1), "stream focus entities stream cells around their transform");

    // Stopping play releases every cell and proxy.
    harness.scene->Runtime().SetPlaying(false);
    for (int frame = 0; frame < 600 && HasEntityNamed(*harness.scene, "Tree -2,-2"); ++frame) {
        static_cast<void>(harness.scene->Runtime().Update(1.0F / 60.0F));
        std::this_thread::yield();
    }
    Check(!HasEntityNamed(*harness.scene, "Tree -2,-2") && !HasEntityNamed(*harness.scene, "HLOD 2,2") &&
        !HasEntityNamed(*harness.scene, "Sun"), "stopping play unloads the world");
    harness.scene.reset();
    std::filesystem::remove_all(fixture.root);
}

void RunBudgetTests() {
    WorldFixture fixture = BuildFixtureWorld("budget", false);
    Check(WorldCellBuilder::Build(fixture.descriptor, nullptr).succeeded, "budget world must build");
    const WorldCellIndexReadResult index = WorldCellIndexIO::Read(WorldPaths::CellIndexPath(fixture.descriptor));
    std::uint64_t persistentBytes = 0U;
    std::uint64_t centreCell = 0U;
    for (const WorldCellUnit& unit : index.index.units) {
        if (unit.persistent) persistentBytes += unit.estimatedBytes;
        else if (unit.dataLayer.empty() && unit.coord == WorldCellCoord{ 0, 0 }) centreCell = unit.estimatedBytes;
    }
    Check(centreCell != 0U, "the centre cell is built");
    StreamingHarness harness = MountWorld(fixture, false);
    WorldPartitionRuntime runtime = harness.Runtime();
    bool threw = false;
    try {
        runtime.ConfigureBudget({ .maxLoadRequestsPerFrame = 0U });
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    Check(threw, "a zero request budget is rejected");
    const std::uint64_t budget = persistentBytes + centreCell * 7U / 2U;
    runtime.ConfigureBudget({ .maxLoadRequestsPerFrame = 1U, .maxPendingLoads = 2U, .maxResidentBytes = budget, .maxMillisecondsPerFrame = 50.0 });
    static_cast<void>(runtime.AddSource({ .position = { 50.0, 0.0, 50.0 }, .loadRadius = 120.0, .unloadRadius = 180.0, .priority = 0 }));
    std::size_t frames = 0U;
    Settle(harness, [&] {
        const WorldStreamingStats stats = runtime.Stats();
        Check(stats.requestsThisFrame <= 1U, "at most one cell request per frame");
        Check(stats.loadingUnits <= 2U, "at most two cells load at once");
        Check(stats.residentBytes <= budget, "resident cells never exceed the memory budget");
        ++frames;
    });
    const std::set<WorldCellCoord> limited = Loaded(harness);
    Check(limited.size() >= 2U && limited.size() < 9U && limited.contains({ 0, 0 }), "the budget admits the nearest cells first: " + std::to_string(limited.size()));
    // Every neighbour left out would not have fitted.
    const std::uint64_t resident = runtime.Stats().residentBytes;
    for (const WorldCellCoord& cell : Square(-1, 1, -1, 1)) {
        if (limited.contains(cell)) continue;
        for (const WorldCellUnit& unit : index.index.units) {
            if (!unit.persistent && unit.dataLayer.empty() && unit.coord == cell) {
                Check(resident + unit.estimatedBytes > budget, "a cell that fits the budget was left unloaded");
            }
        }
    }
    Check(frames >= limited.size(), "requests are spread over frames");
    runtime.ConfigureBudget({ .maxLoadRequestsPerFrame = 8U, .maxPendingLoads = 8U, .maxResidentBytes = 1ULL << 30U, .maxMillisecondsPerFrame = 50.0 });
    Settle(harness);
    Check(Loaded(harness) == Square(-1, 1, -1, 1), "a larger budget completes the neighbourhood");
    runtime.ConfigureBudget({ .maxLoadRequestsPerFrame = 8U, .maxPendingLoads = 8U, .maxResidentBytes = persistentBytes + centreCell * 3U / 2U, .maxMillisecondsPerFrame = 50.0 });
    Settle(harness);
    const std::set<WorldCellCoord> evicted = Loaded(harness);
    Check(evicted == std::set<WorldCellCoord>{ { 0, 0 } } && runtime.Stats().residentBytes <= persistentBytes + centreCell * 3U / 2U,
        "lowering the budget evicts the farthest cells");
    harness.scene.reset();
    std::filesystem::remove_all(fixture.root);
}

[[nodiscard]] std::vector<std::string> RunScriptedPath(const WorldFixture& fixture) {
    StreamingHarness harness = MountWorld(fixture, true);
    WorldPartitionRuntime runtime = harness.Runtime();
    harness.scene->LoadedContent().ConfigureStreaming({ .maxPendingLoads = 8U, .maxOperationsPerFrame = 4096U, .maxMillisecondsPerFrame = 1000.0F });
    runtime.ConfigureBudget({ .maxLoadRequestsPerFrame = 2U, .maxPendingLoads = 1U, .maxResidentBytes = 1ULL << 30U, .maxMillisecondsPerFrame = 1000.0 });
    const std::uint64_t source = runtime.AddSource({ .position = { -150.0, 0.0, -150.0 }, .loadRadius = 90.0, .unloadRadius = 130.0, .priority = 0 });
    std::vector<std::string> log;
    const auto drain = [&] {
        for (const WorldStreamingEvent& event : runtime.DrainEvents()) {
            log.push_back(std::to_string(static_cast<int>(event.kind)) + ":" + std::to_string(event.coord.x) + "," + std::to_string(event.coord.z) +
                ":" + event.dataLayer + (event.persistent ? ":p" : ""));
        }
    };
    const std::array<WorldPoint, 5U> path{ { { -150.0, 0.0, -150.0 }, { -40.0, 0.0, -80.0 }, { 60.0, 0.0, 10.0 }, { 170.0, 0.0, 130.0 }, { -20.0, 0.0, 240.0 } } };
    for (const WorldPoint& point : path) {
        Check(runtime.UpdateSource(source, { .position = point, .loadRadius = 90.0, .unloadRadius = 130.0, .priority = 0 }), "scripted move");
        Settle(harness, drain);
        log.push_back("|");
    }
    return log;
}

void RunDeterminismTests() {
    WorldFixture fixture = BuildFixtureWorld("determinism", true);
    RecordingHlodBaker baker;
    Check(WorldCellBuilder::Build(fixture.descriptor, &baker).succeeded, "determinism world must build");
    const std::vector<std::string> first = RunScriptedPath(fixture);
    const std::vector<std::string> second = RunScriptedPath(fixture);
    Check(first.size() > 40U, "the scripted path streams many cells");
    Check(first == second, "identical source paths produce identical streaming decisions");
    std::filesystem::remove_all(fixture.root);
}

void RunUnbuiltWorldTests() {
    const std::filesystem::path root = FreshDirectory("unbuilt");
    WorldDescriptor descriptor;
    descriptor.guid = "unbuilt";
    descriptor.name = "Unbuilt";
    descriptor.objectsDirectory = "Unbuilt.objects";
    std::string error;
    Check(WorldDescriptorIO::Write(root / "Unbuilt.21kbworld", descriptor, error), "unbuilt world writes");
    scene::Scene scene;
    Check(scene.Assets().Manager().Mounts().Mount("Game", root) && scene.Assets().Manager().DiscoverMountedAssets() == 1U, "unbuilt mount");
    const scene::SceneObject owner = scene.Entities().CreateObject(scene::SceneObjectDesc{ .name = "World" });
    scene.Components().ContentInstances().Set(owner.Entity(), scene::ContentInstanceComponent{
        .assetId = scene.Assets().Manager().Registry().FindByPath("/Game/Unbuilt.21kbworld")->id.value, .kind = scene::ContentInstanceKind::PartitionedWorld });
    static_cast<void>(scene.Runtime().Update(0.0F));
    const std::vector<WorldInstanceInfo> worlds = WorldPartitionRuntime{ scene }.Worlds();
    Check(worlds.size() == 1U && !worlds[0].ready && worlds[0].error.find("has not been built") != std::string::npos,
        "an unbuilt world reports why it cannot stream");
    std::filesystem::remove_all(root);
}

} // namespace

void RunWorldPartitionTests() {
    RunGridTests();
    RunFormatTests();
    RunMigrationAndEditTests();
    RunBuildTests();
    RunStreamingTests();
    RunBudgetTests();
    RunDeterminismTests();
    RunUnbuiltWorldTests();
}

} // namespace kb::tests
