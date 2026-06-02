#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

#define CGLTF_IMPLEMENTATION
#include <cgltf/cgltf.h>
#include <meshoptimizer/src/meshoptimizer.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <istream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <string_view>

namespace kb::render {
namespace {

struct Vec2 {
    float x = 0.0F;
    float y = 0.0F;
};

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct ObjVertexKey {
    int position = 0;
    int texCoord = -1;
    int normal = -1;

    [[nodiscard]] bool operator<(const ObjVertexKey& rhs) const noexcept {
        if (position != rhs.position) {
            return position < rhs.position;
        }
        if (texCoord != rhs.texCoord) {
            return texCoord < rhs.texCoord;
        }
        return normal < rhs.normal;
    }
};

struct ObjImportContext {
    std::vector<Vec3> positions;
    std::vector<Vec2> texCoords;
    std::vector<Vec3> normals;
    std::map<ObjVertexKey, std::uint32_t> vertexMap;
    std::uint32_t currentMaterialSlot = 0U;
    std::uint32_t currentSectionStart = 0U;
};

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1U);
    }
    return text;
}

[[nodiscard]] std::string_view StripComment(std::string_view line) noexcept {
    const std::size_t comment = line.find('#');
    return comment == std::string_view::npos ? line : line.substr(0U, comment);
}

[[nodiscard]] bool ParseFloat(std::string_view text, float& output) noexcept {
    text = Trim(text);
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, output);
    return result.ec == std::errc{} && result.ptr == end;
}

[[nodiscard]] bool ParseInt(std::string_view text, int& output) noexcept {
    text = Trim(text);
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, output);
    return result.ec == std::errc{} && result.ptr == end;
}

[[nodiscard]] bool ParseVec3Line(std::string_view rest, Vec3& output) {
    std::istringstream stream{ std::string{ rest } };
    std::string x;
    std::string y;
    std::string z;
    return (stream >> x >> y >> z) && ParseFloat(x, output.x) && ParseFloat(y, output.y) && ParseFloat(z, output.z);
}

[[nodiscard]] bool ParseVec2Line(std::string_view rest, Vec2& output, bool flipV) {
    std::istringstream stream{ std::string{ rest } };
    std::string x;
    std::string y;
    if (!(stream >> x >> y) || !ParseFloat(x, output.x) || !ParseFloat(y, output.y)) {
        return false;
    }
    if (flipV) {
        output.y = 1.0F - output.y;
    }
    return true;
}

[[nodiscard]] int ResolveObjIndex(int index, std::size_t count) noexcept {
    if (index > 0) {
        return index - 1;
    }
    if (index < 0) {
        return static_cast<int>(count) + index;
    }
    return -1;
}

[[nodiscard]] bool ParseFaceVertex(std::string_view token, const ObjImportContext& context, ObjVertexKey& output) noexcept {
    const std::size_t firstSlash = token.find('/');
    const std::size_t secondSlash = firstSlash == std::string_view::npos ? std::string_view::npos : token.find('/', firstSlash + 1U);

    int position = 0;
    if (!ParseInt(firstSlash == std::string_view::npos ? token : token.substr(0U, firstSlash), position)) {
        return false;
    }
    output.position = ResolveObjIndex(position, context.positions.size());
    if (output.position < 0 || static_cast<std::size_t>(output.position) >= context.positions.size()) {
        return false;
    }

    if (firstSlash != std::string_view::npos && secondSlash != firstSlash + 1U) {
        int texCoord = 0;
        const std::string_view texCoordText = secondSlash == std::string_view::npos
            ? token.substr(firstSlash + 1U)
            : token.substr(firstSlash + 1U, secondSlash - firstSlash - 1U);
        if (!texCoordText.empty() && ParseInt(texCoordText, texCoord)) {
            output.texCoord = ResolveObjIndex(texCoord, context.texCoords.size());
        }
    }
    if (secondSlash != std::string_view::npos) {
        int normal = 0;
        const std::string_view normalText = token.substr(secondSlash + 1U);
        if (!normalText.empty() && ParseInt(normalText, normal)) {
            output.normal = ResolveObjIndex(normal, context.normals.size());
        }
    }
    return output.texCoord < static_cast<int>(context.texCoords.size()) && output.normal < static_cast<int>(context.normals.size());
}

[[nodiscard]] std::uint64_t MaterialAssetIdForName(std::string_view materialName, const RenderMeshObjImportDesc& desc) noexcept {
    for (std::uint32_t bindingIndex = 0U; bindingIndex < desc.materialBindingCount; ++bindingIndex) {
        const RenderMeshAssetMaterialBinding& binding = desc.materialBindings[bindingIndex];
        if (binding.materialName == materialName) {
            return binding.materialAssetId;
        }
    }
    return 0U;
}

[[nodiscard]] std::uint64_t MaterialAssetIdForName(std::string_view materialName, const RenderMeshGltfImportDesc& desc) noexcept {
    for (std::uint32_t bindingIndex = 0U; bindingIndex < desc.materialBindingCount; ++bindingIndex) {
        const RenderMeshAssetMaterialBinding& binding = desc.materialBindings[bindingIndex];
        if (binding.materialName == materialName) {
            return binding.materialAssetId;
        }
    }
    return 0U;
}

[[nodiscard]] RenderMaterialAlphaMode AlphaModeOf(cgltf_alpha_mode mode) noexcept {
    switch (mode) {
    case cgltf_alpha_mode_mask:
        return RenderMaterialAlphaMode::Mask;
    case cgltf_alpha_mode_blend:
        return RenderMaterialAlphaMode::Blend;
    case cgltf_alpha_mode_opaque:
    case cgltf_alpha_mode_max_enum:
        return RenderMaterialAlphaMode::Opaque;
    }
    return RenderMaterialAlphaMode::Opaque;
}

[[nodiscard]] std::string TextureUriOf(const cgltf_texture_view& textureView) {
    if (textureView.texture == nullptr || textureView.texture->image == nullptr || textureView.texture->image->uri == nullptr) {
        return {};
    }
    const std::string_view uri{ textureView.texture->image->uri };
    if (uri.starts_with("data:") || uri.find("://") != std::string_view::npos) {
        return {};
    }
    return std::string{ uri };
}

[[nodiscard]] RenderMeshEmbeddedMaterial BuildEmbeddedMaterial(std::string_view materialName, const cgltf_material* material) {
    RenderMeshEmbeddedMaterial embedded{};
    embedded.name = std::string{ materialName };
    if (material == nullptr) {
        return embedded;
    }

    for (std::uint32_t channel = 0U; channel < 4U; ++channel) {
        embedded.desc.baseColor[channel] = material->pbr_metallic_roughness.base_color_factor[channel];
    }
    for (std::uint32_t channel = 0U; channel < 3U; ++channel) {
        embedded.desc.emissiveColor[channel] = material->emissive_factor[channel];
    }
    embedded.desc.metallicFactor = material->pbr_metallic_roughness.metallic_factor;
    embedded.desc.roughnessFactor = material->pbr_metallic_roughness.roughness_factor;
    embedded.desc.normalScale = material->normal_texture.texture == nullptr ? 1.0F : material->normal_texture.scale;
    embedded.desc.occlusionStrength = material->occlusion_texture.texture == nullptr ? 1.0F : material->occlusion_texture.scale;
    embedded.desc.emissiveStrength = material->has_emissive_strength ? material->emissive_strength.emissive_strength : 1.0F;
    embedded.desc.alphaCutoff = material->alpha_cutoff;
    embedded.desc.alphaMode = AlphaModeOf(material->alpha_mode);
    embedded.desc.doubleSided = material->double_sided;
    embedded.albedoTexturePath = TextureUriOf(material->pbr_metallic_roughness.base_color_texture);
    embedded.normalTexturePath = TextureUriOf(material->normal_texture);
    embedded.metallicRoughnessTexturePath = TextureUriOf(material->pbr_metallic_roughness.metallic_roughness_texture);
    embedded.occlusionTexturePath = TextureUriOf(material->occlusion_texture);
    embedded.emissiveTexturePath = TextureUriOf(material->emissive_texture);
    return embedded;
}

[[nodiscard]] std::uint32_t EnsureMaterialSlot(RenderMeshAssetData& asset, std::string_view materialName, const RenderMeshObjImportDesc& desc) {
    const auto iterator = std::ranges::find_if(asset.materialNames, [materialName](const std::string& name) {
        return name == materialName;
    });
    if (iterator != asset.materialNames.end()) {
        return static_cast<std::uint32_t>(std::distance(asset.materialNames.begin(), iterator));
    }

    asset.materialNames.push_back(std::string{ materialName });
    asset.materialSlots.push_back(RenderMaterialSlotDesc{
        .defaultMaterialAssetId = MaterialAssetIdForName(materialName, desc),
    });
    return static_cast<std::uint32_t>(asset.materialSlots.size() - 1U);
}

[[nodiscard]] std::uint32_t EnsureMaterialSlot(RenderMeshAssetData& asset, std::string_view materialName, const cgltf_material* material, const RenderMeshGltfImportDesc& desc) {
    const auto iterator = std::ranges::find_if(asset.materialNames, [materialName](const std::string& name) {
        return name == materialName;
    });
    if (iterator != asset.materialNames.end()) {
        return static_cast<std::uint32_t>(std::distance(asset.materialNames.begin(), iterator));
    }

    asset.materialNames.push_back(std::string{ materialName });
    asset.embeddedMaterials.push_back(BuildEmbeddedMaterial(materialName, material));
    asset.materialSlots.push_back(RenderMaterialSlotDesc{
        .defaultMaterialAssetId = MaterialAssetIdForName(materialName, desc),
    });
    return static_cast<std::uint32_t>(asset.materialSlots.size() - 1U);
}

void FinishSection(RenderMeshAssetData& asset, ObjImportContext& context) {
    const std::uint32_t indexCount = asset.indices32.empty()
        ? static_cast<std::uint32_t>(asset.indices16.size())
        : static_cast<std::uint32_t>(asset.indices32.size());
    if (indexCount <= context.currentSectionStart) {
        return;
    }

    asset.sections.push_back(RenderMeshSectionDesc{
        .indexStart = context.currentSectionStart,
        .indexCount = indexCount - context.currentSectionStart,
        .materialSlot = context.currentMaterialSlot,
    });
    context.currentSectionStart = indexCount;
}

[[nodiscard]] std::uint32_t AddVertex(RenderMeshAssetData& asset, ObjImportContext& context, const ObjVertexKey& key) {
    const auto existing = context.vertexMap.find(key);
    if (existing != context.vertexMap.end()) {
        return existing->second;
    }

    const Vec3& position = context.positions[static_cast<std::size_t>(key.position)];
    const Vec2 texCoord = key.texCoord >= 0 ? context.texCoords[static_cast<std::size_t>(key.texCoord)] : Vec2{};
    const Vec3 normal = key.normal >= 0 ? context.normals[static_cast<std::size_t>(key.normal)] : Vec3{ 0.0F, 1.0F, 0.0F };
    const std::uint32_t vertexIndex = static_cast<std::uint32_t>(asset.vertices.size());
    asset.vertices.push_back(RenderStaticMeshVertexP3N3UV2{
        .x = position.x,
        .y = position.y,
        .z = position.z,
        .nx = normal.x,
        .ny = normal.y,
        .nz = normal.z,
        .u = texCoord.x,
        .v = texCoord.y,
    });
    context.vertexMap[key] = vertexIndex;
    return vertexIndex;
}

void AppendIndex(RenderMeshAssetData& asset, std::uint32_t index) {
    asset.indices32.push_back(index);
}

[[nodiscard]] bool ParseFace(std::string_view rest, RenderMeshAssetData& asset, ObjImportContext& context) {
    std::istringstream stream{ std::string{ rest } };
    std::vector<std::uint32_t> faceIndices;
    std::string token;
    while (stream >> token) {
        ObjVertexKey key{};
        if (!ParseFaceVertex(token, context, key)) {
            return false;
        }
        faceIndices.push_back(AddVertex(asset, context, key));
    }
    if (faceIndices.size() < 3U) {
        return false;
    }

    for (std::size_t index = 1U; index + 1U < faceIndices.size(); ++index) {
        AppendIndex(asset, faceIndices[0]);
        AppendIndex(asset, faceIndices[index]);
        AppendIndex(asset, faceIndices[index + 1U]);
    }
    return true;
}

void CompactIndices(RenderMeshAssetData& asset) {
    for (const std::uint32_t index : asset.indices32) {
        if (index > 0xFFFFU) {
            return;
        }
    }

    asset.indices16.clear();
    asset.indices16.reserve(asset.indices32.size());
    for (const std::uint32_t index : asset.indices32) {
        asset.indices16.push_back(static_cast<std::uint16_t>(index));
    }
    asset.indices32.clear();
}

[[nodiscard]] std::uint32_t IndexAt(const RenderMeshAssetData& asset, std::uint32_t index) noexcept {
    return asset.indices16.empty() ? asset.indices32[index] : asset.indices16[index];
}

[[nodiscard]] RenderBoundsSphere ComputeBounds(const RenderMeshAssetData& asset, std::uint32_t indexStart, std::uint32_t indexCount) noexcept {
    const std::uint32_t vertexCount = static_cast<std::uint32_t>(asset.tangentVertices.empty() ? asset.vertices.size() : asset.tangentVertices.size());
    const std::uint32_t totalIndexCount = static_cast<std::uint32_t>(asset.indices16.empty() ? asset.indices32.size() : asset.indices16.size());
    if (vertexCount == 0U || indexCount == 0U || indexStart >= totalIndexCount || indexCount > totalIndexCount - indexStart) {
        return {};
    }
    const std::uint32_t indexEnd = indexStart + indexCount;

    const std::uint32_t firstVertexIndex = IndexAt(asset, indexStart);
    if (firstVertexIndex >= vertexCount) {
        return {};
    }

    auto vertexPosition = [&asset](std::uint32_t vertexIndex) noexcept {
        if (!asset.tangentVertices.empty()) {
            const RenderStaticMeshVertexP3N3T4UV2& vertex = asset.tangentVertices[vertexIndex];
            return std::array<float, 3>{ vertex.x, vertex.y, vertex.z };
        }
        const RenderStaticMeshVertexP3N3UV2& vertex = asset.vertices[vertexIndex];
        return std::array<float, 3>{ vertex.x, vertex.y, vertex.z };
    };

    const std::array<float, 3> first = vertexPosition(firstVertexIndex);
    float minX = first[0];
    float minY = first[1];
    float minZ = first[2];
    float maxX = first[0];
    float maxY = first[1];
    float maxZ = first[2];
    for (std::uint32_t index = indexStart; index < indexEnd; ++index) {
        const std::uint32_t vertexIndex = IndexAt(asset, index);
        if (vertexIndex >= vertexCount) {
            continue;
        }
        const std::array<float, 3> position = vertexPosition(vertexIndex);
        minX = std::min(minX, position[0]);
        minY = std::min(minY, position[1]);
        minZ = std::min(minZ, position[2]);
        maxX = std::max(maxX, position[0]);
        maxY = std::max(maxY, position[1]);
        maxZ = std::max(maxZ, position[2]);
    }

    const std::array<float, 3> center{
        (minX + maxX) * 0.5F,
        (minY + maxY) * 0.5F,
        (minZ + maxZ) * 0.5F,
    };
    float radiusSquared = 0.0F;
    for (std::uint32_t index = indexStart; index < indexEnd; ++index) {
        const std::uint32_t vertexIndex = IndexAt(asset, index);
        if (vertexIndex >= vertexCount) {
            continue;
        }
        const std::array<float, 3> position = vertexPosition(vertexIndex);
        const float dx = position[0] - center[0];
        const float dy = position[1] - center[1];
        const float dz = position[2] - center[2];
        radiusSquared = std::max(radiusSquared, dx * dx + dy * dy + dz * dz);
    }

    return RenderBoundsSphere{
        .center = center,
        .radius = std::sqrt(radiusSquared),
    };
}

void EnsureTangentVertexStorage(RenderMeshAssetData& asset) {
    if (!asset.tangentVertices.empty()) {
        return;
    }

    asset.tangentVertices.reserve(asset.vertices.size());
    for (const RenderStaticMeshVertexP3N3UV2& vertex : asset.vertices) {
        asset.tangentVertices.push_back(RenderStaticMeshVertexP3N3T4UV2{
            .x = vertex.x,
            .y = vertex.y,
            .z = vertex.z,
            .nx = vertex.nx,
            .ny = vertex.ny,
            .nz = vertex.nz,
            .tx = 1.0F,
            .ty = 0.0F,
            .tz = 0.0F,
            .tw = 1.0F,
            .u = vertex.u,
            .v = vertex.v,
            .r = vertex.r,
            .g = vertex.g,
            .b = vertex.b,
        });
    }
    asset.vertices.clear();
}

void ComputeAssetBounds(RenderMeshAssetData& asset) noexcept {
    const std::uint32_t indexCount = static_cast<std::uint32_t>(asset.indices16.empty() ? asset.indices32.size() : asset.indices16.size());
    asset.bounds = ComputeBounds(asset, 0U, indexCount);
    for (RenderMeshSectionDesc& section : asset.sections) {
        if (!section.bounds.IsValid()) {
            section.bounds = ComputeBounds(asset, section.indexStart, section.indexCount);
        }
    }
}

[[nodiscard]] std::uint32_t MeshAssetVertexCount(const RenderMeshAssetData& asset) noexcept {
    return static_cast<std::uint32_t>(asset.tangentVertices.empty() ? asset.vertices.size() : asset.tangentVertices.size());
}

[[nodiscard]] bool ValidateMeshAssetIndices(const RenderMeshAssetData& asset) noexcept {
    const std::uint32_t vertexCount = MeshAssetVertexCount(asset);
    if (vertexCount == 0U || asset.indices32.empty()) {
        return false;
    }
    for (const std::uint32_t index : asset.indices32) {
        if (index >= vertexCount) {
            return false;
        }
    }
    for (const RenderMeshSectionDesc& section : asset.sections) {
        if (section.indexCount == 0U ||
            section.indexStart >= asset.indices32.size() ||
            section.indexCount > asset.indices32.size() - section.indexStart ||
            section.indexCount % 3U != 0U) {
            return false;
        }
    }
    return asset.indices32.size() % 3U == 0U;
}

void OptimizeMeshAssetVertexCache(RenderMeshAssetData& asset) {
    const std::uint32_t vertexCount = MeshAssetVertexCount(asset);
    if (vertexCount == 0U || asset.indices32.empty()) {
        return;
    }

    if (asset.sections.empty()) {
        std::vector<std::uint32_t> optimized(asset.indices32.size());
        meshopt_optimizeVertexCache(optimized.data(), asset.indices32.data(), asset.indices32.size(), vertexCount);
        asset.indices32 = std::move(optimized);
        return;
    }

    std::vector<std::uint32_t> optimizedSection;
    for (const RenderMeshSectionDesc& section : asset.sections) {
        optimizedSection.resize(section.indexCount);
        meshopt_optimizeVertexCache(
            optimizedSection.data(),
            asset.indices32.data() + section.indexStart,
            section.indexCount,
            vertexCount);
        std::copy(optimizedSection.begin(), optimizedSection.end(), asset.indices32.begin() + static_cast<std::ptrdiff_t>(section.indexStart));
    }
}

template <typename Vertex>
void OptimizeMeshAssetVertexFetch(std::vector<Vertex>& vertices, std::vector<std::uint32_t>& indices) {
    if (vertices.empty() || indices.empty()) {
        return;
    }

    std::vector<Vertex> optimized(vertices.size());
    const std::size_t optimizedVertexCount = meshopt_optimizeVertexFetch(
        optimized.data(),
        indices.data(),
        indices.size(),
        vertices.data(),
        vertices.size(),
        sizeof(Vertex));
    optimized.resize(optimizedVertexCount);
    vertices = std::move(optimized);
}

[[nodiscard]] bool FinalizeMeshAsset(RenderMeshAssetData& asset) {
    if (!ValidateMeshAssetIndices(asset)) {
        return false;
    }
    OptimizeMeshAssetVertexCache(asset);
    if (!asset.tangentVertices.empty()) {
        OptimizeMeshAssetVertexFetch(asset.tangentVertices, asset.indices32);
    } else {
        OptimizeMeshAssetVertexFetch(asset.vertices, asset.indices32);
    }
    if (!ValidateMeshAssetIndices(asset)) {
        return false;
    }
    CompactIndices(asset);
    ComputeAssetBounds(asset);
    asset.RefreshDesc();
    return true;
}

struct GltfAccessorFloats {
    std::vector<float> values;
    std::uint32_t componentCount = 0;
    std::uint32_t elementCount = 0;

    [[nodiscard]] bool IsValid() const noexcept {
        return componentCount > 0U && elementCount > 0U && values.size() >= static_cast<std::size_t>(componentCount) * elementCount;
    }
};

[[nodiscard]] std::optional<GltfAccessorFloats> UnpackFloats(const cgltf_accessor* accessor, std::uint32_t requiredComponentCount) {
    if (accessor == nullptr || accessor->count == 0U) {
        return std::nullopt;
    }

    const cgltf_size componentCount = cgltf_num_components(accessor->type);
    if (componentCount < requiredComponentCount) {
        return std::nullopt;
    }

    const cgltf_size floatCount = cgltf_accessor_unpack_floats(accessor, nullptr, 0);
    if (floatCount == 0U) {
        return std::nullopt;
    }

    GltfAccessorFloats result{};
    result.values.resize(floatCount);
    if (cgltf_accessor_unpack_floats(accessor, result.values.data(), floatCount) != floatCount) {
        return std::nullopt;
    }
    result.componentCount = static_cast<std::uint32_t>(componentCount);
    result.elementCount = static_cast<std::uint32_t>(accessor->count);
    return result;
}

[[nodiscard]] float AccessorValue(const GltfAccessorFloats& accessor, std::uint32_t element, std::uint32_t component) noexcept {
    return accessor.values[static_cast<std::size_t>(element) * accessor.componentCount + component];
}

[[nodiscard]] std::array<float, 3> TransformPosition(const float matrix[16], float x, float y, float z) noexcept {
    return {
        matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12],
        matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13],
        matrix[2] * x + matrix[6] * y + matrix[10] * z + matrix[14],
    };
}

[[nodiscard]] std::array<float, 3> NormalizeVector(std::array<float, 3> vector, std::array<float, 3> fallback) noexcept {
    const float length = std::sqrt(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2]);
    if (length <= 0.0001F) {
        return fallback;
    }
    vector[0] /= length;
    vector[1] /= length;
    vector[2] /= length;
    return vector;
}

[[nodiscard]] std::array<float, 3> TransformDirection(const float matrix[16], float x, float y, float z) noexcept {
    return NormalizeVector(
        std::array<float, 3>{
            matrix[0] * x + matrix[4] * y + matrix[8] * z,
            matrix[1] * x + matrix[5] * y + matrix[9] * z,
            matrix[2] * x + matrix[6] * y + matrix[10] * z,
        },
        std::array<float, 3>{ 1.0F, 0.0F, 0.0F });
}

[[nodiscard]] std::array<float, 3> TransformSurfaceNormal(const float matrix[16], float x, float y, float z) noexcept {
    const float a = matrix[0];
    const float b = matrix[4];
    const float c = matrix[8];
    const float d = matrix[1];
    const float e = matrix[5];
    const float f = matrix[9];
    const float g = matrix[2];
    const float h = matrix[6];
    const float i = matrix[10];

    const float determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::fabs(determinant) <= 0.0001F) {
        return { 0.0F, 1.0F, 0.0F };
    }

    std::array<float, 3> normal{
        (e * i - f * h) * x + (f * g - d * i) * y + (d * h - e * g) * z,
        (c * h - b * i) * x + (a * i - c * g) * y + (b * g - a * h) * z,
        (b * f - c * e) * x + (c * d - a * f) * y + (a * e - b * d) * z,
    };
    normal[0] /= determinant;
    normal[1] /= determinant;
    normal[2] /= determinant;
    return NormalizeVector(normal, std::array<float, 3>{ 0.0F, 1.0F, 0.0F });
}

void AppendGltfIndex(RenderMeshAssetData& asset, std::uint32_t index) {
    asset.indices32.push_back(index);
}

[[nodiscard]] bool AppendGltfPrimitive(
    RenderMeshAssetData& asset,
    const cgltf_primitive& primitive,
    const float nodeToWorld[16],
    const RenderMeshGltfImportDesc& desc) {
    if (primitive.type != cgltf_primitive_type_triangles) {
        return true;
    }

    const cgltf_accessor* positions = nullptr;
    const cgltf_accessor* normals = nullptr;
    const cgltf_accessor* tangents = nullptr;
    const cgltf_accessor* texCoords = nullptr;
    bool hasSkinningAttributes = false;
    for (cgltf_size attributeIndex = 0U; attributeIndex < primitive.attributes_count; ++attributeIndex) {
        const cgltf_attribute& attribute = primitive.attributes[attributeIndex];
        const std::string_view attributeName = attribute.name == nullptr ? std::string_view{} : std::string_view{ attribute.name };
        if (attribute.type == cgltf_attribute_type_position && attribute.index == 0) {
            positions = attribute.data;
        } else if (attribute.type == cgltf_attribute_type_normal && attribute.index == 0) {
            normals = attribute.data;
        } else if (attribute.type == cgltf_attribute_type_tangent || attributeName == "TANGENT") {
            tangents = attribute.data;
        } else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0) {
            texCoords = attribute.data;
        } else if (attribute.type == cgltf_attribute_type_joints || attribute.type == cgltf_attribute_type_weights) {
            hasSkinningAttributes = true;
        }
    }
    if (hasSkinningAttributes) {
        return false;
    }

    std::optional<GltfAccessorFloats> positionData = UnpackFloats(positions, 3U);
    if (!positionData.has_value()) {
        return false;
    }
    std::optional<GltfAccessorFloats> normalData = UnpackFloats(normals, 3U);
    std::optional<GltfAccessorFloats> tangentData = UnpackFloats(tangents, 4U);
    std::optional<GltfAccessorFloats> texCoordData = UnpackFloats(texCoords, 2U);
    if (normalData.has_value() && normalData->elementCount != positionData->elementCount) {
        return false;
    }
    if (tangentData.has_value() && tangentData->elementCount != positionData->elementCount) {
        return false;
    }
    if (texCoordData.has_value() && texCoordData->elementCount != positionData->elementCount) {
        return false;
    }
    if (tangentData.has_value()) {
        EnsureTangentVertexStorage(asset);
    }

    const std::string_view materialName = primitive.material != nullptr && primitive.material->name != nullptr
        ? std::string_view{ primitive.material->name }
        : std::string_view{};
    const std::uint32_t materialSlot = EnsureMaterialSlot(asset, materialName, primitive.material, desc);
    const std::uint32_t sectionStart = static_cast<std::uint32_t>(asset.indices32.size());
    const bool useTangentFormat = tangentData.has_value() || !asset.tangentVertices.empty();
    const std::uint32_t baseVertex = static_cast<std::uint32_t>(useTangentFormat ? asset.tangentVertices.size() : asset.vertices.size());
    if (useTangentFormat) {
        asset.tangentVertices.reserve(asset.tangentVertices.size() + positionData->elementCount);
    } else {
        asset.vertices.reserve(asset.vertices.size() + positionData->elementCount);
    }
    for (std::uint32_t vertexIndex = 0U; vertexIndex < positionData->elementCount; ++vertexIndex) {
        const std::array<float, 3> position = TransformPosition(
            nodeToWorld,
            AccessorValue(*positionData, vertexIndex, 0U),
            AccessorValue(*positionData, vertexIndex, 1U),
            AccessorValue(*positionData, vertexIndex, 2U));
        const std::array<float, 3> normal = normalData.has_value()
            ? TransformSurfaceNormal(
                  nodeToWorld,
                  AccessorValue(*normalData, vertexIndex, 0U),
                  AccessorValue(*normalData, vertexIndex, 1U),
                  AccessorValue(*normalData, vertexIndex, 2U))
            : std::array<float, 3>{ 0.0F, 1.0F, 0.0F };
        const float u = texCoordData.has_value() ? AccessorValue(*texCoordData, vertexIndex, 0U) : 0.0F;
        const float v = texCoordData.has_value() ? AccessorValue(*texCoordData, vertexIndex, 1U) : 0.0F;
        if (useTangentFormat) {
            const std::array<float, 3> tangent = tangentData.has_value()
                ? TransformDirection(
                      nodeToWorld,
                      AccessorValue(*tangentData, vertexIndex, 0U),
                      AccessorValue(*tangentData, vertexIndex, 1U),
                      AccessorValue(*tangentData, vertexIndex, 2U))
                : std::array<float, 3>{ 1.0F, 0.0F, 0.0F };
            asset.tangentVertices.push_back(RenderStaticMeshVertexP3N3T4UV2{
                .x = position[0],
                .y = position[1],
                .z = position[2],
                .nx = normal[0],
                .ny = normal[1],
                .nz = normal[2],
                .tx = tangent[0],
                .ty = tangent[1],
                .tz = tangent[2],
                .tw = tangentData.has_value() ? AccessorValue(*tangentData, vertexIndex, 3U) : 1.0F,
                .u = u,
                .v = desc.flipV ? 1.0F - v : v,
            });
        } else {
            asset.vertices.push_back(RenderStaticMeshVertexP3N3UV2{
                .x = position[0],
                .y = position[1],
                .z = position[2],
                .nx = normal[0],
                .ny = normal[1],
                .nz = normal[2],
                .u = u,
                .v = desc.flipV ? 1.0F - v : v,
            });
        }
    }

    if (primitive.indices != nullptr) {
        if (primitive.indices->count % 3U != 0U) {
            return false;
        }
        asset.indices32.reserve(asset.indices32.size() + primitive.indices->count);
        for (cgltf_size index = 0U; index < primitive.indices->count; ++index) {
            const cgltf_size sourceIndex = cgltf_accessor_read_index(primitive.indices, index);
            if (sourceIndex >= positionData->elementCount) {
                return false;
            }
            AppendGltfIndex(asset, baseVertex + static_cast<std::uint32_t>(sourceIndex));
        }
    } else {
        if (positionData->elementCount % 3U != 0U) {
            return false;
        }
        asset.indices32.reserve(asset.indices32.size() + positionData->elementCount);
        for (std::uint32_t index = 0U; index < positionData->elementCount; ++index) {
            AppendGltfIndex(asset, baseVertex + index);
        }
    }

    const std::uint32_t sectionEnd = static_cast<std::uint32_t>(asset.indices32.size());
    if (sectionEnd > sectionStart) {
        asset.sections.push_back(RenderMeshSectionDesc{
            .indexStart = sectionStart,
            .indexCount = sectionEnd - sectionStart,
            .materialSlot = materialSlot,
        });
    }
    return true;
}

[[nodiscard]] bool ProcessGltfNode(RenderMeshAssetData& asset, const cgltf_node& node, const RenderMeshGltfImportDesc& desc) {
    if (node.skin != nullptr) {
        return false;
    }

    float nodeToWorld[16]{};
    cgltf_node_transform_world(&node, nodeToWorld);
    if (node.mesh != nullptr) {
        for (cgltf_size primitiveIndex = 0U; primitiveIndex < node.mesh->primitives_count; ++primitiveIndex) {
            if (!AppendGltfPrimitive(asset, node.mesh->primitives[primitiveIndex], nodeToWorld, desc)) {
                return false;
            }
        }
    }
    for (cgltf_size childIndex = 0U; childIndex < node.children_count; ++childIndex) {
        if (node.children[childIndex] == nullptr || !ProcessGltfNode(asset, *node.children[childIndex], desc)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ProcessGltfScene(RenderMeshAssetData& asset, const cgltf_scene& scene, const RenderMeshGltfImportDesc& desc) {
    for (cgltf_size nodeIndex = 0U; nodeIndex < scene.nodes_count; ++nodeIndex) {
        if (scene.nodes[nodeIndex] == nullptr || !ProcessGltfNode(asset, *scene.nodes[nodeIndex], desc)) {
            return false;
        }
    }
    return true;
}

} // namespace

RenderMeshDesc& RenderMeshAssetData::RefreshDesc() noexcept {
    const bool hasTangents = !tangentVertices.empty();
    const void* vertexData = hasTangents
        ? (tangentVertices.empty() ? nullptr : static_cast<const void*>(tangentVertices.data()))
        : (vertices.empty() ? nullptr : static_cast<const void*>(vertices.data()));
    desc = RenderMeshDesc{
        .vertexData = vertexData,
        .vertexCount = static_cast<std::uint32_t>(hasTangents ? tangentVertices.size() : vertices.size()),
        .indices = indices16.empty() ? nullptr : indices16.data(),
        .indices32 = indices32.empty() ? nullptr : indices32.data(),
        .indexCount = static_cast<std::uint32_t>(indices16.empty() ? indices32.size() : indices16.size()),
        .vertexFormat = hasTangents ? RenderVertexFormat::P3N3T4UV2 : RenderVertexFormat::P3N3UV2,
        .indexFormat = indices16.empty() ? RenderIndexFormat::Uint32 : RenderIndexFormat::Uint16,
        .sections = sections.empty() ? nullptr : sections.data(),
        .sectionCount = static_cast<std::uint32_t>(sections.size()),
        .materialSlots = materialSlots.empty() ? nullptr : materialSlots.data(),
        .materialSlotCount = static_cast<std::uint32_t>(materialSlots.size()),
        .bounds = bounds,
    };
    return desc;
}

std::optional<RenderMeshAssetData> RenderMeshAssetBuilder::LoadObj(const std::filesystem::path& path, const RenderMeshObjImportDesc& desc) {
    std::ifstream input{ path };
    if (!input) {
        return std::nullopt;
    }
    return LoadObj(input, desc);
}

std::optional<RenderMeshAssetData> RenderMeshAssetBuilder::LoadObj(std::istream& input, const RenderMeshObjImportDesc& desc) {
    RenderMeshAssetData asset{};
    ObjImportContext context{};
    context.currentMaterialSlot = EnsureMaterialSlot(asset, {}, desc);

    std::string line;
    while (std::getline(input, line)) {
        std::string_view trimmed = Trim(StripComment(line));
        if (trimmed.empty()) {
            continue;
        }

        const std::size_t keywordEnd = trimmed.find_first_of(" \t");
        const std::string_view keyword = keywordEnd == std::string_view::npos ? trimmed : trimmed.substr(0U, keywordEnd);
        const std::string_view rest = keywordEnd == std::string_view::npos ? std::string_view{} : Trim(trimmed.substr(keywordEnd + 1U));
        if (keyword == "v") {
            Vec3 position{};
            if (!ParseVec3Line(rest, position)) {
                return std::nullopt;
            }
            context.positions.push_back(position);
        } else if (keyword == "vt") {
            Vec2 texCoord{};
            if (!ParseVec2Line(rest, texCoord, desc.flipV)) {
                return std::nullopt;
            }
            context.texCoords.push_back(texCoord);
        } else if (keyword == "vn") {
            Vec3 normal{};
            if (!ParseVec3Line(rest, normal)) {
                return std::nullopt;
            }
            context.normals.push_back(normal);
        } else if (keyword == "usemtl") {
            FinishSection(asset, context);
            context.currentMaterialSlot = EnsureMaterialSlot(asset, rest, desc);
        } else if (keyword == "f") {
            if (!ParseFace(rest, asset, context)) {
                return std::nullopt;
            }
        }
    }

    FinishSection(asset, context);
    if (asset.vertices.empty() || asset.indices32.empty()) {
        return std::nullopt;
    }

    if (!FinalizeMeshAsset(asset)) {
        return std::nullopt;
    }
    return asset;
}

std::optional<RenderMeshAssetData> RenderMeshAssetBuilder::LoadGltf(const std::filesystem::path& path, const RenderMeshGltfImportDesc& desc) {
    const std::string pathString = path.string();
    cgltf_options options{};
    cgltf_data* rawData = nullptr;
    if (cgltf_parse_file(&options, pathString.c_str(), &rawData) != cgltf_result_success || rawData == nullptr) {
        return std::nullopt;
    }
    const std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(rawData, &cgltf_free);
    if (cgltf_load_buffers(&options, data.get(), pathString.c_str()) != cgltf_result_success) {
        return std::nullopt;
    }
    if (cgltf_validate(data.get()) != cgltf_result_success) {
        return std::nullopt;
    }

    RenderMeshAssetData asset{};
    bool imported = false;
    if (data->scene != nullptr) {
        imported = ProcessGltfScene(asset, *data->scene, desc);
    } else {
        imported = true;
        for (cgltf_size sceneIndex = 0U; sceneIndex < data->scenes_count; ++sceneIndex) {
            imported = imported && ProcessGltfScene(asset, data->scenes[sceneIndex], desc);
        }
    }
    if (!imported || (asset.vertices.empty() && asset.tangentVertices.empty()) || asset.indices32.empty()) {
        return std::nullopt;
    }

    if (!FinalizeMeshAsset(asset)) {
        return std::nullopt;
    }
    return asset;
}

} // namespace kb::render
