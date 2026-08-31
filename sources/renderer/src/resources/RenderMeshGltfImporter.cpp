#include "resources/RenderMeshGltfImporter.hpp"

#include "resources/RenderMeshAssetFinalizer.hpp"
#include "resources/RenderMeshGltfMaterialImporter.hpp"
#include "resources/RenderMeshGltfTransforms.hpp"

#include <cgltf/cgltf.h>

#include <array>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::render {
namespace {

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
    const cgltf_accessor* texCoords1 = nullptr;
    const cgltf_accessor* colors = nullptr;
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
        } else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 1) {
            texCoords1 = attribute.data;
        } else if (attribute.type == cgltf_attribute_type_color && attribute.index == 0) {
            colors = attribute.data;
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
    std::optional<GltfAccessorFloats> texCoord1Data = UnpackFloats(texCoords1, 2U);
    if (texCoord1Data.has_value() && texCoord1Data->elementCount != positionData->elementCount) {
        return false;
    }
    std::optional<GltfAccessorFloats> colorData = UnpackFloats(colors, 3U); // COLOR_0 is vec3 or vec4
    if (colorData.has_value() && colorData->elementCount != positionData->elementCount) {
        return false;
    }
    const bool hasColorAlpha = colorData.has_value() && colorData->componentCount >= 4U;
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
        RenderMeshAssetFinalizer::EnsureTangentVertexStorage(asset);
    }

    const std::string_view materialName = primitive.material != nullptr && primitive.material->name != nullptr
        ? std::string_view{ primitive.material->name }
        : std::string_view{};
    const std::uint32_t materialSlot = RenderMeshGltfMaterialImporter::EnsureMaterialSlot(asset, materialName, primitive.material, desc);
    const std::uint32_t sectionStart = static_cast<std::uint32_t>(asset.indices32.size());
    const bool useTangentFormat = tangentData.has_value() || !asset.tangentVertices.empty();
    const std::uint32_t baseVertex = static_cast<std::uint32_t>(useTangentFormat ? asset.tangentVertices.size() : asset.vertices.size());
    if (useTangentFormat) {
        asset.tangentVertices.reserve(asset.tangentVertices.size() + positionData->elementCount);
    } else {
        asset.vertices.reserve(asset.vertices.size() + positionData->elementCount);
    }
    for (std::uint32_t vertexIndex = 0U; vertexIndex < positionData->elementCount; ++vertexIndex) {
        const std::array<float, 3> position = RenderMeshGltfTransforms::TransformPosition(
            nodeToWorld,
            AccessorValue(*positionData, vertexIndex, 0U),
            AccessorValue(*positionData, vertexIndex, 1U),
            AccessorValue(*positionData, vertexIndex, 2U));
        const std::array<float, 3> normal = normalData.has_value()
            ? RenderMeshGltfTransforms::TransformSurfaceNormal(
                  nodeToWorld,
                  AccessorValue(*normalData, vertexIndex, 0U),
                  AccessorValue(*normalData, vertexIndex, 1U),
                  AccessorValue(*normalData, vertexIndex, 2U))
            : std::array<float, 3>{ 0.0F, 1.0F, 0.0F };
        const float u = texCoordData.has_value() ? AccessorValue(*texCoordData, vertexIndex, 0U) : 0.0F;
        const float v = texCoordData.has_value() ? AccessorValue(*texCoordData, vertexIndex, 1U) : 0.0F;
        // Second UV set when the source provides it, else fall back to uv0 (MAT-73).
        const float u1 = texCoord1Data.has_value() ? AccessorValue(*texCoord1Data, vertexIndex, 0U) : u;
        const float v1 = texCoord1Data.has_value() ? AccessorValue(*texCoord1Data, vertexIndex, 1U) : v;
        // Per-vertex color (MAT-74), white when absent; alpha 1 when the source color is vec3.
        const float cr = colorData.has_value() ? AccessorValue(*colorData, vertexIndex, 0U) : 1.0F;
        const float cg = colorData.has_value() ? AccessorValue(*colorData, vertexIndex, 1U) : 1.0F;
        const float cb = colorData.has_value() ? AccessorValue(*colorData, vertexIndex, 2U) : 1.0F;
        const float ca = hasColorAlpha ? AccessorValue(*colorData, vertexIndex, 3U) : 1.0F;
        if (useTangentFormat) {
            const std::array<float, 3> tangent = tangentData.has_value()
                ? RenderMeshGltfTransforms::TransformDirection(
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
                .u1 = u1,
                .v1 = desc.flipV ? 1.0F - v1 : v1,
                .r = cr,
                .g = cg,
                .b = cb,
                .a = ca,
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
                .u1 = u1,
                .v1 = desc.flipV ? 1.0F - v1 : v1,
                .r = cr,
                .g = cg,
                .b = cb,
                .a = ca,
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

[[nodiscard]] std::optional<RenderMeshAssetData> BuildGltfMesh(
    cgltf_data* data,
    const RenderMeshGltfImportDesc& desc) {
    if (cgltf_validate(data) != cgltf_result_success) {
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

    if (!RenderMeshAssetFinalizer::Finalize(asset)) {
        return std::nullopt;
    }
    return asset;
}

} // namespace

std::optional<RenderMeshAssetData> RenderMeshGltfImporter::Load(const std::filesystem::path& path, const RenderMeshGltfImportDesc& desc) {
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
    return BuildGltfMesh(data.get(), desc);
}

std::optional<RenderMeshAssetData> RenderMeshGltfImporter::Load(
    std::span<const std::uint8_t> bytes,
    const std::filesystem::path& sourcePath,
    const RenderMeshGltfImportDesc& desc) {
    if (bytes.empty()) {
        return std::nullopt;
    }
    const std::string pathString = sourcePath.string();
    cgltf_options options{};
    cgltf_data* rawData = nullptr;
    if (cgltf_parse(&options, bytes.data(), bytes.size(), &rawData) != cgltf_result_success || rawData == nullptr) {
        return std::nullopt;
    }
    const std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(rawData, &cgltf_free);
    const char* const bufferBasePath = sourcePath.empty() ? nullptr : pathString.c_str();
    if (cgltf_load_buffers(&options, data.get(), bufferBasePath) != cgltf_result_success) {
        return std::nullopt;
    }
    return BuildGltfMesh(data.get(), desc);
}

std::optional<std::vector<std::filesystem::path>> RenderMeshGltfImporter::ExternalBufferUris(
    std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) return std::nullopt;
    cgltf_options options{};
    cgltf_data* rawData = nullptr;
    if (cgltf_parse(&options, bytes.data(), bytes.size(), &rawData) != cgltf_result_success ||
        rawData == nullptr) {
        return std::nullopt;
    }
    const std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(rawData, &cgltf_free);
    std::vector<std::filesystem::path> uris;
    for (cgltf_size index = 0U; index < data->buffers_count; ++index) {
        const char* const uri = data->buffers[index].uri;
        if (uri == nullptr || std::strncmp(uri, "data:", 5U) == 0) continue;
        if (std::strstr(uri, "://") != nullptr) return std::nullopt;
        std::string decoded{ uri };
        static_cast<void>(cgltf_decode_uri(decoded.data()));
        decoded.resize(std::strlen(decoded.c_str()));
        const std::filesystem::path path{ decoded };
        if (path.empty() || path.is_absolute()) return std::nullopt;
        const std::filesystem::path normalized = path.lexically_normal();
        if (std::ranges::find(uris, normalized) == uris.end()) {
            uris.push_back(normalized);
        }
    }
    return uris;
}

} // namespace kb::render
