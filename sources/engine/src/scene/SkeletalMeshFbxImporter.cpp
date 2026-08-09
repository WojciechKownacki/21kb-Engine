#include "engine/scene/SkeletalMeshFbxImporter.hpp"

#include <ufbx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kb::scene {
namespace {

struct SceneDeleter {
    void operator()(ufbx_scene* scene) const noexcept { ufbx_free_scene(scene); }
};
using FbxScene = std::unique_ptr<ufbx_scene, SceneDeleter>;

template <typename T>
[[nodiscard]] std::optional<T> Fail(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return std::nullopt;
}

[[nodiscard]] std::string String(const ufbx_string value, std::string fallback = {}) {
    if (value.data == nullptr || value.length == 0U) return fallback;
    return { value.data, value.length };
}

[[nodiscard]] kb::math::Vec3 Vec3(const ufbx_vec3 value) noexcept {
    return { static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z) };
}

[[nodiscard]] ufbx_matrix BoneBindToWorld(const ufbx_skin_cluster& cluster) noexcept {
    const ufbx_matrix boneToGeometry = ufbx_matrix_invert(&cluster.geometry_to_bone);
    return ufbx_matrix_mul(&cluster.geometry_to_world, &boneToGeometry);
}

[[nodiscard]] ufbx_matrix RelativeMatrix(
    const ufbx_matrix& childWorld,
    const ufbx_matrix* parentWorld) noexcept {
    if (parentWorld == nullptr) return childWorld;
    const ufbx_matrix worldToParent = ufbx_matrix_invert(parentWorld);
    return ufbx_matrix_mul(&worldToParent, &childWorld);
}

[[nodiscard]] LocalTransform Transform(const ufbx_transform value) noexcept {
    return {
        .position = Vec3(value.translation),
        .rotation = kb::math::Normalize({ static_cast<float>(value.rotation.x),
            static_cast<float>(value.rotation.y), static_cast<float>(value.rotation.z),
            static_cast<float>(value.rotation.w) }),
        .scale = Vec3(value.scale),
    };
}

[[nodiscard]] LocalTransform Transform(const ufbx_matrix& value) noexcept {
    return Transform(ufbx_matrix_to_transform(&value));
}

[[nodiscard]] kb::math::Mat4 Matrix(const ufbx_matrix value) noexcept {
    return { .columns = {
        { static_cast<float>(value.cols[0].x), static_cast<float>(value.cols[0].y), static_cast<float>(value.cols[0].z), 0.0F },
        { static_cast<float>(value.cols[1].x), static_cast<float>(value.cols[1].y), static_cast<float>(value.cols[1].z), 0.0F },
        { static_cast<float>(value.cols[2].x), static_cast<float>(value.cols[2].y), static_cast<float>(value.cols[2].z), 0.0F },
        { static_cast<float>(value.cols[3].x), static_cast<float>(value.cols[3].y), static_cast<float>(value.cols[3].z), 1.0F },
    } };
}

[[nodiscard]] bool Finite(const kb::math::Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool SameTransform(const LocalTransform& lhs, const LocalTransform& rhs) noexcept {
    constexpr float epsilon = 0.00001F;
    const auto close = [](float a, float b) { return std::abs(a - b) <= epsilon; };
    return close(lhs.position.x, rhs.position.x) && close(lhs.position.y, rhs.position.y) && close(lhs.position.z, rhs.position.z) &&
        close(lhs.rotation.x, rhs.rotation.x) && close(lhs.rotation.y, rhs.rotation.y) && close(lhs.rotation.z, rhs.rotation.z) && close(lhs.rotation.w, rhs.rotation.w) &&
        close(lhs.scale.x, rhs.scale.x) && close(lhs.scale.y, rhs.scale.y) && close(lhs.scale.z, rhs.scale.z);
}

[[nodiscard]] bool SameMatrix(const kb::math::Mat4& lhs, const kb::math::Mat4& rhs) noexcept {
    constexpr float epsilon = 0.00001F;
    const auto close = [](float a, float b) { return std::abs(a - b) <= epsilon; };
    for (std::size_t column = 0U; column < 4U; ++column) {
        const kb::math::Vec4& a = lhs.columns[column];
        const kb::math::Vec4& b = rhs.columns[column];
        if (!close(a.x, b.x) || !close(a.y, b.y) || !close(a.z, b.z) || !close(a.w, b.w)) return false;
    }
    return true;
}

struct Influence {
    std::uint32_t joint = 0U;
    float weight = 0.0F;
};

struct SkinnedMeshNode {
    const ufbx_node* node = nullptr;
    const ufbx_mesh* mesh = nullptr;
    const ufbx_skin_deformer* skin = nullptr;
};

[[nodiscard]] std::optional<SkeletalMeshFbxImportResult> Build(
    const ufbx_scene& scene, std::uint64_t skeletonAssetId,
    const SkeletalMeshFbxImportOptions& options, std::string* error) {
    std::vector<SkinnedMeshNode> meshNodes;
    for (std::size_t index = 0U; index < scene.nodes.count; ++index) {
        ufbx_node* node = scene.nodes.data[index];
        if (node == nullptr || node->mesh == nullptr || node->mesh->skin_deformers.count == 0U) continue;
        if (node->mesh->skin_deformers.count != 1U) return Fail<SkeletalMeshFbxImportResult>(error,
            "Skeletal FBX import requires exactly one skin deformer per mesh node.");
        const ufbx_skin_deformer* skin = node->mesh->skin_deformers.data[0];
        if (skin == nullptr || skin->clusters.count == 0U || skin->vertices.count != node->mesh->num_vertices) {
            return Fail<SkeletalMeshFbxImportResult>(error,
                "Skeletal FBX mesh has missing or inconsistent skin vertex data.");
        }
        meshNodes.push_back({ node, node->mesh, skin });
    }
    if (meshNodes.empty()) return Fail<SkeletalMeshFbxImportResult>(error,
        "Skeletal FBX import requires at least one skinned mesh node.");
    if (!options.combineMeshes && meshNodes.size() != 1U) return Fail<SkeletalMeshFbxImportResult>(error,
        "Skeletal FBX source contains multiple skinned mesh nodes; enable Combine meshes to import them as one asset.");

    SkeletalMeshFbxImportResult result{};
    std::unordered_map<const ufbx_node*, std::uint32_t> jointIndex;
    std::unordered_map<const ufbx_node*, const ufbx_skin_cluster*> clusterByBone;
    std::vector<const ufbx_node*> boneNodes;
    for (const SkinnedMeshNode& mesh : meshNodes) {
        for (std::size_t index = 0U; index < mesh.skin->clusters.count; ++index) {
            const ufbx_skin_cluster* cluster = mesh.skin->clusters.data[index];
            if (cluster == nullptr || cluster->bone_node == nullptr) return Fail<SkeletalMeshFbxImportResult>(error,
                "Skeletal FBX skin contains a null bone cluster.");
            if (!jointIndex.contains(cluster->bone_node)) {
                if (boneNodes.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1U) {
                    return Fail<SkeletalMeshFbxImportResult>(error, "Skeletal FBX skin exceeds the joint palette limit.");
                }
                jointIndex.emplace(cluster->bone_node, static_cast<std::uint32_t>(boneNodes.size()));
                clusterByBone.emplace(cluster->bone_node, cluster);
                boneNodes.push_back(cluster->bone_node);
            } else if (!SameMatrix(
                    Matrix(BoneBindToWorld(*clusterByBone.at(cluster->bone_node))),
                    Matrix(BoneBindToWorld(*cluster)))) {
                return Fail<SkeletalMeshFbxImportResult>(error,
                    "Skeletal FBX mesh nodes use incompatible bind poses and cannot be combined safely.");
            }
        }
    }
    std::unordered_map<const ufbx_node*, std::int32_t> emitted;
    std::unordered_set<const ufbx_node*> visiting;
    const auto emitBone = [&](const auto& self, const ufbx_node* node) -> bool {
        if (emitted.contains(node)) return true;
        if (!visiting.insert(node).second) return false;
        std::int32_t parent = -1;
        if (node->parent != nullptr && jointIndex.contains(node->parent)) {
            if (!self(self, node->parent)) return false;
            parent = emitted.at(node->parent);
        }
        const std::uint32_t index = jointIndex.at(node);
        const ufbx_skin_cluster& cluster = *clusterByBone.at(node);
        const ufbx_matrix bindWorld = BoneBindToWorld(cluster);
        const ufbx_matrix* parentBindWorld = nullptr;
        ufbx_matrix parentBindWorldStorage{};
        if (node->parent != nullptr && jointIndex.contains(node->parent)) {
            parentBindWorldStorage = BoneBindToWorld(*clusterByBone.at(node->parent));
            parentBindWorld = &parentBindWorldStorage;
        }
        const ufbx_matrix bindLocal = RelativeMatrix(bindWorld, parentBindWorld);
        const ufbx_matrix inverseBindWorld = ufbx_matrix_invert(&bindWorld);
        SkeletonBone bone{};
        bone.id = static_cast<SkeletonBoneId>(index + 1U);
        bone.parentIndex = parent;
        bone.name = String(node->name, "Bone_" + std::to_string(index));
        bone.referencePose = Transform(bindLocal);
        bone.inverseBind = Matrix(inverseBindWorld);
        emitted.emplace(node, static_cast<std::int32_t>(result.skeleton.bones.size()));
        result.skeleton.bones.push_back(std::move(bone));
        visiting.erase(node);
        return true;
    };
    for (const ufbx_node* boneNode : boneNodes) {
        if (!emitBone(emitBone, boneNode)) {
            return Fail<SkeletalMeshFbxImportResult>(error, "Skeletal FBX skin has a cyclic bone hierarchy.");
        }
    }
    const SkeletonAssetValidationResult skeletonValidation = ValidateSkeletonAsset(result.skeleton);
    if (!skeletonValidation.valid) return Fail<SkeletalMeshFbxImportResult>(error, skeletonValidation.error);

    SkeletalMeshLod lod{};
    std::vector<SkeletalMeshSection> sections;
    std::optional<std::uint32_t> previousSourceMaterial;
    const auto beginSection = [&](std::uint32_t sourceMaterial, std::uint64_t materialAssetId) {
        if (previousSourceMaterial == sourceMaterial) return;
        if (!sections.empty()) {
            sections.back().indexCount = static_cast<std::uint32_t>(lod.indices.size()) - sections.back().firstIndex;
        }
        sections.push_back({ .firstIndex = static_cast<std::uint32_t>(lod.indices.size()),
            .materialAssetId = materialAssetId });
        previousSourceMaterial = sourceMaterial;
    };
    for (const SkinnedMeshNode& skinnedMesh : meshNodes) {
      const ufbx_node& meshNode = *skinnedMesh.node;
      const ufbx_mesh& sourceMesh = *skinnedMesh.mesh;
      const ufbx_skin_deformer& skin = *skinnedMesh.skin;
      const ufbx_skin_cluster* firstCluster = skin.clusters.count == 0U ? nullptr : skin.clusters.data[0];
      if (firstCluster == nullptr) return Fail<SkeletalMeshFbxImportResult>(error,
          "Skeletal FBX mesh has no bind-space transform.");
      const ufbx_matrix bindGeometryToWorld = firstCluster->geometry_to_world;
      const ufbx_matrix bindNormalToWorld = ufbx_matrix_for_normals(&bindGeometryToWorld);
      previousSourceMaterial.reset();
      for (std::size_t faceIndex = 0U; faceIndex < sourceMesh.faces.count; ++faceIndex) {
        const ufbx_face face = sourceMesh.faces.data[faceIndex];
        if (face.num_indices < 3U) continue;
        const std::uint32_t materialSlot = options.importMaterialSlots && sourceMesh.face_material.count > faceIndex
            ? sourceMesh.face_material.data[faceIndex] : 0U;
        std::uint64_t materialAssetId = 0U;
        if (options.importMaterialSlots && options.materialResolver != nullptr && materialSlot < meshNode.materials.count) {
            const ufbx_material* material = meshNode.materials.data[materialSlot];
            const std::string materialName = material == nullptr
                ? "Material_" + std::to_string(materialSlot)
                : String(material->name, "Material_" + std::to_string(materialSlot));
            materialAssetId = options.materialResolver(materialName, options.materialResolverUserData);
            if (materialAssetId == 0U) return Fail<SkeletalMeshFbxImportResult>(error,
                "Skeletal FBX material resolver returned an invalid Material asset id.");
        }
        beginSection(materialSlot, materialAssetId);
        std::vector<std::uint32_t> triangles((static_cast<std::size_t>(face.num_indices) - 2U) * 3U);
        const std::uint32_t triangleCount = ufbx_triangulate_face(
            triangles.data(), triangles.size(), &sourceMesh, face);
        if (triangleCount == 0U) return Fail<SkeletalMeshFbxImportResult>(error, "Skeletal FBX contains an untriangulatable face.");
        for (std::size_t triIndex = 0U; triIndex < static_cast<std::size_t>(triangleCount) * 3U; ++triIndex) {
            const std::uint32_t corner = triangles[triIndex];
            if (corner >= sourceMesh.num_indices) return Fail<SkeletalMeshFbxImportResult>(error, "Skeletal FBX contains an invalid triangle index.");
            const std::uint32_t logicalVertex = sourceMesh.vertex_indices.data[corner];
            if (logicalVertex >= skin.vertices.count) return Fail<SkeletalMeshFbxImportResult>(error, "Skeletal FBX contains a skin vertex outside the mesh.");
            const ufbx_skin_vertex sourceVertex = skin.vertices.data[logicalVertex];
            if (sourceVertex.num_weights != 0U &&
                static_cast<std::size_t>(sourceVertex.weight_begin) + sourceVertex.num_weights > skin.weights.count) {
                return Fail<SkeletalMeshFbxImportResult>(error, "Skeletal FBX contains a skin-weight range outside the source mesh.");
            }
            std::vector<Influence> influences;
            influences.reserve(std::max<std::size_t>(sourceVertex.num_weights, 1U));
            if (sourceVertex.num_weights == 0U) {
                // FBX permits partially skinned meshes. Keep such vertices
                // deterministic by binding them to the first cluster rather
                // than emitting invalid zero-weight runtime vertices.
                const ufbx_skin_cluster* fallbackCluster = skin.clusters.data[0];
                if (fallbackCluster == nullptr || fallbackCluster->bone_node == nullptr ||
                    !jointIndex.contains(fallbackCluster->bone_node)) {
                    return Fail<SkeletalMeshFbxImportResult>(error,
                        "Skeletal FBX mesh has no valid fallback bone for an unweighted vertex.");
                }
                influences.push_back({ jointIndex.at(fallbackCluster->bone_node), 1.0F });
            }
            for (std::size_t weightIndex = 0U; weightIndex < sourceVertex.num_weights; ++weightIndex) {
                const ufbx_skin_weight weight = skin.weights.data[sourceVertex.weight_begin + weightIndex];
                if (weight.cluster_index >= skin.clusters.count || !std::isfinite(weight.weight) || weight.weight < 0.0) {
                    return Fail<SkeletalMeshFbxImportResult>(error, "Skeletal FBX contains an invalid skin weight.");
                }
                const ufbx_skin_cluster* cluster = skin.clusters.data[weight.cluster_index];
                if (cluster == nullptr || cluster->bone_node == nullptr || !jointIndex.contains(cluster->bone_node)) {
                    return Fail<SkeletalMeshFbxImportResult>(error, "Skeletal FBX skin weight references an unknown bone cluster.");
                }
                influences.push_back({ jointIndex.at(cluster->bone_node), static_cast<float>(weight.weight) });
            }
            std::sort(influences.begin(), influences.end(), [](const Influence& lhs, const Influence& rhs) {
                return lhs.weight == rhs.weight ? lhs.joint < rhs.joint : lhs.weight > rhs.weight;
            });
            SkeletalMeshVertex vertex{};
            vertex.position = Vec3(ufbx_transform_position(
                &bindGeometryToWorld,
                ufbx_get_vertex_vec3(&sourceMesh.vertex_position, corner)));
            if (sourceMesh.vertex_normal.exists) {
                vertex.normal = kb::math::Normalize(Vec3(ufbx_transform_direction(
                    &bindNormalToWorld,
                    ufbx_get_vertex_vec3(&sourceMesh.vertex_normal, corner))));
            }
            if (sourceMesh.vertex_tangent.exists) {
                const kb::math::Vec3 tangent = kb::math::Normalize(Vec3(ufbx_transform_direction(
                    &bindGeometryToWorld,
                    ufbx_get_vertex_vec3(&sourceMesh.vertex_tangent, corner))));
                vertex.tangent = { tangent.x, tangent.y, tangent.z, sourceMesh.reversed_winding ? -1.0F : 1.0F };
            }
            if (sourceMesh.vertex_uv.exists) {
                const ufbx_vec2 uv = ufbx_get_vertex_vec2(&sourceMesh.vertex_uv, corner);
                vertex.uv = { static_cast<float>(uv.x), static_cast<float>(uv.y) };
            }
            float sum = 0.0F;
            for (std::size_t influenceIndex = 0U; influenceIndex < vertex.jointWeights.size() && influenceIndex < influences.size(); ++influenceIndex) {
                vertex.jointIndices[influenceIndex] = static_cast<std::uint16_t>(influences[influenceIndex].joint);
                vertex.jointWeights[influenceIndex] = influences[influenceIndex].weight;
                sum += influences[influenceIndex].weight;
            }
            if (!Finite(vertex.position) || !Finite(vertex.normal) || !std::isfinite(sum) || sum <= 0.0F) {
                return Fail<SkeletalMeshFbxImportResult>(error, "Skeletal FBX contains non-finite vertex data or zero skin weight.");
            }
            for (float& weight : vertex.jointWeights) weight /= sum;
            lod.indices.push_back(static_cast<std::uint32_t>(lod.vertices.size()));
            lod.vertices.push_back(vertex);
        }
      }
    }
    if (lod.vertices.empty()) return Fail<SkeletalMeshFbxImportResult>(error, "Skeletal FBX contains no triangle geometry.");
    if (!sections.empty()) {
        sections.back().indexCount = static_cast<std::uint32_t>(lod.indices.size()) - sections.back().firstIndex;
    }
    for (SkeletalMeshSection& section : sections) {
        for (std::uint32_t index = section.firstIndex; index < section.firstIndex + section.indexCount; ++index) {
            const SkeletalMeshVertex& vertex = lod.vertices[lod.indices[index]];
            for (std::size_t influence = 0U; influence < vertex.jointWeights.size(); ++influence) {
                if (vertex.jointWeights[influence] <= 0.0F) continue;
                const SkeletonBoneId boneId = static_cast<SkeletonBoneId>(vertex.jointIndices[influence]) + 1U;
                if (std::find(section.boneMap.begin(), section.boneMap.end(), boneId) == section.boneMap.end()) section.boneMap.push_back(boneId);
            }
        }
        if (section.boneMap.size() > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1U) {
            return Fail<SkeletalMeshFbxImportResult>(error, "Skeletal FBX section exceeds the joint palette limit.");
        }
        for (std::uint32_t index = section.firstIndex; index < section.firstIndex + section.indexCount; ++index) {
            SkeletalMeshVertex& vertex = lod.vertices[lod.indices[index]];
            for (std::size_t influence = 0U; influence < vertex.jointWeights.size(); ++influence) {
                if (vertex.jointWeights[influence] <= 0.0F) { vertex.jointIndices[influence] = 0U; continue; }
                const SkeletonBoneId boneId = static_cast<SkeletonBoneId>(vertex.jointIndices[influence]) + 1U;
                vertex.jointIndices[influence] = static_cast<std::uint16_t>(std::distance(section.boneMap.begin(),
                    std::find(section.boneMap.begin(), section.boneMap.end(), boneId)));
            }
        }
    }
    lod.sections = std::move(sections);
    for (const SkeletalMeshSection& section : lod.sections) lod.requiredBones.insert(lod.requiredBones.end(), section.boneMap.begin(), section.boneMap.end());
    std::sort(lod.requiredBones.begin(), lod.requiredBones.end());
    lod.requiredBones.erase(std::unique(lod.requiredBones.begin(), lod.requiredBones.end()), lod.requiredBones.end());
    BuildSkeletalMeshLodBoneBounds(lod);
    result.mesh.skeletonAssetId = skeletonAssetId;
    result.mesh.skeletonCompatibilitySignature = SkeletonCompatibilitySignature(result.skeleton);
    result.mesh.lods.push_back(std::move(lod));
    kb::math::Vec3 minimum = result.mesh.lods[0].vertices.front().position, maximum = minimum;
    for (const SkeletalMeshVertex& vertex : result.mesh.lods[0].vertices) {
        minimum.x = std::min(minimum.x, vertex.position.x); minimum.y = std::min(minimum.y, vertex.position.y); minimum.z = std::min(minimum.z, vertex.position.z);
        maximum.x = std::max(maximum.x, vertex.position.x); maximum.y = std::max(maximum.y, vertex.position.y); maximum.z = std::max(maximum.z, vertex.position.z);
    }
    result.mesh.conservativeBounds = { .center = (minimum + maximum) * 0.5F, .extents = (maximum - minimum) * 0.5F };
    result.mesh.fixedBounds = result.mesh.conservativeBounds;
    const SkeletalMeshAssetValidationResult meshValidation = ValidateSkeletalMeshAsset(result.mesh);
    if (!meshValidation.valid) return Fail<SkeletalMeshFbxImportResult>(error, meshValidation.error);

    const double fps = scene.settings.frames_per_second > 0.0 ? scene.settings.frames_per_second : 30.0;
    for (std::size_t stackIndex = 0U; stackIndex < scene.anim_stacks.count; ++stackIndex) {
        const ufbx_anim_stack* stack = scene.anim_stacks.data[stackIndex];
        if (stack == nullptr || stack->anim == nullptr || stack->time_end <= stack->time_begin) continue;
        const double duration = stack->time_end - stack->time_begin;
        const std::size_t sampleCount = static_cast<std::size_t>(std::ceil(duration * fps)) + 1U;
        if (sampleCount > 120001U) return Fail<SkeletalMeshFbxImportResult>(error,
            "Skeletal FBX animation exceeds the 120,000-frame import safety limit.");
        AnimationClip clip{};
        clip.durationSeconds = static_cast<float>(duration);
        clip.targetSkeletonAssetId = skeletonAssetId;
        clip.targetSkeletonCompatibilitySignature = result.mesh.skeletonCompatibilitySignature;
        std::vector<AnimationBoneTrack> tracks(result.skeleton.bones.size());
        for (std::size_t boneIndex = 0U; boneIndex < result.skeleton.bones.size(); ++boneIndex) {
            tracks[boneIndex].boneId = result.skeleton.bones[boneIndex].id;
            tracks[boneIndex].keyframes.reserve(sampleCount);
        }
        const bool requiresExactSceneEvaluation = std::ranges::any_of(
            boneNodes, [](const ufbx_node* node) {
                for (const ufbx_node* current = node; current != nullptr; current = current->parent) {
                    if (current->inherit_mode != UFBX_INHERIT_MODE_NORMAL) return true;
                }
                return false;
            });
        for (std::size_t sample = 0U; sample < sampleCount; ++sample) {
            const double time = std::min(stack->time_begin + static_cast<double>(sample) / fps, stack->time_end);
            FbxScene evaluated;
            if (requiresExactSceneEvaluation) {
                ufbx_error evaluateError{};
                evaluated.reset(ufbx_evaluate_scene(&scene, stack->anim, time, nullptr, &evaluateError));
                if (!evaluated) return Fail<SkeletalMeshFbxImportResult>(error,
                    "Skeletal FBX animation could not be evaluated.");
            }
            std::vector<ufbx_matrix> evaluatedWorld(scene.nodes.count);
            std::vector<std::uint8_t> evaluatedState(scene.nodes.count, 0U);
            const auto evaluateWorld = [&](const auto& self, const ufbx_node* node) -> const ufbx_matrix& {
                const std::size_t nodeIndex = node->typed_id;
                if (evaluatedState[nodeIndex] != 0U) return evaluatedWorld[nodeIndex];
                const ufbx_transform local = ufbx_evaluate_transform(stack->anim, node, time);
                const ufbx_matrix localMatrix = ufbx_transform_to_matrix(&local);
                evaluatedWorld[nodeIndex] = node->parent == nullptr
                    ? localMatrix
                    : ufbx_matrix_mul(&self(self, node->parent), &localMatrix);
                evaluatedState[nodeIndex] = 1U;
                return evaluatedWorld[nodeIndex];
            };
            for (std::size_t boneIndex = 0U; boneIndex < result.skeleton.bones.size(); ++boneIndex) {
                const SkeletonBone& bone = result.skeleton.bones[boneIndex];
                const ufbx_node* sourceNode = boneNodes[bone.id - 1U];
                const ufbx_node* sourceParent = sourceNode->parent != nullptr && jointIndex.contains(sourceNode->parent)
                    ? sourceNode->parent : nullptr;
                const ufbx_matrix sourceWorld = requiresExactSceneEvaluation
                    ? evaluated->nodes.data[sourceNode->typed_id]->node_to_world
                    : evaluateWorld(evaluateWorld, sourceNode);
                ufbx_matrix sourceParentWorldStorage{};
                const ufbx_matrix* sourceParentWorld = nullptr;
                if (sourceParent != nullptr) {
                    sourceParentWorldStorage = requiresExactSceneEvaluation
                        ? evaluated->nodes.data[sourceParent->typed_id]->node_to_world
                        : evaluateWorld(evaluateWorld, sourceParent);
                    sourceParentWorld = &sourceParentWorldStorage;
                }
                const ufbx_matrix localMatrix = RelativeMatrix(sourceWorld, sourceParentWorld);
                tracks[boneIndex].keyframes.push_back({
                    .timeSeconds = static_cast<float>(time - stack->time_begin),
                    .transform = Transform(localMatrix),
                });
            }
        }
        for (std::size_t boneIndex = 0U; boneIndex < result.skeleton.bones.size(); ++boneIndex) {
            const SkeletonBone& bone = result.skeleton.bones[boneIndex];
            AnimationBoneTrack& track = tracks[boneIndex];
            bool animated = false;
            for (const AnimationBoneKeyframe& key : track.keyframes) if (!SameTransform(key.transform, bone.referencePose)) { animated = true; break; }
            if (animated) clip.skeletalTracks.push_back(std::move(track));
        }
        if (!clip.skeletalTracks.empty()) result.clips.push_back(std::move(clip));
    }
    return result;
}

} // namespace

std::optional<SkeletalMeshFbxImportResult> SkeletalMeshFbxImporter::Import(
    const std::filesystem::path& path, std::uint64_t skeletonAssetId,
    const SkeletalMeshFbxImportOptions& options, std::string* error) {
    if (error != nullptr) error->clear();
    if (skeletonAssetId == 0U) return Fail<SkeletalMeshFbxImportResult>(error,
        "Skeletal FBX import requires a valid Skeleton asset id.");
    ufbx_load_opts loadOptions{};
    loadOptions.target_axes = ufbx_axes_left_handed_y_up;
    loadOptions.target_unit_meters = 1.0;
    // An explicit mirror axis keeps the handedness conversion as a reflection
    // on geometry. Without it ufbx represents the reflection as a negative
    // uniform root scale plus a 180-degree rotation, which turns Y-up rigs
    // upside down when MODIFY_GEOMETRY bakes that root scale into vertices.
    loadOptions.handedness_conversion_axis = UFBX_MIRROR_AXIS_Z;
    loadOptions.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    loadOptions.generate_missing_normals = true;
    ufbx_error loadError{};
    FbxScene scene{ ufbx_load_file(path.string().c_str(), &loadOptions, &loadError) };
    if (!scene) return Fail<SkeletalMeshFbxImportResult>(error,
        "Skeletal FBX import could not parse the source file: " + String(loadError.description, "unknown ufbx error"));
    return Build(*scene, skeletonAssetId, options, error);
}

} // namespace kb::scene
