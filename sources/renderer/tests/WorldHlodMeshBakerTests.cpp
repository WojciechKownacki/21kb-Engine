#include "RendererTestSupport.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/world/WorldCellBuilder.hpp"
#include "engine/world/WorldCellIndex.hpp"
#include "engine/world/WorldDescriptor.hpp"
#include "engine/world/WorldObjectFile.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/world/WorldHlodMeshBaker.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace kb::render::tests {
namespace {

void Check(bool condition, const std::string& message) {
    Require(condition, message.c_str());
}

// A flat, finely tessellated sheet: many triangles the simplifier can remove.
void WriteSheet(const std::filesystem::path& path, int segments) {
    std::ofstream output{ path, std::ios::trunc };
    for (int z = 0; z <= segments; ++z) {
        for (int x = 0; x <= segments; ++x) {
            output << "v " << x << " 0 " << z << "\n";
            output << "vt " << static_cast<float>(x) / static_cast<float>(segments) << " " << static_cast<float>(z) / static_cast<float>(segments) << "\n";
        }
    }
    output << "vn 0 1 0\n";
    output << "usemtl ground\n";
    for (int z = 0; z < segments; ++z) {
        for (int x = 0; x < segments; ++x) {
            const int a = z * (segments + 1) + x + 1;
            const int b = a + 1;
            const int c = a + segments + 1;
            const int d = c + 1;
            output << "f " << a << "/" << a << "/1 " << c << "/" << c << "/1 " << b << "/" << b << "/1\n";
            output << "f " << b << "/" << b << "/1 " << c << "/" << c << "/1 " << d << "/" << d << "/1\n";
        }
    }
}

[[nodiscard]] std::filesystem::path FreshRoot() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("kb_hlod_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "Meshes");
    return root;
}

void RunBakerTests() {
    const std::filesystem::path root = FreshRoot();
    WriteSheet(root / "Meshes" / "Sheet.obj", 20);
    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "HLOD test mesh loader");
    Require(manager.Mounts().Mount("Game", root) && manager.DiscoverMountedAssets() == 1U, "HLOD test mesh discovery");
    const kb::assets::AssetMetadata* sheet = manager.Registry().FindByPath("/Game/Meshes/Sheet.obj");
    Require(sheet != nullptr, "HLOD test mesh registered");

    WorldHlodMeshBaker baker{ manager };
    kb::world::WorldHlodRequest request;
    request.coord = { 3, -2 };
    request.cellSize = 100.0;
    request.triangleRatio = 0.1;
    kb::world::WorldHlodMeshInstance first;
    first.meshAssetId = sheet->id.value;
    first.transform = { 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 10.0, 0.0, 10.0 };
    kb::world::WorldHlodMeshInstance second = first;
    second.transform[9] = 60.0;
    second.slotMaterials[0] = 0x55U;
    second.slotMaterialCount = 1U;
    kb::world::WorldHlodMeshInstance missing = first;
    missing.meshAssetId = 0xDEADU;
    request.instances = { first, second, missing };
    const kb::world::WorldHlodResult result = baker.Bake(request);
    Check(result.succeeded, "HLOD bake succeeds: " + result.error);
    Check(result.sourceTriangleCount == 1600U, "HLOD merges every source triangle: " + std::to_string(result.sourceTriangleCount));
    Check(result.triangleCount > 0U && result.triangleCount <= 200U, "HLOD simplifies towards the triangle ratio: " + std::to_string(result.triangleCount));
    Check(result.materials.size() == 2U && (result.materials[0] == 0x55U || result.materials[1] == 0x55U), "HLOD keeps one slot per final material");

    std::istringstream obj{ result.objText };
    const std::optional<RenderMeshAssetData> proxy = RenderMeshAssetBuilder::LoadObj(obj);
    Check(proxy.has_value() && proxy->materialSlots.size() == 2U && proxy->sections.size() == 2U, "the HLOD document loads as a mesh with two slots");
    float minX = 1e9F;
    float maxX = -1e9F;
    const auto visit = [&](float x, float y, float ny) {
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        Check(std::fabs(y) < 1e-4F && ny > 0.99F, "HLOD vertices keep their surface and normal");
    };
    for (const RenderStaticMeshVertexP3N3UV2& vertex : proxy->vertices) visit(vertex.x, vertex.y, vertex.ny);
    for (const RenderStaticMeshVertexP3N3T4UV2& vertex : proxy->tangentVertices) visit(vertex.x, vertex.y, vertex.ny);
    // The simplifier keeps the sheets' corners, so the proxy spans both instances exactly.
    Check(NearlyEqual(minX, 10.0F) && NearlyEqual(maxX, 80.0F),
        "HLOD vertices are placed in cell-local space: " + std::to_string(minX) + " " + std::to_string(maxX));

    kb::world::WorldHlodRequest empty;
    empty.instances = { missing };
    const kb::world::WorldHlodResult skipped = baker.Bake(empty);
    Check(!skipped.succeeded && !skipped.error.empty(), "a cell whose meshes cannot load reports it");

    // The world builder writes the proxy next to the cells and the index names it.
    const std::filesystem::path descriptorPath = root / "Worlds" / "Hills.21kbworld";
    kb::world::WorldDescriptor descriptor;
    descriptor.guid = "hills";
    descriptor.name = "Hills";
    descriptor.cellSize = 100.0;
    descriptor.objectsDirectory = "Hills.objects";
    descriptor.hlod = { .enabled = true, .range = 400.0, .triangleRatio = 0.2 };
    std::string error;
    Check(kb::world::WorldDescriptorIO::Write(descriptorPath, descriptor, error), "HLOD world descriptor: " + error);
    kb::world::WorldObjectFile object;
    object.header.guid = kb::world::MakeDeterministicWorldObjectGuid("hill");
    object.header.name = "Hill";
    object.header.position = { 150.0, 0.0, 150.0 };
    kb::scene::ScenePrefabNodeDesc node;
    node.stableId = kb::world::WorldObjectStableId(object.header.guid, 0U);
    node.name = "Hill";
    node.transform.localPosition = { 150.0F, 0.0F, 150.0F };
    node.components.meshRenderer = kb::scene::MeshRendererComponent{ .meshAssetId = sheet->id.value };
    static_cast<void>(object.prefab.AddNode(node));
    object.header.nodeCount = 1U;
    const std::vector<std::uint8_t> bytes = kb::world::WorldObjectFileIO::Serialize(object, error);
    Check(!bytes.empty() && kb::world::WorldObjectFileIO::WriteBytes(root / "Worlds" / "Hills.objects" / (object.header.guid + ".21kbobject"), bytes, error),
        "HLOD world object: " + error);
    const kb::world::WorldBuildResult built = kb::world::WorldCellBuilder::Build(descriptorPath, &baker);
    Check(built.succeeded && built.report.hlodCount == 1U, "the world build produces the cell's HLOD: " + built.error);
    const kb::world::WorldCellIndexReadResult index = kb::world::WorldCellIndexIO::Read(kb::world::WorldPaths::CellIndexPath(descriptorPath));
    Check(index.succeeded && index.index.hlods.size() == 1U && index.index.hlods[0].coord == kb::world::WorldCellCoord{ 1, 1 } &&
        index.index.hlods[0].triangleCount < index.index.hlods[0].sourceTriangleCount, "the index names the simplified proxy");
    kb::assets::AssetManager rediscovered;
    Require(rediscovered.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "HLOD rediscovery loader");
    Require(rediscovered.Mounts().Mount("Game", root), "HLOD rediscovery mount");
    static_cast<void>(rediscovered.DiscoverMountedAssets());
    const kb::assets::AssetHandle<RenderMeshAssetData> loaded =
        rediscovered.Load<RenderMeshAssetData>("/Game/Worlds/Hills.cells/" + index.index.hlods[0].mesh);
    Check(loaded.IsLoaded() && loaded->desc.indexCount == index.index.hlods[0].triangleCount * 3U, "the HLOD proxy is a regular mesh asset");
    std::filesystem::remove_all(root);
}

} // namespace

void RunWorldHlodMeshBakerTests() {
    RunBakerTests();
}

} // namespace kb::render::tests
