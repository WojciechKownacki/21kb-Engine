// glTF documents that keep their buffers and images in separate files: the files resolve next to
// the document and nowhere else, an import carries the buffers so the mesh still loads once the
// source folder is gone, and materials without a name keep distinct slots. Every fixture is
// written by the test.

#include "RendererTestSupport.hpp"

#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/GltfExternalResources.hpp"
#include "engine/assets/ImportedAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderMeshSourceImport.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace kb::render::tests {
namespace {

void RequireThat(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << '\n';
        Require(false, "glTF external resource test failed");
    }
}

class TempRoot final {
public:
    TempRoot() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("21kb_gltf_external_" + std::to_string(std::random_device{}()) + "_" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }
    ~TempRoot() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    TempRoot(const TempRoot&) = delete;
    TempRoot& operator=(const TempRoot&) = delete;

    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

void WriteBytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    RequireThat(output.good(), "Fixture could not be written: " + path.string());
}

void WriteText(const std::filesystem::path& path, std::string_view text) {
    WriteBytes(path, std::span<const std::uint8_t>{ reinterpret_cast<const std::uint8_t*>(text.data()), text.size() });
}

// Positions of one triangle, (0,0,0) (1,0,0) (0,1,0), as 36 bytes of little-endian floats.
[[nodiscard]] std::vector<std::uint8_t> TriangleBuffer(float scale = 1.0F) {
    std::vector<std::uint8_t> bytes;
    for (const float value : { 0.0F, 0.0F, 0.0F, scale, 0.0F, 0.0F, 0.0F, scale, 0.0F }) {
        const auto bits = std::bit_cast<std::uint32_t>(value);
        for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) bytes.push_back(static_cast<std::uint8_t>(bits >> shift));
    }
    return bytes;
}

[[nodiscard]] std::string JsonString(std::string_view text) {
    std::string quoted = "\"";
    for (const char character : text) {
        if (character == '"' || character == '\\') quoted.push_back('\\');
        quoted.push_back(character);
    }
    return quoted + "\"";
}

// One triangle whose positions live in the buffer `bufferUri` names.
[[nodiscard]] std::string TriangleDocument(std::string_view bufferUri) {
    return std::string{ R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],)" } +
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],)" +
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],)" +
        R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36}],)" +
        R"("buffers":[{"byteLength":36,"uri":)" + JsonString(bufferUri) + "}]}";
}

[[nodiscard]] float MaxX(const RenderMeshAssetData& mesh) {
    float maxX = 0.0F;
    for (const RenderStaticMeshVertexP3N3UV2& vertex : mesh.vertices) maxX = std::max(maxX, vertex.x);
    for (const RenderStaticMeshVertexP3N3T4UV2& vertex : mesh.tangentVertices) maxX = std::max(maxX, vertex.x);
    return maxX;
}

struct ImportProject {
    kb::assets::AssetManager manager;
    std::filesystem::path assets;

    explicit ImportProject(const std::filesystem::path& root) : assets(root / "Project" / "Assets") {
        std::filesystem::create_directories(assets);
        Require(manager.RegisterLoader(std::make_unique<kb::assets::ImportedAssetLoader>()), "Imported asset loader registration failed");
        Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Render mesh loader registration failed");
        Require(manager.Mounts().Mount("Game", assets), "glTF import project mount failed");
    }

    [[nodiscard]] kb::assets::AssetImportItemResult Import(const std::filesystem::path& source) {
        const std::array files{ source };
        const kb::assets::AssetImportResult result = kb::assets::AssetImportService::ImportFiles(manager, files, "/Game/Meshes");
        Require(result.items.size() == 1U, "glTF import returned no item");
        return result.items.front();
    }
};

void RelativeResourcePathsAreTheOnlyAcceptedFileUris() {
    using kb::assets::GltfRelativeResourcePath;
    const auto accepted = [](std::string_view uri, std::string_view expected) {
        const std::optional<std::filesystem::path> path = GltfRelativeResourcePath(uri);
        RequireThat(path.has_value() && path->generic_string() == expected, "A relative glTF URI was refused: " + std::string{ uri });
    };
    accepted("mesh.bin", "mesh.bin");
    accepted("buffers/mesh.bin", "buffers/mesh.bin");
    accepted("./buffers/mesh%20data.bin", "buffers/mesh data.bin");
    for (const std::string_view uri : {
             "", "../mesh.bin", "buffers/../../mesh.bin", "%2E%2E/mesh.bin", "..%2Fmesh.bin", "/mesh.bin",
             "//server/share/mesh.bin", "C:/mesh.bin", "C:mesh.bin", "c%3A/mesh.bin", "file:///C:/mesh.bin",
             "http://example.com/mesh.bin", "..\\mesh.bin", "buffers\\mesh.bin", "mesh.bin?x=1", "mesh.bin#frag",
             "mesh%00.bin", "mesh%5C..%5Cx.bin", "buffers//mesh.bin", "mesh%2", "data:application/octet-stream;base64,AAAA" }) {
        RequireThat(!GltfRelativeResourcePath(uri).has_value(), "An unsafe glTF URI was accepted: " + std::string{ uri });
    }
}

void ExternalBufferImportsAndLoadsWithoutTheSourceFolder() {
    const TempRoot root;
    const std::filesystem::path sourceFolder = root.Path() / "Source";
    const std::filesystem::path document = sourceFolder / "Triangle.gltf";
    WriteText(document, TriangleDocument("buffers/triangle%20data.bin"));
    WriteBytes(sourceFolder / "buffers" / "triangle data.bin", TriangleBuffer());

    const std::optional<RenderMeshAssetData> direct = RenderMeshAssetBuilder::LoadGltf(document);
    Require(direct.has_value() && direct->desc.vertexCount == 3U, "glTF with an external buffer did not load from its folder");

    ImportProject project{ root.Path() };
    const kb::assets::AssetImportItemResult imported = project.Import(document);
    RequireThat(imported.status == kb::assets::AssetImportItemStatus::Created, "glTF with an external buffer did not import: " + imported.error);

    // A changed buffer makes the import stale even though the .gltf itself did not change.
    WriteBytes(sourceFolder / "buffers" / "triangle data.bin", TriangleBuffer(2.0F));
    const kb::assets::AssetImportItemResult reimported = project.Import(document);
    Require(reimported.status == kb::assets::AssetImportItemStatus::Created && reimported.id != imported.id,
        "Changing only the external buffer of a glTF reused the stale import");

    std::error_code error;
    std::filesystem::remove_all(sourceFolder, error);
    Require(!error && !std::filesystem::exists(sourceFolder), "glTF source folder could not be removed");

    const kb::assets::AssetHandle<RenderMeshAssetData> mesh = project.manager.Load<RenderMeshAssetData>(reimported.id);
    Require(mesh.IsLoaded() && mesh->desc.vertexCount == 3U && mesh->desc.indexCount == 3U,
        "Imported glTF did not load from the buffer the import carried");
    Require(MaxX(*mesh) > 1.5F, "Imported glTF loaded an older buffer than the one imported last");

    kb::assets::AssetManager rediscovered;
    Require(rediscovered.RegisterLoader(std::make_unique<kb::assets::ImportedAssetLoader>()) &&
            rediscovered.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()) &&
            rediscovered.Mounts().Mount("Game", project.assets),
        "glTF rediscovery setup failed");
    Require(rediscovered.DiscoverMountedAssets() >= 1U, "Imported glTF container was not rediscovered");
    const kb::assets::AssetMetadata* metadata = rediscovered.Registry().FindByPath(reimported.virtualPath);
    Require(metadata != nullptr && metadata->type == "RenderMesh", "Rediscovered glTF container is not a render mesh");
    Require(rediscovered.Load<RenderMeshAssetData>(reimported.id).IsLoaded(), "Rediscovered glTF container did not load");
}

void BufferUrisOutsideTheSourceFolderAreRefused() {
    const TempRoot root;
    const std::filesystem::path outside = root.Path() / "outside.bin";
    WriteBytes(outside, TriangleBuffer());
    const std::filesystem::path folder = root.Path() / "Model";
    std::filesystem::create_directories(folder / "sub");
    const std::string absolute = outside.generic_string();
    const std::array<std::string, 9U> uris{
        "../outside.bin",
        "sub/../../outside.bin",
        "%2E%2E/outside.bin",
        absolute,
        "/" + absolute,
        "file:///" + absolute,
        "http://example.com/outside.bin",
        "..\\outside.bin",
        "missing.bin",
    };
    ImportProject project{ root.Path() };
    for (std::size_t index = 0U; index < uris.size(); ++index) {
        const std::filesystem::path document = folder / ("Escape" + std::to_string(index) + ".gltf");
        const std::string text = TriangleDocument(uris[index]);
        WriteText(document, text);
        RequireThat(!RenderMeshAssetBuilder::LoadGltf(document).has_value(),
            "glTF loaded a buffer from outside its folder: " + uris[index]);
        const std::span<const std::uint8_t> bytes{ reinterpret_cast<const std::uint8_t*>(text.data()), text.size() };
        RequireThat(!RenderMeshAssetBuilder::LoadGltf(bytes, document).has_value(),
            "glTF bytes loaded a buffer from outside their folder: " + uris[index]);
        RequireThat(!RenderMeshSourceImport::Inspect(document).has_value(),
            "glTF material inspection loaded a buffer from outside its folder: " + uris[index]);
        if (uris[index] != "missing.bin") {
            RequireThat(!RenderMeshAssetBuilder::GltfExternalBufferUris(bytes).has_value(),
                "glTF cooker accepted an external buffer outside the document folder: " + uris[index]);
        }
        const kb::assets::AssetImportItemResult imported = project.Import(document);
        RequireThat(imported.status == kb::assets::AssetImportItemStatus::Failed && !imported.error.empty(),
            "glTF import accepted a buffer outside its folder: " + uris[index]);
    }
}

[[nodiscard]] std::string MaterialDocument(std::string_view materials, std::string_view imageUri) {
    std::string document = std::string{ R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],)" } +
        R"("meshes":[{"primitives":[)" +
        R"({"attributes":{"POSITION":0},"material":0},)" +
        R"({"attributes":{"POSITION":0},"material":1},)" +
        R"({"attributes":{"POSITION":0},"material":2}]}],)" +
        R"("materials":)" + std::string{ materials } + "," +
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],)" +
        R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36}],)" +
        R"("buffers":[{"byteLength":36,"uri":"triangle.bin"}])";
    if (!imageUri.empty()) {
        document += R"(,"textures":[{"source":0}],"images":[{"uri":)" + JsonString(imageUri) + "}]";
    }
    return document + "}";
}

void UnnamedMaterialsKeepDistinctStableSlots() {
    const TempRoot root;
    const std::filesystem::path document = root.Path() / "Materials.gltf";
    // Material 0 is unnamed while material 1 is explicitly called what material 0 would default to.
    WriteText(document, MaterialDocument(R"([{},{"name":"Material_0"},{}])", ""));
    WriteBytes(root.Path() / "triangle.bin", TriangleBuffer());

    const std::optional<RenderMeshAssetData> first = RenderMeshAssetBuilder::LoadGltf(document);
    const std::optional<RenderMeshAssetData> second = RenderMeshAssetBuilder::LoadGltf(document);
    Require(first.has_value() && second.has_value(), "glTF with three materials did not load");
    const std::set<std::string> names{ first->materialNames.begin(), first->materialNames.end() };
    RequireThat(first->materialNames.size() == 3U && names.size() == 3U && first->materialSlots.size() == 3U,
        "Unnamed glTF materials collapsed into a shared slot (" + std::to_string(first->materialNames.size()) + " slots)");
    Require(first->materialNames == second->materialNames, "glTF material slot names are not stable across loads");
    std::set<std::uint32_t> usedSlots;
    for (const RenderMeshSectionDesc& section : first->sections) usedSlots.insert(section.materialSlot);
    Require(usedSlots.size() == 3U, "Each glTF primitive must draw with its own material slot");

    std::string error;
    const std::optional<RenderMeshSourceImportManifest> manifest = RenderMeshSourceImport::Inspect(document, &error);
    RequireThat(manifest.has_value() && manifest->materials.size() == 3U, "glTF material inspection failed: " + error);
    for (const RenderMeshEmbeddedMaterial& material : manifest->materials) {
        RequireThat(names.contains(material.name),
            "Generated material " + material.name + " does not match a mesh slot, so it would never bind");
    }
    Require(manifest->materials[1].name == "Material_0", "An explicitly named glTF material must keep its name");
}

void ExternalImagesResolveInsideTheDocumentFolderOnly() {
    const TempRoot root;
    const std::filesystem::path folder = root.Path() / "Model";
    WriteBytes(folder / "triangle.bin", TriangleBuffer());
    WriteText(folder / "textures" / "albedo map.png", "png bytes");
    WriteText(root.Path() / "outside.png", "png bytes");
    const std::string materials = R"([{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}},{"name":"B"},{"name":"C"}])";

    const std::filesystem::path inside = folder / "Inside.gltf";
    WriteText(inside, MaterialDocument(materials, "textures/albedo%20map.png"));
    std::string error;
    const std::optional<RenderMeshSourceImportManifest> manifest = RenderMeshSourceImport::Inspect(inside, &error);
    RequireThat(manifest.has_value() && manifest->textures.size() == 1U, "glTF with an external image did not inspect: " + error);
    Require(!manifest->textures.front().IsEmbedded() &&
            std::filesystem::equivalent(manifest->textures.front().sourcePath, folder / "textures" / "albedo map.png"),
        "glTF external image did not resolve next to its document");
    Require(manifest->materials.front().albedoTexturePath == manifest->textures.front().key,
        "glTF material does not reference the image it was inspected with");

    for (const std::string_view uri : { "../outside.png", "textures/../../outside.png", "http://example.com/a.png",
             "file:///C:/outside.png", "C:/outside.png" }) {
        const std::filesystem::path document = folder / "Escape.gltf";
        WriteText(document, MaterialDocument(materials, uri));
        RequireThat(!RenderMeshSourceImport::Inspect(document).has_value(),
            "glTF material inspection accepted an image outside its folder: " + std::string{ uri });
    }
}

} // namespace

void RunGltfExternalResourceTests() {
    RelativeResourcePathsAreTheOnlyAcceptedFileUris();
    ExternalBufferImportsAndLoadsWithoutTheSourceFolder();
    BufferUrisOutsideTheSourceFolderAreRefused();
    UnnamedMaterialsKeepDistinctStableSlots();
    ExternalImagesResolveInsideTheDocumentFolderOnly();
}

} // namespace kb::render::tests
