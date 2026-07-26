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
#include "engine/scene/SceneMaterialInstances.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePostProcessAccess.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/VisibilityComponent.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/overlay/SceneGizmoPass.hpp"
#include "kb/render/scene/SceneRenderer.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialTypeAssetLoader.hpp"
#include "kb/render/resources/PostProcessProfileAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "kb/render/runtime/RuntimeMaterialResolver.hpp"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <cmath>
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
        const RenderMaterialGraphLink addToOutput = MakeGraphLink(
            RenderMaterialGraphNodeKind::Add,
            3U,
            "value",
            RenderMaterialGraphNodeKind::MaterialOutput,
            1U,
            "baseColor");
        output
            << "graphNode 3 Add 300 80\n"
            << "graphNode 4 Multiply 460 80\n"
            << "graphLink " << addToMultiply.id << ' '
            << addToMultiply.fromNodeId << ' ' << addToMultiply.fromPinId << ' ' << addToMultiply.fromPin << ' '
            << addToMultiply.toNodeId << ' ' << addToMultiply.toPinId << ' ' << addToMultiply.toPin << "\n"
            << "graphLink " << multiplyToAdd.id << ' '
            << multiplyToAdd.fromNodeId << ' ' << multiplyToAdd.fromPinId << ' ' << multiplyToAdd.fromPin << ' '
            << multiplyToAdd.toNodeId << ' ' << multiplyToAdd.toPinId << ' ' << multiplyToAdd.toPin << "\n"
            << "graphLink " << addToOutput.id << ' '
            << addToOutput.fromNodeId << ' ' << addToOutput.fromPinId << ' ' << addToOutput.fromPin << ' '
            << addToOutput.toNodeId << ' ' << addToOutput.toPinId << ' ' << addToOutput.toPin << "\n";
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

    RenderMaterialAssetData textureObjectNormal{};
    textureObjectNormal.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::TextureObject, "normalObject"),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::TextureSample),
        MakeGraphNode(4U, RenderMaterialGraphNodeKind::NormalUnpack),
    };
    textureObjectNormal.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::TextureObject, 2U, "texture", RenderMaterialGraphNodeKind::TextureSample, 3U, "texture"),
        MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 3U, "color", RenderMaterialGraphNodeKind::NormalUnpack, 4U, "color"),
        MakeGraphLink(RenderMaterialGraphNodeKind::NormalUnpack, 4U, "normal", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "normal"),
    };
    textureObjectNormal.graphParameterValues = {
        MakeTextureGraphValue("normalObject", 202U),
    };
    const ResolvedRuntimeMaterialDesc textureObjectResolved =
        RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, textureObjectNormal);
    Require(textureObjectResolved.desc.normalTextureAssetId == 202U,
        "KBMAT-RUNTIME: TextureObject through TextureSample and Normal Unpack did not drive Material Output Normal texture");

    RenderMaterialAssetData disconnected{};
    disconnected.desc.baseColor[0] = 0.75F;
    disconnected.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::TextureSample),
    };
    const ResolvedRuntimeMaterialDesc disconnectedResolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, disconnected);
    Require(
        NearlyEqual(disconnectedResolved.desc.baseColor[0], 1.0F) &&
            NearlyEqual(disconnectedResolved.desc.baseColor[1], 1.0F) &&
            NearlyEqual(disconnectedResolved.desc.baseColor[2], 1.0F) &&
            NearlyEqual(disconnectedResolved.desc.baseColor[3], 1.0F),
        "KBMAT-RUNTIME: Disconnected graph Base Color should resolve using MaterialSurface white default");
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

    RenderMaterialAssetData switchMaterial{};
    switchMaterial.desc.baseColor[0] = 1.0F;
    switchMaterial.desc.baseColor[1] = 1.0F;
    switchMaterial.desc.baseColor[2] = 1.0F;
    switchMaterial.desc.baseColor[3] = 1.0F;
    switchMaterial.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "2.4"),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "9"),
        MakeGraphNode(4U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.1"),
        MakeGraphNode(5U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.2"),
        MakeGraphNode(6U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.4"),
        MakeGraphNode(7U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.7"),
        MakeGraphNode(8U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.9"),
        MakeGraphNode(9U, RenderMaterialGraphNodeKind::RuntimeSwitch),
        MakeGraphNode(10U, RenderMaterialGraphNodeKind::RuntimeSwitch),
    };
    switchMaterial.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 2U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 9U, "index"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 4U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 9U, "default"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 5U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 9U, "case0"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 6U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 9U, "case1"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 7U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 9U, "case2"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 8U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 9U, "case3"),
        MakeGraphLink(RenderMaterialGraphNodeKind::RuntimeSwitch, 9U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 3U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 10U, "index"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 4U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 10U, "default"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 5U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 10U, "case0"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 6U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 10U, "case1"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 7U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 10U, "case2"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 8U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 10U, "case3"),
        MakeGraphLink(RenderMaterialGraphNodeKind::RuntimeSwitch, 10U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"),
    };
    const ResolvedRuntimeMaterialDesc switchResolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, switchMaterial);
    Require(NearlyEqual(switchResolved.desc.roughnessFactor, 0.7F), "KBMAT-RUNTIME: Switch case2 branch did not evaluate into Roughness");
    Require(NearlyEqual(switchResolved.desc.baseColor[3], 0.1F), "KBMAT-RUNTIME: Switch default branch did not evaluate into Alpha");

    RenderMaterialAssetData sobolMaterial{};
    sobolMaterial.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "1"),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::Sobol),
    };
    sobolMaterial.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 2U, "value", RenderMaterialGraphNodeKind::Sobol, 3U, "index"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Sobol, 3U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Sobol, 3U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"),
    };
    const ResolvedRuntimeMaterialDesc sobolResolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, sobolMaterial);
    Require(NearlyEqual(sobolResolved.desc.baseColor[0], 34432.0F / 65536.0F) &&
            NearlyEqual(sobolResolved.desc.baseColor[1], 19584.0F / 65536.0F) &&
            NearlyEqual(sobolResolved.desc.baseColor[2], 0.0F) &&
            NearlyEqual(sobolResolved.desc.baseColor[3], 1.0F),
        "KBMAT-RUNTIME: Sobol graph did not evaluate its deterministic Float2 sample into Base Color");
    Require(NearlyEqual(sobolResolved.desc.roughnessFactor, 34432.0F / 65536.0F),
        "KBMAT-RUNTIME: Sobol graph did not coerce its Float2 sample into Roughness");

    RenderMaterialAssetData surfaceMaterial{};
    surfaceMaterial.desc.baseColor[0] = 1.0F;
    surfaceMaterial.desc.baseColor[1] = 1.0F;
    surfaceMaterial.desc.baseColor[2] = 1.0F;
    surfaceMaterial.desc.baseColor[3] = 1.0F;
    surfaceMaterial.desc.roughnessFactor = 0.0F;
    surfaceMaterial.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantColor, {}, "1 0 0 1"),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "1"),
        MakeGraphNode(4U, RenderMaterialGraphNodeKind::Desaturate),
        MakeGraphNode(5U, RenderMaterialGraphNodeKind::ConstantVector, {}, "0 0 1"),
        MakeGraphNode(6U, RenderMaterialGraphNodeKind::ConstantVector, {}, "0 0 -1"),
        MakeGraphNode(7U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "2"),
        MakeGraphNode(8U, RenderMaterialGraphNodeKind::Fresnel),
    };
    surfaceMaterial.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::Desaturate, 4U, "color"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 3U, "value", RenderMaterialGraphNodeKind::Desaturate, 4U, "fraction"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Desaturate, 4U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 5U, "xyz", RenderMaterialGraphNodeKind::Fresnel, 8U, "normal"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 6U, "xyz", RenderMaterialGraphNodeKind::Fresnel, 8U, "view"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 7U, "value", RenderMaterialGraphNodeKind::Fresnel, 8U, "exponent"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Fresnel, 8U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"),
    };
    const ResolvedRuntimeMaterialDesc surfaceResolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, surfaceMaterial);
    Require(NearlyEqual(surfaceResolved.desc.baseColor[0], 0.299F) &&
            NearlyEqual(surfaceResolved.desc.baseColor[1], 0.299F) &&
            NearlyEqual(surfaceResolved.desc.baseColor[2], 0.299F),
        "KBMAT-RUNTIME: Desaturate graph did not evaluate into Base Color");
    Require(NearlyEqual(surfaceResolved.desc.roughnessFactor, 1.0F), "KBMAT-RUNTIME: Fresnel graph did not evaluate into Roughness");

    RenderMaterialAssetData advancedMathMaterial{};
    advancedMathMaterial.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "-0.25"),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::Negate),
        MakeGraphNode(4U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.6"),
        MakeGraphNode(5U, RenderMaterialGraphNodeKind::Round),
        MakeGraphNode(6U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.75"),
        MakeGraphNode(7U, RenderMaterialGraphNodeKind::Truncate),
        MakeGraphNode(8U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"),
        MakeGraphNode(9U, RenderMaterialGraphNodeKind::Sign),
        MakeGraphNode(10U, RenderMaterialGraphNodeKind::MakeVector),
        MakeGraphNode(11U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0"),
        MakeGraphNode(12U, RenderMaterialGraphNodeKind::Tangent),
        MakeGraphNode(13U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"),
        MakeGraphNode(14U, RenderMaterialGraphNodeKind::ArcSine),
        MakeGraphNode(15U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"),
        MakeGraphNode(16U, RenderMaterialGraphNodeKind::ArcCosine),
        MakeGraphNode(17U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "1"),
        MakeGraphNode(18U, RenderMaterialGraphNodeKind::ArcTangent),
        MakeGraphNode(19U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "1"),
        MakeGraphNode(20U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "1"),
        MakeGraphNode(21U, RenderMaterialGraphNodeKind::ArcTangent2),
    };
    advancedMathMaterial.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 2U, "value", RenderMaterialGraphNodeKind::Negate, 3U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Negate, 3U, "value", RenderMaterialGraphNodeKind::MakeVector, 10U, "x"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 4U, "value", RenderMaterialGraphNodeKind::Round, 5U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Round, 5U, "value", RenderMaterialGraphNodeKind::MakeVector, 10U, "y"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 6U, "value", RenderMaterialGraphNodeKind::Truncate, 7U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Truncate, 7U, "value", RenderMaterialGraphNodeKind::MakeVector, 10U, "z"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 8U, "value", RenderMaterialGraphNodeKind::Sign, 9U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Sign, 9U, "value", RenderMaterialGraphNodeKind::MakeVector, 10U, "w"),
        MakeGraphLink(RenderMaterialGraphNodeKind::MakeVector, 10U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 11U, "value", RenderMaterialGraphNodeKind::Tangent, 12U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::Tangent, 12U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 13U, "value", RenderMaterialGraphNodeKind::ArcSine, 14U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ArcSine, 14U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 15U, "value", RenderMaterialGraphNodeKind::ArcCosine, 16U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ArcCosine, 16U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 17U, "value", RenderMaterialGraphNodeKind::ArcTangent, 18U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ArcTangent, 18U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "occlusion"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 19U, "value", RenderMaterialGraphNodeKind::ArcTangent2, 21U, "y"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 20U, "value", RenderMaterialGraphNodeKind::ArcTangent2, 21U, "x"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ArcTangent2, 21U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"),
    };
    const ResolvedRuntimeMaterialDesc advancedMathResolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, advancedMathMaterial);
    Require(NearlyEqual(advancedMathResolved.desc.baseColor[0], 0.25F) &&
            NearlyEqual(advancedMathResolved.desc.baseColor[1], 1.0F) &&
            NearlyEqual(advancedMathResolved.desc.baseColor[2], 0.0F),
        "KBMAT-RUNTIME: Negate/Round/Truncate graph did not evaluate into Base Color");
    Require(NearlyEqual(advancedMathResolved.desc.baseColor[3], 0.785398F), "KBMAT-RUNTIME: Atan2 graph did not evaluate into Alpha");
    Require(NearlyEqual(advancedMathResolved.desc.roughnessFactor, 0.0F), "KBMAT-RUNTIME: Tangent graph did not evaluate into Roughness");
    Require(NearlyEqual(advancedMathResolved.desc.metallicFactor, 0.523599F), "KBMAT-RUNTIME: ArcSine graph did not evaluate into Metallic");
    Require(NearlyEqual(advancedMathResolved.desc.occlusionStrength, 0.785398F), "KBMAT-RUNTIME: ArcTangent graph did not evaluate into Occlusion");
    Require(NearlyEqual(advancedMathResolved.desc.emissiveColor[0], 1.047198F), "KBMAT-RUNTIME: ArcCosine graph did not evaluate into Emissive");

    RenderMaterialAssetData fastTrigMaterial{};
    fastTrigMaterial.graph.nodes = {
        MakeGraphNode(1U, RenderMaterialGraphNodeKind::MaterialOutput),
        MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"),
        MakeGraphNode(3U, RenderMaterialGraphNodeKind::ArcSineFast),
        MakeGraphNode(4U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"),
        MakeGraphNode(5U, RenderMaterialGraphNodeKind::ArcCosineFast),
        MakeGraphNode(6U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "1"),
        MakeGraphNode(7U, RenderMaterialGraphNodeKind::ArcTangentFast),
        MakeGraphNode(8U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "1"),
        MakeGraphNode(9U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "1"),
        MakeGraphNode(10U, RenderMaterialGraphNodeKind::ArcTangent2Fast),
    };
    fastTrigMaterial.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 2U, "value", RenderMaterialGraphNodeKind::ArcSineFast, 3U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ArcSineFast, 3U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 4U, "value", RenderMaterialGraphNodeKind::ArcCosineFast, 5U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ArcCosineFast, 5U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 6U, "value", RenderMaterialGraphNodeKind::ArcTangentFast, 7U, "value"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ArcTangentFast, 7U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "occlusion"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 8U, "value", RenderMaterialGraphNodeKind::ArcTangent2Fast, 10U, "y"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 9U, "value", RenderMaterialGraphNodeKind::ArcTangent2Fast, 10U, "x"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ArcTangent2Fast, 10U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"),
    };
    const ResolvedRuntimeMaterialDesc fastTrigResolved = RuntimeMaterialResolver{}.ResolveLoadedMaterial(manager, materialMetadata, fastTrigMaterial);
    Require(NearlyEqual(fastTrigResolved.desc.metallicFactor, 0.523403F), "KBMAT-RUNTIME: ArcSineFast graph did not evaluate into Metallic");
    Require(NearlyEqual(fastTrigResolved.desc.emissiveColor[0], 1.047394F), "KBMAT-RUNTIME: ArcCosineFast graph did not evaluate into Emissive");
    Require(NearlyEqual(fastTrigResolved.desc.occlusionStrength, 0.785814F), "KBMAT-RUNTIME: ArcTangentFast graph did not evaluate into Occlusion");
    Require(NearlyEqual(fastTrigResolved.desc.baseColor[3], 0.785814F), "KBMAT-RUNTIME: ArcTangent2Fast graph did not evaluate into Alpha");
}

[[nodiscard]] RenderMaterialAssetData MakeDimensionGraphMaterial(
    RenderMaterialGraphNodeKind sampleKind,
    std::string stableId,
    std::uint64_t textureAssetId) {
    RenderMaterialAssetData material{};
    material.graph = MakeDefaultRenderMaterialGraphDocument();
    material.graph.shadingModel = "unlit";
    RenderMaterialGraphNode sample = MakeGraphNode(2U, sampleKind, stableId);
    sample.parameter.textureRole = "occlusion";
    sample.parameter.expectedTextureColorSpace = RenderMaterialTextureColorSpace::Linear;
    material.graph.nodes.push_back(std::move(sample));
    material.graph.links.push_back(MakeGraphLink(
        sampleKind,
        2U,
        "a",
        RenderMaterialGraphNodeKind::MaterialOutput,
        1U,
        "alpha"));
    material.graphParameterValues.push_back(MakeTextureGraphValue(std::move(stableId), textureAssetId));
    return material;
}

void WriteDimensionTexture(
    const std::filesystem::path& path,
    RenderTextureDimension dimension,
    std::uint16_t depth = 1U,
    std::uint16_t layers = 1U) {
    std::ofstream output{ path, std::ios::trunc };
    output << "dimension ";
    switch (dimension) {
    case RenderTextureDimension::Texture2D: output << "2d\n"; break;
    case RenderTextureDimension::TextureCube: output << "cube\n"; break;
    case RenderTextureDimension::Texture3D: output << "3d\n"; break;
    case RenderTextureDimension::Texture2DArray: output << "2dArray\n"; break;
    }
    output << "size 2 2\n";
    if (dimension == RenderTextureDimension::Texture3D) {
        output << "depth " << depth << "\n";
    }
    if (dimension == RenderTextureDimension::Texture2DArray) {
        output << "layers " << layers << "\n";
    }
    output << "rgba8 32 96 224 255\n";
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

void RunRuntimeGraphMaterialRenderModeReportingTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_runtime_graph_render_mode";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Graph render mode test could not create temp root");

    RenderMaterialAssetData gpuMaterial{};
    gpuMaterial.graph = MakeDefaultRenderMaterialGraphDocument();
    gpuMaterial.graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .positionX = -160, .positionY = 64 });
    gpuMaterial.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    Require(RenderMaterialAssetWriter::Save(root / "gpu.kbmat", gpuMaterial), "Graph render mode test could not save GPU graph material");

    RenderMaterialAssetData builtinMaterial{};
    builtinMaterial.desc.baseColor[0] = 0.5F;
    Require(RenderMaterialAssetWriter::Save(root / "builtin.kbmat", builtinMaterial), "Graph render mode test could not save builtin material");

    // A valid MaterialOutput subgraph plus an orphan cycle: the runtime MaterialOutput subset stays valid (no error
    // material), but the full shader compile rejects the cycle, so no GPU program is produced and the runtime must
    // fall back to CPU PBR flattening with an explicit reason.
    RenderMaterialAssetData cpuMaterial{};
    cpuMaterial.graph = MakeDefaultRenderMaterialGraphDocument();
    cpuMaterial.graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .positionX = -160, .positionY = 64 });
    cpuMaterial.graph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::Add, .positionX = -360, .positionY = 200 });
    cpuMaterial.graph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::Add, .positionX = -360, .positionY = 320 });
    cpuMaterial.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    cpuMaterial.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Add, 3U, "value", RenderMaterialGraphNodeKind::Add, 4U, "a"));
    cpuMaterial.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Add, 4U, "value", RenderMaterialGraphNodeKind::Add, 3U, "a"));
    Require(RenderMaterialAssetWriter::Save(root / "cpu.kbmat", cpuMaterial), "Graph render mode test could not save CPU fallback material");

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Graph render mode test could not register material loader");
    Require(manager.Mounts().Mount("Game", root), "Graph render mode test could not mount asset root");
    Require(manager.DiscoverMountedAssets() == 3U, "Graph render mode test did not discover three materials");

    RuntimeMaterialResolver resolver;

    const kb::assets::AssetMetadata* gpuMeta = manager.Registry().FindByPath("/Game/gpu.kbmat");
    Require(gpuMeta != nullptr, "Graph render mode test did not find GPU material metadata");
    const ResolvedRuntimeMaterialAsset gpu = resolver.ResolveAsset(manager, *gpuMeta);
    Require(gpu.resolved && gpu.status == RuntimeMaterialResolveStatus::Resolved, "MAT-26: A valid graph material must resolve");
    Require(gpu.renderMode == RuntimeMaterialRenderMode::GpuMaterialGraph, "MAT-26: A valid graph material must resolve to the GPU material graph path, not CPU flattening");
    Require(gpu.material.graphProgram.active && gpu.material.graphProgram.graphSourceHash != 0U, "MAT-26: GPU graph program must be bound with a non-zero program key");
    Require(gpu.cpuFallbackReason == RuntimeMaterialCpuFallbackReason::None, "MAT-26: A GPU graph material must not carry a CPU fallback reason");

    const kb::assets::AssetMetadata* builtinMeta = manager.Registry().FindByPath("/Game/builtin.kbmat");
    Require(builtinMeta != nullptr, "Graph render mode test did not find builtin material metadata");
    const ResolvedRuntimeMaterialAsset builtin = resolver.ResolveAsset(manager, *builtinMeta);
    Require(builtin.resolved && builtin.renderMode == RuntimeMaterialRenderMode::BuiltinPbr, "MAT-27: A non-graph material must keep the builtin PBR path working");
    Require(!builtin.material.graphProgram.active, "MAT-27: A builtin PBR material must not bind a graph program");

    const kb::assets::AssetMetadata* cpuMeta = manager.Registry().FindByPath("/Game/cpu.kbmat");
    Require(cpuMeta != nullptr, "Graph render mode test did not find CPU fallback material metadata");
    const ResolvedRuntimeMaterialAsset cpu = resolver.ResolveAsset(manager, *cpuMeta);
    Require(cpu.resolved && cpu.status == RuntimeMaterialResolveStatus::Resolved, "MAT-27: A graph material without a GPU program must still resolve instead of erroring");
    Require(cpu.renderMode == RuntimeMaterialRenderMode::CpuPbrFlatteningFallback, "MAT-27: A graph material without a GPU program must fall back to CPU PBR flattening");
    Require(cpu.cpuFallbackReason == RuntimeMaterialCpuFallbackReason::GraphProgramUnavailable, "MAT-27: CPU fallback must carry an explicit reason code");
    Require(!cpu.material.graphProgram.active, "MAT-27: CPU fallback material must not advertise an active GPU program");
    bool foundFallbackDiagnostic = false;
    for (const RuntimeMaterialResolveDiagnostic& diagnostic : cpu.diagnostics) {
        if (diagnostic.message.find("CPU PBR flattening") != std::string::npos) {
            foundFallbackDiagnostic = true;
        }
    }
    Require(foundFallbackDiagnostic, "MAT-27: CPU fallback must be reported through a resolver diagnostic, not hidden");

    Require(std::string_view{ "GpuMaterialGraph" } == RuntimeMaterialRenderModeName(RuntimeMaterialRenderMode::GpuMaterialGraph) &&
            std::string_view{ "CpuPbrFlatteningFallback" } == RuntimeMaterialRenderModeName(RuntimeMaterialRenderMode::CpuPbrFlatteningFallback) &&
            std::string_view{ "BuiltinPbr" } == RuntimeMaterialRenderModeName(RuntimeMaterialRenderMode::BuiltinPbr),
        "MAT-26: Render mode names must be stable for telemetry");
    Require(std::string_view{ "GraphProgramUnavailable" } == RuntimeMaterialCpuFallbackReasonName(RuntimeMaterialCpuFallbackReason::GraphProgramUnavailable),
        "MAT-26: CPU fallback reason names must be stable for telemetry");

    std::filesystem::remove_all(root, error);
}

void RunRuntimeMaterialInstanceDynamicParameterOverrideTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_runtime_material_instance_dynamic";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "MAT-40 instance dynamic test could not create temp root");

    RenderMaterialAssetData parent{};
    parent.graph = MakeDefaultRenderMaterialGraphDocument();
    parent.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ParameterColor,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "tint",
            .displayName = "Tint",
            .defaultValueHint = "1 0 0 1",
            .overrideSupported = true,
        },
    });
    parent.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ParameterColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    parent.graphParameterValues.push_back(RenderMaterialGraphParameterValue{
        .stableId = "tint",
        .type = RenderMaterialParameterType::Color,
        .numbers = { 1.0F, 0.0F, 0.0F, 1.0F },
    });
    Require(RenderMaterialAssetWriter::Save(root / "Parent.kbmat", parent), "MAT-40 instance dynamic test could not save parent material");

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "MAT-40 instance dynamic test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialInstanceAssetLoader>()), "MAT-40 instance dynamic test could not register instance loader");
    Require(manager.Mounts().Mount("Game", root), "MAT-40 instance dynamic test could not mount asset root");
    Require(manager.DiscoverMountedAssets() == 1U, "MAT-40 instance dynamic test did not discover parent material");
    const kb::assets::AssetMetadata* parentMeta = manager.Registry().FindByPath("/Game/Parent.kbmat");
    Require(parentMeta != nullptr, "MAT-40 instance dynamic test did not find parent material metadata");

    RenderMaterialInstanceAssetData instance{};
    instance.parentMaterialAssetId = parentMeta->id;
    instance.hasOverrides = true;
    instance.overrides = parent;
    instance.overrides.graphParameterValues.clear();
    instance.overrides.graphParameterValues.push_back(RenderMaterialGraphParameterValue{
        .stableId = "tint",
        .type = RenderMaterialParameterType::Color,
        .numbers = { 0.0F, 1.0F, 0.0F, 1.0F },
    });
    Require(RenderMaterialInstanceAssetWriter::Save(root / "Parent_Inst.kbmatinst", instance), "MAT-40 instance dynamic test could not save material instance");

    Require(manager.DiscoverMountedAssets() == 2U, "MAT-40 instance dynamic test did not discover parent and instance");
    parentMeta = manager.Registry().FindByPath("/Game/Parent.kbmat");
    const kb::assets::AssetMetadata* instanceMeta = manager.Registry().FindByPath("/Game/Parent_Inst.kbmatinst");
    Require(parentMeta != nullptr && instanceMeta != nullptr, "MAT-40 instance dynamic test did not find discovered materials");
    Require(instance.parentMaterialAssetId == parentMeta->id, "MAT-40 instance dynamic test parent id must match discovered parent metadata");

    RuntimeMaterialResolver resolver;
    const ResolvedRuntimeMaterialAsset resolvedParent = resolver.ResolveAsset(manager, *parentMeta);
    const ResolvedRuntimeMaterialAsset resolvedInstance = resolver.ResolveAsset(manager, *instanceMeta);
    Require(resolvedParent.resolved && resolvedParent.renderMode == RuntimeMaterialRenderMode::GpuMaterialGraph,
        "MAT-40 parent graph material must resolve to GPU material graph");
    Require(resolvedInstance.resolved && resolvedInstance.renderMode == RuntimeMaterialRenderMode::GpuMaterialGraph,
        "MAT-40 material instance override must resolve to GPU material graph");
    Require(resolvedParent.material.graphProgram.active && resolvedInstance.material.graphProgram.active,
        "MAT-40 parent and instance must both bind active graph programs");
    Require(resolvedParent.material.graphProgram.graphSourceHash == resolvedInstance.material.graphProgram.graphSourceHash &&
            resolvedParent.material.graphProgram.materialTypeId == resolvedInstance.material.graphProgram.materialTypeId &&
            resolvedParent.material.graphProgram.materialTypeVersion == resolvedInstance.material.graphProgram.materialTypeVersion,
        "MAT-40 material instance dynamic override must keep the same graph program key");

    const auto findTint = [](const RenderMaterialGraphProgramBinding& binding) -> const RenderMaterialGraphUniformBinding* {
        for (const RenderMaterialGraphUniformBinding& uniform : binding.uniforms) {
            if (uniform.stableId == "tint") {
                return &uniform;
            }
        }
        return nullptr;
    };
    const RenderMaterialGraphUniformBinding* parentTint = findTint(resolvedParent.material.graphProgram);
    const RenderMaterialGraphUniformBinding* instanceTint = findTint(resolvedInstance.material.graphProgram);
    Require(parentTint != nullptr && instanceTint != nullptr, "MAT-40 graph program binding must expose the tint uniform");
    Require(NearlyEqual(parentTint->value[0], 1.0F) && NearlyEqual(parentTint->value[1], 0.0F),
        "MAT-40 parent graph uniform must carry the parent tint value");
    Require(NearlyEqual(instanceTint->value[0], 0.0F) && NearlyEqual(instanceTint->value[1], 1.0F),
        "MAT-40 material instance override must update the tint uniform without changing program key");

    RenderMaterialInstanceAssetData invalid = instance;
    invalid.overrides.graphParameterValues.front().stableId = "missingTint";
    Require(RenderMaterialInstanceAssetWriter::Save(root / "Invalid_Inst.kbmatinst", invalid), "MAT-40 instance dynamic test could not save invalid instance");
    static_cast<void>(manager.DiscoverMountedAssets());
    const kb::assets::AssetMetadata* invalidMeta = manager.Registry().FindByPath("/Game/Invalid_Inst.kbmatinst");
    Require(invalidMeta != nullptr, "MAT-40 instance dynamic test did not discover invalid instance");
    const ResolvedRuntimeMaterialAsset invalidResolved = resolver.ResolveAsset(manager, *invalidMeta);
    Require(invalidResolved.status == RuntimeMaterialResolveStatus::ErrorMaterial &&
            std::ranges::any_of(invalidResolved.diagnostics, [](const RuntimeMaterialResolveDiagnostic& diagnostic) {
                return diagnostic.kind == RuntimeMaterialResolveDiagnosticKind::MaterialInstanceValidationFailed &&
                    diagnostic.message.find("unknown_override_parameter") != std::string::npos;
            }),
        "MAT-40 invalid material instance override must fail with typed validation diagnostics");

    std::filesystem::remove_all(root, error);
}

// LIB-140: proves RuntimeMaterialResolver::ResolveAssetWithParameterOverrides - the isolated
// resolution path RuntimeMaterialResourceEnsurer uses for a runtime kb::scene::MaterialInstance
// handle - actually changes the resolved material's roughness when a Scalar override is
// supplied (not just accepted-but-ignored), keeps a no-override resolve at the parent's own
// baked default, honestly falls back to the default material for an unresolvable parent, and
// rejects a RenderMaterialInstance parent as out of the LIB-140 v1 scope cut (error material).
void RunRuntimeMaterialResolverResolvesInstanceParameterOverridesTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_runtime_material_instance_scalar_override";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "MAT-140 instance override test could not create temp root");

    RenderMaterialAssetData parent{};
    parent.graph = MakeDefaultRenderMaterialGraphDocument();
    parent.graph.nodes.push_back(MakeGraphNode(2U, RenderMaterialGraphNodeKind::ParameterScalar, "roughnessOverride", "0.1"));
    parent.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ParameterScalar, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"));
    Require(RenderMaterialAssetWriter::Save(root / "Parent.kbmat", parent), "MAT-140 instance override test could not save parent material");

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "MAT-140 instance override test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialInstanceAssetLoader>()), "MAT-140 instance override test could not register instance loader");
    Require(manager.Mounts().Mount("Game", root), "MAT-140 instance override test could not mount asset root");
    Require(manager.DiscoverMountedAssets() == 1U, "MAT-140 instance override test did not discover parent material");
    const kb::assets::AssetMetadata* parentMeta = manager.Registry().FindByPath("/Game/Parent.kbmat");
    Require(parentMeta != nullptr, "MAT-140 instance override test did not find parent material metadata");

    RuntimeMaterialResolver resolver;

    const ResolvedRuntimeMaterialAsset withoutOverride = resolver.ResolveAssetWithParameterOverrides(manager, parentMeta->id, {});
    Require(withoutOverride.resolved && withoutOverride.status == RuntimeMaterialResolveStatus::Resolved,
        "MAT-140: parent material with no overrides must resolve cleanly");
    Require(NearlyEqual(withoutOverride.material.desc.roughnessFactor, 0.1F),
        "MAT-140: parent material with no overrides must keep its baked roughness default");

    const std::vector<RenderMaterialGraphParameterValue> overrides{
        RenderMaterialGraphParameterValue{
            .stableId = "roughnessOverride",
            .type = RenderMaterialParameterType::Scalar,
            .numbers = { 0.9F, 0.0F, 0.0F, 0.0F },
        },
    };
    const ResolvedRuntimeMaterialAsset withOverride = resolver.ResolveAssetWithParameterOverrides(manager, parentMeta->id, overrides);
    Require(withOverride.resolved && withOverride.status == RuntimeMaterialResolveStatus::Resolved,
        "MAT-140: parent material with a scalar override must resolve cleanly");
    Require(NearlyEqual(withOverride.material.desc.roughnessFactor, 0.9F),
        "MAT-140: a runtime MaterialInstance parameter override must change the resolved roughness value");

    const ResolvedRuntimeMaterialAsset missingParent = resolver.ResolveAssetWithParameterOverrides(manager, kb::assets::AssetId{ 999999U }, overrides);
    Require(missingParent.resolved && missingParent.status == RuntimeMaterialResolveStatus::DefaultMaterial,
        "MAT-140: an unresolvable parent material asset must fall back to the default material");

    RenderMaterialInstanceAssetData instanceAsset{};
    instanceAsset.parentMaterialAssetId = parentMeta->id;
    Require(RenderMaterialInstanceAssetWriter::Save(root / "Parent_Inst.kbmatinst", instanceAsset), "MAT-140 instance override test could not save a material instance parent fixture");
    Require(manager.DiscoverMountedAssets() == 2U, "MAT-140 instance override test did not discover the instance fixture");
    const kb::assets::AssetMetadata* instanceMeta = manager.Registry().FindByPath("/Game/Parent_Inst.kbmatinst");
    Require(instanceMeta != nullptr, "MAT-140 instance override test did not find the instance fixture metadata");
    const ResolvedRuntimeMaterialAsset instanceParent = resolver.ResolveAssetWithParameterOverrides(manager, instanceMeta->id, overrides);
    Require(instanceParent.resolved && instanceParent.status == RuntimeMaterialResolveStatus::ErrorMaterial,
        "MAT-140: a RenderMaterialInstance parent must be rejected as out of scope (error material), not silently accepted");

    std::filesystem::remove_all(root, error);
}

// LIB-140: end-to-end through Renderer::SubmitScene - proves a live kb::scene::MaterialInstance
// handle on a MeshRendererComponent actually drives material resolution (materialLoadedCount
// increments once for the first submit), that a SetParameterScalar call between frames is
// detected and reloads the bound material (materialReloadCount increments), and that
// resubmitting with no further change is a cache hit (no further reload).
void RunRendererAppliesMaterialInstanceParameterOverrideTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_material_instance_override";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "MAT-140 renderer override test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    WriteTriangleObj(meshPath);

    RenderMaterialAssetData parent{};
    parent.graph = MakeDefaultRenderMaterialGraphDocument();
    parent.graph.nodes.push_back(MakeGraphNode(2U, RenderMaterialGraphNodeKind::ParameterScalar, "roughnessOverride", "0.1"));
    parent.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ParameterScalar, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"));
    Require(RenderMaterialAssetWriter::Save(root / "Parent.kbmat", parent), "MAT-140 renderer override test could not save parent material");

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "MAT-140 renderer override test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "MAT-140 renderer override test could not register material loader");
    Require(manager.Mounts().Mount("Game", root), "MAT-140 renderer override test could not mount asset root");
    Require(manager.DiscoverMountedAssets() == 2U, "MAT-140 renderer override test did not discover mesh and material assets");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/Parent.kbmat");
    Require(meshMetadata != nullptr && materialMetadata != nullptr, "MAT-140 renderer override test discovered wrong metadata");

    const std::uint64_t instance = scene.MaterialInstances().Create(materialMetadata->id.value);
    Require(instance != 0U, "MAT-140 renderer override test could not create a material instance");

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Instance Override Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
        .materialInstanceHandle = instance,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "MAT-140 renderer override test renderer did not initialize");
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

    Require(renderer.BeginFrame(), "MAT-140 renderer override test renderer did not begin first frame");
    Require(renderer.SubmitScene(scene, desc), "MAT-140 renderer override test renderer did not submit first frame");
    const Renderer::RuntimeSceneResourceStats afterFirstSubmit = renderer.RuntimeResourceStats();
    Require(afterFirstSubmit.materialLoadedCount == 1U, "MAT-140: the first submit must resolve and load the instance's material exactly once");
    Require(afterFirstSubmit.materialReloadCount == 0U, "MAT-140: the first submit is not a reload");
    Require(afterFirstSubmit.materialErrorCount == 0U, "MAT-140: the first submit must not report a material error");
    renderer.EndFrame();

    Require(scene.MaterialInstances().SetParameterScalar(instance, "roughnessOverride", 0.9F),
        "MAT-140 renderer override test could not set a parameter override");
    Require(renderer.BeginFrame(), "MAT-140 renderer override test renderer did not begin second frame");
    Require(renderer.SubmitScene(scene, desc), "MAT-140 renderer override test renderer did not submit second frame");
    const Renderer::RuntimeSceneResourceStats afterOverride = renderer.RuntimeResourceStats();
    Require(afterOverride.materialReloadCount == 1U,
        "MAT-140: changing a material instance parameter must invalidate the cache and reload the bound material");
    renderer.EndFrame();

    Require(renderer.BeginFrame(), "MAT-140 renderer override test renderer did not begin third frame");
    Require(renderer.SubmitScene(scene, desc), "MAT-140 renderer override test renderer did not submit third frame");
    const Renderer::RuntimeSceneResourceStats afterStableResubmit = renderer.RuntimeResourceStats();
    // Renderer::SubmitScene(s) resets these stats to zero at the start of every call (see
    // Renderer::SubmitScenes), so "no reload this frame" is materialReloadCount == 0, not a
    // carried-over count from the previous frame's genuine reload.
    Require(afterStableResubmit.materialReloadCount == 0U,
        "MAT-140: resubmitting with no further parameter change must be a cache hit, not another reload");
    Require(afterStableResubmit.materialErrorCount == 0U, "MAT-140: a stable resubmit must not report a material error");
    renderer.EndFrame();

    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

// LIB-142: proves PostProcessProfileAssetLoader::SaveProfile/LoadProfile round-trips
// ScenePostProcessSettings through the real on-disk text format - representative fields
// across the flat top-level struct, the nested outputTransform, and the doubly-nested
// autoExposure, plus a non-default value for every enum.
void RunPostProcessProfileAssetSaveLoadRoundTripTest() {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "21kb_post_process_profile_round_trip.kbppfx";
    std::error_code removeError;
    std::filesystem::remove(path, removeError);

    ScenePostProcessSettings settings{};
    settings.bloomEnabled = false;
    settings.bloomStrength = 0.42F;
    settings.bloomThreshold = 1.8F;
    settings.bloomSoftKnee = 0.3F;
    settings.bloomRadiusPixels = 2.5F;
    settings.temporalAntiAliasingEnabled = false;
    settings.temporalJitterEnabled = false;
    settings.temporalHistoryBlend = 0.5F;
    settings.fxaaEnabled = true;
    settings.tonemapEnabled = false;
    settings.autoExposureMetering = ScenePostProcessSettings::AutoExposureMeteringMode::Manual;
    settings.outputTransform.exposureStops = 1.25F;
    settings.outputTransform.gamma = 2.4F;
    settings.outputTransform.tonemap = FullscreenTextureTonemapOperator::AgxApprox;
    settings.outputTransform.colorGradingLutStrength = 0.75F;
    settings.outputTransform.autoExposure.enabled = false;
    settings.outputTransform.autoExposure.meteredAverageLuminance = 0.3F;
    settings.outputTransform.autoExposure.middleGray = 0.22F;
    settings.outputTransform.autoExposure.minExposureStops = -4.0F;
    settings.outputTransform.autoExposure.maxExposureStops = 6.0F;
    settings.outputTransform.autoExposure.biasStops = 0.5F;
    settings.outputTransform.autoExposure.temporalAdaptationEnabled = false;
    settings.outputTransform.autoExposure.brightAdaptationRate = 3.0F;
    settings.outputTransform.autoExposure.darkAdaptationRate = 1.0F;

    Require(PostProcessProfileAssetLoader::SaveProfile(path, settings), "PostProcess profile asset save failed");
    const std::optional<ScenePostProcessSettings> loaded = PostProcessProfileAssetLoader::LoadProfile(path);
    Require(loaded.has_value(), "PostProcess profile asset did not load");
    Require(!loaded->bloomEnabled && NearlyEqual(loaded->bloomStrength, 0.42F) && NearlyEqual(loaded->bloomThreshold, 1.8F) &&
            NearlyEqual(loaded->bloomSoftKnee, 0.3F) && NearlyEqual(loaded->bloomRadiusPixels, 2.5F),
        "PostProcess profile asset round trip lost bloom fields");
    Require(!loaded->temporalAntiAliasingEnabled && !loaded->temporalJitterEnabled && NearlyEqual(loaded->temporalHistoryBlend, 0.5F) && loaded->fxaaEnabled,
        "PostProcess profile asset round trip lost anti-aliasing fields");
    Require(!loaded->tonemapEnabled && loaded->autoExposureMetering == ScenePostProcessSettings::AutoExposureMeteringMode::Manual,
        "PostProcess profile asset round trip lost tonemapEnabled/autoExposureMetering");
    Require(NearlyEqual(loaded->outputTransform.exposureStops, 1.25F) && NearlyEqual(loaded->outputTransform.gamma, 2.4F) &&
            loaded->outputTransform.tonemap == FullscreenTextureTonemapOperator::AgxApprox && NearlyEqual(loaded->outputTransform.colorGradingLutStrength, 0.75F),
        "PostProcess profile asset round trip lost outputTransform fields");
    Require(!loaded->outputTransform.autoExposure.enabled && NearlyEqual(loaded->outputTransform.autoExposure.meteredAverageLuminance, 0.3F) &&
            NearlyEqual(loaded->outputTransform.autoExposure.middleGray, 0.22F) && NearlyEqual(loaded->outputTransform.autoExposure.minExposureStops, -4.0F) &&
            NearlyEqual(loaded->outputTransform.autoExposure.maxExposureStops, 6.0F) && NearlyEqual(loaded->outputTransform.autoExposure.biasStops, 0.5F) &&
            !loaded->outputTransform.autoExposure.temporalAdaptationEnabled && NearlyEqual(loaded->outputTransform.autoExposure.brightAdaptationRate, 3.0F) &&
            NearlyEqual(loaded->outputTransform.autoExposure.darkAdaptationRate, 1.0F),
        "PostProcess profile asset round trip lost outputTransform.autoExposure fields");

    const std::optional<ScenePostProcessSettings> missing = PostProcessProfileAssetLoader::LoadProfile(
        std::filesystem::temp_directory_path() / "21kb_post_process_profile_does_not_exist.kbppfx");
    Require(!missing.has_value(), "PostProcess profile asset load must honestly fail for a nonexistent file, not return defaults");

    std::filesystem::remove(path, removeError);
}

// LIB-142: end-to-end through Renderer::SubmitScene - proves a scene's asset-based active
// PostProcessProfile actually drives the resolved post-process settings when the caller
// supplies no explicit per-submit override, and that an explicit per-submit override still
// wins over the scene's profile (the override is purely additive, not a new precedence rule
// - see Renderer.cpp's ResolveScenePostProcessProfile doc comment).
void RunRendererAppliesScenePostProcessProfileTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_post_process_profile";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Post process profile renderer test could not create temp root");

    ScenePostProcessSettings profileSettings{};
    profileSettings.bloomEnabled = true;
    profileSettings.bloomStrength = 0.77F;
    profileSettings.fxaaEnabled = true;
    profileSettings.tonemapEnabled = false;
    Require(PostProcessProfileAssetLoader::SaveProfile(root / "Profile.kbppfx", profileSettings), "Post process profile renderer test could not save profile asset");

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<PostProcessProfileAssetLoader>()), "Post process profile renderer test could not register loader");
    Require(manager.Mounts().Mount("Game", root), "Post process profile renderer test could not mount asset root");
    Require(manager.DiscoverMountedAssets() == 1U, "Post process profile renderer test did not discover the profile asset");
    const kb::assets::AssetMetadata* profileMetadata = manager.Registry().FindByPath("/Game/Profile.kbppfx");
    Require(profileMetadata != nullptr, "Post process profile renderer test did not find profile metadata");

    kb::scene::ScenePostProcessAccess::SetActiveProfile(scene, profileMetadata->id.value);

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "Post process profile renderer test renderer did not initialize");
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

    Require(renderer.BeginFrame(), "Post process profile renderer test renderer did not begin first frame");
    Require(renderer.SubmitScene(scene, desc), "Post process profile renderer test renderer did not submit first frame");
    const std::optional<ScenePostProcessSettings> withProfile = renderer.LastResolvedPostProcessSettings();
    Require(withProfile.has_value() && NearlyEqual(withProfile->bloomStrength, 0.77F) && withProfile->fxaaEnabled && !withProfile->tonemapEnabled,
        "Renderer did not apply the scene's active PostProcessProfile when no explicit submit override was supplied");
    renderer.EndFrame();

    RenderSceneSubmitDesc overriddenDesc = desc;
    overriddenDesc.postProcessSettings = ScenePostProcessSettings{
        .bloomEnabled = true,
        .bloomStrength = 0.11F,
        .fxaaEnabled = false,
        .tonemapEnabled = true,
    };
    Require(renderer.BeginFrame(), "Post process profile renderer test renderer did not begin second frame");
    Require(renderer.SubmitScene(scene, overriddenDesc), "Post process profile renderer test renderer did not submit second frame");
    const std::optional<ScenePostProcessSettings> withExplicitOverride = renderer.LastResolvedPostProcessSettings();
    Require(withExplicitOverride.has_value() && NearlyEqual(withExplicitOverride->bloomStrength, 0.11F) && !withExplicitOverride->fxaaEnabled && withExplicitOverride->tonemapEnabled,
        "An explicit per-submit postProcessSettings override must still win over the scene's active PostProcessProfile");
    renderer.EndFrame();

    kb::scene::ScenePostProcessAccess::SetActiveProfile(scene, 0U);
    Require(renderer.BeginFrame(), "Post process profile renderer test renderer did not begin third frame");
    Require(renderer.SubmitScene(scene, desc), "Post process profile renderer test renderer did not submit third frame");
    Require(!renderer.LastResolvedPostProcessSettings().has_value(),
        "With neither an explicit submit override nor an active scene profile, the renderer must honestly resolve to no override, not stale state from a previous frame");
    renderer.EndFrame();

    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunRuntimeMaterialInstanceStaticBaseOverrideChainTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_runtime_material_instance_static_base_chain";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "MAT-47 static/base chain test could not create temp root");

    RenderMaterialAssetData parent{};
    parent.graph = MakeDefaultRenderMaterialGraphDocument();
    parent.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::StaticBoolParameter,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "useRed", .displayName = "Use Red", .defaultValueHint = "true" },
    });
    parent.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 0 0 1" },
    });
    parent.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 4U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0 0 1 1" },
    });
    parent.graph.nodes.push_back(RenderMaterialGraphNode{ .id = 5U, .kind = RenderMaterialGraphNodeKind::StaticSwitch });
    parent.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::StaticBoolParameter, 2U, "value", RenderMaterialGraphNodeKind::StaticSwitch, 5U, "value"));
    parent.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 3U, "rgba", RenderMaterialGraphNodeKind::StaticSwitch, 5U, "true"));
    parent.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 4U, "rgba", RenderMaterialGraphNodeKind::StaticSwitch, 5U, "false"));
    parent.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::StaticSwitch, 5U, "result", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    Require(RenderMaterialAssetWriter::Save(root / "Parent.kbmat", parent), "MAT-47 static/base chain test could not save parent material");

    kb::assets::AssetManager manager;
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "MAT-47 static/base chain test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialInstanceAssetLoader>()), "MAT-47 static/base chain test could not register instance loader");
    Require(manager.Mounts().Mount("Game", root), "MAT-47 static/base chain test could not mount asset root");
    Require(manager.DiscoverMountedAssets() == 1U, "MAT-47 static/base chain test did not discover parent material");
    const kb::assets::AssetMetadata* parentMeta = manager.Registry().FindByPath("/Game/Parent.kbmat");
    Require(parentMeta != nullptr && parentMeta->type == "RenderMaterial", "MAT-47 static/base chain test did not find parent material metadata");

    RenderMaterialInstanceAssetData mid{};
    mid.parentMaterialAssetId = parentMeta->id;
    mid.staticParameterOverrides.push_back(RenderMaterialInstanceStaticParameterOverride{
        .stableId = "useRed",
        .nodeKind = RenderMaterialGraphNodeKind::StaticBoolParameter,
        .value = "false",
    });
    mid.basePropertyOverrides.overrideBlendMode = true;
    mid.basePropertyOverrides.blendMode = RenderMaterialGraphBlendMode::Additive;
    mid.basePropertyOverrides.overrideShadingModel = true;
    mid.basePropertyOverrides.shadingModel = RenderMaterialShadingModel::Unlit;
    mid.basePropertyOverrides.overrideTwoSided = true;
    mid.basePropertyOverrides.twoSided = true;
    mid.basePropertyOverrides.overrideOpacityMaskClip = true;
    mid.basePropertyOverrides.opacityMaskClip = 0.33F;
    Require(RenderMaterialInstanceAssetWriter::Save(root / "Mid.kbmatinst", mid), "MAT-47 static/base chain test could not save mid instance");
    Require(manager.DiscoverMountedAssets() == 2U, "MAT-47 static/base chain test did not discover mid instance");
    const kb::assets::AssetMetadata* midMeta = manager.Registry().FindByPath("/Game/Mid.kbmatinst");
    Require(midMeta != nullptr && midMeta->type == "RenderMaterialInstance", "MAT-47 static/base chain test did not find mid metadata");

    RenderMaterialInstanceAssetData child{};
    child.parentMaterialAssetId = midMeta->id;
    Require(RenderMaterialInstanceAssetWriter::Save(root / "Child.kbmatinst", child), "MAT-47 static/base chain test could not save child instance");
    Require(manager.DiscoverMountedAssets() == 3U, "MAT-47 static/base chain test did not discover child instance");
    parentMeta = manager.Registry().FindByPath("/Game/Parent.kbmat");
    midMeta = manager.Registry().FindByPath("/Game/Mid.kbmatinst");
    const kb::assets::AssetMetadata* childMeta = manager.Registry().FindByPath("/Game/Child.kbmatinst");
    Require(parentMeta != nullptr && midMeta != nullptr && childMeta != nullptr, "MAT-47 static/base chain test lost discovered metadata");

    RuntimeMaterialResolver resolver;
    const ResolvedRuntimeMaterialAsset resolvedParent = resolver.ResolveAsset(manager, *parentMeta);
    const ResolvedRuntimeMaterialAsset resolvedMid = resolver.ResolveAsset(manager, *midMeta);
    const ResolvedRuntimeMaterialAsset resolvedChild = resolver.ResolveAsset(manager, *childMeta);
    Require(resolvedParent.resolved && resolvedParent.renderMode == RuntimeMaterialRenderMode::GpuMaterialGraph,
        "MAT-47 parent graph material must resolve to a GPU graph");
    Require(resolvedMid.resolved, "MAT-47 mid instance static override did not resolve");
    Require(resolvedMid.material.graphProgram.active, "MAT-47 mid instance static override did not bind an active graph program");
    Require(resolvedMid.renderMode == RuntimeMaterialRenderMode::GpuMaterialGraph,
        "MAT-47 mid instance static override did not report GPU graph render mode");
    Require(resolvedChild.resolved, "MAT-47 child instance did not resolve");
    Require(resolvedChild.material.graphProgram.active, "MAT-47 child instance did not bind an inherited graph program");
    Require(resolvedChild.renderMode == RuntimeMaterialRenderMode::GpuMaterialGraph,
        "MAT-47 child instance did not report inherited GPU graph render mode");
    Require(resolvedParent.material.graphProgram.graphSourceHash != resolvedMid.material.graphProgram.graphSourceHash,
        "MAT-47 static override must produce a different runtime graph variant key");
    Require(resolvedMid.material.graphProgram.graphSourceHash == resolvedChild.material.graphProgram.graphSourceHash,
        "MAT-47 child instance did not inherit the static variant key from its parent instance");
    Require(resolvedChild.material.graphProgram.alphaMode == RenderMaterialAlphaMode::Blend &&
            resolvedChild.material.graphProgram.translucencyBlend == RenderMaterialTranslucencyBlend::Additive,
        "MAT-47 blendMode override did not propagate to the child graph render state");
    Require(resolvedChild.material.desc.doubleSided && NearlyEqual(resolvedChild.material.desc.alphaCutoff, 0.33F),
        "MAT-47 base property overrides did not propagate through the instance chain");
    Require(resolvedChild.contentHash != childMeta->contentHash,
        "MAT-47 child runtime content hash must include parent instance/material content");

    const std::uint64_t childRuntimeHashBeforeParentReload =
        RuntimeMaterialResolver::MaterialRuntimeContentHash(manager, *childMeta);
    parent.desc.roughnessFactor = 0.42F;
    Require(RenderMaterialAssetWriter::Save(root / "Parent.kbmat", parent), "MAT-47 static/base chain test could not rewrite parent material");
    Require(manager.DiscoverMountedAssets() == 3U, "MAT-47 static/base chain test did not rediscover parent material reload");
    childMeta = manager.Registry().FindByPath("/Game/Child.kbmatinst");
    Require(childMeta != nullptr && childMeta->type == "RenderMaterialInstance", "MAT-47 static/base chain test lost child metadata after parent reload");
    const std::uint64_t childRuntimeHashAfterParentReload =
        RuntimeMaterialResolver::MaterialRuntimeContentHash(manager, *childMeta);
    Require(childRuntimeHashAfterParentReload != childRuntimeHashBeforeParentReload,
        "MAT-47 parent reload must invalidate the child instance runtime material hash through the chain");

    std::filesystem::remove_all(root, error);
}

void RunRendererBindsGraphMaterialGpuProgramTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_graph_gpu_program";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Graph GPU program submit test could not create temp root");

    WriteTriangleObj(root / "triangle.obj");

    RenderMaterialAssetData gpuMaterial{};
    gpuMaterial.graph = MakeDefaultRenderMaterialGraphDocument();
    gpuMaterial.graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .positionX = -160, .positionY = 64 });
    gpuMaterial.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    Require(RenderMaterialAssetWriter::Save(root / "graph.kbmat", gpuMaterial), "Graph GPU program submit test could not save graph material");

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Graph GPU program submit test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Graph GPU program submit test could not register material loader");
    Require(manager.Mounts().Mount("Game", root), "Graph GPU program submit test could not mount asset root");
    Require(manager.DiscoverMountedAssets() == 2U, "Graph GPU program submit test did not discover mesh and material");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/graph.kbmat");
    Require(meshMetadata != nullptr && materialMetadata != nullptr, "Graph GPU program submit test did not discover both assets");

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Graph Material Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
        .materialAssetId = materialMetadata->id.value,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "Graph GPU program submit test renderer did not initialize");
    Require(renderer.BeginFrame(), "Graph GPU program submit test renderer did not begin frame");
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
    Require(renderer.SubmitScene(scene, desc), "Graph GPU program submit test renderer did not submit scene");

    const Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
    // Finding 3: this graph material is saved but never cooked, so no GPU program binary exists.
    // graphMaterialGpuCount/CpuFallback report the ACTUAL draw outcome (not the resolve-time renderMode
    // intent), so a material that renders the builtin flatten because its cooked binary is missing must
    // be counted as a CPU fallback, NOT as a live GPU graph material. (MAT-31 covers the cooked GPU path.)
    Require(runtimeStats.graphMaterialGpuCount == 0U, "MAT-26: A graph material with no cooked GPU binary must NOT be counted as a GPU material (it renders the builtin flatten)");
    Require(runtimeStats.graphMaterialCpuFallbackCount == 1U, "MAT-26: A graph material that falls back to the builtin flatten at draw must count as exactly one CPU fallback");
    Require(runtimeStats.materialErrorCount == 0U, "MAT-26: A valid but uncooked graph material must still resolve (not an error material)");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

#if defined(KB_TEST_GRAPH_SHADERC_PATH)
// MAT-31: the public Renderer::SetGraphShaderCacheRoot must reach the mesh pass resources so a
// cooked graph binary is loaded as the bound program (not the builtin fallback). This proves the
// full forwarding chain Renderer -> SceneRenderer -> SceneMeshSubmitter -> SceneMeshPassResources.
void RunRendererPublicGraphShaderCacheRootBindsCookedProgramTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_public_cache_root";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "MAT-31 public cache root test could not create temp root");

    WriteTriangleObj(root / "triangle.obj");

    RenderMaterialAssetData gpuMaterial{};
    gpuMaterial.graph = MakeDefaultRenderMaterialGraphDocument();
    gpuMaterial.graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .positionX = -160, .positionY = 64 });
    gpuMaterial.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    RenderMaterialGraphNode transitionUniform{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ParameterScalar,
        .positionX = -160,
        .positionY = 220,
    };
    transitionUniform.parameter.stableId = "transition.roughness";
    transitionUniform.parameter.defaultValueHint = "0.25";
    gpuMaterial.graph.nodes.push_back(std::move(transitionUniform));
    gpuMaterial.graph.links.push_back(MakeGraphLink(
        RenderMaterialGraphNodeKind::ParameterScalar,
        3U,
        "value",
        RenderMaterialGraphNodeKind::MaterialOutput,
        1U,
        "roughness"));
    Require(RenderMaterialAssetWriter::Save(root / "graph.kbmat", gpuMaterial), "MAT-31 public cache root test could not save graph material");

    // Cook the graph's BaseOpaque binary for the headless Noop renderer (dxbc directory) into a
    // dedicated cache root that we will hand to the renderer through its public setter.
    const std::filesystem::path cacheRoot = root / "graph_shaders";
    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(gpuMaterial.graph, RenderMaterialGraphBuildContext{ .assetId = 0x3100U });
    Require(compiled.Succeeded(), "MAT-31 public cache root test graph must compile");
    RenderMaterialGraphShaderArtifactRequest request{};
    request.shadercPath = KB_TEST_GRAPH_SHADERC_PATH;
    request.varyingDefPath = KB_TEST_GRAPH_SHADER_VARYING_DEF;
    request.includeDirs = { KB_TEST_GRAPH_SHADER_INCLUDE_DIR, KB_TEST_GRAPH_BGFX_SHADER_INCLUDE_DIR };
    request.cacheRoot = cacheRoot.generic_string();
    request.pass = "BaseOpaque";
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };
    Require(CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, request).Succeeded(),
        "MAT-31 public cache root test must cook a DXBC graph binary");

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "MAT-31 public cache root test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "MAT-31 public cache root test could not register material loader");
    Require(manager.Mounts().Mount("Game", root), "MAT-31 public cache root test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 2U, "MAT-31 public cache root test did not discover mesh and material");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/graph.kbmat");
    Require(meshMetadata != nullptr && materialMetadata != nullptr, "MAT-31 public cache root test did not discover both assets");

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Cooked Graph Material Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
        .materialAssetId = materialMetadata->id.value,
    });

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    // Set the cache root BEFORE Initialize to exercise the deferred-apply path added in MAT-31.
    renderer.SetGraphShaderCacheRoot(cacheRoot.generic_string());
    Require(renderer.GraphShaderCacheRoot() == cacheRoot.generic_string(),
        "MAT-31: Renderer must retain the graph shader cache root through its public setter");
    Require(renderer.Initialize(surface, &config), "MAT-31 public cache root test renderer did not initialize");
    Require(renderer.BeginFrame(), "MAT-31 public cache root test renderer did not begin frame");
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
    Require(renderer.SubmitScene(scene, desc), "MAT-31 public cache root test renderer did not submit scene");

    const Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
    Require(runtimeStats.graphMaterialGpuCount == 1U, "MAT-31: A cooked graph material must resolve to the GPU graph path");
    const MaterialProgramRegistryStats programStats = renderer.MaterialProgramStats();
    Require(programStats.loads >= 1U,
        "MAT-31: Setting the cache root through the public renderer must load the cooked graph program from disk");
    Require(programStats.failures == 0U,
        "MAT-31: A present cooked graph binary must load without a program failure/fallback");
    Require(programStats.liveProgramCount >= 1U,
        "MAT-31: The cooked graph program must be retained live in the material program registry");

    renderer.EndFrame();

    // Regression for Material Editor -> Scene View: the graph frame has a reflected, graph-only
    // numeric uniform in flight. Render an empty frame, return to the graph during the grace
    // period, then leave it long enough to retire resources. Before the deferred-uniform fix,
    // EndFrame destroyed the renderer-side uniform slot immediately and the render thread crashed
    // in RendererContextD3D12::updateUniform while consuming the previous frame.
    kb::scene::Scene emptyScene;
    const auto submitTransitionFrame = [&](const kb::scene::Scene& submittedScene, const char* failure) {
        Require(renderer.BeginFrame(), failure);
        Require(renderer.SubmitScene(submittedScene, desc), failure);
        renderer.EndFrame();
    };
    submitTransitionFrame(emptyScene, "Material graph transition test could not submit the first Scene View frame");
    submitTransitionFrame(scene, "Material graph transition test could not reactivate the graph preview frame");
    submitTransitionFrame(emptyScene, "Material graph transition test could not submit retirement frame one");
    submitTransitionFrame(emptyScene, "Material graph transition test could not submit retirement frame two");
    submitTransitionFrame(emptyScene, "Material graph transition test could not submit retirement frame three");
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

// MAT-84/85: a scene that references several distinct graph materials (none of them "open" in an
// editor) must render each through its own cooked GPU program once the cache root is supplied.
void RunRendererSceneRendersMultipleCookedGraphMaterialsTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_scene_multi_graph";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "MAT-84 scene multi-graph test could not create temp root");

    WriteTriangleObj(root / "triangle.obj");
    const std::filesystem::path cacheRoot = root / "graph_shaders";

    const std::array<std::string_view, 3U> colors{ "0.9 0.1 0.1 1", "0.1 0.9 0.1 1", "0.1 0.1 0.9 1" };
    std::array<std::string, 3U> materialFiles{ "red.kbmat", "green.kbmat", "blue.kbmat" };
    for (std::size_t index = 0U; index < colors.size(); ++index) {
        RenderMaterialAssetData material{};
        material.graph = MakeDefaultRenderMaterialGraphDocument();
        material.graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .positionX = -160, .positionY = 64 });
        material.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        material.graph.nodes[1].parameter.defaultValueHint = std::string{ colors[index] };
        Require(RenderMaterialAssetWriter::Save(root / materialFiles[index], material), "MAT-84 scene multi-graph test could not save a graph material");

        const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(material.graph, RenderMaterialGraphBuildContext{ .assetId = 0x8400U + index });
        Require(compiled.Succeeded(), "MAT-84 scene multi-graph test material must compile");
        RenderMaterialGraphShaderArtifactRequest request{};
        request.shadercPath = KB_TEST_GRAPH_SHADERC_PATH;
        request.varyingDefPath = KB_TEST_GRAPH_SHADER_VARYING_DEF;
        request.includeDirs = { KB_TEST_GRAPH_SHADER_INCLUDE_DIR, KB_TEST_GRAPH_BGFX_SHADER_INCLUDE_DIR };
        request.cacheRoot = cacheRoot.generic_string();
        request.pass = "BaseOpaque";
        const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };
        Require(CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, request).Succeeded(),
            "MAT-84 scene multi-graph test must cook each graph material");
    }

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "MAT-84 scene multi-graph test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "MAT-84 scene multi-graph test could not register material loader");
    Require(manager.Mounts().Mount("Game", root), "MAT-84 scene multi-graph test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 4U, "MAT-84 scene multi-graph test did not discover mesh and materials");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    Require(meshMetadata != nullptr, "MAT-84 scene multi-graph test did not discover the mesh");

    for (std::size_t index = 0U; index < materialFiles.size(); ++index) {
        const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath(std::string{ "/Game/" } + materialFiles[index]);
        Require(materialMetadata != nullptr, "MAT-84 scene multi-graph test did not discover a graph material");
        const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
            .name = "Graph Mesh " + std::to_string(index),
            // Keep every mesh inside the identity camera's clip volume ([-1,1]) so all three actually
            // DRAW — graphMaterialGpuCount now reflects the draw-time outcome, so a mesh culled off
            // screen would (correctly) not count as a rendered GPU material.
            .transform = TransformAt((static_cast<float>(index) - 1.0F) * 0.3F, 0.0F, 0.0F),
        });
        scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
            .meshAssetId = meshMetadata->id.value,
            .materialAssetId = materialMetadata->id.value,
        });
    }

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.SetGraphShaderCacheRoot(cacheRoot.generic_string());
    Require(renderer.Initialize(surface, &config), "MAT-84 scene multi-graph test renderer did not initialize");
    Require(renderer.BeginFrame(), "MAT-84 scene multi-graph test renderer did not begin frame");
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
    Require(renderer.SubmitScene(scene, desc), "MAT-84 scene multi-graph test renderer did not submit scene");

    const Renderer::RuntimeSceneResourceStats runtimeStats = renderer.RuntimeResourceStats();
    Require(runtimeStats.graphMaterialGpuCount == 3U,
        "MAT-84: A scene with three distinct graph materials must bind three GPU graph programs without opening them");
    Require(runtimeStats.graphMaterialCpuFallbackCount == 0U,
        "MAT-84: Cooked scene graph materials must not fall back to CPU flattening");
    const MaterialProgramRegistryStats programStats = renderer.MaterialProgramStats();
    Require(programStats.liveProgramCount >= 3U,
        "MAT-85: Three distinct cooked graph materials must retain three distinct live programs");
    Require(programStats.failures == 0U,
        "MAT-85: Present cooked scene graph binaries must all load without a program failure");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}
#endif

// LIB-144: the end-to-end proof that a real Renderer::SubmitScene publishes the CPU-side
// per-entity visibility/bounds feedback frame into the scene (SceneRenderFeedback) - real
// mesh asset on disk, real headless Noop submit, real bounds resolved from the mesh
// resource, real frustum cull against the camera the submit rendered with, zero GPU
// readback anywhere. IdentityCamera()'s identity view*projection extracts the NDC unit cube
// as the frustum, so |x|,|y|,|z| <= 1 is inside.
void RunRendererPublishesSceneVisibilityFeedbackTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_visibility_feedback";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Visibility feedback test could not create temp root");
    const std::filesystem::path meshPath = root / "triangle.obj";
    WriteTriangleObj(meshPath);

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Visibility feedback test could not register mesh loader");
    Require(manager.Mounts().Mount("Game", root), "Visibility feedback test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 1U, "Visibility feedback test did not discover the mesh asset");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Visibility feedback test discovered wrong mesh metadata");
    const std::uint64_t meshAssetId = meshMetadata->id.value;

    const kb::scene::SceneEntity onScreenEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "On Screen Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(onScreenEntity, kb::scene::MeshRendererComponent{ .meshAssetId = meshAssetId });
    const kb::scene::SceneEntity offScreenEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Off Screen Mesh",
        .transform = TransformAt(100.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(offScreenEntity, kb::scene::MeshRendererComponent{ .meshAssetId = meshAssetId });
    const kb::scene::SceneEntity hiddenEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Hidden Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(hiddenEntity, kb::scene::MeshRendererComponent{ .meshAssetId = meshAssetId });
    scene.Components().Visibility().Set(hiddenEntity, kb::scene::VisibilityComponent{ .visible = false });
    const kb::scene::SceneEntity meshlessEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "No Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });

    Require(!kb::scene::SceneRenderFeedback::HasFrame(scene), "A never-submitted scene must not report a published visibility frame");

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "Visibility feedback test renderer did not initialize");
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

    Require(renderer.BeginFrame(), "Visibility feedback test renderer did not begin first frame");
    Require(renderer.SubmitScene(scene, desc), "Visibility feedback test renderer did not submit first frame");
    renderer.EndFrame();

    Require(kb::scene::SceneRenderFeedback::HasFrame(scene), "SubmitScene must publish a visibility feedback frame into the scene");
    Require(kb::scene::SceneRenderFeedback::PublishCount(scene) == 1U, "The first submit must publish exactly one visibility frame");
    Require(kb::scene::SceneRenderFeedback::IsVisible(scene, onScreenEntity), "An in-frustum, visible mesh entity must be reported visible");
    Require(!kb::scene::SceneRenderFeedback::IsVisible(scene, offScreenEntity), "A mesh entity far outside the camera frustum must be reported not visible");
    Require(!kb::scene::SceneRenderFeedback::IsVisible(scene, hiddenEntity), "A VisibilityComponent-disabled mesh entity must be reported not visible");
    Require(!kb::scene::SceneRenderFeedback::IsVisible(scene, meshlessEntity), "An entity with no MeshRenderer must have no visibility entry");

    // Bounds come from the real mesh resource (triangle.obj, ~0.14 world radius),
    // transformed by each entity's own model matrix - tracked even for culled/hidden
    // entities (bounds answer "where is it", not "was it drawn").
    const kb::scene::SceneRenderBounds onScreenBounds = kb::scene::SceneRenderFeedback::WorldBounds(scene, onScreenEntity);
    Require(onScreenBounds.IsValid() && onScreenBounds.radius > 0.05F && onScreenBounds.radius < 1.0F,
        "The on-screen entity's world bounds must carry the real mesh-resource bounding sphere");
    Require(std::abs(onScreenBounds.center.x) < 0.2F, "The on-screen entity's world bounds must be centered near its origin transform");
    const kb::scene::SceneRenderBounds offScreenBounds = kb::scene::SceneRenderFeedback::WorldBounds(scene, offScreenEntity);
    Require(offScreenBounds.IsValid() && std::abs(offScreenBounds.center.x - 100.0F) < 0.2F,
        "The off-screen entity's world bounds must be transformed by its own model matrix");
    Require(kb::scene::SceneRenderFeedback::WorldBounds(scene, hiddenEntity).IsValid(),
        "A hidden entity keeps valid bounds - bounds report placement, not draw status");
    Require(!kb::scene::SceneRenderFeedback::WorldBounds(scene, meshlessEntity).IsValid(),
        "An entity with no MeshRenderer must report invalid bounds");

    // The published frustum is queryable directly (identity clip = NDC unit cube).
    Require(kb::scene::SceneRenderFeedback::TestFrustum(scene, kb::math::Vec3{}, 0.0F), "TestFrustum must accept the origin inside the identity-camera frustum");
    Require(!kb::scene::SceneRenderFeedback::TestFrustum(scene, kb::math::Vec3{ 100.0F, 0.0F, 0.0F }, 0.0F), "TestFrustum must reject a point far outside the identity-camera frustum");
    Require(kb::scene::SceneRenderFeedback::TestFrustum(scene, kb::math::Vec3{ 1.5F, 0.0F, 0.0F }, 1.0F), "TestFrustum must accept a sphere straddling the identity-camera frustum boundary");

    // Every subsequent submit republishes - the feedback tracks the latest frame, never
    // frozen first-frame state.
    Require(renderer.BeginFrame(), "Visibility feedback test renderer did not begin second frame");
    Require(renderer.SubmitScene(scene, desc), "Visibility feedback test renderer did not submit second frame");
    renderer.EndFrame();
    Require(kb::scene::SceneRenderFeedback::PublishCount(scene) == 2U, "A second submit must publish a second visibility frame");
    Require(kb::scene::SceneRenderFeedback::IsVisible(scene, onScreenEntity), "The second published frame must still track the on-screen entity");

    // LIB-145: the published frame carries the submit camera, so the screen/world
    // conversions work end-to-end. IdentityCamera + 64x64 viewport: the world origin
    // projects to the viewport center, and the center pixel's ray runs along +Z.
    const kb::scene::SceneRenderScreenPoint projected = kb::scene::SceneRenderFeedback::WorldToScreen(scene, kb::math::Vec3{});
    Require(projected.valid && projected.onScreen && std::abs(projected.screenX - 32.0F) < 0.01F && std::abs(projected.screenY - 32.0F) < 0.01F,
        "WorldToScreen must project the world origin to the identity camera's viewport center after a real submit");
    const kb::scene::SceneRenderCameraRay centerRay = kb::scene::SceneRenderFeedback::ScreenPointToRay(scene, 32.0F, 32.0F);
    Require(centerRay.valid && std::abs(centerRay.ray.direction.z - 1.0F) < 0.01F,
        "ScreenPointToRay must build a forward ray through the identity camera's viewport center after a real submit");

    // LIB-145: async screen capture through a real submit. The headless Noop backend has
    // no TEXTURE_READ_BACK/TEXTURE_BLIT caps, so the honest terminal answer is Failed -
    // delivered through the request->consume->complete channel, never a forever-Pending
    // hang and never a fake success.
    const std::filesystem::path capturePath = root / "capture.png";
    const std::uint64_t captureId = kb::scene::SceneRenderFeedback::RequestScreenCapture(scene, capturePath.string());
    Require(captureId != 0U, "Visibility feedback test could not request a screen capture");
    Require(kb::scene::SceneRenderFeedback::ScreenCaptureStatus(scene, captureId) == kb::scene::SceneScreenCaptureStatus::Pending,
        "A requested capture must report Pending before the next submit");
    Require(renderer.BeginFrame(), "Visibility feedback test renderer did not begin capture frame");
    Require(renderer.SubmitScene(scene, desc), "Visibility feedback test renderer did not submit capture frame");
    renderer.EndFrame();
    Require(kb::scene::SceneRenderFeedback::ScreenCaptureStatus(scene, captureId) == kb::scene::SceneScreenCaptureStatus::Failed,
        "A capture that can never succeed under the Noop backend must be honestly completed as Failed on its submit");
    Require(!std::filesystem::exists(capturePath), "A failed capture must not leave a file behind");
    Require(kb::scene::SceneRenderFeedback::RequestScreenCapture(scene, capturePath.string()) != 0U,
        "A terminal capture result must free the single pending slot for the next request");

    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

// LIB-146: shared harness bits for the render-resource lifecycle tests below - a scene
// with one mesh entity backed by a real on-disk triangle.obj under its own asset root.
struct LifecycleSceneFixture {
    kb::scene::Scene scene;
    std::uint64_t meshAssetId = 0;
    kb::scene::SceneEntity entity{};
};

void PrepareLifecycleScene(LifecycleSceneFixture& fixture, const std::filesystem::path& root) {
    std::error_code error;
    std::filesystem::create_directories(root, error);
    Require(!error, "LIB-146 lifecycle test could not create asset root");
    WriteTriangleObj(root / "triangle.obj");
    kb::assets::AssetManager& manager = fixture.scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "LIB-146 lifecycle test could not register mesh loader");
    Require(manager.Mounts().Mount("Game", root), "LIB-146 lifecycle test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 1U, "LIB-146 lifecycle test did not discover the mesh asset");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "LIB-146 lifecycle test discovered wrong mesh metadata");
    fixture.meshAssetId = meshMetadata->id.value;
    fixture.entity = fixture.scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Lifecycle Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    fixture.scene.Components().MeshRenderers().Set(fixture.entity, kb::scene::MeshRendererComponent{ .meshAssetId = fixture.meshAssetId });
}

[[nodiscard]] RenderSceneSubmitDesc LifecycleSubmitDesc(std::uint32_t viewportId) {
    return RenderSceneSubmitDesc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ viewportId },
                .extent = RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
    };
}

void SubmitLifecycleFrame(Renderer& renderer, const kb::scene::Scene& scene, const RenderSceneSubmitDesc& desc, const char* failure) {
    Require(renderer.BeginFrame(), failure);
    Require(renderer.SubmitScene(scene, desc), failure);
    renderer.EndFrame();
}

// LIB-146 (scene unload): Renderer::ReleaseScene must destroy exactly the released
// scene's runtime GPU resources ({sceneId, assetId}-keyed isolation - a second live scene
// keeps its own), the released scene must remain fully re-submittable (release is a clean
// unload, not poisoning), and ReleaseAllScenes must drop everything.
void RunRendererReleaseSceneDropsRuntimeResourcesTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_release_scene";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    LifecycleSceneFixture first;
    LifecycleSceneFixture second;
    PrepareLifecycleScene(first, root / "a");
    PrepareLifecycleScene(second, root / "b");

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "LIB-146 release test renderer did not initialize");
    const RenderSceneSubmitDesc desc = LifecycleSubmitDesc(1U);

    SubmitLifecycleFrame(renderer, first.scene, desc, "LIB-146 release test did not submit the first scene");
    SubmitLifecycleFrame(renderer, second.scene, desc, "LIB-146 release test did not submit the second scene");
    Require(renderer.RuntimeResourceStats().cachedMeshCount == 2U, "Two submitted scenes must cache one mesh resource each");

    renderer.ReleaseScene(first.scene);
    Require(renderer.RuntimeResourceStats().cachedMeshCount == 1U, "ReleaseScene must destroy exactly the released scene's cached resources");
    SubmitLifecycleFrame(renderer, second.scene, desc, "LIB-146 release test did not resubmit the surviving scene");
    Require(renderer.LastSceneSubmitStats().visibleMeshCount > 0U, "Releasing one scene must not break another scene's rendering");
    Require(renderer.RuntimeResourceStats().cachedMeshCount == 1U, "The surviving scene's cache entry must be reused, not rebuilt");

    SubmitLifecycleFrame(renderer, first.scene, desc, "LIB-146 release test did not resubmit the released scene");
    Require(renderer.LastSceneSubmitStats().visibleMeshCount > 0U, "A released scene must be cleanly re-submittable");
    Require(renderer.RuntimeResourceStats().cachedMeshCount == 2U, "Resubmitting a released scene must re-ensure its resources");

    renderer.ReleaseAllScenes();
    Require(renderer.RuntimeResourceStats().cachedMeshCount == 0U && renderer.RuntimeResourceStats().cachedMaterialCount == 0U && renderer.RuntimeResourceStats().cachedTextureCount == 0U,
        "ReleaseAllScenes must drop every cached runtime resource");
    SubmitLifecycleFrame(renderer, first.scene, desc, "LIB-146 release test did not resubmit after ReleaseAllScenes");
    Require(renderer.LastSceneSubmitStats().visibleMeshCount > 0U, "ReleaseAllScenes must leave the renderer fully usable");

    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

// LIB-146 (entity destroy + retention eviction): destroying the last entity referencing a
// mesh removes its render proxy immediately, but the GPU resource stays cached BY DESIGN
// for kRuntimeAssetRetentionFrames (120) - and PruneUnused then actually evicts it. A new
// entity using the same asset after eviction re-ensures the resource from scratch.
void RunRendererPrunesUnreferencedResourcesAfterRetentionTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_retention_prune";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    LifecycleSceneFixture fixture;
    PrepareLifecycleScene(fixture, root);

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "LIB-146 retention test renderer did not initialize");
    const RenderSceneSubmitDesc desc = LifecycleSubmitDesc(1U);

    SubmitLifecycleFrame(renderer, fixture.scene, desc, "LIB-146 retention test did not submit the initial frame");
    Require(renderer.LastSceneSubmitStats().visibleMeshCount > 0U && renderer.RuntimeResourceStats().cachedMeshCount == 1U,
        "LIB-146 retention test initial submit did not cache the mesh resource");

    fixture.scene.Entities().Destroy(fixture.entity);
    SubmitLifecycleFrame(renderer, fixture.scene, desc, "LIB-146 retention test did not submit after entity destroy");
    Require(renderer.LastSceneSubmitStats().visibleMeshCount == 0U, "A destroyed entity's proxy must stop rendering on the next full sync");
    Require(renderer.RuntimeResourceStats().cachedMeshCount == 1U,
        "An unreferenced resource must stay cached inside the retention window (destroying the entity is not an immediate GPU destroy)");

    // Age the resource past the retention window; PruneUnused runs at the end of every
    // SubmitScenes, keyed to the completed-frame counter.
    for (std::uint32_t frame = 0U; frame < Renderer::kRuntimeAssetRetentionFrames + 8U; ++frame) {
        SubmitLifecycleFrame(renderer, fixture.scene, desc, "LIB-146 retention test did not submit an aging frame");
    }
    Require(renderer.RuntimeResourceStats().cachedMeshCount == 0U,
        "PruneUnused must actually evict a resource once nothing referenced it for kRuntimeAssetRetentionFrames");

    const kb::scene::SceneEntity revived = fixture.scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Revived Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    fixture.scene.Components().MeshRenderers().Set(revived, kb::scene::MeshRendererComponent{ .meshAssetId = fixture.meshAssetId });
    SubmitLifecycleFrame(renderer, fixture.scene, desc, "LIB-146 retention test did not submit the revived entity");
    Require(renderer.LastSceneSubmitStats().visibleMeshCount > 0U && renderer.RuntimeResourceStats().cachedMeshCount == 1U,
        "A pruned asset must be cleanly re-ensured when a new entity references it again");

    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

// LIB-146 (asset reload): a mesh asset whose on-disk content (and therefore contentHash)
// changed must be rebuilt into a NEW GPU handle on the next submit, with the old handle
// honestly unresolvable - the mesh sibling of the long-standing material/texture reload
// tests (RunRendererReloadsChangedRuntimeMaterialAssetTest).
void RunRendererReloadsChangedRuntimeMeshAssetTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_mesh_reload";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    LifecycleSceneFixture fixture;
    PrepareLifecycleScene(fixture, root);

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "LIB-146 mesh reload test renderer did not initialize");
    const RenderSceneSubmitDesc desc = LifecycleSubmitDesc(1U);

    SubmitLifecycleFrame(renderer, fixture.scene, desc, "LIB-146 mesh reload test did not submit the initial frame");
    const RenderMeshHandle firstHandle = renderer.SceneResourceMap()->ResolveMesh(fixture.meshAssetId);
    Require(firstHandle.IsValid() && renderer.SceneResources()->FindMesh(firstHandle) != nullptr,
        "LIB-146 mesh reload test initial submit did not bind a live mesh handle");

    // Rewrite the mesh with different geometry -> new contentHash on rediscovery.
    {
        std::ofstream output{ root / "triangle.obj", std::ios::trunc };
        output
            << "v -0.3 -0.3 0.0\n"
            << "v 0.3 -0.3 0.0\n"
            << "v 0.0 0.3 0.1\n"
            << "vt 0 0\n"
            << "vt 1 0\n"
            << "vt 0.5 1\n"
            << "vn 0 0 1\n"
            << "f 1/1/1 2/2/1 3/3/1\n";
    }
    Require(fixture.scene.Assets().Manager().DiscoverMountedAssets() >= 1U, "LIB-146 mesh reload test rediscovery failed");
    SubmitLifecycleFrame(renderer, fixture.scene, desc, "LIB-146 mesh reload test did not submit the reload frame");

    const RenderMeshHandle secondHandle = renderer.SceneResourceMap()->ResolveMesh(fixture.meshAssetId);
    Require(secondHandle.IsValid() && secondHandle.value != firstHandle.value,
        "A changed mesh contentHash must rebuild the GPU mesh into a new handle on the next submit");
    Require(renderer.SceneResources()->FindMesh(firstHandle) == nullptr,
        "The replaced mesh's old handle must be honestly unresolvable after the reload");
    Require(renderer.SceneResources()->FindMesh(secondHandle) != nullptr && renderer.RuntimeResourceStats().cachedMeshCount == 1U,
        "The reloaded mesh must be live and cached exactly once");
    Require(renderer.LastSceneSubmitStats().visibleMeshCount > 0U, "The reloaded mesh must keep rendering");

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

    const MaterialProgramRegistryStats programStats = renderer.MaterialProgramStats();
    Require(programStats.loads == 4U,
        "KBMAT-MAT05: Renderer init must load the builtin mesh/GBuffer/shadow/selection programs through the MaterialProgramRegistry");
    Require(programStats.liveProgramCount == 4U,
        "KBMAT-MAT05: MaterialProgramRegistry stats must be exposed through renderer diagnostics with the live builtin programs");
    Require(programStats.failures == 0U,
        "KBMAT-MAT05: Builtin program loading must not report failures under the headless Noop backend");

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
    // 16 opaque instances render in the opaque + shadow passes (32); the 1 blended instance renders in
    // the now-active transparent pass (MAT-80), so the visible/submitted totals are instanceCount*2 + 1.
    Require(submitStats.visibleMeshCount == instanceCount * 2U + 1U, "Runtime submit did not keep opaque (opaque+shadow) and blended (transparent) mesh instances visible");
    Require(submitStats.submittedMeshCount == instanceCount * 2U + 1U, "Runtime submit did not submit opaque (opaque+shadow) and blended (transparent) mesh instances");
    // Opaque instances = 1 instanced draw in the opaque pass + 1 in the shadow pass; the blended instance
    // adds 1 draw in the transparent pass (MAT-80) = 3 draw calls.
    Require(submitStats.submittedDrawCallCount == 3U, "Runtime submit must draw opaque (opaque+shadow) and blended (transparent) materials");
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
    Require(passStats[2].pass == MeshPassType::BaseTransparent && passStats[2].stats.submittedMeshCount == 1U, "Runtime submit transparent pass must submit the blended material (MAT-80)");
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
        if (event.kind == SceneRenderDiagnosticKind::UnsupportedMaterialAlphaBlend) {
            foundDisabledBlendDiagnostic = true;
        }
    }
    Require(foundUnresolvedTexturePathDiagnostic, "Runtime submit did not emit unresolved material texture path diagnostic");
    // MAT-80: blended materials are now supported via the transparent pass, so they must NOT raise the
    // "unsupported alpha blend" diagnostic anymore.
    Require(!foundDisabledBlendDiagnostic, "Runtime submit must not flag blended materials as unsupported once the transparent pass is active");
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

void RunRendererEnsuresGraphProgramTextureResourcesTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_graph_texture_resources";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Graph texture resource test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path texturePath = root / "graph_specular.kbtex";
    const std::filesystem::path materialPath = root / "graph_texture_only.kbmat";
    WriteTriangleObj(meshPath);
    WriteTexture(texturePath, 220U, 64U, 32U);

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Graph texture resource test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Graph texture resource test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderTextureAssetLoader>()), "Graph texture resource test could not register texture loader");
    Require(manager.Mounts().Mount("Game", root), "Graph texture resource test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 2U, "Graph texture resource test did not discover mesh and texture assets");

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* textureMetadata = manager.Registry().FindByPath("/Game/graph_specular.kbtex");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Graph texture resource test discovered wrong mesh metadata");
    Require(textureMetadata != nullptr && textureMetadata->type == "RenderTexture", "Graph texture resource test discovered wrong texture metadata");
    const std::uint64_t meshAssetId = meshMetadata->id.value;
    const std::uint64_t textureAssetId = textureMetadata->id.value;

    RenderMaterialAssetData material{};
    material.graph = MakeDefaultRenderMaterialGraphDocument();
    material.graph.nodes.push_back(MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantColor, {}, "0.18 0.24 0.32 1"));
    RenderMaterialGraphNode textureParameter = MakeGraphNode(3U, RenderMaterialGraphNodeKind::ParameterTexture, "graphSpecularTexture");
    textureParameter.parameter.textureRole = "baseColor";
    textureParameter.parameter.expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb;
    material.graph.nodes.push_back(std::move(textureParameter));
    material.graph.nodes.push_back(MakeGraphNode(4U, RenderMaterialGraphNodeKind::TextureSample));
    material.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ParameterTexture, 3U, "texture", RenderMaterialGraphNodeKind::TextureSample, 4U, "texture"),
        MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 4U, "r", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "specular"),
    };
    material.graphParameterValues.push_back(MakeTextureGraphValue("graphSpecularTexture", textureAssetId));
    Require(RenderMaterialAssetWriter::Save(materialPath, material), "Graph texture resource test could not save graph material");
    Require(manager.DiscoverMountedAssets() >= 3U, "Graph texture resource test did not discover graph material asset");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/graph_texture_only.kbmat");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Graph texture resource test discovered wrong material metadata");
    const std::uint64_t materialAssetId = materialMetadata->id.value;

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Graph Texture Resource Mesh",
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
        .cachedTextures = 1U,
        .frameReferencedMeshes = 1U,
        .frameReferencedMaterials = 1U,
        .frameReferencedTextures = 1U,
        .scenePassSubmitStats = 1U,
        .renderSceneMeshProxies = 1U,
        .renderSceneDrawGroupKeys = 1U,
        .meshResourceSlots = 1U,
        .materialResourceSlots = 1U,
        .textureResourceSlots = 1U,
        .meshBindings = 1U,
        .materialBindings = 1U,
        .textureBindings = 1U,
        .syncMeshProxies = 1U,
        .syncTransformCacheEntries = 1U,
        .syncTransformResolvingEntries = 1U,
    });
    Require(renderer.Initialize(surface, &config), "Graph texture resource test renderer did not initialize");

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
            .maxDrawCommands = 2U,
            .maxVisibleInstances = 2U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
    };

    Require(renderer.BeginFrame(), "Graph texture resource test did not begin frame");
    Require(renderer.SubmitScene(scene, desc), "Graph texture resource test did not submit scene");
    const SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const RenderResourceRegistry* resources = renderer.SceneResources();
    Require(resourceMap != nullptr && resources != nullptr, "Graph texture resource test could not inspect scene resources");
    const RenderMaterialHandle materialHandle = resourceMap->ResolveMaterial(materialAssetId);
    const RenderMaterialResource* materialResource = resources->FindMaterial(materialHandle);
    Require(materialHandle.IsValid() && materialResource != nullptr, "Graph texture resource test did not bind material resource");
    Require(materialResource->albedoTextureAssetId == 0U,
        "KBMAT-MAT99-16: graph-only texture test must not pass through the legacy albedo texture slot");
    const auto graphTextureIt = std::find_if(
        materialResource->graphProgram.textures.begin(),
        materialResource->graphProgram.textures.end(),
        [textureAssetId](const RenderMaterialGraphTextureBinding& binding) {
            return binding.stableId == "graphSpecularTexture" && binding.textureAssetId == textureAssetId;
        });
    Require(graphTextureIt != materialResource->graphProgram.textures.end(),
        "KBMAT-MAT99-16: graph material did not expose the authored texture parameter binding");
    const RenderTextureHandle textureHandle = resourceMap->ResolveTexture(textureAssetId, graphTextureIt->colorSpace);
    const RenderTextureResource* textureResource = resources->FindTexture(textureHandle);
    Require(textureHandle.IsValid() && textureResource != nullptr,
        "KBMAT-MAT99-16: runtime submit must ensure textures referenced only by graphProgram.textures");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

#if defined(KB_TEST_GRAPH_SHADERC_PATH)
void RunRendererPreservesGraphTextureDimensionsThroughProductionPipelineTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_graph_texture_dimensions";
    const std::filesystem::path cacheRoot = root / "graph_shaders";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Graph texture dimension test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    WriteTriangleObj(meshPath);
    WriteDimensionTexture(root / "cube.kbtex", RenderTextureDimension::TextureCube);
    WriteDimensionTexture(root / "volume.kbtex", RenderTextureDimension::Texture3D, 3U);
    WriteDimensionTexture(root / "array.kbtex", RenderTextureDimension::Texture2DArray, 1U, 3U);
    WriteDimensionTexture(root / "flat.kbtex", RenderTextureDimension::Texture2D);

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Graph texture dimension test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Graph texture dimension test could not register material loader");
    Require(manager.RegisterLoader(std::make_unique<RenderTextureAssetLoader>()), "Graph texture dimension test could not register texture loader");
    Require(manager.Mounts().Mount("Game", root), "Graph texture dimension test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 5U, "Graph texture dimension test did not discover mesh and texture assets");

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* cubeMetadata = manager.Registry().FindByPath("/Game/cube.kbtex");
    const kb::assets::AssetMetadata* volumeMetadata = manager.Registry().FindByPath("/Game/volume.kbtex");
    const kb::assets::AssetMetadata* arrayMetadata = manager.Registry().FindByPath("/Game/array.kbtex");
    const kb::assets::AssetMetadata* flatMetadata = manager.Registry().FindByPath("/Game/flat.kbtex");
    Require(meshMetadata != nullptr && cubeMetadata != nullptr && volumeMetadata != nullptr &&
            arrayMetadata != nullptr && flatMetadata != nullptr,
        "Graph texture dimension test could not resolve discovered asset metadata");
    const std::uint64_t meshAssetId = meshMetadata->id.value;
    const std::uint64_t cubeAssetId = cubeMetadata->id.value;
    const std::uint64_t volumeAssetId = volumeMetadata->id.value;
    const std::uint64_t arrayAssetId = arrayMetadata->id.value;
    const std::uint64_t flatAssetId = flatMetadata->id.value;

    struct MaterialCase {
        const char* filename;
        const char* virtualPath;
        RenderMaterialGraphNodeKind sampleKind;
        const char* stableId;
        std::uint64_t textureAssetId;
        RenderTextureDimension expectedDimension;
        bool expectMismatch;
        std::uint64_t materialAssetId = 0U;
    };
    std::array materialCases{
        // The first stable id intentionally aliases a legacy PBR field. A Cube graph parameter
        // must still remain graph-only instead of being mirrored into the sampler2D slot.
        MaterialCase{ "cube_material.kbmat", "/Game/cube_material.kbmat", RenderMaterialGraphNodeKind::TextureSampleCube, "occlusionTexture", cubeAssetId, RenderTextureDimension::TextureCube, false },
        MaterialCase{ "volume_material.kbmat", "/Game/volume_material.kbmat", RenderMaterialGraphNodeKind::TextureSampleVolume, "volumeTex", volumeAssetId, RenderTextureDimension::Texture3D, false },
        MaterialCase{ "array_material.kbmat", "/Game/array_material.kbmat", RenderMaterialGraphNodeKind::TextureSample2DArray, "arrayTex", arrayAssetId, RenderTextureDimension::Texture2DArray, false },
        MaterialCase{ "mismatch_material.kbmat", "/Game/mismatch_material.kbmat", RenderMaterialGraphNodeKind::TextureSampleCube, "mismatchCubeTex", flatAssetId, RenderTextureDimension::TextureCube, true },
    };

    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };
    std::uint64_t compileAssetId = 0xD140U;
    for (const MaterialCase& materialCase : materialCases) {
        const RenderMaterialAssetData material = MakeDimensionGraphMaterial(
            materialCase.sampleKind,
            materialCase.stableId,
            materialCase.textureAssetId);
        const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(
            material.graph,
            RenderMaterialGraphBuildContext{ .assetId = compileAssetId++ });
        if (!compiled.Succeeded()) {
            std::cerr << "Graph texture dimension compile failed for " << materialCase.stableId << ':\n';
            for (const RenderMaterialGraphDiagnostic& diagnostic : compiled.diagnostics) {
                std::cerr << "  " << diagnostic.message << '\n';
            }
        }
        Require(compiled.Succeeded() && compiled.shader.reflection.textures.size() == 1U &&
                compiled.shader.reflection.textures[0].dimension == materialCase.expectedDimension,
            "Graph texture dimension test graph did not compile with the expected sampler dimension");
        RenderMaterialGraphShaderArtifactRequest request{};
        request.shadercPath = KB_TEST_GRAPH_SHADERC_PATH;
        request.varyingDefPath = KB_TEST_GRAPH_SHADER_VARYING_DEF;
        request.includeDirs = { KB_TEST_GRAPH_SHADER_INCLUDE_DIR, KB_TEST_GRAPH_BGFX_SHADER_INCLUDE_DIR };
        request.cacheRoot = cacheRoot.generic_string();
        request.pass = "BaseOpaque";
        Require(CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, request).Succeeded(),
            "Graph texture dimension test could not cook the production graph program");
        Require(RenderMaterialAssetWriter::Save(root / materialCase.filename, material),
            "Graph texture dimension test could not save material asset");
    }

    Require(manager.DiscoverMountedAssets() >= 9U, "Graph texture dimension test did not discover material assets");
    for (MaterialCase& materialCase : materialCases) {
        const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath(materialCase.virtualPath);
        Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial",
            "Graph texture dimension test discovered wrong material metadata");
        materialCase.materialAssetId = materialMetadata->id.value;
        const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
            .name = materialCase.stableId,
            .transform = TransformAt(static_cast<float>(materialCase.materialAssetId & 3U) * 0.2F, 0.0F, 0.0F),
        });
        scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
            .meshAssetId = meshAssetId,
            .materialAssetId = materialCase.materialAssetId,
        });
    }

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "Graph texture dimension test renderer did not initialize");
    renderer.SetGraphShaderCacheRoot(cacheRoot.generic_string());

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
            .maxVisibleInstances = 8U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
    };
    Require(renderer.BeginFrame(), "Graph texture dimension test did not begin frame");
    Require(renderer.SubmitScene(scene, desc), "Graph texture dimension test did not submit scene");

    const SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const RenderResourceRegistry* resources = renderer.SceneResources();
    Require(resourceMap != nullptr && resources != nullptr, "Graph texture dimension test could not inspect production resources");
    for (const MaterialCase& materialCase : materialCases) {
        const RenderMaterialResource* material = resources->FindMaterial(resourceMap->ResolveMaterial(materialCase.materialAssetId));
        Require(material != nullptr && material->graphProgram.textures.size() == 1U,
            "Graph texture dimension test did not build the graph material binding");
        Require(material->occlusionTextureAssetId == 0U,
            "Non-2D graph texture was incorrectly mirrored into the legacy sampler2D PBR slot");
        const RenderMaterialGraphTextureBinding& binding = material->graphProgram.textures[0];
        Require(binding.dimension == materialCase.expectedDimension,
            "Graph texture dimension was lost before runtime binding");
        const RenderTextureResource* texture = resources->FindTexture(
            resourceMap->ResolveTexture(materialCase.textureAssetId, binding.colorSpace));
        const RenderTextureDimension expectedResourceDimension = materialCase.expectMismatch
            ? RenderTextureDimension::Texture2D
            : materialCase.expectedDimension;
        Require(texture != nullptr && texture->dimension == expectedResourceDimension,
            "Production graph texture resource did not preserve its asset dimension");
    }

    // Re-check resources with the authored asset dimension, including depth/layer metadata.
    const RenderTextureResource* cubeResource = resources->FindTexture(resourceMap->ResolveTexture(cubeAssetId, RenderTextureColorSpace::Linear));
    const RenderTextureResource* volumeResource = resources->FindTexture(resourceMap->ResolveTexture(volumeAssetId, RenderTextureColorSpace::Linear));
    const RenderTextureResource* arrayResource = resources->FindTexture(resourceMap->ResolveTexture(arrayAssetId, RenderTextureColorSpace::Linear));
    const RenderTextureResource* flatResource = resources->FindTexture(resourceMap->ResolveTexture(flatAssetId, RenderTextureColorSpace::Linear));
    Require(cubeResource != nullptr && cubeResource->dimension == RenderTextureDimension::TextureCube && cubeResource->layers == 1U,
        "Production resource pipeline did not create a cube texture resource");
    Require(volumeResource != nullptr && volumeResource->dimension == RenderTextureDimension::Texture3D && volumeResource->depth == 3U,
        "Production resource pipeline did not create a 3D texture resource");
    Require(arrayResource != nullptr && arrayResource->dimension == RenderTextureDimension::Texture2DArray && arrayResource->layers == 3U,
        "Production resource pipeline did not create a 2D array texture resource");
    Require(flatResource != nullptr && flatResource->dimension == RenderTextureDimension::Texture2D &&
            flatResource->mipCount == 2U,
        "Production resource pipeline did not create a complete mip chain for the 2D texture");

    Require(renderer.LastSceneSubmitStats().textureDimensionMismatchCount == 1U,
        "Production submit must count exactly the authored 2D-to-Cube mismatch");
    std::uint32_t mismatchDiagnosticCount = 0U;
    for (const SceneRenderDiagnosticEvent& event : renderer.LastSceneDiagnostics().events) {
        if (event.kind != SceneRenderDiagnosticKind::TextureDimensionMismatch) {
            continue;
        }
        ++mismatchDiagnosticCount;
        Require(event.textureAssetId == flatAssetId &&
                event.expectedTextureDimension == RenderTextureDimension::TextureCube &&
                event.actualTextureDimension == RenderTextureDimension::Texture2D &&
                event.fallbackTextureDimension == RenderTextureDimension::TextureCube,
            "Texture dimension mismatch diagnostic did not identify the asset, actual sampler type and Cube fallback");
    }
    Require(mismatchDiagnosticCount == 1U,
        "Production submit must emit one deterministic texture dimension mismatch diagnostic");

    renderer.EndFrame();
    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}
#endif

void RunRendererRoutesGraphBlendModeToTransparentPassTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_graph_blend_pass";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Graph blend pass test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path materialPath = root / "graph_additive.kbmat";
    WriteTriangleObj(meshPath);

    RenderMaterialAssetData material{};
    material.graph = MakeDefaultRenderMaterialGraphDocument();
    material.graph.blendMode = "additive";
    material.graph.nodes.push_back(MakeGraphNode(2U, RenderMaterialGraphNodeKind::ConstantColor, {}, "0.8 0.2 0.1 0.5"));
    material.graph.nodes.push_back(MakeGraphNode(3U, RenderMaterialGraphNodeKind::ConstantScalar, {}, "0.5"));
    material.graph.links = {
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"),
        MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 3U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"),
    };
    Require(RenderMaterialAssetWriter::Save(materialPath, material), "Graph blend pass test could not save additive graph material");

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Graph blend pass test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Graph blend pass test could not register material loader");
    Require(manager.Mounts().Mount("Game", root), "Graph blend pass test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 2U, "Graph blend pass test did not discover mesh/material assets");
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* materialMetadata = manager.Registry().FindByPath("/Game/graph_additive.kbmat");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Graph blend pass test discovered wrong mesh metadata");
    Require(materialMetadata != nullptr && materialMetadata->type == "RenderMaterial", "Graph blend pass test discovered wrong material metadata");
    const std::uint64_t materialAssetId = materialMetadata->id.value;

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Graph Additive Material Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
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
        .scenePassSubmitStats = 2U,
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
    Require(renderer.Initialize(surface, &config), "Graph blend pass test renderer did not initialize");

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
            .maxDrawCommands = 2U,
            .maxVisibleInstances = 2U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueAndTransparent,
        .shadowPassEnabled = false,
    };

    Require(renderer.BeginFrame(), "Graph blend pass test did not begin frame");
    Require(renderer.SubmitScene(scene, desc), "Graph blend pass test did not submit scene");
    const SceneRenderResourceMap* resourceMap = renderer.SceneResourceMap();
    const RenderResourceRegistry* resources = renderer.SceneResources();
    Require(resourceMap != nullptr && resources != nullptr, "Graph blend pass test could not inspect scene resources");
    const RenderMaterialHandle materialHandle = resourceMap->ResolveMaterial(materialAssetId);
    const RenderMaterialResource* materialResource = resources->FindMaterial(materialHandle);
    Require(materialHandle.IsValid() && materialResource != nullptr, "Graph blend pass test did not bind material resource");
    Require(materialResource->graphProgram.alphaMode == RenderMaterialAlphaMode::Blend &&
            materialResource->alphaMode == RenderMaterialAlphaMode::Blend,
        "KBMAT-MAT99-25: graph blend mode must drive material alpha mode used by the scene pipeline");
    Require(materialResource->graphProgram.translucencyBlend == RenderMaterialTranslucencyBlend::Additive &&
            materialResource->translucencyBlend == RenderMaterialTranslucencyBlend::Additive,
        "KBMAT-MAT99-25: graph additive blend mode must drive the scene translucency blend state");
    const std::span<const SceneRenderPassSubmitStats> passStats = renderer.LastScenePassSubmitStats();
    Require(passStats.size() == 2U, "KBMAT-MAT99-25: graph blend pass test should report opaque and transparent passes");
    Require(passStats[0].pass == MeshPassType::BaseOpaque && passStats[0].stats.submittedMeshCount == 0U,
        "KBMAT-MAT99-25: additive graph material must not submit in the opaque scene pass");
    Require(passStats[1].pass == MeshPassType::BaseTransparent && passStats[1].stats.submittedMeshCount == 1U &&
            passStats[1].stats.submittedDrawCallCount == 1U,
        "KBMAT-MAT99-25: additive graph material must submit in the transparent scene pass");

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

void RunGraphMaterialReportsGpuMaterialGraphModeTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_graph_material_cpu_fallback_counter";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "Graph material CPU fallback counter test could not create temp root");

    const std::filesystem::path meshPath = root / "triangle.obj";
    const std::filesystem::path graphMaterialPath = root / "graph_mat.kbmat";
    const std::filesystem::path pbrMaterialPath = root / "pbr_mat.kbmat";
    WriteTriangleObj(meshPath);
    WriteGraphValidationMaterial(graphMaterialPath, 0.6F, true);
    {
        std::ofstream pbrOut{ pbrMaterialPath, std::ios::trunc };
        pbrOut
            << "version 1\n"
            << "materialType builtin.pbr\n"
            << "materialTypeVersion 1\n"
            << "baseColor 0.3 0.4 0.5 1\n"
            << "roughnessFactor 0.5\n"
            << "alphaMode OPAQUE\n";
    }

    // This test asserts the GPU material-graph render mode, so the graph material must actually have a
    // cooked GPU binary (graphMaterialGpuCount now reflects the true draw outcome, not resolve intent).
    const std::filesystem::path cacheRoot = root / "graph_shaders";
    {
        const std::optional<RenderMaterialAssetData> graphMaterial = RenderMaterialAssetLoader::LoadMaterial(graphMaterialPath);
        Require(graphMaterial.has_value(), "MAT-27 GPU material mode test could not load the graph material to cook");
        const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graphMaterial->graph, RenderMaterialGraphBuildContext{ .assetId = 0x2700U });
        Require(compiled.Succeeded(), "MAT-27 GPU material mode test graph must compile");
        RenderMaterialGraphShaderArtifactRequest request{};
        request.shadercPath = KB_TEST_GRAPH_SHADERC_PATH;
        request.varyingDefPath = KB_TEST_GRAPH_SHADER_VARYING_DEF;
        request.includeDirs = { KB_TEST_GRAPH_SHADER_INCLUDE_DIR, KB_TEST_GRAPH_BGFX_SHADER_INCLUDE_DIR };
        request.cacheRoot = cacheRoot.generic_string();
        request.pass = "BaseOpaque";
        const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };
        Require(CookRenderMaterialGraphShaderArtifact(compiled.shader, backends, request).Succeeded(),
            "MAT-27 GPU material mode test must cook the graph binary");
    }

    kb::scene::Scene scene;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    Require(manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()), "Graph CPU fallback counter test could not register mesh loader");
    Require(manager.RegisterLoader(std::make_unique<RenderMaterialAssetLoader>()), "Graph CPU fallback counter test could not register material loader");
    Require(manager.Mounts().Mount("Game", root), "Graph CPU fallback counter test could not mount asset root");
    Require(manager.DiscoverMountedAssets() >= 3U, "Graph CPU fallback counter test did not discover assets");

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath("/Game/triangle.obj");
    const kb::assets::AssetMetadata* graphMaterialMetadata = manager.Registry().FindByPath("/Game/graph_mat.kbmat");
    const kb::assets::AssetMetadata* pbrMaterialMetadata = manager.Registry().FindByPath("/Game/pbr_mat.kbmat");
    Require(meshMetadata != nullptr && meshMetadata->type == "RenderMesh", "Graph CPU fallback counter test discovered wrong mesh");
    Require(graphMaterialMetadata != nullptr && graphMaterialMetadata->type == "RenderMaterial", "Graph CPU fallback counter test discovered wrong graph material");
    Require(pbrMaterialMetadata != nullptr && pbrMaterialMetadata->type == "RenderMaterial", "Graph CPU fallback counter test discovered wrong PBR material");

    const kb::scene::SceneEntity graphEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Graph Material Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(graphEntity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
        .materialAssetId = graphMaterialMetadata->id.value,
    });

    const kb::scene::SceneEntity pbrEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "PBR Material Mesh",
        .transform = TransformAt(1.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(pbrEntity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshMetadata->id.value,
        .materialAssetId = pbrMaterialMetadata->id.value,
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
        .frameReferencedMeshes = 1U,
        .frameReferencedMaterials = 2U,
        .scenePassSubmitStats = 2U,
        .renderSceneMeshProxies = 4U,
        .renderSceneDrawGroupKeys = 4U,
        .meshResourceSlots = 1U,
        .materialResourceSlots = 2U,
        .meshBindings = 1U,
        .materialBindings = 2U,
        .syncMeshProxies = 4U,
        .syncTransformCacheEntries = 4U,
        .syncTransformResolvingEntries = 4U,
    });
    renderer.SetGraphShaderCacheRoot(cacheRoot.generic_string());
    Require(renderer.Initialize(surface, &config), "Graph CPU fallback counter test did not initialize renderer");

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
            .maxVisibleInstances = 8U,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueOnly,
        .shadowPassEnabled = false,
    };

    Require(renderer.BeginFrame(), "Graph GPU material mode test did not begin first frame");
    Require(renderer.SubmitScene(scene, desc), "Graph GPU material mode test did not submit first frame");
    const Renderer::RuntimeSceneResourceStats firstStats = renderer.RuntimeResourceStats();
    Require(firstStats.graphMaterialGpuCount == 1U,
        "MAT-27: first frame did not report exactly one graph material using the GPU material graph path");
    Require(firstStats.graphMaterialCpuFallbackCount == 0U,
        "MAT-27: first frame fell back a valid graph material to CPU PBR flattening instead of the GPU path");
    Require(firstStats.materialLoadedCount == 2U,
        "MAT-27: first frame did not load exactly two materials (graph + builtin PBR)");
    renderer.EndFrame();

    Require(renderer.BeginFrame(), "Graph GPU material mode test did not begin steady frame");
    Require(renderer.SubmitScene(scene, desc), "Graph GPU material mode test did not submit steady frame");
    const Renderer::RuntimeSceneResourceStats steadyStats = renderer.RuntimeResourceStats();
    Require(steadyStats.graphMaterialGpuCount == 1U,
        "MAT-27: steady frame did not retain the GPU material graph render mode from cache");
    Require(steadyStats.graphMaterialCpuFallbackCount == 0U,
        "MAT-27: steady frame fell back the cached graph material to CPU PBR flattening");
    Require(steadyStats.materialLoadedCount == 0U,
        "MAT-27: steady frame reloaded materials unexpectedly");
    renderer.EndFrame();

    renderer.Shutdown();
    std::filesystem::remove_all(root, error);
}

void RunRendererSubmitsDeferredGBufferAndLightingPassesInHeadlessNoopTest() {
    kb::scene::Scene scene;

    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 1U,
        .scenePassSubmitStats = 4U,
    });
    Require(renderer.Initialize(surface, &config), "Deferred runtime test did not initialize renderer");
    Require(renderer.BeginFrame(), "Deferred runtime test did not begin frame");

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
        .lightingConfig = SceneRenderLightingConfig{
            .lightingPath = SceneRenderLightingPath::Deferred,
        },
        .meshPassMode = SceneRenderMeshPassMode::OpaqueAndTransparent,
        .shadowPassEnabled = false,
        .postProcessEnabled = false,
        .selectionMaskEnabled = false,
        .selectionOutlineEnabled = false,
    };

    Require(renderer.SubmitScene(scene, desc), "Deferred runtime test did not submit scene");
    const std::span<const SceneRenderPassSubmitStats> passStats = renderer.LastScenePassSubmitStats();
    Require(passStats.size() == 3U, "Deferred runtime test did not report GBuffer, lighting and transparent passes");
    Require(passStats[0].renderPass == RenderPassKind::GBufferGeometry && passStats[0].pass == MeshPassType::GBuffer,
        "Deferred runtime test did not submit GBuffer geometry as the first scene pass");
    Require(passStats[1].renderPass == RenderPassKind::DeferredLighting,
        "Deferred runtime test did not submit the deferred lighting pass after GBuffer");
    Require(passStats[2].renderPass == RenderPassKind::TransparentScene && passStats[2].pass == MeshPassType::BaseTransparent,
        "Deferred runtime test did not keep transparent rendering in the forward transparent pass");
    for (const SceneRenderPassSubmitStats& pass : passStats) {
        Require(pass.renderPass != RenderPassKind::OpaqueScene,
            "Deferred runtime test must not use the forward opaque pass as deferred proof");
    }
    Require(!renderer.LastSceneDiagnostics().HasErrors(), "Deferred runtime test produced diagnostics for a valid GBuffer path");

    renderer.EndFrame();
    renderer.Shutdown();
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

void RunEditorCameraWireframesSubmitInHeadlessNoopTest() {
    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType =
        static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    Require(
        renderer.Initialize(surface, &config),
        "Camera wireframe renderer did not initialize in headless Noop mode");
    Require(
        renderer.BeginFrame(),
        "Camera wireframe renderer did not begin a frame");

    SceneGizmoPass gizmoPass;
    Require(
        gizmoPass.Initialize(),
        "Camera wireframe gizmo pass did not initialize");
    const SceneRenderCamera camera = IdentityCamera();
    const std::array<EditorCameraWireframeDesc, 2U> wireframes{{
        EditorCameraWireframeDesc{
            .projection =
                EditorCameraWireframeProjection::Perspective,
            .position = {0.0F, 0.0F, 0.0F},
            .verticalFovDegrees = 60.0F,
            .nearClip = 0.1F,
            .farClip = 100.0F,
            .aspect = 16.0F / 9.0F,
        },
        EditorCameraWireframeDesc{
            .projection =
                EditorCameraWireframeProjection::Orthographic,
            .position = {2.0F, 0.0F, 0.0F},
            .orthographicHeight = 12.0F,
            .nearClip = 0.25F,
            .farClip = 250.0F,
            .aspect = 4.0F / 3.0F,
        },
    }};
    Require(
        gizmoPass.Submit(SceneGizmoPassDesc{
            .viewId = 0U,
            .frameBuffer = BGFX_INVALID_HANDLE,
            .extent = RenderExtent{64U, 64U},
            .camera = &camera,
            .cameraWireframes = wireframes,
        }),
        "Perspective and orthographic camera wireframes did not reach the real gizmo pass");

    gizmoPass.Shutdown();
    renderer.EndFrame();
    renderer.Shutdown();
}

} // namespace

// MAT-72: material frame time accumulates and is exposed as the u_time vec4 (time, delta, frameIndex).
void RunMaterialFrameTimeAdvanceTest() {
    const auto nearly = [](float a, float b) noexcept { return std::fabs(a - b) <= 0.0005F; };

    SceneRenderer sceneRenderer;
    Require(nearly(sceneRenderer.FrameTimeConstants()[0], 0.0F), "MAT-72: Frame time must start at zero seconds");
    sceneRenderer.AdvanceFrameTime(0.5F);
    sceneRenderer.AdvanceFrameTime(0.25F);
    const std::array<float, 4> constants = sceneRenderer.FrameTimeConstants();
    Require(nearly(constants[0], 0.75F), "MAT-72: Frame seconds must accumulate across advances");
    Require(nearly(constants[1], 0.25F), "MAT-72: Frame delta must reflect the most recent advance");
    Require(nearly(constants[2], 2.0F), "MAT-72: Frame index must increment per advance");
    sceneRenderer.SetDynamicParameter({ 0.1F, 0.2F, 0.3F, 0.4F });
    const std::array<float, 4> dynamicParameter = sceneRenderer.DynamicParameterConstants();
    Require(nearly(dynamicParameter[0], 0.1F) && nearly(dynamicParameter[3], 0.4F),
        "MAT-30: SceneRenderer must retain DynamicParameter constants for graph shader submission");

    Renderer renderer;
    renderer.SetFrameDeltaSeconds(1.0F / 30.0F);
    Require(nearly(renderer.FrameDeltaSeconds(), 1.0F / 30.0F), "MAT-72: Renderer must retain the per-frame delta seconds");
}

void RunRendererRuntimeSubmitTests() {
    RunMaterialFrameTimeAdvanceTest();
    RunRuntimeMaterialResolverReturnsTypedFallbacksAndDiagnosticsTest();
    RunRuntimeMaterialResolverEvaluatesMaterialOutputTextureGraphTest();
    RunRuntimeMaterialResolverEvaluatesConstantAndMathGraphTest();
    RunRendererPublishesSceneVisibilityFeedbackTest();
    RunRendererReleaseSceneDropsRuntimeResourcesTest();
    RunRendererPrunesUnreferencedResourcesAfterRetentionTest();
    RunRendererReloadsChangedRuntimeMeshAssetTest();
    RunRendererSubmitsRuntimeMeshAssetInHeadlessNoopTest();
    RunRendererUsesResolverDefaultFallbackForMissingMaterialTest();
    RunRuntimeGraphMaterialRenderModeReportingTest();
    RunRuntimeMaterialInstanceDynamicParameterOverrideTest();
    RunRuntimeMaterialResolverResolvesInstanceParameterOverridesTest();
    RunRendererAppliesMaterialInstanceParameterOverrideTest();
    RunPostProcessProfileAssetSaveLoadRoundTripTest();
    RunRendererAppliesScenePostProcessProfileTest();
    RunRuntimeMaterialInstanceStaticBaseOverrideChainTest();
    RunRendererBindsGraphMaterialGpuProgramTest();
#if defined(KB_TEST_GRAPH_SHADERC_PATH)
    RunRendererPublicGraphShaderCacheRootBindsCookedProgramTest();
    RunRendererSceneRendersMultipleCookedGraphMaterialsTest();
    RunRendererPreservesGraphTextureDimensionsThroughProductionPipelineTest();
#endif
    RunRendererReloadsChangedRuntimeMaterialAssetTest();
    RunRendererEnsuresGraphProgramTextureResourcesTest();
    RunRendererRoutesGraphBlendModeToTransparentPassTest();
    RunGraphBackedMaterialArtifactDependencyReloadInvalidatesOnlyTouchedBindingTest();
    RunCookedGraphBackedMaterialRuntimeDoesNotCompileGraphTest();
    RunInvalidGraphMaterialUsesLastGoodThenRefreshesAfterFixTest();
    RunRendererSubmitsMaterialInstanceAssetInHeadlessNoopTest();
    RunRendererMaterialInstanceInheritsGraphBackedParentParametersTest();
    RunRendererReloadsMaterialInstanceWhenParentMaterialChangesTest();
    RunRendererSubmitsWorkspaceSceneCubeMaterialAfterReopenTest();
    RunRendererSubmitsGltfEmbeddedMaterialInHeadlessNoopTest();
    RunGraphMaterialReportsGpuMaterialGraphModeTest();
    RunRendererSubmitsDeferredGBufferAndLightingPassesInHeadlessNoopTest();
    RunRendererSubmitsDockedAndDetachedViewportsInSameFrameTest();
    RunEditorCameraWireframesSubmitInHeadlessNoopTest();
}

} // namespace kb::render::tests
