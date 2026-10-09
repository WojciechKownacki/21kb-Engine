// Every extension the import dialog offers must reach a decoder that reads it. Each advertised
// extension is imported here from a small fixture -- written by the test, or one of the image
// files already in the repository -- and loaded the way the runtime loads it: a model through the
// render mesh loader, a texture through the texture loader. Audio and font formats are checked
// against their decoder contracts, and the remaining categories, which import as opaque data, must
// round-trip their bytes. A catalog entry without a working importer fails this test.

#include "RendererTestSupport.hpp"

#include "engine/assets/AssetImportCatalog.hpp"
#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/ImportedAsset.hpp"
#include "engine/assets/ImportedAssetLoader.hpp"
#include "engine/audio/AudioClipFormats.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "private/ui/ScreenUIFontPayloadValidator.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace kb::render::tests {
namespace {

using Bytes = std::vector<std::uint8_t>;

void RequireThat(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << '\n';
        Require(false, "Asset import catalog coverage failed");
    }
}

[[nodiscard]] std::filesystem::path UniqueTempRoot(std::string_view prefix) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        (std::string{ prefix } + "_" + std::to_string(std::random_device{}()) + "_" + std::to_string(stamp));
}

[[nodiscard]] std::filesystem::path SourceRoot() {
    return std::filesystem::path{ KB_RENDERER_TEST_SOURCE_ROOT };
}

[[nodiscard]] Bytes ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    RequireThat(input.is_open(), "Fixture could not be opened: " + path.string());
    return Bytes{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

void WriteFileBytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    RequireThat(output.good(), "Fixture could not be written: " + path.string());
}

[[nodiscard]] Bytes TextBytes(std::string_view text) {
    return Bytes{ text.begin(), text.end() };
}

void AppendLe32(Bytes& bytes, std::uint32_t value) {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

void AppendLe64(Bytes& bytes, std::uint64_t value) {
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

void AppendBe16(Bytes& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

void AppendBe32(Bytes& bytes, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

void AppendFloat(Bytes& bytes, float value) {
    AppendLe32(bytes, std::bit_cast<std::uint32_t>(value));
}

void AppendText(Bytes& bytes, std::string_view text, bool terminate) {
    bytes.insert(bytes.end(), text.begin(), text.end());
    if (terminate) bytes.push_back(0U);
}

// One triangle, positions only: (0,0,0) (1,0,0) (0,1,0), three 32-bit float vec3.
[[nodiscard]] Bytes TrianglePositions() {
    Bytes bytes;
    for (const float value : { 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F }) AppendFloat(bytes, value);
    return bytes;
}

[[nodiscard]] std::string TriangleGltfJson(std::string_view bufferUriMember) {
    return std::string{ R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],)" }
        + R"("meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],)"
        + R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],)"
        + R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36}],)"
        + R"("buffers":[{"byteLength":36)" + std::string{ bufferUriMember } + "}]}";
}

[[nodiscard]] Bytes GltfFixture() {
    // The buffer travels as a data URI, so the document is self-contained.
    return TextBytes(TriangleGltfJson(
        R"(,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA")"));
}

[[nodiscard]] Bytes GlbFixture() {
    std::string json = TriangleGltfJson("");
    while (json.size() % 4U != 0U) json.push_back(' ');
    const Bytes binary = TrianglePositions();
    Bytes bytes;
    AppendLe32(bytes, 0x46546C67U); // "glTF"
    AppendLe32(bytes, 2U);
    AppendLe32(bytes, static_cast<std::uint32_t>(12U + 8U + json.size() + 8U + binary.size()));
    AppendLe32(bytes, static_cast<std::uint32_t>(json.size()));
    AppendLe32(bytes, 0x4E4F534AU); // "JSON"
    AppendText(bytes, json, false);
    AppendLe32(bytes, static_cast<std::uint32_t>(binary.size()));
    AppendLe32(bytes, 0x004E4942U); // "BIN\0"
    bytes.insert(bytes.end(), binary.begin(), binary.end());
    return bytes;
}

[[nodiscard]] Bytes ObjFixture() {
    return TextBytes("v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 1\nf 1//1 2//1 3//1\n");
}

// One node of a binary FBX 7.4 document: end offset, property count and bytes, name, properties,
// children, and the 13-byte null record that closes a node with children. `output` already holds
// everything before the node, so the end offset is absolute.
void AppendFbxNode(Bytes& output, std::string_view name, const Bytes& properties, std::uint32_t propertyCount,
    const std::vector<std::pair<std::string_view, Bytes>>& children) {
    const std::size_t start = output.size();
    output.resize(start + 12U);
    output.push_back(static_cast<std::uint8_t>(name.size()));
    AppendText(output, name, false);
    output.insert(output.end(), properties.begin(), properties.end());
    for (const auto& [childName, childProperties] : children) {
        AppendFbxNode(output, childName, childProperties, 1U, {});
    }
    if (!children.empty()) output.insert(output.end(), 13U, 0U);
    Bytes header;
    AppendLe32(header, static_cast<std::uint32_t>(output.size()));
    AppendLe32(header, propertyCount);
    AppendLe32(header, static_cast<std::uint32_t>(properties.size()));
    std::copy(header.begin(), header.end(), output.begin() + static_cast<std::ptrdiff_t>(start));
}

[[nodiscard]] Bytes FbxFixture() {
    // Binary FBX 7.4 with one triangle polygon: Objects > Geometry > Vertices, PolygonVertexIndex.
    Bytes vertices{ 'd' };
    AppendLe32(vertices, 9U);
    AppendLe32(vertices, 0U);
    AppendLe32(vertices, 72U);
    for (const double value : { 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0 }) {
        AppendLe64(vertices, std::bit_cast<std::uint64_t>(value));
    }
    Bytes polygon{ 'i' };
    AppendLe32(polygon, 3U);
    AppendLe32(polygon, 0U);
    AppendLe32(polygon, 12U);
    for (const std::int32_t value : { 0, 1, -3 }) AppendLe32(polygon, static_cast<std::uint32_t>(value));
    Bytes geometryProperties{ 'L' };
    AppendLe64(geometryProperties, 1U);
    for (const std::string_view text : { std::string_view{ "Geometry::Triangle" }, std::string_view{ "Mesh" } }) {
        geometryProperties.push_back('S');
        AppendLe32(geometryProperties, static_cast<std::uint32_t>(text.size()));
        AppendText(geometryProperties, text, false);
    }

    Bytes output = TextBytes("Kaydara FBX Binary  ");
    output.insert(output.end(), { 0x00U, 0x1AU, 0x00U });
    AppendLe32(output, 7400U);
    // Objects holds Geometry, which holds the two arrays.
    const std::size_t objectsStart = output.size();
    output.resize(objectsStart + 12U);
    output.push_back(7U);
    AppendText(output, "Objects", false);
    AppendFbxNode(output, "Geometry", geometryProperties, 3U, { { "Vertices", vertices }, { "PolygonVertexIndex", polygon } });
    output.insert(output.end(), 13U, 0U);
    Bytes header;
    AppendLe32(header, static_cast<std::uint32_t>(output.size()));
    AppendLe32(header, 0U);
    AppendLe32(header, 0U);
    std::copy(header.begin(), header.end(), output.begin() + static_cast<std::ptrdiff_t>(objectsStart));
    output.insert(output.end(), 13U, 0U);
    return output;
}

[[nodiscard]] std::optional<Bytes> ModelFixture(std::string_view extension) {
    if (extension == ".obj") return ObjFixture();
    if (extension == ".gltf") return GltfFixture();
    if (extension == ".glb") return GlbFixture();
    if (extension == ".fbx") return FbxFixture();
    return std::nullopt;
}

// 1x1 uncompressed true-colour TGA.
[[nodiscard]] Bytes TgaFixture() {
    Bytes bytes{ 0U, 0U, 2U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U, 0U, 1U, 0U, 24U, 0U };
    bytes.insert(bytes.end(), { 0x20U, 0x40U, 0x80U });
    return bytes;
}

// 1x1 Radiance RGBE image, stored flat.
[[nodiscard]] Bytes HdrFixture() {
    Bytes bytes = TextBytes("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n");
    bytes.insert(bytes.end(), { 128U, 64U, 32U, 129U });
    return bytes;
}

// 1x1 8-bit RGB Photoshop document, raw (uncompressed) planar data.
[[nodiscard]] Bytes PsdFixture() {
    Bytes bytes = TextBytes("8BPS");
    AppendBe16(bytes, 1U);
    bytes.insert(bytes.end(), 6U, 0U);
    AppendBe16(bytes, 3U); // channels
    AppendBe32(bytes, 1U); // height
    AppendBe32(bytes, 1U); // width
    AppendBe16(bytes, 8U); // bits per channel
    AppendBe16(bytes, 3U); // RGB colour mode
    AppendBe32(bytes, 0U); // colour mode data
    AppendBe32(bytes, 0U); // image resources
    AppendBe32(bytes, 0U); // layer and mask information
    AppendBe16(bytes, 0U); // raw image data
    bytes.insert(bytes.end(), { 0x80U, 0x40U, 0x20U });
    return bytes;
}

void AppendExrAttribute(Bytes& bytes, std::string_view name, std::string_view type, const Bytes& value) {
    AppendText(bytes, name, true);
    AppendText(bytes, type, true);
    AppendLe32(bytes, static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

// 1x1 scanline OpenEXR, uncompressed, 32-bit float B, G and R channels.
[[nodiscard]] Bytes ExrFixture() {
    Bytes bytes{ 0x76U, 0x2FU, 0x31U, 0x01U, 2U, 0U, 0U, 0U };
    Bytes channels;
    for (const std::string_view channel : { "B", "G", "R" }) {
        AppendText(channels, channel, true);
        AppendLe32(channels, 2U); // FLOAT
        channels.insert(channels.end(), 4U, 0U); // pLinear and reserved
        AppendLe32(channels, 1U);
        AppendLe32(channels, 1U);
    }
    channels.push_back(0U);
    AppendExrAttribute(bytes, "channels", "chlist", channels);
    AppendExrAttribute(bytes, "compression", "compression", Bytes{ 0U });
    Bytes window;
    for (int corner = 0; corner < 4; ++corner) AppendLe32(window, 0U);
    AppendExrAttribute(bytes, "dataWindow", "box2i", window);
    AppendExrAttribute(bytes, "displayWindow", "box2i", window);
    AppendExrAttribute(bytes, "lineOrder", "lineOrder", Bytes{ 0U });
    Bytes one;
    AppendFloat(one, 1.0F);
    AppendExrAttribute(bytes, "pixelAspectRatio", "float", one);
    Bytes center;
    AppendFloat(center, 0.0F);
    AppendFloat(center, 0.0F);
    AppendExrAttribute(bytes, "screenWindowCenter", "v2f", center);
    AppendExrAttribute(bytes, "screenWindowWidth", "float", one);
    bytes.push_back(0U);
    AppendLe64(bytes, static_cast<std::uint64_t>(bytes.size() + 8U));
    AppendLe32(bytes, 0U);  // scanline y
    AppendLe32(bytes, 12U); // bytes that follow
    AppendFloat(bytes, 0.25F);
    AppendFloat(bytes, 0.5F);
    AppendFloat(bytes, 1.0F);
    return bytes;
}

[[nodiscard]] std::optional<Bytes> TextureFixture(std::string_view extension) {
    const std::filesystem::path images = SourceRoot() / "third_party/bgfx.cmake/bgfx/examples/runtime/images";
    const std::filesystem::path textures = SourceRoot() / "third_party/bgfx.cmake/bgfx/examples/runtime/textures";
    const std::filesystem::path images2d = SourceRoot() / "sources/editor/tests/fixtures/ui";
    if (extension == ".png") return ReadFileBytes(images / "SplashScreen.png");
    if (extension == ".jpg" || extension == ".jpeg") return ReadFileBytes(images / "image1.jpg");
    if (extension == ".dds") return ReadFileBytes(textures / "fieldstone-rgba.dds");
    if (extension == ".ktx") return ReadFileBytes(textures / "texture_compression_bc1.ktx");
    if (extension == ".bmp") return ReadFileBytes(images2d / "AuditBMP.bmp");
    if (extension == ".gif") return ReadFileBytes(images2d / "AuditGIF.gif");
    if (extension == ".tga") return TgaFixture();
    if (extension == ".hdr") return HdrFixture();
    if (extension == ".psd") return PsdFixture();
    if (extension == ".exr") return ExrFixture();
    return std::nullopt;
}

struct ImportFixture {
    kb::assets::AssetManager manager;
    std::filesystem::path root;
    std::filesystem::path sources;

    ImportFixture() : root(UniqueTempRoot("21kb_import_catalog")), sources(root / "Sources") {
        std::filesystem::create_directories(sources);
        std::filesystem::create_directories(root / "Project" / "Assets");
        Require(manager.RegisterLoader(std::make_unique<kb::assets::ImportedAssetLoader>()),
            "Imported asset loader registration failed");
        Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()),
            "Render mesh loader registration failed");
        Require(manager.Mounts().Mount("Game", root / "Project" / "Assets"), "Import catalog project mount failed");
    }
    ~ImportFixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    ImportFixture(const ImportFixture&) = delete;
    ImportFixture& operator=(const ImportFixture&) = delete;

    [[nodiscard]] kb::assets::AssetImportItemResult Import(std::string_view extension, std::span<const std::uint8_t> bytes) {
        const std::string stem = "Fixture_" + std::string{ extension.substr(1U) };
        const std::filesystem::path source = sources / (stem + std::string{ extension });
        WriteFileBytes(source, bytes);
        const std::array files{ source };
        kb::assets::AssetImportResult result = kb::assets::AssetImportService::ImportFiles(manager, files, "/Game/Imports");
        RequireThat(result.items.size() == 1U, "Import returned no item for " + std::string{ extension });
        return result.items.front();
    }
};

void RequireImportedModelLoads(ImportFixture& fixture, std::string_view extension) {
    const std::optional<Bytes> bytes = ModelFixture(extension);
    RequireThat(bytes.has_value(), "The import catalog offers model format " + std::string{ extension } +
        " but no mesh importer reads it");
    const std::vector<std::string> loaderExtensions = RenderMeshAssetLoader{}.Extensions();
    RequireThat(std::ranges::find(loaderExtensions, std::string{ extension }) != loaderExtensions.end(),
        "The render mesh loader does not claim catalog model format " + std::string{ extension });
    const kb::assets::AssetImportItemResult item = fixture.Import(extension, *bytes);
    RequireThat(item.status == kb::assets::AssetImportItemStatus::Created && item.category == kb::assets::AssetImportCategory::Model,
        "Model fixture did not import: " + std::string{ extension } + " " + item.error);
    const kb::assets::AssetHandle<RenderMeshAssetData> mesh = fixture.manager.Load<RenderMeshAssetData>(item.id);
    RequireThat(mesh.IsLoaded() && mesh->desc.vertexCount >= 3U && mesh->desc.indexCount == 3U,
        "Imported model did not load as a render mesh: " + std::string{ extension });
}

void RequireImportedTextureDecodes(ImportFixture& fixture, std::string_view extension) {
    const std::optional<Bytes> bytes = TextureFixture(extension);
    RequireThat(bytes.has_value(), "The import catalog offers texture format " + std::string{ extension } +
        " but no texture decoder fixture proves it reads");
    const kb::assets::AssetImportItemResult item = fixture.Import(extension, *bytes);
    RequireThat(item.status == kb::assets::AssetImportItemStatus::Created && item.category == kb::assets::AssetImportCategory::Texture,
        "Texture fixture did not import: " + std::string{ extension } + " " + item.error);
    const std::optional<RenderTextureAssetData> texture = RenderTextureAssetLoader::LoadTexture(item.assetPhysicalPath);
    RequireThat(texture.has_value() && texture->width > 0U && texture->height > 0U &&
            texture->rgba8.size() == static_cast<std::size_t>(texture->width) * texture->height * 4U,
        "Imported texture did not decode: " + std::string{ extension });
}

void RequireImportedDataRoundTrips(ImportFixture& fixture, std::string_view extension, kb::assets::AssetImportCategory category) {
    const Bytes bytes = TextBytes("catalog fixture for " + std::string{ extension } + "\n");
    const kb::assets::AssetImportItemResult item = fixture.Import(extension, bytes);
    RequireThat(item.status == kb::assets::AssetImportItemStatus::Created && item.category == category,
        "Data fixture did not import: " + std::string{ extension } + " " + item.error);
    const kb::assets::AssetHandle<kb::assets::ImportedAsset> imported = fixture.manager.Load<kb::assets::ImportedAsset>(item.id);
    RequireThat(imported.IsLoaded() && imported->category == category &&
            imported->payload.size() == bytes.size() &&
            std::memcmp(imported->payload.data(), bytes.data(), bytes.size()) == 0,
        "Imported data did not keep its bytes: " + std::string{ extension });
}

void EveryAdvertisedExtensionHasAWorkingImporter() {
    ImportFixture fixture;
    std::size_t models = 0U;
    std::size_t textures = 0U;
    for (const std::string& extension : kb::assets::AssetImportCatalog::SupportedSourceExtensions()) {
        const kb::assets::AssetImportCategory category = kb::assets::AssetImportCatalog::ClassifyExtension(extension);
        RequireThat(category != kb::assets::AssetImportCategory::Unknown, "Catalog extension has no category: " + extension);
        switch (category) {
        case kb::assets::AssetImportCategory::Model:
            RequireImportedModelLoads(fixture, extension);
            ++models;
            break;
        case kb::assets::AssetImportCategory::Texture:
            RequireImportedTextureDecodes(fixture, extension);
            ++textures;
            break;
        case kb::assets::AssetImportCategory::Audio:
            RequireThat(kb::audio::IsSupportedAudioClipExtension(extension),
                "The import catalog offers an audio format the clip decoder does not read: " + extension);
            break;
        case kb::assets::AssetImportCategory::Font:
            RequireThat(ScreenUIFontPayloadValidator::SupportsExtension(extension),
                "The import catalog offers a font format the UI cannot rasterize: " + extension);
            break;
        default:
            RequireImportedDataRoundTrips(fixture, extension, category);
            break;
        }
    }
    Require(models == 4U, "The catalog must offer exactly the OBJ, glTF, GLB and FBX model importers");
    Require(textures > 0U, "The catalog offers no texture format");
}

void FormatsWithoutAnImporterAreNotOffered() {
    constexpr std::array<std::string_view, 25U> unsupported{
        ".usd", ".usda", ".usdc", ".usdz", ".abc", ".max", ".ma", ".mb", ".blend", ".dae", ".3ds", ".stl", ".ply",
        ".x", ".lwo", ".c4d", ".ase", ".smd", ".ktx2", ".basis", ".webp", ".tif", ".tiff", ".svg", ".ico",
    };
    const std::vector<std::string> catalog = kb::assets::AssetImportCatalog::SupportedSourceExtensions();
    const std::string filter = kb::assets::AssetImportCatalog::WindowsFileDialogFilter();
    ImportFixture fixture;
    for (const std::string_view extension : unsupported) {
        RequireThat(kb::assets::AssetImportCatalog::ClassifyExtension(std::filesystem::path{ extension }) ==
                    kb::assets::AssetImportCategory::Unknown &&
                std::ranges::find(catalog, std::string{ extension }) == catalog.end() &&
                filter.find("*" + std::string{ extension } + ";") == std::string::npos &&
                filter.find("*" + std::string{ extension } + ")") == std::string::npos,
            "A format no importer reads is still offered: " + std::string{ extension });
        const kb::assets::AssetImportItemResult item = fixture.Import(extension, TextBytes("not importable"));
        RequireThat(item.status == kb::assets::AssetImportItemStatus::Unsupported,
            "A format no importer reads was accepted by the import service: " + std::string{ extension });
    }
}

} // namespace

void RunAssetImportCatalogCoverageTests() {
    EveryAdvertisedExtensionHasAWorkingImporter();
    FormatsWithoutAnImporterAreNotOffered();
}

} // namespace kb::render::tests
