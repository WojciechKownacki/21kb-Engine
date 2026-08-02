#include "engine/scene/SkeletalMeshGltfImporter.hpp"

#define CGLTF_IMPLEMENTATION
#include <cgltf/cgltf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace kb::scene {
namespace {

struct GltfDataDeleter {
    void operator()(cgltf_data* data) const noexcept { cgltf_free(data); }
};

using GltfData = std::unique_ptr<cgltf_data, GltfDataDeleter>;

template <typename T>
[[nodiscard]] std::optional<T> Fail(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return std::nullopt;
}

[[nodiscard]] bool ReadFloat(const cgltf_accessor* accessor, cgltf_size index,
    cgltf_float* output, cgltf_size count) {
    return accessor != nullptr && index < accessor->count &&
        cgltf_accessor_read_float(accessor, index, output, count) != 0;
}

[[nodiscard]] bool ReadUint(const cgltf_accessor* accessor, cgltf_size index,
    cgltf_uint* output, cgltf_size count) {
    return accessor != nullptr && index < accessor->count &&
        cgltf_accessor_read_uint(accessor, index, output, count) != 0;
}

[[nodiscard]] const cgltf_accessor* Attribute(
    const cgltf_primitive& primitive,
    cgltf_attribute_type type,
    cgltf_int attributeIndex = 0) {
    for (cgltf_size index = 0U; index < primitive.attributes_count; ++index) {
        const cgltf_attribute& attribute = primitive.attributes[index];
        if (attribute.type == type && attribute.index == attributeIndex) {
            return attribute.data;
        }
    }
    return nullptr;
}

[[nodiscard]] bool EqualCount(
    const cgltf_accessor* accessor,
    cgltf_size count) noexcept {
    return accessor != nullptr && accessor->count == count;
}

[[nodiscard]] bool IsFinite(const SkeletalMeshVertex& vertex) noexcept {
    const auto finite = [](float value) { return std::isfinite(value); };
    return finite(vertex.position.x) && finite(vertex.position.y) && finite(vertex.position.z) &&
        finite(vertex.normal.x) && finite(vertex.normal.y) && finite(vertex.normal.z) &&
        finite(vertex.tangent.x) && finite(vertex.tangent.y) && finite(vertex.tangent.z) &&
        finite(vertex.tangent.w) && finite(vertex.uv[0]) && finite(vertex.uv[1]);
}

struct SkinInfluence {
    std::uint32_t joint = 0U;
    float weight = 0.0F;
    std::uint32_t sourceOrder = 0U;
};

} // namespace

std::optional<SkeletalMeshGltfImportResult> SkeletalMeshGltfImporter::Import(
    const std::filesystem::path& path,
    std::uint64_t skeletonAssetId,
    std::string* error) {
    if (error != nullptr) error->clear();
    if (skeletonAssetId == 0U) {
        return Fail<SkeletalMeshGltfImportResult>(error,
            "Skeletal glTF import requires a valid Skeleton asset id.");
    }

    cgltf_options options{};
    cgltf_data* rawData = nullptr;
    if (cgltf_parse_file(&options, path.string().c_str(), &rawData) != cgltf_result_success ||
        rawData == nullptr) {
        return Fail<SkeletalMeshGltfImportResult>(error,
            "Skeletal glTF import could not parse the source file.");
    }
    GltfData data{ rawData };
    if (cgltf_load_buffers(&options, data.get(), path.string().c_str()) != cgltf_result_success) {
        return Fail<SkeletalMeshGltfImportResult>(error,
            "Skeletal glTF import could not load source buffers.");
    }
    if (data->skins_count != 1U || data->skins[0].joints_count == 0U) {
        return Fail<SkeletalMeshGltfImportResult>(error,
            "Skeletal glTF import requires exactly one non-empty skin.");
    }
    const cgltf_skin& skin = data->skins[0];
    if (skin.inverse_bind_matrices == nullptr ||
        skin.inverse_bind_matrices->count != skin.joints_count) {
        return Fail<SkeletalMeshGltfImportResult>(error,
            "Skeletal glTF import requires one inverse bind matrix per skin joint.");
    }

    std::unordered_map<const cgltf_node*, std::size_t> jointSourceIndex;
    jointSourceIndex.reserve(static_cast<std::size_t>(skin.joints_count));
    for (cgltf_size index = 0U; index < skin.joints_count; ++index) {
        if (skin.joints[index] == nullptr ||
            !jointSourceIndex.emplace(skin.joints[index], static_cast<std::size_t>(index)).second) {
            return Fail<SkeletalMeshGltfImportResult>(error,
                "Skeletal glTF skin has null or duplicate joint nodes.");
        }
    }

    SkeletalMeshGltfImportResult result{};
    std::unordered_map<const cgltf_node*, std::int32_t> emittedBone;
    emittedBone.reserve(jointSourceIndex.size());
    const auto emitBone = [&](const auto& self, const cgltf_node* node) -> bool {
        if (emittedBone.contains(node)) return true;
        std::int32_t parentIndex = -1;
        if (node->parent != nullptr && jointSourceIndex.contains(node->parent)) {
            if (!self(self, node->parent)) return false;
            parentIndex = emittedBone.at(node->parent);
        }
        const std::size_t sourceIndex = jointSourceIndex.at(node);
        SkeletonBone bone{};
        bone.id = static_cast<SkeletonBoneId>(sourceIndex + 1U);
        bone.parentIndex = parentIndex;
        bone.name = node->name == nullptr || node->name[0] == '\0'
            ? "Joint_" + std::to_string(sourceIndex)
            : std::string{ node->name };
        bone.referencePose.position = {
            node->has_translation ? node->translation[0] : 0.0F,
            node->has_translation ? node->translation[1] : 0.0F,
            node->has_translation ? node->translation[2] : 0.0F,
        };
        bone.referencePose.rotation = {
            node->has_rotation ? node->rotation[0] : 0.0F,
            node->has_rotation ? node->rotation[1] : 0.0F,
            node->has_rotation ? node->rotation[2] : 0.0F,
            node->has_rotation ? node->rotation[3] : 1.0F,
        };
        bone.referencePose.scale = {
            node->has_scale ? node->scale[0] : 1.0F,
            node->has_scale ? node->scale[1] : 1.0F,
            node->has_scale ? node->scale[2] : 1.0F,
        };
        std::array<cgltf_float, 16U> inverseBind{};
        if (!ReadFloat(skin.inverse_bind_matrices, sourceIndex, inverseBind.data(), inverseBind.size())) {
            return false;
        }
        for (std::size_t column = 0U; column < 4U; ++column) {
            bone.inverseBind.columns[column] = {
                inverseBind[column * 4U], inverseBind[column * 4U + 1U],
                inverseBind[column * 4U + 2U], inverseBind[column * 4U + 3U],
            };
        }
        emittedBone.emplace(node, static_cast<std::int32_t>(result.skeleton.bones.size()));
        result.skeleton.bones.push_back(std::move(bone));
        return true;
    };
    for (cgltf_size index = 0U; index < skin.joints_count; ++index) {
        if (!emitBone(emitBone, skin.joints[index])) {
            return Fail<SkeletalMeshGltfImportResult>(error,
                "Skeletal glTF import could not read an inverse bind matrix.");
        }
    }
    if (!ValidateSkeletonAsset(result.skeleton).valid) {
        return Fail<SkeletalMeshGltfImportResult>(error,
            "Skeletal glTF import produced an invalid Skeleton hierarchy.");
    }

    const cgltf_node* meshNode = nullptr;
    for (cgltf_size index = 0U; index < data->nodes_count; ++index) {
        if (data->nodes[index].skin == &skin && data->nodes[index].mesh != nullptr) {
            meshNode = &data->nodes[index];
            break;
        }
    }
    if (meshNode == nullptr) {
        return Fail<SkeletalMeshGltfImportResult>(error,
            "Skeletal glTF import could not find a mesh node bound to the skin.");
    }

    SkeletalMeshLod lod{};
    lod.minScreenCoverage = 0.0F;
    for (cgltf_size primitiveIndex = 0U;
         primitiveIndex < meshNode->mesh->primitives_count; ++primitiveIndex) {
        const cgltf_primitive& primitive = meshNode->mesh->primitives[primitiveIndex];
        if (primitive.type != cgltf_primitive_type_triangles) continue;
        const cgltf_accessor* positions = Attribute(primitive, cgltf_attribute_type_position);
        const cgltf_accessor* normals = Attribute(primitive, cgltf_attribute_type_normal);
        const cgltf_accessor* tangents = Attribute(primitive, cgltf_attribute_type_tangent);
        const cgltf_accessor* texCoords = Attribute(primitive, cgltf_attribute_type_texcoord);
        const cgltf_accessor* joints = Attribute(primitive, cgltf_attribute_type_joints);
        const cgltf_accessor* weights = Attribute(primitive, cgltf_attribute_type_weights);
        const cgltf_accessor* joints1 = Attribute(primitive, cgltf_attribute_type_joints, 1);
        const cgltf_accessor* weights1 = Attribute(primitive, cgltf_attribute_type_weights, 1);
        if (!EqualCount(positions, positions == nullptr ? 0U : positions->count) ||
            !EqualCount(joints, positions->count) || !EqualCount(weights, positions->count) ||
            ((joints1 == nullptr) != (weights1 == nullptr)) ||
            (joints1 != nullptr && !EqualCount(joints1, positions->count)) ||
            (weights1 != nullptr && !EqualCount(weights1, positions->count)) ||
            (normals != nullptr && !EqualCount(normals, positions->count)) ||
            (tangents != nullptr && !EqualCount(tangents, positions->count)) ||
            (texCoords != nullptr && !EqualCount(texCoords, positions->count)) ||
            positions->count == 0U) {
            return Fail<SkeletalMeshGltfImportResult>(error,
                "Skeletal glTF primitive has incomplete POSITION, JOINTS_0, or WEIGHTS_0 data.");
        }
        const std::uint32_t baseVertex = static_cast<std::uint32_t>(lod.vertices.size());
        SkeletalMeshSection section{};
        section.firstIndex = static_cast<std::uint32_t>(lod.indices.size());
        section.materialAssetId = 0U;
        section.boneMap.reserve(static_cast<std::size_t>(skin.joints_count));
        for (cgltf_size joint = 0U; joint < skin.joints_count; ++joint) {
            section.boneMap.push_back(static_cast<SkeletonBoneId>(joint + 1U));
        }
        for (cgltf_size vertexIndex = 0U; vertexIndex < positions->count; ++vertexIndex) {
            std::array<cgltf_float, 4U> position{};
            std::array<cgltf_float, 4U> normal{ 0.0F, 1.0F, 0.0F, 0.0F };
            std::array<cgltf_float, 4U> tangent{ 1.0F, 0.0F, 0.0F, 1.0F };
            std::array<cgltf_float, 4U> uv{};
            std::array<cgltf_float, 4U> weight{};
            std::array<cgltf_uint, 4U> joint{};
            if (!ReadFloat(positions, vertexIndex, position.data(), 3U) ||
                !ReadUint(joints, vertexIndex, joint.data(), joint.size()) ||
                !ReadFloat(weights, vertexIndex, weight.data(), weight.size())) {
                return Fail<SkeletalMeshGltfImportResult>(error,
                    "Skeletal glTF primitive has unreadable vertex data.");
            }
            if (normals != nullptr && !ReadFloat(normals, vertexIndex, normal.data(), 3U)) return Fail<SkeletalMeshGltfImportResult>(error, "Skeletal glTF primitive has unreadable NORMAL data.");
            if (tangents != nullptr && !ReadFloat(tangents, vertexIndex, tangent.data(), 4U)) return Fail<SkeletalMeshGltfImportResult>(error, "Skeletal glTF primitive has unreadable TANGENT data.");
            if (texCoords != nullptr && !ReadFloat(texCoords, vertexIndex, uv.data(), 2U)) return Fail<SkeletalMeshGltfImportResult>(error, "Skeletal glTF primitive has unreadable TEXCOORD_0 data.");
            SkeletalMeshVertex vertex{};
            vertex.position = { position[0], position[1], position[2] };
            vertex.normal = { normal[0], normal[1], normal[2] };
            vertex.tangent = { tangent[0], tangent[1], tangent[2], tangent[3] };
            vertex.uv = { uv[0], uv[1] };
            std::array<SkinInfluence, 8U> influences{};
            for (std::size_t influence = 0U; influence < joint.size(); ++influence) {
                if (joint[influence] >= skin.joints_count ||
                    joint[influence] > std::numeric_limits<std::uint16_t>::max() ||
                    !std::isfinite(weight[influence]) || weight[influence] < 0.0F) {
                    return Fail<SkeletalMeshGltfImportResult>(error,
                        "Skeletal glTF primitive has an invalid JOINTS_0 or WEIGHTS_0 influence.");
                }
                influences[influence] = {
                    .joint = joint[influence], .weight = weight[influence],
                    .sourceOrder = static_cast<std::uint32_t>(influence),
                };
            }
            if (joints1 != nullptr) {
                std::array<cgltf_float, 4U> weight1{};
                std::array<cgltf_uint, 4U> joint1{};
                if (!ReadUint(joints1, vertexIndex, joint1.data(), joint1.size()) ||
                    !ReadFloat(weights1, vertexIndex, weight1.data(), weight1.size())) {
                    return Fail<SkeletalMeshGltfImportResult>(error,
                        "Skeletal glTF primitive has unreadable JOINTS_1 or WEIGHTS_1 data.");
                }
                for (std::size_t influence = 0U; influence < joint1.size(); ++influence) {
                    if (joint1[influence] >= skin.joints_count ||
                        joint1[influence] > std::numeric_limits<std::uint16_t>::max() ||
                        !std::isfinite(weight1[influence]) || weight1[influence] < 0.0F) {
                        return Fail<SkeletalMeshGltfImportResult>(error,
                            "Skeletal glTF primitive has an invalid JOINTS_1 or WEIGHTS_1 influence.");
                    }
                    influences[influence + 4U] = {
                        .joint = joint1[influence], .weight = weight1[influence],
                        .sourceOrder = static_cast<std::uint32_t>(influence + 4U),
                    };
                }
            }
            std::sort(influences.begin(), influences.end(),
                [](const SkinInfluence& lhs, const SkinInfluence& rhs) {
                    if (lhs.weight != rhs.weight) return lhs.weight > rhs.weight;
                    if (lhs.joint != rhs.joint) return lhs.joint < rhs.joint;
                    return lhs.sourceOrder < rhs.sourceOrder;
                });
            float weightSum = 0.0F;
            for (std::size_t influence = 0U; influence < vertex.jointWeights.size(); ++influence) {
                vertex.jointIndices[influence] = static_cast<std::uint16_t>(influences[influence].joint);
                vertex.jointWeights[influence] = influences[influence].weight;
                weightSum += vertex.jointWeights[influence];
            }
            if (!std::isfinite(weightSum) || weightSum <= 0.0F) {
                return Fail<SkeletalMeshGltfImportResult>(error,
                    "Skeletal glTF primitive has a zero-weight skin binding.");
            }
            for (float& influenceWeight : vertex.jointWeights) {
                influenceWeight /= weightSum;
            }
            if (!IsFinite(vertex)) return Fail<SkeletalMeshGltfImportResult>(error, "Skeletal glTF primitive has non-finite vertex data.");
            lod.vertices.push_back(vertex);
        }
        const cgltf_size indexCount = primitive.indices == nullptr
            ? positions->count
            : primitive.indices->count;
        if (indexCount == 0U || indexCount % 3U != 0U) return Fail<SkeletalMeshGltfImportResult>(error, "Skeletal glTF primitive has non-triangle indices.");
        for (cgltf_size index = 0U; index < indexCount; ++index) {
            const cgltf_size sourceIndex = primitive.indices == nullptr
                ? index
                : cgltf_accessor_read_index(primitive.indices, index);
            if (sourceIndex >= positions->count) return Fail<SkeletalMeshGltfImportResult>(error, "Skeletal glTF primitive index is outside its vertex range.");
            lod.indices.push_back(baseVertex + static_cast<std::uint32_t>(sourceIndex));
        }
        section.indexCount = static_cast<std::uint32_t>(lod.indices.size()) - section.firstIndex;
        lod.sections.push_back(std::move(section));
    }
    if (lod.vertices.empty() || lod.indices.empty()) {
        return Fail<SkeletalMeshGltfImportResult>(error,
            "Skeletal glTF import found no triangle primitives.");
    }
    std::unordered_set<SkeletonBoneId> required;
    for (const SkeletalMeshSection& section : lod.sections) {
        required.insert(section.boneMap.begin(), section.boneMap.end());
    }
    lod.requiredBones.assign(required.begin(), required.end());
    std::sort(lod.requiredBones.begin(), lod.requiredBones.end());
    result.mesh.skeletonAssetId = skeletonAssetId;
    result.mesh.skeletonCompatibilitySignature =
        SkeletonCompatibilitySignature(result.skeleton);
    result.mesh.lods.push_back(std::move(lod));
    kb::math::Vec3 minimum = result.mesh.lods[0].vertices.front().position;
    kb::math::Vec3 maximum = minimum;
    for (const SkeletalMeshVertex& vertex : result.mesh.lods[0].vertices) {
        minimum.x = std::min(minimum.x, vertex.position.x); minimum.y = std::min(minimum.y, vertex.position.y); minimum.z = std::min(minimum.z, vertex.position.z);
        maximum.x = std::max(maximum.x, vertex.position.x); maximum.y = std::max(maximum.y, vertex.position.y); maximum.z = std::max(maximum.z, vertex.position.z);
    }
    result.mesh.conservativeBounds.center = { (minimum.x + maximum.x) * 0.5F, (minimum.y + maximum.y) * 0.5F, (minimum.z + maximum.z) * 0.5F };
    result.mesh.conservativeBounds.extents = { (maximum.x - minimum.x) * 0.5F, (maximum.y - minimum.y) * 0.5F, (maximum.z - minimum.z) * 0.5F };
    const SkeletalMeshAssetValidationResult validation = ValidateSkeletalMeshAsset(result.mesh);
    if (!validation.valid) return Fail<SkeletalMeshGltfImportResult>(error, validation.error);
    return result;
}

} // namespace kb::scene
