#include "RendererTestSupport.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneLightingAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialTypeAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "kb/render/runtime/RuntimeMaterialResolver.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace kb::render::tests {
namespace {

class HeadlessSurface final : public RenderSurface {
public:
    [[nodiscard]] std::uint32_t Width() const noexcept override {
        return 64U;
    }

    [[nodiscard]] std::uint32_t Height() const noexcept override {
        return 64U;
    }

    [[nodiscard]] void* NativeWindowHandle() const noexcept override {
        return nullptr;
    }

    [[nodiscard]] void* NativeDisplayHandle() const noexcept override {
        return nullptr;
    }
};

[[nodiscard]] kb::scene::TransformComponent TransformAt(float x, float y, float z) {
    return kb::scene::TransformComponent{
        .localPosition = kb::scene::Vec3{ x, y, z },
        .worldPosition = kb::scene::Vec3{ x, y, z },
        .worldDirty = false,
    };
}

[[nodiscard]] SceneRenderCamera IdentityCamera() noexcept {
    return SceneRenderCamera{
        .view = {
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F,
        },
        .projection = {
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F,
        },
    };
}

void WriteTriangleObj(const std::filesystem::path& path) {
    std::ofstream output{ path, std::ios::trunc };
    output
        << "v -0.1 -0.1 0.0\n"
        << "v 0.1 -0.1 0.0\n"
        << "v 0.0 0.1 0.0\n"
        << "vt 0 0\n"
        << "vt 1 0\n"
        << "vt 0.5 1\n"
        << "vn 0 0 1\n"
        << "f 1/1/1 2/2/1 3/3/1\n";
}

void WriteTexture(const std::filesystem::path& path, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    std::ofstream output{ path, std::ios::trunc };
    output
        << "size 1 1\n"
        << "rgba8 "
        << static_cast<std::uint32_t>(r) << " "
        << static_cast<std::uint32_t>(g) << " "
        << static_cast<std::uint32_t>(b) << " 255\n";
}

void WriteEmbeddedMaterialTriangleGltf(const std::filesystem::path& root) {
    const std::filesystem::path binPath = root / "embedded_mesh.bin";
    {
        const std::vector<float> positions{
            -0.1F, -0.1F, 0.0F,
            0.1F, -0.1F, 0.0F,
            0.0F, 0.1F, 0.0F,
        };
        const std::vector<float> normals{
            0.0F, 0.0F, 1.0F,
            0.0F, 0.0F, 1.0F,
            0.0F, 0.0F, 1.0F,
        };
        const std::vector<float> tangents{
            1.0F, 0.0F, 0.0F, 1.0F,
            1.0F, 0.0F, 0.0F, 1.0F,
            1.0F, 0.0F, 0.0F, 1.0F,
        };
        const std::vector<float> texCoords{
            0.0F, 0.0F,
            1.0F, 0.0F,
            0.5F, 1.0F,
        };
        const std::uint16_t indices[]{ 0U, 1U, 2U };

        std::ofstream output{ binPath, std::ios::binary | std::ios::trunc };
        output.write(reinterpret_cast<const char*>(positions.data()), static_cast<std::streamsize>(positions.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(normals.data()), static_cast<std::streamsize>(normals.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(tangents.data()), static_cast<std::streamsize>(tangents.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(texCoords.data()), static_cast<std::streamsize>(texCoords.size() * sizeof(float)));
        output.write(reinterpret_cast<const char*>(indices), static_cast<std::streamsize>(sizeof(indices)));
        const std::uint16_t padding = 0U;
        output.write(reinterpret_cast<const char*>(&padding), static_cast<std::streamsize>(sizeof(padding)));
    }

    std::ofstream output{ root / "embedded_triangle.gltf", std::ios::trunc };
    output
        << "{\n"
        << "  \"asset\": { \"version\": \"2.0\" },\n"
        << "  \"scene\": 0,\n"
        << "  \"scenes\": [{ \"nodes\": [0] }],\n"
        << "  \"nodes\": [{ \"mesh\": 0 }],\n"
        << "  \"materials\": [{\n"
        << "    \"name\": \"embedded_surface\",\n"
        << "    \"pbrMetallicRoughness\": {\n"
        << "      \"baseColorFactor\": [0.7, 0.8, 0.9, 0.5],\n"
        << "      \"metallicFactor\": 0.25,\n"
        << "      \"roughnessFactor\": 0.45,\n"
        << "      \"baseColorTexture\": { \"index\": 0 },\n"
        << "      \"metallicRoughnessTexture\": { \"index\": 1 }\n"
        << "    },\n"
        << "    \"normalTexture\": { \"index\": 2, \"scale\": 0.8 },\n"
        << "    \"occlusionTexture\": { \"index\": 3, \"strength\": 0.7 },\n"
        << "    \"emissiveFactor\": [0.05, 0.1, 0.2],\n"
        << "    \"emissiveTexture\": { \"index\": 4 },\n"
        << "    \"alphaMode\": \"MASK\"\n"
        << "  }],\n"
        << "  \"textures\": [{ \"source\": 0 }, { \"source\": 1 }, { \"source\": 2 }, { \"source\": 3 }, { \"source\": 4 }],\n"
        << "  \"images\": [{ \"uri\": \"embedded_albedo.kbtex\" }, { \"uri\": \"embedded_mr.kbtex\" }, { \"uri\": \"embedded_normal.kbtex\" }, { \"uri\": \"embedded_ao.kbtex\" }, { \"uri\": \"embedded_emissive.kbtex\" }],\n"
        << "  \"meshes\": [{ \"primitives\": [{ \"attributes\": { \"POSITION\": 0, \"NORMAL\": 1, \"TANGENT\": 2, \"TEXCOORD_0\": 3 }, \"indices\": 4, \"material\": 0 }] }],\n"
        << "  \"buffers\": [{ \"uri\": \"embedded_mesh.bin\", \"byteLength\": 152 }],\n"
        << "  \"bufferViews\": [\n"
        << "    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 36, \"target\": 34962 },\n"
        << "    { \"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 36, \"target\": 34962 },\n"
        << "    { \"buffer\": 0, \"byteOffset\": 72, \"byteLength\": 48, \"target\": 34962 },\n"
        << "    { \"buffer\": 0, \"byteOffset\": 120, \"byteLength\": 24, \"target\": 34962 },\n"
        << "    { \"buffer\": 0, \"byteOffset\": 144, \"byteLength\": 6, \"target\": 34963 }\n"
        << "  ],\n"
        << "  \"accessors\": [\n"
        << "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\", \"min\": [-0.1, -0.1, 0], \"max\": [0.1, 0.1, 0] },\n"
        << "    { \"bufferView\": 1, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
        << "    { \"bufferView\": 2, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC4\" },\n"
        << "    { \"bufferView\": 3, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC2\" },\n"
        << "    { \"bufferView\": 4, \"componentType\": 5123, \"count\": 3, \"type\": \"SCALAR\" }\n"
        << "  ]\n"
        << "}\n";
}

void WriteMaterial(
    const std::filesystem::path& path,
    std::uint64_t albedoTextureId,
    std::uint64_t normalTextureId,
    std::uint64_t metallicRoughnessTextureId,
    std::uint64_t occlusionTextureId,
    std::uint64_t emissiveTextureId,
    const char* alphaMode = "OPAQUE",
    float alpha = 1.0F) {
    std::ofstream output{ path, std::ios::trunc };
    output
        << "baseColor 0.8 0.7 0.6 " << alpha << "\n"
        << "emissiveColor 0.1 0.2 0.3\n"
        << "metallicFactor 0.2\n"
        << "roughnessFactor 0.55\n"
        << "normalScale 0.9\n"
        << "occlusionStrength 0.85\n"
        << "emissiveStrength 1.5\n"
        << "alphaMode " << alphaMode << "\n"
        << "albedoTextureAssetId " << albedoTextureId << "\n"
        << "normalTextureAssetId " << normalTextureId << "\n"
        << "metallicRoughnessTextureAssetId " << metallicRoughnessTextureId << "\n"
        << "occlusionTextureAssetId " << occlusionTextureId << "\n"
        << "emissiveTextureAssetId " << emissiveTextureId << "\n";
}

void WriteMaterialInstance(const std::filesystem::path& path, kb::assets::AssetId parentMaterialAssetId) {
    RenderMaterialInstanceAssetData instance{};
    instance.parentMaterialAssetId = parentMaterialAssetId;
    Require(RenderMaterialInstanceAssetWriter::Save(path, instance), "Material instance fixture could not be written");
}

void WriteMaterialWithTexturePaths(
    const std::filesystem::path& path,
    const char* alphaMode = "OPAQUE",
    float alpha = 1.0F,
    const char* emissiveTexturePath = "emissive.kbtex") {
    std::ofstream output{ path, std::ios::trunc };
    output
        << "baseColor 0.8 0.7 0.6 " << alpha << "\n"
        << "emissiveColor 0.1 0.2 0.3\n"
        << "metallicFactor 0.2\n"
        << "roughnessFactor 0.55\n"
        << "normalScale 0.9\n"
        << "occlusionStrength 0.85\n"
        << "emissiveStrength 1.5\n"
        << "alphaMode " << alphaMode << "\n"
        << "baseColorTexture albedo.kbtex\n"
        << "normalTexture normal.kbtex\n"
        << "metallicRoughnessTexture metallic_roughness.kbtex\n"
        << "occlusionTexture occlusion.kbtex\n"
        << "emissiveTexture " << emissiveTexturePath << "\n";
}

[[nodiscard]] std::optional<std::filesystem::path> FindWorkspaceProjectAssets() {
    std::filesystem::path cursor = std::filesystem::current_path();
    for (int depth = 0; depth < 8; ++depth) {
        const std::filesystem::path assets = cursor / "Project" / "Assets";
        std::error_code error;
        if (std::filesystem::is_regular_file(assets / "Cube.21kb", error) &&
            std::filesystem::is_regular_file(assets / "Scenes" / "Main.21kbscene", error)) {
            return assets;
        }
        if (!cursor.has_parent_path()) {
            break;
        }
        cursor = cursor.parent_path();
    }
    return std::nullopt;
}

void RunRuntimeMaterialResolverReturnsTypedFallbacksAndDiagnosticsTest() {
    const RuntimeFallbackMaterialProfile defaultProfile = RuntimeMaterialResolver::FallbackMaterialProfile(RuntimeFallbackMaterialKind::Default);
    const RuntimeFallbackMaterialProfile errorProfile = RuntimeMaterialResolver::FallbackMaterialProfile(RuntimeFallbackMaterialKind::Error);
    Require(defaultProfile.kind == RuntimeFallbackMaterialKind::Default &&
            defaultProfile.status == RuntimeMaterialResolveStatus::DefaultMaterial &&
            defaultProfile.stableName == "runtime.default_material",
        "KBMAT-1005: Default material profile should be explicit and stable");
    Require(errorProfile.kind == RuntimeFallbackMaterialKind::Error &&
            errorProfile.status == RuntimeMaterialResolveStatus::ErrorMaterial &&
            errorProfile.stableName == "runtime.error_material",
        "KBMAT-1005: Error material profile should be explicit and stable");
    Require(NearlyEqual(defaultProfile.desc.baseColor[0], 1.0F) &&
            NearlyEqual(defaultProfile.desc.baseColor[1], 1.0F) &&
            NearlyEqual(defaultProfile.desc.baseColor[2], 1.0F) &&
            NearlyEqual(defaultProfile.desc.roughnessFactor, RuntimeMaterialResolver::DefaultMaterialDesc().roughnessFactor),
        "KBMAT-1005: Default material profile should expose the runtime default descriptor");
    Require(NearlyEqual(errorProfile.desc.baseColor[0], 1.0F) &&
            NearlyEqual(errorProfile.desc.baseColor[1], 0.0F) &&
            NearlyEqual(errorProfile.desc.baseColor[2], 1.0F) &&
            NearlyEqual(errorProfile.desc.roughnessFactor, RuntimeMaterialResolver::ErrorMaterialDesc().roughnessFactor),
        "KBMAT-1005: Error material profile should expose the runtime error descriptor");

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_runtime_material_resolver";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Runtime material resolver test could not create temp root");

    const std::filesystem::path brokenMaterialPath = root / "broken.kbmat";
    {
        std::ofstream output{ brokenMaterialPath, std::ios::trunc };
        output << "roughnessFactor broken\n";
    }

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Runtime material resolver test could not register material loader");
    Require(manager.Mounts().Mount("Game", root), "Runtime material resolver test could not mount asset root");
    Require(manager.DiscoverMountedAssets() == 1U, "Runtime material resolver test did not discover the broken material");
    const kb::assets::AssetMetadata* brokenMetadata = manager.Registry().FindByPath("/Game/broken.kbmat");
    Require(brokenMetadata != nullptr && brokenMetadata->type == "RenderMaterial", "Runtime material resolver test discovered wrong material metadata");

    RuntimeMaterialResolver resolver;
    const ResolvedRuntimeMaterialAsset missing = resolver.ResolveAsset(manager, kb::assets::AssetId{ 404404U });
    Require(missing.resolved, "Runtime material resolver should resolve missing materials to a fallback");
    Require(missing.status == RuntimeMaterialResolveStatus::DefaultMaterial, "Missing material asset should use the default material fallback");
    Require(missing.diagnostics.size() == 1U && missing.diagnostics[0].kind == RuntimeMaterialResolveDiagnosticKind::MissingMaterialAsset, "Missing material asset should report a typed diagnostic");
    Require(NearlyEqual(missing.material.desc.baseColor[0], 1.0F) && NearlyEqual(missing.material.desc.baseColor[1], 1.0F), "Default material fallback should be white");
    Require(NearlyEqual(missing.material.desc.baseColor[2], defaultProfile.desc.baseColor[2]), "KBMAT-1005: Missing material fallback should use the explicit default material profile");

    const ResolvedRuntimeMaterialAsset broken = resolver.ResolveAsset(manager, *brokenMetadata);
    Require(broken.resolved, "Runtime material resolver should resolve invalid materials to a fallback");
    Require(broken.status == RuntimeMaterialResolveStatus::ErrorMaterial, "Invalid material asset should use the error material fallback");
    Require(!broken.diagnostics.empty(), "Invalid material asset should expose parser diagnostics");
    Require(broken.diagnostics[0].kind == RuntimeMaterialResolveDiagnosticKind::MaterialLoadFailed, "Invalid material diagnostic should be typed as a load failure");
    Require(broken.diagnostics[0].message.find("invalid_float") != std::string::npos, "Invalid material diagnostic should preserve parser diagnostic code");
    Require(NearlyEqual(broken.material.desc.baseColor[0], 1.0F) && NearlyEqual(broken.material.desc.baseColor[1], 0.0F) && NearlyEqual(broken.material.desc.baseColor[2], 1.0F), "Error material fallback should be magenta");
    Require(NearlyEqual(broken.material.desc.roughnessFactor, errorProfile.desc.roughnessFactor), "KBMAT-1005: Broken material fallback should use the explicit error material profile");

    std::filesystem::remove_all(root, error);
}

[[nodiscard]] bool ContainsAssetDependency(const std::vector<kb::assets::AssetId>& dependencies, kb::assets::AssetId id) noexcept {
    for (const kb::assets::AssetId dependency : dependencies) {
        if (dependency == id) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] RenderMaterialGraphLink MakeGraphLink(
    RenderMaterialGraphNodeKind fromKind,
    std::uint32_t fromNodeId,
    std::string fromPin,
    RenderMaterialGraphNodeKind toKind,
    std::uint32_t toNodeId,
    std::string toPin) {
    RenderMaterialGraphLink link{
        .fromNodeId = fromNodeId,
        .fromPinId = RenderMaterialGraphStablePinId(fromKind, fromPin, true),
        .fromPin = std::move(fromPin),
        .toNodeId = toNodeId,
        .toPinId = RenderMaterialGraphStablePinId(toKind, toPin, false),
        .toPin = std::move(toPin),
    };
    link.id = MakeRenderMaterialGraphLinkId(link);
    return link;
}

void WriteReloadableMaterial(
    const std::filesystem::path& path,
    float red,
    float roughness,
    std::uint64_t albedoTextureId) {
    std::ofstream output{ path, std::ios::trunc };
    output
        << "baseColor " << red << " 0.25 0.5 1\n"
        << "roughnessFactor " << roughness << "\n"
        << "alphaMode OPAQUE\n"
        << "albedoTextureAssetId " << albedoTextureId << "\n";
}

void WriteGraphBackedReloadableMaterial(
    const std::filesystem::path& path,
    float red,
    float roughness,
    std::uint64_t materialTypeAssetId,
    std::string_view materialTypeAssetPath,
    std::uint64_t artifactAssetId,
    std::uint64_t artifactContentHash) {
    std::ofstream output{ path, std::ios::trunc };
    output
        << "version 1\n"
        << "materialType " << kRenderMaterialAssetBuiltInPbrType << "\n"
        << "materialTypeVersion " << kRenderMaterialAssetBuiltInPbrTypeVersion << "\n"
        << "materialTypeAssetId " << materialTypeAssetId << "\n"
        << "materialTypeAsset " << materialTypeAssetPath << "\n"
        << "baseColor 0.01 0.02 0.03 1\n"
        << "roughnessFactor 0.99\n"
        << "graphParameterValue baseColor Color " << red << " 0.35 0.15 1\n"
        << "graphParameterValue roughnessFactor Scalar " << roughness << "\n"
        << "alphaMode OPAQUE\n"
        << "graphLastGoodArtifactAssetId " << artifactAssetId << "\n"
        << "graphLastGoodArtifactHash " << artifactContentHash << "\n";
}

void WriteGraphValidationMaterial(const std::filesystem::path& path, float red, bool connectBaseColor) {
    std::ofstream output{ path, std::ios::trunc };
    output
        << "version 1\n"
        << "materialType builtin.pbr\n"
        << "materialTypeVersion 1\n"
        << "baseColor " << red << " 0.25 0.5 1\n"
        << "roughnessFactor 0.45\n"
        << "alphaMode OPAQUE\n"
        << "graphNode 1 MaterialOutput 640 240\n"
        << "graphNode 2 ConstantColor 120 80\n";
    if (connectBaseColor) {
        const RenderMaterialGraphLink link = MakeGraphLink(
            RenderMaterialGraphNodeKind::ConstantColor,
            2U,
            "rgba",
            RenderMaterialGraphNodeKind::MaterialOutput,
            1U,
            "baseColor");
        output
            << "graphLink " << link.id << ' '
            << link.fromNodeId << ' ' << link.fromPinId << ' ' << link.fromPin << ' '
            << link.toNodeId << ' ' << link.toPinId << ' ' << link.toPin << "\n";
    } else {
        const RenderMaterialGraphLink addToMultiply = MakeGraphLink(
            RenderMaterialGraphNodeKind::Add,
            3U,
            "value",
            RenderMaterialGraphNodeKind::Multiply,
            4U,
            "a");
        const RenderMaterialGraphLink multiplyToAdd = MakeGraphLink(
            RenderMaterialGraphNodeKind::Multiply,
            4U,
            "value",
            RenderMaterialGraphNodeKind::Add,
            3U,
            "a");
        output
            << "graphNode 3 Add 300 80\n"
            << "graphNode 4 Multiply 460 80\n"
            << "graphLink " << addToMultiply.id << ' '
            << addToMultiply.fromNodeId << ' ' << addToMultiply.fromPinId << ' ' << addToMultiply.fromPin << ' '
            << addToMultiply.toNodeId << ' ' << addToMultiply.toPinId << ' ' << addToMultiply.toPin << "\n"
            << "graphLink " << multiplyToAdd.id << ' '
            << multiplyToAdd.fromNodeId << ' ' << multiplyToAdd.fromPinId << ' ' << multiplyToAdd.fromPin << ' '
            << multiplyToAdd.toNodeId << ' ' << multiplyToAdd.toPinId << ' ' << multiplyToAdd.toPin << "\n";
    }
}

[[nodiscard]] RenderMaterialGraphNode MakeGraphNode(
    std::uint32_t id,
    RenderMaterialGraphNodeKind kind,
    std::string stableId = {},
    std::string defaultValueHint = {}) {
    RenderMaterialGraphNode node{
        .id = id,
        .kind = kind,
        .positionX = static_cast<std::int32_t>(id * 140U),
        .positionY = 120,
    };
    node.parameter.stableId = std::move(stableId);
    node.parameter.defaultValueHint = std::move(defaultValueHint);
    return node;
}

[[nodiscard]] RenderMaterialGraphParameterValue MakeTextureGraphValue(std::string stableId, std::uint64_t assetId) {
    return RenderMaterialGraphParameterValue{
        .stableId = std::move(stableId),
        .type = RenderMaterialParameterType::Texture,
        .assetId = assetId,
    };
}

void RunRuntimeMaterialResolverEvaluatesMaterialOutputTextureGraphTest() {
    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    const kb::assets::AssetMetadata materialMetadata{
        .id = kb::assets::AssetId{ 71001U },
        .type = "RenderMaterial",
        .name = "GraphTextureMaterial",
    };

    RenderMaterialAssetData material{};
    material.desc.baseColor[0] = 0.25F;
    material.desc.baseColor[1] = 0.25F;
    material.desc.baseColor[2] = 0.25F;
    material.desc.baseColor[3] = 1.0F;
    material.desc.roughnessFactor = 0.25F;
    material.desc.metallicFactor = 0.0F;
    material.desc.occlusionStrength = 0.5F;

    material.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::TextureSample),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::TextureSample),
        MakeGraphNode(4U, RenderMaterialGraphNodeKind::NormalUnpack),
        MakeGraphNode(5U, RenderMaterialGraphNodeKind::TextureSample),
        MakeGraphNode(6U, RenderMaterialGraphNodeKind::TextureSample),
        MakeGraphNode(7U, RenderMaterialGraphNodeKind::TextureSample),
        MakeGraphNode(8U, RenderMaterialGraphNodeKind::ParameterScalar, "opacity"),
        MakeGraphNode(9U, RenderMaterialGraphNodeKind::ParameterTexture, "albedoParam"),
    };
    material.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ParameterTexture, 9U, "texture", RenderMaterialGraphNodeKind::TextureSample, 2U, "texture"),
        MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 2U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"),
        MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 3U, "color", RenderMaterialGraphNodeKind::NormalUnpack, 4U, "color"),
        MakeGraphLink(RenderMaterialGraphNodeKind::NormalUnpack, 4U, "normal", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "normal"),
        MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 5U, "g", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"),
        MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 5U, "b", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"),
        MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 6U, "r", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "occlusion"),
        MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 7U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ParameterScalar, 8U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"),
    };
    material.graphParameterValues = {
        MakeTextureGraphValue("albedoParam", 101U),
        MakeTextureGraphValue("textureSample2", 999U),
        MakeTextureGraphValue("textureSample3", 102U),
        MakeTextureGraphValue("textureSample5", 103U),
        MakeTextureGraphValue("textureSample6", 104U),
        MakeTextureGraphValue("textureSample7", 105U),
        RenderMaterialGraphParameterValue{
            .stableId = "opacity",
            .type = RenderMaterialParameterType::Scalar,
            .numbers = { 0.42F, 0.0F, 0.0F, 0.0F },
        },
    };

    const ResolvedRuntimeMaterialDesc resolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, material);
    Require(resolved.desc.albedoTextureAssetId == 101U, "KBMAT-RUNTIME: Texture Sample Color did not drive Material Output Base Color texture");
    Require(resolved.desc.normalTextureAssetId == 102U, "KBMAT-RUNTIME: Texture Sample through Normal Unpack did not drive Material Output Normal texture");
    Require(resolved.desc.metallicRoughnessTextureAssetId == 103U, "KBMAT-RUNTIME: Texture Sample G/B did not drive metallic-roughness texture");
    Require(resolved.desc.occlusionTextureAssetId == 104U, "KBMAT-RUNTIME: Texture Sample R did not drive occlusion texture");
    Require(resolved.desc.emissiveTextureAssetId == 105U, "KBMAT-RUNTIME: Texture Sample Color did not drive emissive texture");
    Require(NearlyEqual(resolved.desc.baseColor[0], 1.0F) && NearlyEqual(resolved.desc.baseColor[3], 0.42F), "KBMAT-RUNTIME: Material Output graph factors were not applied to base color/alpha");
    Require(NearlyEqual(resolved.desc.roughnessFactor, 1.0F) && NearlyEqual(resolved.desc.metallicFactor, 1.0F), "KBMAT-RUNTIME: Material Output scalar channels were not evaluated");

    RenderMaterialAssetData disconnected{};
    disconnected.desc.baseColor[0] = 0.75F;
    disconnected.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::TextureSample),
    };
    const ResolvedRuntimeMaterialDesc disconnectedResolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, disconnected);
    Require(
        NearlyEqual(disconnectedResolved.desc.baseColor[0], 0.0F) &&
            NearlyEqual(disconnectedResolved.desc.baseColor[1], 0.0F) &&
            NearlyEqual(disconnectedResolved.desc.baseColor[2], 0.0F) &&
            NearlyEqual(disconnectedResolved.desc.baseColor[3], 1.0F),
        "KBMAT-RUNTIME: Disconnected graph Base Color should resolve to black material output");
}

void RunRuntimeMaterialResolverEvaluatesConstantAndMathGraphTest() {
    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    const kb::assets::AssetMetadata materialMetadata{
        .id = kb::assets::AssetId{ 71002U },
        .type = "RenderMaterial",
        .name = "GraphConstantMaterial",
    };

    RenderMaterialAssetData material{};
    material.desc.baseColor[0] = 0.9F;
    material.desc.baseColor[1] = 0.9F;
    material.desc.baseColor[2] = 0.9F;
    material.desc.baseColor[3] = 1.0F;
    material.desc.roughnessFactor = 1.0F;
    material.desc.metallicFactor = 0.0F;
    material.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantColor, {}, "0 0 0 1"),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::ConstantColor, {}, "0.8 0.4 0.2 1"),
        MakeGraphNode(4U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.25"),
        MakeGraphNode(5U, RenderMaterialGraphNodeKind::Lerp),
        MakeGraphNode(6U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.8"),
        MakeGraphNode(7U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.3"),
        MakeGraphNode(8U, RenderMaterialGraphNodeKind::Subtract),
        MakeGraphNode(9U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "2"),
        MakeGraphNode(10U, RenderMaterialGraphNodeKind::Power),
        MakeGraphNode(11U, RenderMaterialGraphNodeKind::OneMinus),
        MakeGraphNode(12U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.15"),
        MakeGraphNode(13U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.3"),
        MakeGraphNode(14U, RenderMaterialGraphNodeKind::Divide),
    };
    material.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::Lerp, 5U, "a"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 3U, "rgba", RenderMaterialGraphNodeKind::Lerp, 5U, "b"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 4U, "value", RenderMaterialGraphNodeKind::Lerp, 5U, "t"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Lerp, 5U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 6U, "value", RenderMaterialGraphNodeKind::Subtract, 8U, "a"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 7U, "value", RenderMaterialGraphNodeKind::Subtract, 8U, "b"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Subtract, 8U, "value", RenderMaterialGraphNodeKind::Power, 10U, "base"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 9U, "value", RenderMaterialGraphNodeKind::Power, 10U, "exponent"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Power, 10U, "value", RenderMaterialGraphNodeKind::OneMinus, 11U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::OneMinus, 11U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 12U, "value", RenderMaterialGraphNodeKind::Divide, 14U, "a"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 13U, "value", RenderMaterialGraphNodeKind::Divide, 14U, "b"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Divide, 14U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"),
    };

    const ResolvedRuntimeMaterialDesc resolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, material);
    Require(NearlyEqual(resolved.desc.baseColor[0], 0.2F) &&
            NearlyEqual(resolved.desc.baseColor[1], 0.1F) &&
            NearlyEqual(resolved.desc.baseColor[2], 0.05F) &&
            NearlyEqual(resolved.desc.baseColor[3], 1.0F),
        "KBMAT-RUNTIME: Constant Color through Lerp did not evaluate into Material Output Base Color");
    Require(NearlyEqual(resolved.desc.roughnessFactor, 0.75F), "KBMAT-RUNTIME: Subtract/Power/OneMinus graph did not evaluate into Roughness");
    Require(NearlyEqual(resolved.desc.metallicFactor, 0.5F), "KBMAT-RUNTIME: Divide graph did not evaluate into Metallic");

    RenderMaterialAssetData utilityMaterial{};
    utilityMaterial.desc.baseColor[0] = 1.0F;
    utilityMaterial.desc.baseColor[1] = 1.0F;
    utilityMaterial.desc.baseColor[2] = 1.0F;
    utilityMaterial.desc.baseColor[3] = 1.0F;
    utilityMaterial.desc.roughnessFactor = 1.0F;
    utilityMaterial.desc.metallicFactor = 0.0F;
    utilityMaterial.desc.occlusionStrength = 1.0F;
    utilityMaterial.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "-0.25"),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::Absolute),
        MakeGraphNode(4U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.4"),
        MakeGraphNode(5U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.8"),
        MakeGraphNode(6U, RenderMaterialGraphNodeKind::Minimum),
        MakeGraphNode(7U, RenderMaterialGraphNodeKind::Maximum),
        MakeGraphNode(8U, RenderMaterialGraphNodeKind::Saturate),
        MakeGraphNode(9U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "1.75"),
        MakeGraphNode(10U, RenderMaterialGraphNodeKind::Fraction),
        MakeGraphNode(11U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.25"),
        MakeGraphNode(12U, RenderMaterialGraphNodeKind::SquareRoot),
        MakeGraphNode(13U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.9"),
        MakeGraphNode(14U, RenderMaterialGraphNodeKind::Floor),
        MakeGraphNode(15U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.1"),
        MakeGraphNode(16U, RenderMaterialGraphNodeKind::Ceil),
        MakeGraphNode(17U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0"),
        MakeGraphNode(18U, RenderMaterialGraphNodeKind::Sine),
        MakeGraphNode(19U, RenderMaterialGraphNodeKind::Cosine),
    };
    utilityMaterial.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 2U, "value", RenderMaterialGraphNodeKind::Absolute, 3U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 4U, "value", RenderMaterialGraphNodeKind::Minimum, 6U, "a"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 5U, "value", RenderMaterialGraphNodeKind::Minimum, 6U, "b"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Absolute, 3U, "value", RenderMaterialGraphNodeKind::Maximum, 7U, "a"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Minimum, 6U, "value", RenderMaterialGraphNodeKind::Maximum, 7U, "b"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Maximum, 7U, "value", RenderMaterialGraphNodeKind::Saturate, 8U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Saturate, 8U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 9U, "value", RenderMaterialGraphNodeKind::Fraction, 10U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Fraction, 10U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 11U, "value", RenderMaterialGraphNodeKind::SquareRoot, 12U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::SquareRoot, 12U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "occlusion"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 13U, "value", RenderMaterialGraphNodeKind::Floor, 14U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Floor, 14U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 15U, "value", RenderMaterialGraphNodeKind::Ceil, 16U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Ceil, 16U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 17U, "value", RenderMaterialGraphNodeKind::Sine, 18U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Sine, 18U, "value", RenderMaterialGraphNodeKind::Cosine, 19U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Cosine, 19U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"),
    };
    const ResolvedRuntimeMaterialDesc utilityResolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, utilityMaterial);
    Require(NearlyEqual(utilityResolved.desc.roughnessFactor, 0.4F), "KBMAT-RUNTIME: Abs/Min/Max/Saturate graph did not evaluate into Roughness");
    Require(NearlyEqual(utilityResolved.desc.metallicFactor, 0.75F), "KBMAT-RUNTIME: Fraction graph did not evaluate into Metallic");
    Require(NearlyEqual(utilityResolved.desc.occlusionStrength, 0.5F), "KBMAT-RUNTIME: SquareRoot graph did not evaluate into Occlusion");
    Require(NearlyEqual(utilityResolved.desc.baseColor[0], 1.0F), "KBMAT-RUNTIME: Ceil graph did not evaluate into Base Color");
    Require(NearlyEqual(utilityResolved.desc.baseColor[3], 0.0F), "KBMAT-RUNTIME: Floor graph did not evaluate into Alpha");
    Require(NearlyEqual(utilityResolved.desc.emissiveColor[0], 1.0F), "KBMAT-RUNTIME: Sine/Cosine graph did not evaluate into Emissive");

    RenderMaterialAssetData vectorMaterial{};
    vectorMaterial.desc.baseColor[0] = 1.0F;
    vectorMaterial.desc.baseColor[1] = 1.0F;
    vectorMaterial.desc.baseColor[2] = 1.0F;
    vectorMaterial.desc.baseColor[3] = 1.0F;
    vectorMaterial.desc.roughnessFactor = 1.0F;
    vectorMaterial.desc.metallicFactor = 0.0F;
    vectorMaterial.desc.occlusionStrength = 1.0F;
    vectorMaterial.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantVector, {}, "1 0 0"),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::ConstantVector, {}, "0.5 0 0"),
        MakeGraphNode(4U, RenderMaterialGraphNodeKind::DotProduct),
        MakeGraphNode(5U, RenderMaterialGraphNodeKind::ConstantVector, {}, "1 0 0"),
        MakeGraphNode(6U, RenderMaterialGraphNodeKind::ConstantVector, {}, "0 1 0"),
        MakeGraphNode(7U, RenderMaterialGraphNodeKind::CrossProduct),
        MakeGraphNode(8U, RenderMaterialGraphNodeKind::Normalize),
        MakeGraphNode(9U, RenderMaterialGraphNodeKind::Length),
        MakeGraphNode(10U, RenderMaterialGraphNodeKind::ConstantVector, {}, "0 0 0"),
        MakeGraphNode(11U, RenderMaterialGraphNodeKind::ConstantVector, {}, "0 0 0.25"),
        MakeGraphNode(12U, RenderMaterialGraphNodeKind::Distance),
        MakeGraphNode(13U, RenderMaterialGraphNodeKind::ConstantColor, {}, "1 1 1 1"),
    };
    vectorMaterial.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 13U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 2U, "xyz", RenderMaterialGraphNodeKind::DotProduct, 4U, "a"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 3U, "xyz", RenderMaterialGraphNodeKind::DotProduct, 4U, "b"),
        MakeGraphLink(RenderMaterialGraphNodeKind::DotProduct, 4U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 5U, "xyz", RenderMaterialGraphNodeKind::CrossProduct, 7U, "a"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 6U, "xyz", RenderMaterialGraphNodeKind::CrossProduct, 7U, "b"),
        MakeGraphLink(RenderMaterialGraphNodeKind::CrossProduct, 7U, "value", RenderMaterialGraphNodeKind::Normalize, 8U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Normalize, 8U, "value", RenderMaterialGraphNodeKind::Length, 9U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Length, 9U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 10U, "xyz", RenderMaterialGraphNodeKind::Distance, 12U, "a"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 11U, "xyz", RenderMaterialGraphNodeKind::Distance, 12U, "b"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Distance, 12U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "occlusion"),
    };
    const ResolvedRuntimeMaterialDesc vectorResolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, vectorMaterial);
    Require(NearlyEqual(vectorResolved.desc.roughnessFactor, 0.5F), "KBMAT-RUNTIME: DotProduct graph did not evaluate into Roughness");
    Require(NearlyEqual(vectorResolved.desc.metallicFactor, 1.0F), "KBMAT-RUNTIME: CrossProduct/Normalize/Length graph did not evaluate into Metallic");
    Require(NearlyEqual(vectorResolved.desc.occlusionStrength, 0.25F), "KBMAT-RUNTIME: Distance graph did not evaluate into Occlusion");

    RenderMaterialAssetData channelMaterial{};
    channelMaterial.desc.baseColor[0] = 1.0F;
    channelMaterial.desc.baseColor[1] = 1.0F;
    channelMaterial.desc.baseColor[2] = 1.0F;
    channelMaterial.desc.baseColor[3] = 1.0F;
    channelMaterial.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantColor, {}, "0.2 0.4 0.6 0.8"),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::BreakVector),
        MakeGraphNode(4U, RenderMaterialGraphNodeKind::MakeVector),
    };
    channelMaterial.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::BreakVector, 3U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::BreakVector, 3U, "z", RenderMaterialGraphNodeKind::MakeVector, 4U, "x"),
        MakeGraphLink(RenderMaterialGraphNodeKind::BreakVector, 3U, "y", RenderMaterialGraphNodeKind::MakeVector, 4U, "y"),
        MakeGraphLink(RenderMaterialGraphNodeKind::BreakVector, 3U, "x", RenderMaterialGraphNodeKind::MakeVector, 4U, "z"),
        MakeGraphLink(RenderMaterialGraphNodeKind::BreakVector, 3U, "w", RenderMaterialGraphNodeKind::MakeVector, 4U, "w"),
        MakeGraphLink(RenderMaterialGraphNodeKind::MakeVector, 4U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"),
    };
    const ResolvedRuntimeMaterialDesc channelResolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, channelMaterial);
    Require(NearlyEqual(channelResolved.desc.baseColor[0], 0.6F) &&
            NearlyEqual(channelResolved.desc.baseColor[1], 0.4F) &&
            NearlyEqual(channelResolved.desc.baseColor[2], 0.2F) &&
            NearlyEqual(channelResolved.desc.baseColor[3], 0.8F),
        "KBMAT-RUNTIME: BreakVector/MakeVector graph did not remap Base Color channels");

    RenderMaterialAssetData conditionalMaterial{};
    conditionalMaterial.desc.baseColor[0] = 1.0F;
    conditionalMaterial.desc.baseColor[1] = 1.0F;
    conditionalMaterial.desc.baseColor[2] = 1.0F;
    conditionalMaterial.desc.baseColor[3] = 1.0F;
    conditionalMaterial.desc.roughnessFactor = 0.0F;
    conditionalMaterial.desc.metallicFactor = 0.0F;
    conditionalMaterial.desc.occlusionStrength = 1.0F;
    conditionalMaterial.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantColor, {}, "1 1 1 1"),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"),
        MakeGraphNode(4U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.75"),
        MakeGraphNode(5U, RenderMaterialGraphNodeKind::Step),
        MakeGraphNode(6U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0"),
        MakeGraphNode(7U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "1"),
        MakeGraphNode(8U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"),
        MakeGraphNode(9U, RenderMaterialGraphNodeKind::SmoothStep),
        MakeGraphNode(10U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.25"),
        MakeGraphNode(11U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"),
        MakeGraphNode(12U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.2"),
        MakeGraphNode(13U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.4"),
        MakeGraphNode(14U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.8"),
        MakeGraphNode(15U, RenderMaterialGraphNodeKind::If),
        MakeGraphNode(16U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"),
        MakeGraphNode(17U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"),
        MakeGraphNode(18U, RenderMaterialGraphNodeKind::If),
        MakeGraphNode(19U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.75"),
        MakeGraphNode(20U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"),
        MakeGraphNode(21U, RenderMaterialGraphNodeKind::If),
    };
    conditionalMaterial.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 3U, "value", RenderMaterialGraphNodeKind::Step, 5U, "edge"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 4U, "value", RenderMaterialGraphNodeKind::Step, 5U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Step, 5U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 6U, "value", RenderMaterialGraphNodeKind::SmoothStep, 9U, "min"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 7U, "value", RenderMaterialGraphNodeKind::SmoothStep, 9U, "max"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 8U, "value", RenderMaterialGraphNodeKind::SmoothStep, 9U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::SmoothStep, 9U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 10U, "value", RenderMaterialGraphNodeKind::If, 15U, "a"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 11U, "value", RenderMaterialGraphNodeKind::If, 15U, "b"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 12U, "value", RenderMaterialGraphNodeKind::If, 15U, "less"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 13U, "value", RenderMaterialGraphNodeKind::If, 15U, "equal"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 14U, "value", RenderMaterialGraphNodeKind::If, 15U, "greater"),
        MakeGraphLink(RenderMaterialGraphNodeKind::If, 15U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "occlusion"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 16U, "value", RenderMaterialGraphNodeKind::If, 18U, "a"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 17U, "value", RenderMaterialGraphNodeKind::If, 18U, "b"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 12U, "value", RenderMaterialGraphNodeKind::If, 18U, "less"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 13U, "value", RenderMaterialGraphNodeKind::If, 18U, "equal"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 14U, "value", RenderMaterialGraphNodeKind::If, 18U, "greater"),
        MakeGraphLink(RenderMaterialGraphNodeKind::If, 18U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 19U, "value", RenderMaterialGraphNodeKind::If, 21U, "a"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 20U, "value", RenderMaterialGraphNodeKind::If, 21U, "b"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 12U, "value", RenderMaterialGraphNodeKind::If, 21U, "less"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 13U, "value", RenderMaterialGraphNodeKind::If, 21U, "equal"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 14U, "value", RenderMaterialGraphNodeKind::If, 21U, "greater"),
        MakeGraphLink(RenderMaterialGraphNodeKind::If, 21U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"),
    };
    const ResolvedRuntimeMaterialDesc conditionalResolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, conditionalMaterial);
    Require(NearlyEqual(conditionalResolved.desc.roughnessFactor, 1.0F), "KBMAT-RUNTIME: Step graph did not evaluate into Roughness");
    Require(NearlyEqual(conditionalResolved.desc.metallicFactor, 0.5F), "KBMAT-RUNTIME: SmoothStep graph did not evaluate into Metallic");
    Require(NearlyEqual(conditionalResolved.desc.occlusionStrength, 0.2F), "KBMAT-RUNTIME: If less branch did not evaluate into Occlusion");
    Require(NearlyEqual(conditionalResolved.desc.baseColor[3], 0.4F), "KBMAT-RUNTIME: If equal branch did not evaluate into Alpha");
    Require(NearlyEqual(conditionalResolved.desc.emissiveColor[0], 0.8F), "KBMAT-RUNTIME: If greater branch did not evaluate into Emissive");
}

void RunRendererUsesResolverDefaultFallbackForMissingMaterialTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_missing_material_fallback";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Missing material fallback test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    WriteTriangleObj(meshPath);

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Missing material fallback test could not register mesh loader");
    Require(manager.Mounts().Mount("Game", root), "Missing material fallback test could not mount asset root");
    Require(manager.DiscoverMountedAssets() == 1U, "Missing material fallback test did not discover mesh asset");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Missing material fallback test discovered wrong mesh metadata");

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Missing Material Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
        .materialAssetId = 404404U,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "Missing material fallback test renderer did not initialize");
    Require(renderer.BeginFrame(), "Missing material fallback test renderer did not begin frame");
    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
    };
    Require(renderer.SubmitScene(scene, desc), "Missing material fallback test renderer did not submit scene");

    const Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
    Require(runtimeStats.defaultMaterialFallbackCount == 1U, "Missing material fallback should increment runtime default material fallback stats");
    Require(runtimeStats.errorMaterialFallbackCount == 0U, "Missing material fallback should not increment runtime error material fallback stats");
    Require(runtimeStats.materialLoadedCount == 1U, "KBMAT-0901: Missing material fallback should count one material load");
    Require(runtimeStats.materialFallbackCount == 1U, "KBMAT-0901: Missing material fallback should count one material fallback");
    Require(runtimeStats.materialErrorCount == 0U, "KBMAT-0901: Missing material fallback should not count as material error");
    Require(runtimeStats.materialReloadCount == 0U, "KBMAT-0901: Missing material fallback should not count as material reload");
    Require(runtimeStats.materialResolverDiagnosticCount == 1U, "Missing material fallback should report one resolver diagnostic");
    Require(runtimeStats.cachedMaterialCount == 1U, "Missing material fallback should register a runtime material resource");
    Require(!renderer.LastSceneSubmitStats().HasMissingResources(), "Missing material fallback should keep submit resources valid");

    bool foundMissingMaterialDiagnostic = false;
    for (const SceneRenderDiagnosticEvent& event : renderer.LastSceneDiagnostics().events) {
        if (event.kind == SceneRenderDiagnosticKind::MissingMaterialAsset && event.materialAssetId == 404404U) {
            foundMissingMaterialDiagnostic = true;
        }
    }
    Require(foundMissingMaterialDiagnostic, "Missing material fallback should emit a render diagnostic");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunRendererSubmitsRuntimeMeshAssetInHeadlessNoopTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_runtime_submit";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Runtime submit test could not create temp root");
    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path albedoPath = root / "albedo.kbtex";
    const std::filesystem::path normalPath = root / "normal.kbtex";
    const std::filesystem::path metallicRoughnessPath = root / "metallic_roughness.kbtex";
    const std::filesystem::path occlusionPath = root / "occlusion.kbtex";
    const std::filesystem::path emissivePath = root / "emissive.kbtex";
    const std::filesystem::path materialPath = root / "paint.kbmat";
    const std::filesystem::path transparentMaterialPath = root / "glass.kbmat";
    WriteTriangleObj(meshPath);
    WriteTexture(albedoPath, 180U, 160U, 140U);
    WriteTexture(normalPath, 128U, 128U, 255U);
    WriteTexture(metallicRoughnessPath, 0U, 180U, 80U);
    WriteTexture(occlusionPath, 192U, 192U, 192U);
    WriteTexture(emissivePath, 16U, 32U, 64U);

    kb::scene::Scene scene;
    kb::scene::SceneLightingAccess::SetBasicLightingEnabled(scene, true);
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Runtime submit test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Runtime submit test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderTextureAssetLoader>()), "Runtime submit test could not register texture loader");
    Require(manager.Mounts().Mount("Game", root), "Runtime submit test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 5U, "Runtime submit test did not discover mesh and texture assets");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Runtime submit test discovered wrong mesh metadata");
    const kb::assets::AssetMetadata* albedoMetadata = manager.Registry().FindByPath("/Game/albedo.kbtex");
    const kb::assets::AssetMetadata* normalMetadata = manager.Registry().FindByPath("/Game/normal.kbtex");
    const kb::assets::AssetMetadata* metallicRoughnessMetadata = manager.Registry().FindByPath("/Game/metallic_roughness.kbtex");
    const kb::assets::AssetMetadata* occlusionMetadata = manager.Registry().FindByPath("/Game/occlusion.kbtex");
    const kb::assets::AssetMetadata* emissiveMetadata = manager.Registry().FindByPath("/Game/emissive.kbtex");
    Require(albedoMetadata != nullptr && normalMetadata != nullptr && metallicRoughnessMetadata != nullptr && occlusionMetadata != nullptr && emissiveMetadata != nullptr, "Runtime submit test did not discover texture metadata");
    WriteMaterial(materialPath, albedoMetadata->id.value, normalMetadata->id.value, metallicRoughnessMetadata->id.value, occlusionMetadata->id.value, emissiveMetadata->id.value);
    WriteMaterialWithTexturePaths(transparentMaterialPath, "BLEND", 0.5F, "missing_emissive.kbtex");
    Require(manager.DiscoverMountedAssets() >= 7U, "Runtime submit test did not discover material assets");
    meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Runtime submit test lost mesh metadata after material discovery");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/paint.kbmat");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Runtime submit test discovered wrong material metadata");
    const kb::assets::AssetMetadata* transparentMaterialMetadata = manager.Registry().FindByPath("/Game/glass.kbmat");
    Require(transparentMaterialMetadata != nullptr && transparentMaterialMetadata->type == "RenderMaterial", "Runtime submit test discovered wrong transparent material metadata");

    constexpr std::uint32_t instanceCount = 16U;
    for (std::uint32_t index = 0U; index < instanceCount; ++index) {
        const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
            .name = "Runtime Mesh",
            .transform = TransformAt(0.0F, 0.0F, 0.0F),
        });
        scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
            .meshAssetId = meshMetadata->id.value,
            .materialAssetId = materialMetadata->id.value,
        });
    }
    const kb::scene::SceneEntity transparentEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Transparent Runtime Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(transparentEntity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
        .materialAssetId = transparentMaterialMetadata->id.value,
    });
    const kb::scene::SceneEntity light = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Runtime Directional Light",
        .transform = TransformAt(0.0F, 10.0F, -10.0F),
    });
    scene.Components().Lights().Set(light, kb::scene::LightComponent{
        .kind = kb::scene::LightKind::Directional,
        .intensity = 1.0F,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 4U,
        .cachedMaterials = 4U,
        .cachedTextures = 8U,
        .frameReferencedMeshes = 4U,
        .frameReferencedMaterials = 4U,
        .frameReferencedTextures = 8U,
        .scenePassSubmitStats = 1U,
        .renderSceneMeshProxies = 32U,
        .renderSceneDrawGroupKeys = 8U,
        .meshResourceSlots = 4U,
        .materialResourceSlots = 4U,
        .textureResourceSlots = 8U,
        .meshBindings = 4U,
        .materialBindings = 4U,
        .textureBindings = 8U,
        .syncMeshProxies = 32U,
        .syncTransformCacheEntries = 32U,
        .syncTransformResolvingEntries = 32U,
    });
    Require(renderer.Initialize(surface, &config), "Renderer did not initialize in explicit headless Noop mode");
    Require(renderer.BeginFrame(), "Renderer did not begin headless runtime frame");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 8U,
            .maxVisibleInstances = 64U,
        },
    };
    Require(renderer.SubmitScene(scene, desc), "Renderer did not submit runtime mesh asset scene");

    const SceneRenderSubmitStats submitStats = renderer.LastSceneSubmitStats();
    Require(submitStats.visibleMeshCount == instanceCount * 2U, "Runtime submit did not keep shadow and opaque mesh instances visible while disabling blend");
    Require(submitStats.submittedMeshCount == instanceCount * 2U, "Runtime submit did not submit shadow and opaque mesh instances while disabling blend");
    Require(submitStats.submittedDrawCallCount == 2U, "Runtime submit should not draw disabled blend materials");
    Require(submitStats.shadowCasterCount == instanceCount, "Runtime submit did not count shadow casters");
    Require(submitStats.submittedShadowCasterCount == instanceCount, "Runtime submit did not submit shadow casters");
    Require(submitStats.submittedShadowDrawCallCount == 1U, "Runtime submit did not draw one shadow caster batch");
    Require(submitStats.shadowFilterSampleCount == 9U, "Runtime submit did not report PCF shadow filter sample count");
    Require(submitStats.shadowLightEntityId == light.Id(), "Runtime submit did not report selected shadow light entity");
    Require(submitStats.shadowMapAllocationBytes == 1024ULL * 1024ULL * 4ULL, "Runtime submit did not report shadow map allocation bytes");
    Require(submitStats.submittedEnvironmentLightingCount == 3U, "Runtime submit did not report environment lighting for every mesh pass");
    Require(submitStats.environmentLightingMode == static_cast<std::uint32_t>(SceneRenderEnvironmentMode::Constant) + 1U, "Runtime submit did not report default environment lighting mode");
    Require(submitStats.environmentLightingSampleCount == 1U, "Runtime submit did not report default environment sample count");
    Require(!submitStats.HasMissingResources(), "Runtime submit reported missing resources for discovered mesh asset");
    const std::span<const SceneRenderPassSubmitStats> passStats = renderer.LastScenePassSubmitStats();
    Require(passStats.size() == 3U, "Runtime submit did not report shadow, opaque, and transparent pass stats");
    Require(passStats[0].pass == MeshPassType::ShadowDepth && passStats[0].stats.submittedShadowCasterCount == instanceCount, "Runtime submit shadow pass stats are wrong");
    Require(passStats[0].stats.shadowFilterSampleCount == 9U, "Runtime submit shadow pass did not report PCF filter sample count");
    Require(passStats[0].stats.shadowLightEntityId == light.Id(), "Runtime submit shadow pass did not report selected shadow light entity");
    Require(passStats[0].stats.shadowMapAllocationBytes == 1024ULL * 1024ULL * 4ULL, "Runtime submit shadow pass did not report shadow map allocation bytes");
    Require(passStats[1].pass == MeshPassType::BaseOpaque && passStats[1].stats.submittedMeshCount == instanceCount, "Runtime submit opaque pass stats are wrong");
    Require(passStats[2].pass == MeshPassType::BaseTransparent && passStats[2].stats.submittedMeshCount == 0U, "Runtime submit transparent pass should not submit disabled blend materials");
    Require(renderer.LastSceneExposureStats().empty(), "Runtime submit without post-process unexpectedly reported exposure stats");

    const Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
    Require(runtimeStats.cachedMeshCount == 1U, "Runtime submit did not cache exactly one mesh resource");
    Require(runtimeStats.cachedMaterialCount == 2U, "Runtime submit did not cache opaque and transparent material resources");
    Require(runtimeStats.cachedTextureCount == 5U, "Runtime submit did not cache every referenced material texture");
    Require(runtimeStats.materialLoadedCount == 2U, "KBMAT-0901: Runtime submit should count loaded material resources");
    Require(runtimeStats.materialFallbackCount == 0U, "KBMAT-0901: Runtime submit should not count material fallbacks for valid materials");
    Require(runtimeStats.materialErrorCount == 0U, "KBMAT-0901: Runtime submit should not count material errors for valid materials");
    Require(runtimeStats.materialReloadCount == 0U, "KBMAT-0901: First runtime submit should not count material reloads");
    Require(runtimeStats.referencedMeshAssetCount == 1U, "Runtime submit did not reference exactly one mesh asset");
    Require(runtimeStats.referencedMaterialAssetCount == 2U, "Runtime submit did not reference opaque and transparent material assets");
    Require(runtimeStats.referencedTextureAssetCount == 5U, "Runtime submit did not reference every material texture asset");
    Require(runtimeStats.unresolvedMaterialTexturePathCount == 1U, "Runtime submit did not report unresolved material texture paths");
    Require(runtimeStats.shadowMapAllocated && runtimeStats.shadowMapSize == 1024U, "Runtime submit did not allocate the configured runtime shadow map");
    Require(runtimeStats.shadowMapAllocationBytes == 1024ULL * 1024ULL * 4ULL, "Runtime submit did not expose shadow map allocation bytes");
    Require(runtimeStats.defaultEnvironmentLightingMode == static_cast<std::uint32_t>(SceneRenderEnvironmentMode::Constant) + 1U, "Runtime submit did not expose default environment lighting mode");
    Require(runtimeStats.defaultEnvironmentLightingSampleCount == 1U, "Runtime submit did not expose default environment sample count");
    Require(runtimeStats.defaultShadowFilterSampleCount == 9U, "Runtime submit did not expose default shadow filter sample count");
    bool foundUnresolvedTexturePathDiagnostic = false;
    bool foundDisabledBlendDiagnostic = false;
    for (const SceneRenderDiagnosticEvent& event : renderer.LastSceneDiagnostics().events) {
        if (event.severity == SceneRenderDiagnosticSeverity::Warning &&
            event.kind == SceneRenderDiagnosticKind::UnresolvedMaterialTexturePath &&
            event.materialAssetId == transparentMaterialMetadata->id.value &&
            event.instanceCount == 1U) {
            foundUnresolvedTexturePathDiagnostic = true;
        }
        if (event.severity == SceneRenderDiagnosticSeverity::Warning &&
            event.kind == SceneRenderDiagnosticKind::UnsupportedMaterialAlphaBlend &&
            event.materialAssetId == transparentMaterialMetadata->id.value &&
            event.instanceCount == 1U) {
            foundDisabledBlendDiagnostic = true;
        }
    }
    Require(foundUnresolvedTexturePathDiagnostic, "Runtime submit did not emit unresolved material texture path diagnostic");
    Require(foundDisabledBlendDiagnostic, "Runtime submit did not emit disabled blend material diagnostic");
    Require(runtimeStats.renderSceneMeshProxyCount == instanceCount + 1U, "Runtime submit did not keep scene mesh proxies");
    Require(runtimeStats.meshResourceSlotCapacity >= 4U, "Runtime submit did not apply mesh resource slot reserve");
    Require(runtimeStats.materialResourceSlotCapacity >= 4U, "Runtime submit did not apply material resource slot reserve");
    Require(runtimeStats.textureResourceSlotCapacity >= 8U, "Runtime submit did not apply texture resource slot reserve");
    Require(runtimeStats.meshBindingCapacity >= 4U, "Runtime submit did not apply mesh binding reserve");
    Require(runtimeStats.materialBindingCapacity >= 4U, "Runtime submit did not apply material binding reserve");
    Require(runtimeStats.textureBindingCapacity >= 8U, "Runtime submit did not apply texture binding reserve");
    Require(runtimeStats.renderSceneMeshProxyCapacity >= 32U, "Runtime submit did not apply render scene mesh proxy reserve");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunRendererReloadsChangedRuntimeMaterialAssetTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_runtime_material_reload";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Runtime material reload test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path texturePath = root / "albedo.kbtex";
    const std::filesystem::path materialPath = root / "reloadable.kbmat";
    const std::filesystem::path stableMaterialPath = root / "stable.kbmat";
    WriteTriangleObj(meshPath);
    WriteTexture(texturePath, 180U, 160U, 140U);

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Runtime material reload test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Runtime material reload test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderTextureAssetLoader>()), "Runtime material reload test could not register texture loader");
    Require(manager.Mounts().Mount("Game", root), "Runtime material reload test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 2U, "Runtime material reload test did not discover mesh and texture assets");

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* textureMetadata = manager.Registry().FindByPath("/Game/albedo.kbtex");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Runtime material reload test discovered wrong mesh metadata");
    Require(textureMetadata != nullptr && textureMetadata->type == "RenderTexture", "Runtime material reload test discovered wrong texture metadata");
    const std::uint64_t meshAssetId = meshMetadata->id.value;
    const std::uint64_t textureAssetId = textureMetadata->id.value;

    WriteReloadableMaterial(materialPath, 0.2F, 0.7F, textureAssetId);
    WriteReloadableMaterial(stableMaterialPath, 0.4F, 0.8F, 0U);
    Require(manager.DiscoverMountedAssets() >= 4U, "Runtime material reload test did not discover material assets");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/reloadable.kbmat");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Runtime material reload test discovered wrong material metadata");
    const kb::assets::AssetMetadata* stableMaterialMetadata = manager.Registry().FindByPath("/Game/stable.kbmat");
    Require(stableMaterialMetadata != nullptr && stableMaterialMetadata->type == "RenderMaterial", "Runtime material reload test discovered wrong stable material metadata");
    const std::uint64_t materialAssetId = materialMetadata->id.value;
    const std::uint64_t stableMaterialAssetId = stableMaterialMetadata->id.value;
    const std::uint64_t firstContentHash = materialMetadata->contentHash;

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Reloadable Runtime Material Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshAssetId,
        .materialAssetId = materialAssetId,
    });
    const kb::scene::SceneEntity stableEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Stable Runtime Material Mesh",
        .transform = TransformAt(2.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(stableEntity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshAssetId,
        .materialAssetId = stableMaterialAssetId,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 2U,
        .cachedMaterials = 2U,
        .cachedTextures = 2U,
        .frameReferencedMeshes = 2U,
        .frameReferencedMaterials = 2U,
        .frameReferencedTextures = 2U,
        .scenePassSubmitStats = 1U,
        .renderSceneMeshProxies = 2U,
        .renderSceneDrawGroupKeys = 2U,
        .meshResourceSlots = 2U,
        .materialResourceSlots = 2U,
        .textureResourceSlots = 2U,
        .meshBindings = 2U,
        .materialBindings = 2U,
        .textureBindings = 2U,
        .syncMeshProxies = 2U,
        .syncTransformCacheEntries = 2U,
        .syncTransformResolvingEntries = 2U,
    });
    Require(renderer.Initialize(surface, &config), "Renderer did not initialize in runtime material reload test");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 4U,
            .maxVisibleInstances = 8U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
    };

    Require(renderer.BeginFrame(), "Renderer did not begin first material reload frame");
    Require(renderer.SubmitScene(scene, desc), "Renderer did not submit first material reload frame");
    const SceneRenderSubmitStats firstSubmitStats = renderer.LastSceneSubmitStats();
    Require(firstSubmitStats.meshDrawCommandCacheMissCount == 1U, "First material reload submit did not build one draw command cache entry");
    Require(firstSubmitStats.meshDrawCommandCacheBuildCount == 1U, "First material reload submit did not report one cache build");
    Require(firstSubmitStats.meshDrawCommandCacheHitCount == 0U, "First material reload submit unexpectedly hit draw command cache");

    const SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const RenderResourceRegistry* resources = renderer.SceneResources();
    Require(resourceMap != nullptr && resources != nullptr, "Runtime material reload test could not access scene resources");
    const RenderMaterialHandle firstHandle = resourceMap->ResolveMaterial(materialAssetId);
    const RenderMaterialResource* firstMaterial = resources->FindMaterial(firstHandle);
    Require(firstHandle.IsValid() && firstMaterial != nullptr, "Runtime material reload test did not bind initial material resource");
    const RenderMaterialHandle stableFirstHandle = resourceMap->ResolveMaterial(stableMaterialAssetId);
    const RenderMaterialResource* stableFirstMaterial = resources->FindMaterial(stableFirstHandle);
    Require(stableFirstHandle.IsValid() && stableFirstMaterial != nullptr, "Runtime material reload test did not bind stable material resource");
    const std::uint64_t firstVersion = firstMaterial->version;
    Require(firstVersion != 0U, "Initial material resource did not receive a version");
    Require(NearlyEqual(firstMaterial->baseColor[0], 0.2F), "Initial runtime material resource did not use first asset color");
    Require(NearlyEqual(firstMaterial->roughnessFactor, 0.7F), "Initial runtime material resource did not use first asset roughness");
    const RenderTextureHandle firstTextureHandle = resourceMap->ResolveTexture(textureAssetId, RenderTextureColorSpace::Srgb);
    const RenderTextureResource* firstTexture = resources->FindTexture(firstTextureHandle);
    Require(firstTextureHandle.IsValid() && firstTexture != nullptr, "Runtime material reload test did not bind initial albedo texture");
    const std::uint64_t firstTextureVersion = firstTexture->version;
    renderer.EndFrame();

    WriteTexture(texturePath, 24U, 96U, 220U);
    Require(manager.DiscoverMountedAssets() >= 3U, "Runtime material reload test did not rediscover changed texture asset");
    textureMetadata = manager.Registry().FindByPath("/Game/albedo.kbtex");
    Require(textureMetadata != nullptr && textureMetadata->type == "RenderTexture", "Runtime material reload test lost texture metadata after rediscovery");

    Require(renderer.BeginFrame(), "Renderer did not begin texture reload frame");
    Require(renderer.SubmitScene(scene, desc), "Renderer did not submit texture reload frame");
    const SceneRenderSubmitStats textureReloadStats = renderer.LastSceneSubmitStats();
    Require(textureReloadStats.meshDrawCommandCacheMissCount == 1U, "KBMAT-0902: Changed material texture should invalidate cached draw command");
    Require(textureReloadStats.meshDrawCommandCacheBuildCount == 1U, "KBMAT-0902: Changed material texture should rebuild cached draw command");
    Require(textureReloadStats.meshDrawCommandCachePruneCount == 1U, "KBMAT-0902: Changed material texture should prune stale draw command");
    const RenderMaterialHandle textureReloadMaterialHandle = resourceMap->ResolveMaterial(materialAssetId);
    Require(textureReloadMaterialHandle == firstHandle, "KBMAT-0902: Texture-only reload should not reload the material handle");
    Require(resourceMap->ResolveMaterial(stableMaterialAssetId) == stableFirstHandle, "KBMAT-0903: Texture-only reload should not change unrelated material handle");
    const RenderTextureHandle secondTextureHandle = resourceMap->ResolveTexture(textureAssetId, RenderTextureColorSpace::Srgb);
    const RenderTextureResource* secondTexture = resources->FindTexture(secondTextureHandle);
    Require(secondTextureHandle.IsValid() && secondTexture != nullptr, "KBMAT-0902: Texture-only reload did not bind a live texture");
    Require(secondTextureHandle != firstTextureHandle, "KBMAT-0902: Texture-only reload reused the stale texture handle");
    Require(resources->FindTexture(firstTextureHandle) == nullptr, "KBMAT-0902: Texture-only reload kept stale texture handle resolvable");
    Require(secondTexture->version != firstTextureVersion, "KBMAT-0902: Texture-only reload did not receive a new texture version");
    renderer.EndFrame();

    {
        std::ofstream output{ materialPath, std::ios::trunc };
        output
            << "version 1\n"
            << "materialType builtin.pbr\n"
            << "materialTypeVersion 1\n"
            << "roughnessFactor broken\n";
    }
    Require(renderer.BeginFrame(), "Renderer did not begin steady-state material cache frame");
    Require(renderer.SubmitScene(scene, desc), "Renderer did not submit steady-state material cache frame");
    const SceneRenderSubmitStats steadySubmitStats = renderer.LastSceneSubmitStats();
    Require(steadySubmitStats.meshDrawCommandCacheHitCount == 1U, "Unchanged material metadata should reuse the cached draw command");
    Require(steadySubmitStats.meshDrawCommandCacheMissCount == 0U, "Unchanged material metadata should not rebuild draw command cache");
    const Renderer::RuntimeSceneResourceStats steadyRuntimeStats = renderer.RuntimeResourceStats();
    Require(steadyRuntimeStats.materialLoadedCount == 0U, "KBMAT-0907: Runtime steady-state should not load material resources in render frame");
    Require(steadyRuntimeStats.materialReloadCount == 0U, "KBMAT-0907: Runtime steady-state should not reload material resources in render frame");
    Require(steadyRuntimeStats.materialFallbackCount == 0U, "KBMAT-0907: Runtime steady-state should not create material fallbacks in render frame");
    Require(steadyRuntimeStats.materialErrorCount == 0U, "KBMAT-0907: Runtime steady-state should not hit material errors in render frame");
    const RenderMaterialHandle steadyHandle = resourceMap->ResolveMaterial(materialAssetId);
    const RenderMaterialResource* steadyMaterial = resources->FindMaterial(steadyHandle);
    Require(steadyHandle.IsValid() && steadyMaterial != nullptr, "Unchanged material metadata should keep a valid cached material resource");
    Require(steadyHandle == firstHandle, "Unchanged material metadata should keep the cached material handle");
    Require(steadyMaterial == firstMaterial, "Unchanged material metadata should keep the cached material resource");
    Require(NearlyEqual(steadyMaterial->baseColor[0], 0.2F), "Runtime steady-state should not reparse material files without registry rediscovery");
    Require(NearlyEqual(steadyMaterial->roughnessFactor, 0.7F), "Runtime steady-state should not replace cached material with an undiscovered file edit");
    renderer.EndFrame();

    Require(manager.DiscoverMountedAssets() >= 3U, "Runtime material reload test did not rediscover broken material asset");
    materialMetadata = manager.Registry().FindByPath("/Game/reloadable.kbmat");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Runtime material reload test lost broken material metadata after rediscovery");
    Require(materialMetadata->id.value == materialAssetId, "Runtime material reload test changed material asset id after broken rediscovery");
    const std::uint64_t brokenContentHash = materialMetadata->contentHash;
    Require(brokenContentHash != firstContentHash, "Runtime material reload test did not update broken material content hash");

    Require(renderer.BeginFrame(), "Renderer did not begin broken material reload frame");
    Require(renderer.SubmitScene(scene, desc), "Renderer did not submit broken material reload frame");
    const RenderMaterialHandle brokenHandle = resourceMap->ResolveMaterial(materialAssetId);
    const RenderMaterialResource* brokenMaterial = resources->FindMaterial(brokenHandle);
    Require(brokenHandle == firstHandle, "KBMAT-0810: Broken material reload should keep the last-good material handle");
    Require(brokenMaterial == firstMaterial, "KBMAT-0810: Broken material reload should keep the last-good material resource");
    Require(NearlyEqual(brokenMaterial->baseColor[0], 0.2F), "KBMAT-0810: Broken material reload replaced last-good base color");
    Require(NearlyEqual(brokenMaterial->roughnessFactor, 0.7F), "KBMAT-0810: Broken material reload replaced last-good roughness");
    const Renderer::RuntimeSceneResourceStats brokenRuntimeStats = renderer.RuntimeResourceStats();
    Require(brokenRuntimeStats.materialResolverDiagnosticCount >= 1U, "KBMAT-0810: Broken material reload should report resolver diagnostics while keeping last-good");
    Require(brokenRuntimeStats.errorMaterialFallbackCount == 0U, "KBMAT-0810: Broken material reload should not render the error material while last-good exists");
    Require(brokenRuntimeStats.materialLoadedCount == 0U, "KBMAT-0901: Broken material reload with last-good should not count a new material load");
    Require(brokenRuntimeStats.materialFallbackCount == 0U, "KBMAT-0901: Broken material reload with last-good should not count a rendered fallback");
    Require(brokenRuntimeStats.materialErrorCount == 1U, "KBMAT-0901: Broken material reload should count one material error");
    Require(brokenRuntimeStats.materialReloadCount == 0U, "KBMAT-0901: Broken material reload with last-good should not count a successful reload");
    renderer.EndFrame();

    WriteReloadableMaterial(materialPath, 0.9F, 0.35F, textureAssetId);
    Require(manager.DiscoverMountedAssets() >= 3U, "Runtime material reload test did not rediscover changed material asset");
    materialMetadata = manager.Registry().FindByPath("/Game/reloadable.kbmat");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Runtime material reload test lost material metadata after rediscovery");
    Require(materialMetadata->id.value == materialAssetId, "Runtime material reload test changed material asset id after rediscovery");
    Require(materialMetadata->contentHash != firstContentHash, "Runtime material reload test did not update material content hash");
    Require(materialMetadata->contentHash != brokenContentHash, "Runtime material reload test did not update fixed material content hash");

    Require(renderer.BeginFrame(), "Renderer did not begin second material reload frame");
    Require(renderer.SubmitScene(scene, desc), "Renderer did not submit second material reload frame");
    const SceneRenderSubmitStats secondSubmitStats = renderer.LastSceneSubmitStats();
    Require(secondSubmitStats.meshDrawCommandCacheMissCount == 1U, "Changed material did not invalidate draw command cache");
    Require(secondSubmitStats.meshDrawCommandCacheBuildCount == 1U, "Changed material did not rebuild draw command cache");
    Require(secondSubmitStats.meshDrawCommandCachePruneCount == 1U, "Changed material did not prune the stale draw command cache entry");

    const RenderMaterialHandle secondHandle = resourceMap->ResolveMaterial(materialAssetId);
    const RenderMaterialResource* secondMaterial = resources->FindMaterial(secondHandle);
    Require(secondHandle.IsValid() && secondMaterial != nullptr, "Runtime material reload test did not bind reloaded material resource");
    Require(secondHandle != firstHandle, "Runtime material reload test reused stale material handle after content change");
    Require(resourceMap->ResolveMaterial(stableMaterialAssetId) == stableFirstHandle, "KBMAT-0903: Changed material should not reload unrelated material handle");
    Require(resources->FindMaterial(firstHandle) == nullptr, "Runtime material reload test kept stale material handle resolvable");
    Require(secondMaterial->version != firstVersion, "Reloaded material resource did not receive a new version");
    Require(NearlyEqual(secondMaterial->baseColor[0], 0.9F), "Reloaded runtime material resource did not use changed asset color");
    Require(NearlyEqual(secondMaterial->roughnessFactor, 0.35F), "Reloaded runtime material resource did not use changed asset roughness");
    const Renderer::RuntimeSceneResourceStats reloadedRuntimeStats = renderer.RuntimeResourceStats();
    Require(reloadedRuntimeStats.cachedMaterialCount == 2U, "Runtime material reload test should keep exactly the changed and stable material resources after reload");
    Require(reloadedRuntimeStats.materialLoadedCount == 1U, "KBMAT-0901: Fixed material reload should count one loaded material");
    Require(reloadedRuntimeStats.materialFallbackCount == 0U, "KBMAT-0901: Fixed material reload should not count a fallback");
    Require(reloadedRuntimeStats.materialErrorCount == 0U, "KBMAT-0901: Fixed material reload should not count an error");
    Require(reloadedRuntimeStats.materialReloadCount == 1U, "KBMAT-0901: Fixed material reload should count one material reload");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunGraphBackedMaterialArtifactDependencyReloadInvalidatesOnlyTouchedBindingTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_graph_material_artifact_reload";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Graph material artifact reload test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path typePath = root / "BuiltinPbr.kbmaterialtype";
    const std::filesystem::path artifactPath = root / "GraphArtifact.kbmaterialgraph";
    const std::filesystem::path materialPath = root / "graph_backed.kbmat";
    const std::filesystem::path stableMaterialPath = root / "stable.kbmat";
    WriteTriangleObj(meshPath);
    Require(RenderMaterialTypeAssetLoader::SaveType(typePath, GetBuiltInPbrMaterialTypeDocument()), "KBMAT-GRAPH-0405: could not write material type fixture");
    RenderMaterialGraphDocument artifact = MakeDefaultRenderMaterialGraphDocument();
    Require(RenderMaterialGraphAssetLoader::SaveGraph(artifactPath, artifact), "KBMAT-GRAPH-0405: could not write graph artifact fixture");

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Graph material artifact reload test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Graph material artifact reload test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialGraphAssetLoader>()), "Graph material artifact reload test could not register material graph loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialTypeAssetLoader>()), "Graph material artifact reload test could not register material type loader");
    Require(manager.Mounts().Mount("Game", root), "Graph material artifact reload test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 3U, "Graph material artifact reload test did not discover mesh/type/artifact assets");

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* typeMetadata = manager.Registry().FindByPath("/Game/BuiltinPbr.kbmaterialtype");
    const kb::assets::AssetMetadata* artifactMetadata = manager.Registry().FindByPath("/Game/GraphArtifact.kbmaterialgraph");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Graph material artifact reload test discovered wrong mesh metadata");
    Require(typeMetadata != nullptr && typeMetadata->type == kRenderMaterialTypeAssetType, "Graph material artifact reload test discovered wrong material type metadata");
    Require(artifactMetadata != nullptr && artifactMetadata->type == kRenderMaterialGraphAssetType, "Graph material artifact reload test discovered wrong artifact metadata");
    const std::uint64_t meshAssetId = meshMetadata->id.value;
    const kb::assets::AssetId artifactAssetId = artifactMetadata->id;
    const std::uint64_t firstArtifactContentHash = artifactMetadata->contentHash;

    WriteGraphBackedReloadableMaterial(
        materialPath,
        0.25F,
        0.55F,
        typeMetadata->id.value,
        "/Game/BuiltinPbr.kbmaterialtype",
        artifactAssetId.value,
        firstArtifactContentHash);
    WriteReloadableMaterial(stableMaterialPath, 0.75F, 0.3F, 0U);
    Require(manager.DiscoverMountedAssets() >= 5U, "Graph material artifact reload test did not discover material assets");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/graph_backed.kbmat");
    const kb::assets::AssetMetadata* stableMaterialMetadata = manager.Registry().FindByPath("/Game/stable.kbmat");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Graph material artifact reload test lost graph material metadata");
    Require(stableMaterialMetadata != nullptr && stableMaterialMetadata->type == "RenderMaterial", "Graph material artifact reload test lost stable material metadata");
    const std::uint64_t materialAssetId = materialMetadata->id.value;
    const std::uint64_t stableMaterialAssetId = stableMaterialMetadata->id.value;
    Require(ContainsAssetDependency(materialMetadata->dependencies, artifactAssetId),
        "KBMAT-GRAPH-0405: graph-backed material metadata did not depend on its compile artifact");

    const kb::scene::SceneEntity graphEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Graph Artifact Material Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(graphEntity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshAssetId,
        .materialAssetId = materialAssetId,
    });
    const kb::scene::SceneEntity stableEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Stable Material Mesh",
        .transform = TransformAt(0.25F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(stableEntity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshAssetId,
        .materialAssetId = stableMaterialAssetId,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 1U,
        .cachedMaterials = 2U,
        .frameReferencedMeshes = 2U,
        .frameReferencedMaterials = 2U,
        .scenePassSubmitStats = 1U,
        .renderSceneMeshProxies = 2U,
        .renderSceneDrawGroupKeys = 2U,
        .meshResourceSlots = 1U,
        .materialResourceSlots = 2U,
        .meshBindings = 1U,
        .materialBindings = 2U,
        .syncMeshProxies = 2U,
        .syncTransformCacheEntries = 2U,
        .syncTransformResolvingEntries = 2U,
    });
    Require(renderer.Initialize(surface, &config), "Graph material artifact reload test renderer did not initialize");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 4U,
            .maxVisibleInstances = 8U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
    };

    Require(renderer.BeginFrame(), "Graph material artifact reload test did not begin first frame");
    Require(renderer.SubmitScene(scene, desc), "Graph material artifact reload test did not submit first frame");
    const SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const RenderResourceRegistry* resources = renderer.SceneResources();
    Require(resourceMap != nullptr && resources != nullptr, "Graph material artifact reload test could not inspect resource map");
    const RenderMaterialHandle firstGraphHandle = resourceMap->ResolveMaterial(materialAssetId);
    const RenderMaterialHandle firstStableHandle = resourceMap->ResolveMaterial(stableMaterialAssetId);
    const RenderMaterialResource* firstGraphMaterial = resources->FindMaterial(firstGraphHandle);
    Require(firstGraphHandle.IsValid() && firstStableHandle.IsValid() && firstGraphMaterial != nullptr,
        "KBMAT-GRAPH-0405: first submit did not bind graph/stable material resources");
    Require(NearlyEqual(firstGraphMaterial->baseColor[0], 0.25F), "KBMAT-GRAPH-0405: graph material fixture did not resolve initial base color");
    Require(NearlyEqual(firstGraphMaterial->roughnessFactor, 0.55F), "KBMAT-GRAPH-0405: graph material fixture did not resolve initial roughness parameter");
    renderer.EndFrame();

    artifact.nodes.push_back(RenderMaterialGraphNode{
        .id = 77U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 240,
        .positionY = 80,
    });
    Require(RenderMaterialGraphAssetLoader::SaveGraph(artifactPath, artifact), "KBMAT-GRAPH-0405: could not update graph artifact fixture");
    Require(manager.DiscoverMountedAssets() >= 5U, "Graph material artifact reload test did not rediscover changed artifact");
    artifactMetadata = manager.Registry().FindByPath("/Game/GraphArtifact.kbmaterialgraph");
    Require(artifactMetadata != nullptr && artifactMetadata->contentHash != 0U, "KBMAT-GRAPH-0405: changed artifact metadata was not current");
    Require(artifactMetadata->contentHash != firstArtifactContentHash, "KBMAT-GRAPH-0405: changed artifact did not update its metadata content hash");

    Require(renderer.BeginFrame(), "Graph material artifact reload test did not begin artifact reload frame");
    Require(renderer.SubmitScene(scene, desc), "Graph material artifact reload test did not submit artifact reload frame");
    const RenderMaterialHandle secondGraphHandle = resourceMap->ResolveMaterial(materialAssetId);
    const RenderMaterialHandle secondStableHandle = resourceMap->ResolveMaterial(stableMaterialAssetId);
    const RenderMaterialResource* secondGraphMaterial = resources->FindMaterial(secondGraphHandle);
    Require(secondGraphHandle.IsValid() && secondGraphMaterial != nullptr, "KBMAT-GRAPH-0405: graph material was not rebound after artifact change");
    Require(secondGraphHandle != firstGraphHandle, "KBMAT-GRAPH-0405: graph artifact change did not reload the graph-backed material binding");
    Require(secondStableHandle == firstStableHandle, "KBMAT-GRAPH-0405: graph artifact change reloaded an unrelated stable material binding");
    Require(resources->FindMaterial(firstGraphHandle) == nullptr, "KBMAT-GRAPH-0405: stale graph material handle remained live after artifact change");
    Require(NearlyEqual(secondGraphMaterial->baseColor[0], 0.25F), "KBMAT-GRAPH-0405: graph artifact reload changed material parameters unexpectedly");
    Require(NearlyEqual(secondGraphMaterial->roughnessFactor, 0.55F), "KBMAT-GRAPH-0405: graph artifact reload lost material scalar parameters");
    const Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
    Require(runtimeStats.materialReloadCount == 1U && runtimeStats.cachedMaterialCount == 2U,
        "KBMAT-GRAPH-0405: artifact change should count one material reload and keep only graph/stable resources cached");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunCookedGraphBackedMaterialRuntimeDoesNotCompileGraphTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_graph_material_no_runtime_compile";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Cooked graph material no-compile test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path typePath = root / "BuiltinPbr.kbmaterialtype";
    const std::filesystem::path artifactPath = root / "GraphArtifact.kbmaterialgraph";
    const std::filesystem::path materialPath = root / "cooked_graph.kbmat";
    WriteTriangleObj(meshPath);
    Require(RenderMaterialTypeAssetLoader::SaveType(typePath, GetBuiltInPbrMaterialTypeDocument()), "KBMAT-GRAPH-0505: could not write material type fixture");
    RenderMaterialGraphDocument artifact = MakeDefaultRenderMaterialGraphDocument();
    Require(RenderMaterialGraphAssetLoader::SaveGraph(artifactPath, artifact), "KBMAT-GRAPH-0505: could not write graph artifact fixture");

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Cooked graph material no-compile test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Cooked graph material no-compile test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialGraphAssetLoader>()), "Cooked graph material no-compile test could not register graph loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialTypeAssetLoader>()), "Cooked graph material no-compile test could not register type loader");
    Require(manager.Mounts().Mount("Game", root), "Cooked graph material no-compile test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 3U, "Cooked graph material no-compile test did not discover mesh/type/artifact assets");

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* typeMetadata = manager.Registry().FindByPath("/Game/BuiltinPbr.kbmaterialtype");
    const kb::assets::AssetMetadata* artifactMetadata = manager.Registry().FindByPath("/Game/GraphArtifact.kbmaterialgraph");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Cooked graph material no-compile test discovered wrong mesh metadata");
    Require(typeMetadata != nullptr && typeMetadata->type == kRenderMaterialTypeAssetType, "Cooked graph material no-compile test discovered wrong material type metadata");
    Require(artifactMetadata != nullptr && artifactMetadata->type == kRenderMaterialGraphAssetType, "Cooked graph material no-compile test discovered wrong artifact metadata");
    const std::uint64_t meshAssetId = meshMetadata->id.value;
    const std::uint64_t typeAssetId = typeMetadata->id.value;
    const kb::assets::AssetId artifactAssetId = artifactMetadata->id;
    const std::uint64_t artifactContentHash = artifactMetadata->contentHash;

    WriteGraphBackedReloadableMaterial(
        materialPath,
        0.31F,
        0.47F,
        typeAssetId,
        "/Game/BuiltinPbr.kbmaterialtype",
        artifactAssetId.value,
        artifactContentHash);
    Require(manager.DiscoverMountedAssets() >= 4U, "Cooked graph material no-compile test did not discover cooked material");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/cooked_graph.kbmat");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Cooked graph material no-compile test lost material metadata");
    Require(ContainsAssetDependency(materialMetadata->dependencies, artifactAssetId),
        "KBMAT-GRAPH-0505: cooked graph material should depend on its last-good artifact");

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Cooked Graph Material Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshAssetId,
        .materialAssetId = materialMetadata->id.value,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 1U,
        .cachedMaterials = 1U,
        .frameReferencedMeshes = 1U,
        .frameReferencedMaterials = 1U,
        .scenePassSubmitStats = 1U,
        .renderSceneMeshProxies = 1U,
        .renderSceneDrawGroupKeys = 1U,
        .meshResourceSlots = 1U,
        .materialResourceSlots = 1U,
        .meshBindings = 1U,
        .materialBindings = 1U,
        .syncMeshProxies = 1U,
        .syncTransformCacheEntries = 1U,
        .syncTransformResolvingEntries = 1U,
    });
    Require(renderer.Initialize(surface, &config), "Cooked graph material no-compile test renderer did not initialize");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 4U,
            .maxVisibleInstances = 8U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
    };

    const std::uint64_t compileCountBeforeRuntime = RenderMaterialGraphCompileInvocationCount();
    Require(renderer.BeginFrame(), "Cooked graph material no-compile test did not begin first frame");
    Require(renderer.SubmitScene(scene, desc), "Cooked graph material no-compile test did not submit first frame");
    Require(RenderMaterialGraphCompileInvocationCount() == compileCountBeforeRuntime,
        "KBMAT-GRAPH-0505: runtime initial submit compiled a cooked graph-backed material graph");
    const SceneRenderSubmitStats firstSubmitStats = renderer.LastSceneSubmitStats();
    if (firstSubmitStats.submittedMeshCount != 1U || firstSubmitStats.submittedDrawCallCount != 1U || firstSubmitStats.HasMissingResources()) {
        const Renderer::RuntimeSceneResourceStats firstRuntimeStats = renderer.RuntimeResourceStats();
        std::cerr
            << "KBMAT-GRAPH-0505 first frame stats: visible=" << firstSubmitStats.visibleMeshCount
            << " submittedMesh=" << firstSubmitStats.submittedMeshCount
            << " drawCalls=" << firstSubmitStats.submittedDrawCallCount
            << " missingMeshBinding=" << firstSubmitStats.missingMeshBindingCount
            << " missingMeshResource=" << firstSubmitStats.missingMeshResourceCount
            << " missingMaterialBinding=" << firstSubmitStats.missingMaterialBindingCount
            << " missingMaterialResource=" << firstSubmitStats.missingMaterialResourceCount
            << " materialLoaded=" << firstRuntimeStats.materialLoadedCount
            << " materialErrors=" << firstRuntimeStats.materialErrorCount
            << " materialDiagnostics=" << firstRuntimeStats.materialResolverDiagnosticCount << '\n';
    }
    Require(firstSubmitStats.submittedMeshCount == 1U && firstSubmitStats.submittedDrawCallCount == 1U && !firstSubmitStats.HasMissingResources(),
        "KBMAT-GRAPH-0505: first cooked graph frame should submit one complete draw call");
    const SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const RenderResourceRegistry* resources = renderer.SceneResources();
    Require(resourceMap != nullptr && resources != nullptr, "KBMAT-GRAPH-0505: first cooked graph frame could not inspect resources");
    const RenderMaterialHandle firstHandle = resourceMap->ResolveMaterial(materialMetadata->id.value);
    const RenderMaterialResource* firstMaterial = resources->FindMaterial(firstHandle);
    Require(firstHandle.IsValid() && firstMaterial != nullptr && NearlyEqual(firstMaterial->baseColor[0], 0.31F),
        "KBMAT-GRAPH-0505: first cooked graph frame did not bind the authored graph-backed material");
    Require(NearlyEqual(firstMaterial->roughnessFactor, 0.47F),
        "KBMAT-GRAPH-0505: first cooked graph frame did not bind the authored graph scalar parameter");
    renderer.EndFrame();

    Require(renderer.BeginFrame(), "Cooked graph material no-compile test did not begin steady frame");
    Require(renderer.SubmitScene(scene, desc), "Cooked graph material no-compile test did not submit steady frame");
    Require(RenderMaterialGraphCompileInvocationCount() == compileCountBeforeRuntime,
        "KBMAT-GRAPH-0505: runtime steady frame compiled a cooked graph-backed material graph");
    const SceneRenderSubmitStats steadySubmitStats = renderer.LastSceneSubmitStats();
    const Renderer::RuntimeSceneResourceStats steadyRuntimeStats = renderer.RuntimeResourceStats();
    Require(steadySubmitStats.meshDrawCommandCacheHitCount == 1U &&
            steadySubmitStats.meshDrawCommandCacheMissCount == 0U &&
            steadySubmitStats.meshDrawCommandCacheBuildCount == 0U &&
            steadySubmitStats.meshDrawCommandCachePruneCount == 0U,
        "KBMAT-GRAPH-0505: cooked graph steady frame should reuse cached draw command without pipeline rebuild");
    Require(steadyRuntimeStats.materialLoadedCount == 0U &&
            steadyRuntimeStats.materialReloadCount == 0U &&
            steadyRuntimeStats.materialErrorCount == 0U,
        "KBMAT-GRAPH-0505: cooked graph steady frame should not reload or error material resources");
    renderer.EndFrame();

    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunInvalidGraphMaterialUsesLastGoodThenRefreshesAfterFixTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_graph_material_invalid_fix";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Invalid graph material reload test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path materialPath = root / "graph_invalidatable.kbmat";
    WriteTriangleObj(meshPath);
    WriteGraphValidationMaterial(materialPath, 0.2F, true);

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Invalid graph material reload test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Invalid graph material reload test could not register material loader");
    Require(manager.Mounts().Mount("Game", root), "Invalid graph material reload test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 2U, "Invalid graph material reload test did not discover mesh/material assets");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/graph_invalidatable.kbmat");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Invalid graph material reload test discovered wrong mesh metadata");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Invalid graph material reload test discovered wrong material metadata");
    const std::uint64_t meshAssetId = meshMetadata->id.value;
    const std::uint64_t materialAssetId = materialMetadata->id.value;

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Invalid Graph Material Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshAssetId,
        .materialAssetId = materialAssetId,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 1U,
        .cachedMaterials = 1U,
        .frameReferencedMeshes = 1U,
        .frameReferencedMaterials = 1U,
        .scenePassSubmitStats = 1U,
        .renderSceneMeshProxies = 1U,
        .renderSceneDrawGroupKeys = 1U,
        .meshResourceSlots = 1U,
        .materialResourceSlots = 1U,
        .meshBindings = 1U,
        .materialBindings = 1U,
        .syncMeshProxies = 1U,
        .syncTransformCacheEntries = 2U,
        .syncTransformResolvingEntries = 2U,
    });
    Require(renderer.Initialize(surface, &config), "Invalid graph material reload test renderer did not initialize");
    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 4U,
            .maxVisibleInstances = 8U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
    };

    Require(renderer.BeginFrame(), "Invalid graph material reload test did not begin first frame");
    Require(renderer.SubmitScene(scene, desc), "Invalid graph material reload test did not submit first frame");
    const SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const RenderResourceRegistry* resources = renderer.SceneResources();
    Require(resourceMap != nullptr && resources != nullptr, "Invalid graph material reload test could not inspect resources");
    const RenderMaterialHandle firstHandle = resourceMap->ResolveMaterial(materialAssetId);
    const RenderMaterialResource* firstMaterial = resources->FindMaterial(firstHandle);
    Require(firstHandle.IsValid() && firstMaterial != nullptr, "KBMAT-GRAPH-0502: valid graph material did not bind before invalid edit");
    Require(NearlyEqual(firstMaterial->baseColor[0], 0.2F), "KBMAT-GRAPH-0502: valid graph material did not preserve initial base color");
    renderer.EndFrame();

    WriteGraphValidationMaterial(materialPath, 0.9F, false);
    Require(manager.DiscoverMountedAssets() >= 2U, "Invalid graph material reload test did not rediscover invalid material");
    Require(renderer.BeginFrame(), "Invalid graph material reload test did not begin invalid frame");
    Require(renderer.SubmitScene(scene, desc), "Invalid graph material reload test did not submit invalid frame");
    const RenderMaterialHandle invalidHandle = resourceMap->ResolveMaterial(materialAssetId);
    const RenderMaterialResource* invalidMaterial = resources->FindMaterial(invalidHandle);
    Require(invalidHandle == firstHandle && invalidMaterial == firstMaterial,
        "KBMAT-GRAPH-0502: invalid graph should keep last-good material handle when one exists");
    const Renderer::RuntimeSceneResourceStats invalidStats = renderer.RuntimeResourceStats();
    Require(invalidStats.materialErrorCount == 1U && invalidStats.materialResolverDiagnosticCount >= 1U,
        "KBMAT-GRAPH-0502: invalid graph material should report runtime diagnostics");
    bool foundGraphDiagnostic = false;
    for (const SceneRenderDiagnosticEvent& event : renderer.LastSceneDiagnostics().events) {
        if (event.kind == SceneRenderDiagnosticKind::InvalidMaterialAsset && event.materialAssetId == materialAssetId) {
            foundGraphDiagnostic = true;
        }
    }
    Require(foundGraphDiagnostic, "KBMAT-GRAPH-0502: invalid graph material diagnostic was not visible in scene diagnostics");
    renderer.EndFrame();

    WriteGraphValidationMaterial(materialPath, 0.65F, true);
    Require(manager.DiscoverMountedAssets() >= 2U, "Invalid graph material reload test did not rediscover fixed material");
    Require(renderer.BeginFrame(), "Invalid graph material reload test did not begin fixed frame");
    Require(renderer.SubmitScene(scene, desc), "Invalid graph material reload test did not submit fixed frame");
    const RenderMaterialHandle fixedHandle = resourceMap->ResolveMaterial(materialAssetId);
    const RenderMaterialResource* fixedMaterial = resources->FindMaterial(fixedHandle);
    Require(fixedHandle.IsValid() && fixedMaterial != nullptr && fixedHandle != firstHandle,
        "KBMAT-GRAPH-0502: fixed graph material did not refresh runtime material binding");
    Require(NearlyEqual(fixedMaterial->baseColor[0], 0.65F), "KBMAT-GRAPH-0502: fixed graph material did not load repaired material data");
    const Renderer::RuntimeSceneResourceStats fixedStats = renderer.RuntimeResourceStats();
    Require(fixedStats.materialReloadCount == 1U && fixedStats.materialErrorCount == 0U,
        "KBMAT-GRAPH-0502: fixed graph material should reload cleanly without material errors");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunRendererSubmitsMaterialInstanceAssetInHeadlessNoopTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_runtime_material_instance";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Runtime material instance test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path materialPath = root / "parent.kbmat";
    const std::filesystem::path instancePath = root / "parent_instance.kbmatinst";
    WriteTriangleObj(meshPath);

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Runtime material instance test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Runtime material instance test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialInstanceAssetLoader>()), "Runtime material instance test could not register material instance loader");
    Require(manager.Mounts().Mount("Game", root), "Runtime material instance test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 1U, "Runtime material instance test did not discover mesh asset");

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Runtime material instance test discovered wrong mesh metadata");
    WriteMaterial(materialPath, 0U, 0U, 0U, 0U, 0U);
    Require(manager.DiscoverMountedAssets() >= 2U, "Runtime material instance test did not discover parent material");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/parent.kbmat");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Runtime material instance test discovered wrong parent material metadata");
    const kb::assets::AssetId parentMaterialId = materialMetadata->id;
    RenderMaterialInstanceAssetData instance{};
    instance.parentMaterialAssetId = parentMaterialId;
    instance.hasOverrides = true;
    instance.overrides.materialType = kRenderMaterialAssetBuiltInPbrType;
    instance.overrides.materialTypeVersion = kRenderMaterialAssetBuiltInPbrTypeVersion;
    instance.overrides.hasExplicitMaterialType = true;
    instance.overrides.hasExplicitMaterialTypeVersion = true;
    instance.overrides.desc.baseColor[0] = 0.25F;
    instance.overrides.desc.baseColor[1] = 0.5F;
    instance.overrides.desc.baseColor[2] = 0.75F;
    instance.overrides.desc.baseColor[3] = 1.0F;
    instance.overrides.desc.roughnessFactor = 0.25F;
    Require(RenderMaterialInstanceAssetWriter::Save(instancePath, instance), "Runtime material instance fixture with override could not be written");
    Require(manager.DiscoverMountedAssets() >= 3U, "Runtime material instance test did not discover material instance");
    meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* instanceMetadata = manager.Registry().FindByPath("/Game/parent_instance.kbmatinst");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Runtime material instance test lost mesh metadata");
    Require(instanceMetadata != nullptr && instanceMetadata->type == "RenderMaterialInstance", "Runtime material instance test discovered wrong material instance metadata");

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Runtime Material Instance Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
        .materialAssetId = instanceMetadata->id.value,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 1U,
        .cachedMaterials = 1U,
        .frameReferencedMeshes = 1U,
        .frameReferencedMaterials = 1U,
        .scenePassSubmitStats = 1U,
        .renderSceneMeshProxies = 1U,
        .renderSceneDrawGroupKeys = 1U,
        .meshResourceSlots = 1U,
        .materialResourceSlots = 1U,
        .meshBindings = 1U,
        .materialBindings = 1U,
        .syncMeshProxies = 1U,
        .syncTransformCacheEntries = 1U,
        .syncTransformResolvingEntries = 1U,
    });
    Require(renderer.Initialize(surface, &config), "Renderer did not initialize in runtime material instance test");
    Require(renderer.BeginFrame(), "Renderer did not begin runtime material instance frame");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 4U,
            .maxVisibleInstances = 8U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
    };
    Require(renderer.SubmitScene(scene, desc), "Renderer did not submit runtime material instance scene");
    const SceneRenderSubmitStats submitStats = renderer.LastSceneSubmitStats();
    Require(!submitStats.HasMissingResources(), "Runtime material instance submit reported missing resources");
    Require(submitStats.submittedMeshCount == 1U, "Runtime material instance submit did not submit one mesh");

    const SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const RenderResourceRegistry* resources = renderer.SceneResources();
    Require(resourceMap != nullptr && resources != nullptr, "Runtime material instance test could not access scene resources");
    const RenderMaterialHandle instanceHandle = resourceMap->ResolveMaterial(instanceMetadata->id.value);
    const RenderMaterialResource* materialResource = resources->FindMaterial(instanceHandle);
    Require(instanceHandle.IsValid() && materialResource != nullptr, "Runtime material instance did not bind a material resource under the instance asset id");
    Require(NearlyEqual(materialResource->baseColor[0], 0.25F), "Runtime material instance did not apply override base color");
    Require(NearlyEqual(materialResource->baseColor[1], 0.5F), "Runtime material instance did not apply override base color green channel");
    Require(NearlyEqual(materialResource->roughnessFactor, 0.25F), "Runtime material instance did not apply override roughness");

    const Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
    Require(runtimeStats.cachedMaterialCount == 1U, "Runtime material instance test should cache one resolved material resource");
    Require(runtimeStats.referencedMaterialAssetCount == 1U, "Runtime material instance test should reference the assigned instance material asset");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunRendererMaterialInstanceInheritsGraphBackedParentParametersTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_runtime_graph_material_instance";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Graph material instance inheritance test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path typePath = root / "BuiltinPbr.kbmaterialtype";
    const std::filesystem::path artifactPath = root / "GraphArtifact.kbmaterialgraph";
    const std::filesystem::path parentPath = root / "graph_parent.kbmat";
    const std::filesystem::path instancePath = root / "graph_parent_instance.kbmatinst";
    WriteTriangleObj(meshPath);
    Require(RenderMaterialTypeAssetLoader::SaveType(typePath, GetBuiltInPbrMaterialTypeDocument()), "Graph material instance inheritance test could not write material type fixture");
    RenderMaterialGraphDocument artifact = MakeDefaultRenderMaterialGraphDocument();
    Require(RenderMaterialGraphAssetLoader::SaveGraph(artifactPath, artifact), "Graph material instance inheritance test could not write graph artifact fixture");

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Graph material instance inheritance test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Graph material instance inheritance test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialInstanceAssetLoader>()), "Graph material instance inheritance test could not register material instance loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialGraphAssetLoader>()), "Graph material instance inheritance test could not register graph loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialTypeAssetLoader>()), "Graph material instance inheritance test could not register material type loader");
    Require(manager.Mounts().Mount("Game", root), "Graph material instance inheritance test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 3U, "Graph material instance inheritance test did not discover mesh/type/artifact assets");

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* typeMetadata = manager.Registry().FindByPath("/Game/BuiltinPbr.kbmaterialtype");
    const kb::assets::AssetMetadata* artifactMetadata = manager.Registry().FindByPath("/Game/GraphArtifact.kbmaterialgraph");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Graph material instance inheritance test discovered wrong mesh metadata");
    Require(typeMetadata != nullptr && typeMetadata->type == kRenderMaterialTypeAssetType, "Graph material instance inheritance test discovered wrong material type metadata");
    Require(artifactMetadata != nullptr && artifactMetadata->type == kRenderMaterialGraphAssetType, "Graph material instance inheritance test discovered wrong graph artifact metadata");

    WriteGraphBackedReloadableMaterial(
        parentPath,
        0.41F,
        0.68F,
        typeMetadata->id.value,
        "/Game/BuiltinPbr.kbmaterialtype",
        artifactMetadata->id.value,
        artifactMetadata->contentHash);
    Require(manager.DiscoverMountedAssets() >= 4U, "Graph material instance inheritance test did not discover graph parent material");
    const kb::assets::AssetMetadata* parentMetadata = manager.Registry().FindByPath("/Game/graph_parent.kbmat");
    Require(parentMetadata != nullptr && parentMetadata->type == "RenderMaterial", "Graph material instance inheritance test discovered wrong parent material metadata");

    RenderMaterialInstanceAssetData instance{};
    instance.parentMaterialAssetId = parentMetadata->id;
    instance.hasOverrides = true;
    instance.overrides.materialType = kRenderMaterialAssetBuiltInPbrType;
    instance.overrides.materialTypeVersion = kRenderMaterialAssetBuiltInPbrTypeVersion;
    instance.overrides.hasExplicitMaterialType = true;
    instance.overrides.hasExplicitMaterialTypeVersion = true;
    instance.overrides.desc.baseColor[0] = 0.02F;
    instance.overrides.desc.baseColor[1] = 0.03F;
    instance.overrides.desc.baseColor[2] = 0.04F;
    instance.overrides.desc.roughnessFactor = 0.97F;
    RenderMaterialGraphParameterValue roughnessOverride{};
    roughnessOverride.stableId = "roughnessFactor";
    roughnessOverride.type = RenderMaterialParameterType::Scalar;
    roughnessOverride.numbers[0] = 0.22F;
    instance.overrides.graphParameterValues.push_back(roughnessOverride);
    Require(RenderMaterialInstanceAssetWriter::Save(instancePath, instance), "Graph material instance inheritance test could not write material instance fixture");
    Require(manager.DiscoverMountedAssets() >= 5U, "Graph material instance inheritance test did not discover material instance");

    meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* instanceMetadata = manager.Registry().FindByPath("/Game/graph_parent_instance.kbmatinst");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Graph material instance inheritance test lost mesh metadata");
    Require(instanceMetadata != nullptr && instanceMetadata->type == "RenderMaterialInstance", "Graph material instance inheritance test discovered wrong material instance metadata");

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Graph Material Instance Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
        .materialAssetId = instanceMetadata->id.value,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 1U,
        .cachedMaterials = 1U,
        .frameReferencedMeshes = 1U,
        .frameReferencedMaterials = 1U,
        .scenePassSubmitStats = 1U,
        .renderSceneMeshProxies = 1U,
        .renderSceneDrawGroupKeys = 1U,
        .meshResourceSlots = 1U,
        .materialResourceSlots = 1U,
        .meshBindings = 1U,
        .materialBindings = 1U,
        .syncMeshProxies = 1U,
        .syncTransformCacheEntries = 1U,
        .syncTransformResolvingEntries = 1U,
    });
    Require(renderer.Initialize(surface, &config), "Graph material instance inheritance test renderer did not initialize");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 4U,
            .maxVisibleInstances = 8U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
    };

    Require(renderer.BeginFrame(), "Graph material instance inheritance test did not begin frame");
    Require(renderer.SubmitScene(scene, desc), "Graph material instance inheritance test did not submit scene");
    const SceneRenderSubmitStats submitStats = renderer.LastSceneSubmitStats();
    Require(submitStats.submittedMeshCount == 1U && !submitStats.HasMissingResources(),
        "Graph material instance inheritance test should submit one complete draw call");
    const SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const RenderResourceRegistry* resources = renderer.SceneResources();
    Require(resourceMap != nullptr && resources != nullptr, "Graph material instance inheritance test could not inspect resources");
    const RenderMaterialHandle instanceHandle = resourceMap->ResolveMaterial(instanceMetadata->id.value);
    const RenderMaterialResource* materialResource = resources->FindMaterial(instanceHandle);
    Require(instanceHandle.IsValid() && materialResource != nullptr,
        "Graph material instance inheritance test did not bind a material resource under the instance asset id");
    Require(NearlyEqual(materialResource->baseColor[0], 0.41F),
        "Graph material instance should inherit parent graph baseColor parameter when the instance does not override it");
    Require(NearlyEqual(materialResource->roughnessFactor, 0.22F),
        "Graph material instance should override only the authored graph roughness parameter");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunRendererReloadsMaterialInstanceWhenParentMaterialChangesTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_runtime_material_instance_parent_reload";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Runtime material instance parent reload test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path materialPath = root / "parent.kbmat";
    const std::filesystem::path instancePath = root / "parent_instance.kbmatinst";
    WriteTriangleObj(meshPath);

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Runtime material instance parent reload test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Runtime material instance parent reload test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialInstanceAssetLoader>()), "Runtime material instance parent reload test could not register material instance loader");
    Require(manager.Mounts().Mount("Game", root), "Runtime material instance parent reload test could not mount asset root");

    WriteReloadableMaterial(materialPath, 0.2F, 0.7F, 0U);
    Require(manager.DiscoverMountedAssets() >= 2U, "Runtime material instance parent reload test did not discover mesh and parent material");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* parentMetadata = manager.Registry().FindByPath("/Game/parent.kbmat");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Runtime material instance parent reload test discovered wrong mesh metadata");
    Require(parentMetadata != nullptr && parentMetadata->type == "RenderMaterial", "Runtime material instance parent reload test discovered wrong parent material metadata");
    const kb::assets::AssetId parentMaterialId = parentMetadata->id;
    const std::uint64_t firstParentHash = parentMetadata->contentHash;

    WriteMaterialInstance(instancePath, parentMaterialId);
    Require(manager.DiscoverMountedAssets() >= 3U, "Runtime material instance parent reload test did not discover material instance");
    meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* instanceMetadata = manager.Registry().FindByPath("/Game/parent_instance.kbmatinst");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Runtime material instance parent reload test lost mesh metadata");
    Require(instanceMetadata != nullptr && instanceMetadata->type == "RenderMaterialInstance", "Runtime material instance parent reload test discovered wrong material instance metadata");
    const kb::assets::AssetId instanceMaterialId = instanceMetadata->id;
    const std::uint64_t instanceHash = instanceMetadata->contentHash;

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Runtime Material Instance Parent Reload Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
        .materialAssetId = instanceMaterialId.value,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 1U,
        .cachedMaterials = 1U,
        .frameReferencedMeshes = 1U,
        .frameReferencedMaterials = 1U,
        .scenePassSubmitStats = 1U,
        .renderSceneMeshProxies = 1U,
        .renderSceneDrawGroupKeys = 1U,
        .meshResourceSlots = 1U,
        .materialResourceSlots = 2U,
        .meshBindings = 1U,
        .materialBindings = 1U,
        .syncMeshProxies = 1U,
        .syncTransformCacheEntries = 1U,
        .syncTransformResolvingEntries = 1U,
    });
    Require(renderer.Initialize(surface, &config), "Renderer did not initialize in runtime material instance parent reload test");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 4U,
            .maxVisibleInstances = 8U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
    };

    Require(renderer.BeginFrame(), "Renderer did not begin first runtime material instance parent reload frame");
    Require(renderer.SubmitScene(scene, desc), "Renderer did not submit first runtime material instance parent reload frame");
    const SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const RenderResourceRegistry* resources = renderer.SceneResources();
    Require(resourceMap != nullptr && resources != nullptr, "Runtime material instance parent reload test could not access scene resources");
    const RenderMaterialHandle firstHandle = resourceMap->ResolveMaterial(instanceMaterialId.value);
    const RenderMaterialResource* firstMaterial = resources->FindMaterial(firstHandle);
    Require(firstHandle.IsValid() && firstMaterial != nullptr, "Runtime material instance parent reload test did not bind initial instance material");
    const std::uint64_t firstVersion = firstMaterial->version;
    Require(NearlyEqual(firstMaterial->baseColor[0], 0.2F), "Runtime material instance parent reload test did not inherit initial parent base color");
    Require(NearlyEqual(firstMaterial->roughnessFactor, 0.7F), "Runtime material instance parent reload test did not inherit initial parent roughness");
    renderer.EndFrame();

    WriteReloadableMaterial(materialPath, 0.85F, 0.33F, 0U);
    Require(manager.DiscoverMountedAssets() >= 3U, "Runtime material instance parent reload test did not rediscover changed parent material");
    parentMetadata = manager.Registry().FindByPath("/Game/parent.kbmat");
    instanceMetadata = manager.Registry().FindByPath("/Game/parent_instance.kbmatinst");
    Require(parentMetadata != nullptr && parentMetadata->id == parentMaterialId && parentMetadata->contentHash != firstParentHash,
        "Runtime material instance parent reload test did not update parent material metadata hash");
    Require(instanceMetadata != nullptr && instanceMetadata->id == instanceMaterialId && instanceMetadata->contentHash == instanceHash,
        "Runtime material instance parent reload test should keep the instance asset metadata stable while parent changes");

    Require(renderer.BeginFrame(), "Renderer did not begin second runtime material instance parent reload frame");
    Require(renderer.SubmitScene(scene, desc), "Renderer did not submit second runtime material instance parent reload frame");
    const SceneRenderSubmitStats reloadSubmitStats = renderer.LastSceneSubmitStats();
    Require(reloadSubmitStats.meshDrawCommandCacheMissCount == 1U, "KBMAT-1010: Parent material reload through instance should invalidate cached draw command");
    Require(reloadSubmitStats.meshDrawCommandCacheBuildCount == 1U, "KBMAT-1010: Parent material reload through instance should rebuild cached draw command");
    Require(reloadSubmitStats.meshDrawCommandCachePruneCount == 1U, "KBMAT-1010: Parent material reload through instance should prune stale draw command");

    const RenderMaterialHandle secondHandle = resourceMap->ResolveMaterial(instanceMaterialId.value);
    const RenderMaterialResource* secondMaterial = resources->FindMaterial(secondHandle);
    Require(secondHandle.IsValid() && secondMaterial != nullptr, "KBMAT-1010: Reloaded material instance did not bind a live material resource");
    Require(secondHandle != firstHandle, "KBMAT-1010: Material instance parent reload reused the stale material handle");
    Require(resources->FindMaterial(firstHandle) == nullptr, "KBMAT-1010: Material instance parent reload kept stale material handle resolvable");
    Require(secondMaterial->version != firstVersion, "KBMAT-1010: Material instance parent reload did not allocate a fresh material resource version");
    Require(NearlyEqual(secondMaterial->baseColor[0], 0.85F), "KBMAT-1010: Reloaded material instance did not inherit changed parent base color");
    Require(NearlyEqual(secondMaterial->roughnessFactor, 0.33F), "KBMAT-1010: Reloaded material instance did not inherit changed parent roughness");
    const Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
    Require(runtimeStats.cachedMaterialCount == 1U, "KBMAT-1010: Material instance parent reload should keep only the live instance material cached");
    Require(runtimeStats.materialReloadCount == 1U, "KBMAT-1010: Material instance parent reload should count one successful material reload");
    renderer.EndFrame();

    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunRendererSubmitsWorkspaceSceneCubeMaterialAfterReopenTest() {
    const std::optional<std::filesystem::path> projectAssets = FindWorkspaceProjectAssets();
    if (!projectAssets.has_value()) {
        return;
    }

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Workspace scene test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Workspace scene test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialInstanceAssetLoader>()), "Workspace scene test could not register material instance loader");
    Require(manager.Mounts().Mount("Game", *projectAssets), "Workspace scene test could not mount Project/Assets");
    Require(manager.DiscoverMountedAssets() >= 1U, "Workspace scene test did not discover project assets");

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/Cube.21kb");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/NewMaterial.kbmat");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Workspace Cube.21kb was not discovered as RenderMesh");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Workspace NewMaterial.kbmat was not discovered as RenderMaterial");

    const kb::assets::AssetHandle<RenderMeshAssetData> loadedMesh = manager.Load<RenderMeshAssetData>(meshMetadata->id);
    const kb::assets::AssetHandle<RenderMaterialAssetData> loadedMaterial = manager.Load<RenderMaterialAssetData>(materialMetadata->id);
    Require(loadedMesh.IsLoaded(), "Workspace Cube.21kb could not be loaded as RenderMeshAssetData before scene reopen");
    Require(loadedMaterial.IsLoaded(), "Workspace NewMaterial.kbmat could not be loaded as RenderMaterialAssetData before scene reopen");

    const std::filesystem::path scenePath = *projectAssets / "Scenes" / "Main.21kbscene";
    Require(kb::scene::SceneDocumentService::LoadFileIntoScene(scene, scenePath), "Workspace Main.21kbscene could not be loaded into a scene");
    struct WorkspaceSceneMeshVisit {
        std::uint64_t expectedMeshAssetId = 0U;
        std::uint64_t expectedMaterialAssetId = 0U;
        std::uint32_t meshRendererCount = 0U;
        std::uint32_t expectedMeshRendererCount = 0U;
    } meshVisit{
        .expectedMeshAssetId = meshMetadata->id.value,
        .expectedMaterialAssetId = materialMetadata->id.value,
    };
    scene.Components().Visitors().ForEachMeshRenderer(
        [](kb::scene::SceneEntity, const kb::scene::TransformComponent&, const kb::scene::MeshRendererComponent& renderer, void* context) {
            auto* visit = static_cast<WorkspaceSceneMeshVisit*>(context);
            ++visit->meshRendererCount;
            if (renderer.meshAssetId == visit->expectedMeshAssetId &&
                renderer.materialAssetId == visit->expectedMaterialAssetId) {
                ++visit->expectedMeshRendererCount;
            }
        },
        &meshVisit);
    Require(meshVisit.expectedMeshRendererCount == 1U,
        "Workspace Main.21kbscene did not expose the expected Cube.21kb/NewMaterial.kbmat Mesh Renderer to ECS render iteration");

    const kb::scene::SceneEntity cameraEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Workspace Runtime Test Camera",
        .transform = TransformAt(0.0F, 0.0F, -6.0F),
    });
    scene.Components().Cameras().Set(cameraEntity, kb::scene::CameraComponent{
        .projection = kb::scene::CameraProjection::Perspective,
        .verticalFovDegrees = 60.0F,
        .orthographicHeight = 10.0F,
        .nearClip = 0.01F,
        .farClip = 100.0F,
        .primary = true,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 2U,
        .cachedMaterials = 4U,
        .frameReferencedMeshes = 2U,
        .frameReferencedMaterials = 4U,
        .scenePassSubmitStats = 2U,
        .renderSceneMeshProxies = 4U,
        .renderSceneDrawGroupKeys = 4U,
        .meshResourceSlots = 2U,
        .materialResourceSlots = 4U,
        .meshBindings = 2U,
        .materialBindings = 4U,
        .syncMeshProxies = 4U,
        .syncTransformCacheEntries = 4U,
        .syncTransformResolvingEntries = 4U,
    });
    Require(renderer.Initialize(surface, &config), "Workspace scene renderer did not initialize in headless Noop mode");
    RenderResourceRegistry directResources;
    const RenderMeshHandle directMeshHandle = directResources.RegisterMesh(loadedMesh->desc);
    Require(directMeshHandle.IsValid(), "Workspace Cube.21kb loaded but its mesh desc could not register as a runtime mesh resource");
    directResources.Shutdown();
    Require(renderer.BeginFrame(), "Workspace scene renderer did not begin a frame");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 4U,
            .maxVisibleInstances = 8U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
    };
    Require(renderer.SubmitScene(scene, desc), "Workspace reopened scene did not submit to the runtime renderer");

    const SceneRenderSubmitStats submitStats = renderer.LastSceneSubmitStats();
    const SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const RenderResourceRegistry* resources = renderer.SceneResources();
    Require(resourceMap != nullptr && resources != nullptr, "Workspace scene test could not inspect runtime render resources");
    const RenderMeshHandle meshHandle = resourceMap->ResolveMesh(meshMetadata->id.value);
    const RenderMaterialHandle materialHandle = resourceMap->ResolveMaterial(materialMetadata->id.value);
    const RenderMaterialResource* materialResource = resources->FindMaterial(materialHandle);
    const ResolvedRuntimeMaterialAsset expectedMaterial = RuntimeMaterialResolver{}.ResolveAsset(manager, materialMetadata->id);
    Require(meshHandle.IsValid(), "Workspace reopened scene did not bind Cube.21kb to a runtime mesh handle");
    Require(materialHandle.IsValid() && materialResource != nullptr, "Workspace reopened scene did not bind NewMaterial.kbmat to a runtime material handle");
    Require(expectedMaterial.resolved &&
            NearlyEqual(materialResource->baseColor[0], expectedMaterial.material.desc.baseColor[0]) &&
            NearlyEqual(materialResource->baseColor[1], expectedMaterial.material.desc.baseColor[1]) &&
            NearlyEqual(materialResource->baseColor[2], expectedMaterial.material.desc.baseColor[2]),
        "Workspace runtime material resource did not match the resolved NewMaterial base color");
    Require(!submitStats.HasMissingResources(), "Workspace reopened scene reported missing mesh or material resources");
    Require(submitStats.submittedMeshCount == 1U, "Workspace reopened scene did not submit the cube mesh");
    Require(submitStats.submittedDrawCallCount == 1U, "Workspace reopened scene did not emit one cube draw call");

    renderer.EndFrame();
    renderer.Shutdown();
}

void RunRendererSubmitsGltfEmbeddedMaterialInHeadlessNoopTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_runtime_embedded_material";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Embedded material runtime test could not create temp root");

    WriteEmbeddedMaterialTriangleGltf(root);
    WriteTexture(root / "embedded_albedo.kbtex", 160U, 180U, 200U);
    WriteTexture(root / "embedded_normal.kbtex", 128U, 128U, 255U);
    WriteTexture(root / "embedded_mr.kbtex", 64U, 128U, 192U);
    WriteTexture(root / "embedded_ao.kbtex", 192U, 192U, 192U);
    WriteTexture(root / "embedded_emissive.kbtex", 10U, 20U, 30U);

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Embedded material runtime test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderTextureAssetLoader>()), "Embedded material runtime test could not register texture loader");
    Require(manager.Mounts().Mount("Game", root), "Embedded material runtime test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 5U, "Embedded material runtime test did not discover mesh and texture assets");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/embedded_triangle.gltf");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Embedded material runtime test discovered wrong mesh metadata");

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Embedded Material Runtime Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .cachedMeshes = 2U,
        .cachedMaterials = 4U,
        .cachedTextures = 5U,
        .frameReferencedMeshes = 2U,
        .frameReferencedMaterials = 4U,
        .frameReferencedTextures = 5U,
        .scenePassSubmitStats = 2U,
        .renderSceneMeshProxies = 4U,
        .renderSceneDrawGroupKeys = 4U,
        .meshResourceSlots = 2U,
        .materialResourceSlots = 4U,
        .textureResourceSlots = 5U,
        .meshBindings = 2U,
        .materialBindings = 4U,
        .textureBindings = 5U,
        .syncMeshProxies = 4U,
        .syncTransformCacheEntries = 4U,
        .syncTransformResolvingEntries = 4U,
    });
    Require(renderer.Initialize(surface, &config), "Renderer did not initialize in embedded material runtime test");
    Require(renderer.BeginFrame(), "Renderer did not begin embedded material runtime frame");

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .drawBudget = SceneRenderDrawBudget{
            .maxDrawCommands = 4U,
            .maxVisibleInstances = 8U,
        },
    };
    Require(renderer.SubmitScene(scene, desc), "Renderer did not submit embedded material runtime scene");

    const SceneRenderSubmitStats submitStats = renderer.LastSceneSubmitStats();
    Require(submitStats.visibleMeshCount == 1U, "Embedded material runtime test did not keep mesh visible");
    Require(submitStats.submittedMeshCount == 1U, "Embedded material runtime test did not submit mesh");
    Require(submitStats.submittedDrawCallCount == 1U, "Embedded material runtime test did not draw one embedded material batch");
    Require(!submitStats.HasMissingResources(), "Embedded material runtime test reported missing resources");
    const std::span<const SceneRenderPassSubmitStats> passStats = renderer.LastScenePassSubmitStats();
    Require(passStats.size() == 2U, "Embedded material runtime test did not report both scene passes");
    Require(passStats[0].pass == MeshPassType::BaseOpaque && passStats[0].stats.submittedMeshCount == 1U, "Embedded material runtime opaque pass stats are wrong");
    Require(passStats[1].pass == MeshPassType::BaseTransparent && passStats[1].stats.submittedMeshCount == 0U, "Embedded material runtime transparent pass stats are wrong");

    const Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
    Require(runtimeStats.cachedMeshCount == 1U, "Embedded material runtime test did not cache one mesh resource");
    Require(runtimeStats.cachedMaterialCount == 1U, "Embedded material runtime test did not cache one embedded material resource");
    Require(runtimeStats.cachedTextureCount == 5U, "Embedded material runtime test did not cache embedded material textures");
    Require(runtimeStats.referencedMeshAssetCount == 1U, "Embedded material runtime test did not reference one mesh asset");
    Require(runtimeStats.referencedMaterialAssetCount == 1U, "Embedded material runtime test did not reference one embedded material asset");
    Require(runtimeStats.referencedTextureAssetCount == 5U, "Embedded material runtime test did not reference embedded material textures");
    Require(runtimeStats.unresolvedMaterialTexturePathCount == 0U, "Embedded material runtime test reported unresolved texture paths for valid embedded textures");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunRendererSubmitsDockedAndDetachedViewportsInSameFrameTest() {
    kb::scene::Scene scene;

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "Renderer did not initialize for same-frame multi-viewport test");
    Require(renderer.BeginFrame(), "Renderer did not begin same-frame multi-viewport test");

    const RenderSceneSubmitDesc docked{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .meshPassMode = SceneRenderMeshPassMode::OpaqueAndTransparent,
    };
    const RenderSceneSubmitDesc detached{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 2U },
                .extent = RenderExtent{ 96U, 72U },
                .viewportIndex = 1U,
            },
        },
        .cameraOverride = IdentityCamera(),
        .meshPassMode = SceneRenderMeshPassMode::OpaqueAndTransparent,
    };
    const std::array<Renderer::SceneFrameSubmission, 2U> submissions{
        Renderer::SceneFrameSubmission{ .scene = &scene, .desc = docked },
        Renderer::SceneFrameSubmission{ .scene = &scene, .desc = detached },
    };

    Require(renderer.SubmitScenes(submissions), "Renderer rejected docked and detached viewport submissions in one frame");
    const std::span<const SceneRenderPassSubmitStats> passStats = renderer.LastScenePassSubmitStats();
    Require(passStats.size() == 4U, "Same-frame multi-viewport test did not report both scene passes for both viewports");
    Require(passStats[0].viewportId == 1U && passStats[0].viewportIndex == 0U, "Docked viewport opaque pass metadata is wrong");
    Require(passStats[1].viewportId == 1U && passStats[1].viewportIndex == 0U, "Docked viewport transparent pass metadata is wrong");
    Require(passStats[2].viewportId == 2U && passStats[2].viewportIndex == 1U, "Detached viewport opaque pass metadata is wrong");
    Require(passStats[3].viewportId == 2U && passStats[3].viewportIndex == 1U, "Detached viewport transparent pass metadata is wrong");
    Require(!renderer.LastSceneSubmitStats().HasMissingResources(), "Same-frame multi-viewport test reported missing resources for an empty scene");

    renderer.EndFrame();
    renderer.Shutdown();
}

} // namespace

void RunRendererRuntimeSubmitTests() {
    RunRuntimeMaterialResolverReturnsTypedFallbacksAndDiagnosticsTest();
    RunRuntimeMaterialResolverEvaluatesMaterialOutputTextureGraphTest();
    RunRuntimeMaterialResolverEvaluatesConstantAndMathGraphTest();
    RunRendererSubmitsRuntimeMeshAssetInHeadlessNoopTest();
    RunRendererUsesResolverDefaultFallbackForMissingMaterialTest();
    RunRendererReloadsChangedRuntimeMaterialAssetTest();
    RunGraphBackedMaterialArtifactDependencyReloadInvalidatesOnlyTouchedBindingTest();
    RunCookedGraphBackedMaterialRuntimeDoesNotCompileGraphTest();
    RunInvalidGraphMaterialUsesLastGoodThenRefreshesAfterFixTest();
    RunRendererSubmitsMaterialInstanceAssetInHeadlessNoopTest();
    RunRendererMaterialInstanceInheritsGraphBackedParentParametersTest();
    RunRendererReloadsMaterialInstanceWhenParentMaterialChangesTest();
    RunRendererSubmitsWorkspaceSceneCubeMaterialAfterReopenTest();
    RunRendererSubmitsGltfEmbeddedMaterialInHeadlessNoopTest();
    RunRendererSubmitsDockedAndDetachedViewportsInSameFrameTest();
}

} // namespace kb::render::tests
