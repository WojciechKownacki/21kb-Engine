#include "engine/scene/SkeletalMeshGltfImporter.hpp"

#define CGLTF_IMPLEMENTATION
#include <cgltf/cgltf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kb::scene {
namespace {

struct GltfDataDeleter {
    void operator()(cgltf_data* data) const noexcept { cgltf_free(data); }
};

using GltfData = std::unique_ptr<cgltf_data, GltfDataDeleter>;

struct ImportContext {
    std::string mesh;
    std::string node;
    std::string bone;
    std::int32_t primitiveIndex = -1;
    std::int32_t channelIndex = -1;
};

void AddDiagnostic(SkeletalMeshGltfImportReport* report,
    SkeletalMeshGltfImportDiagnosticSeverity severity,
    const std::filesystem::path& sourcePath,
    const ImportContext& context,
    std::string message) {
    if (report == nullptr) return;
    report->diagnostics.push_back({
        .severity = severity,
        .message = std::move(message),
        .sourcePath = sourcePath,
        .mesh = context.mesh,
        .node = context.node,
        .bone = context.bone,
        .primitiveIndex = context.primitiveIndex,
        .channelIndex = context.channelIndex,
    });
}

class ErrorReporter final {
public:
    ErrorReporter(SkeletalMeshGltfImportReport* report, std::string* error,
        const std::filesystem::path& sourcePath) noexcept
        : report_(report), error_(error), sourcePath_(sourcePath) {}

    ~ErrorReporter() {
        const bool alreadyReported = report_ != nullptr && std::ranges::any_of(
            report_->diagnostics, [](const SkeletalMeshGltfImportDiagnostic& diagnostic) {
                return diagnostic.severity == SkeletalMeshGltfImportDiagnosticSeverity::Error;
            });
        if (report_ != nullptr && error_ != nullptr && !error_->empty() && !alreadyReported) {
            AddDiagnostic(report_, SkeletalMeshGltfImportDiagnosticSeverity::Error,
                sourcePath_, {}, *error_);
        }
    }

    ErrorReporter(const ErrorReporter&) = delete;
    ErrorReporter& operator=(const ErrorReporter&) = delete;

private:
    SkeletalMeshGltfImportReport* report_ = nullptr;
    std::string* error_ = nullptr;
    const std::filesystem::path& sourcePath_;
};

template <typename T>
[[nodiscard]] std::optional<T> Fail(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return std::nullopt;
}

template <typename T>
[[nodiscard]] std::optional<T> FailWithDiagnostic(std::string* error,
    SkeletalMeshGltfImportReport* report,
    const std::filesystem::path& sourcePath,
    const ImportContext& context,
    std::string message) {
    AddDiagnostic(report, SkeletalMeshGltfImportDiagnosticSeverity::Error,
        sourcePath, context, message);
    return Fail<T>(error, std::move(message));
}

[[nodiscard]] std::string NameOf(const cgltf_node* node, std::string_view fallback) {
    return node != nullptr && node->name != nullptr && node->name[0] != '\0'
        ? std::string{ node->name }
        : std::string{ fallback };
}

[[nodiscard]] std::string NameOf(const cgltf_mesh* mesh, std::string_view fallback) {
    return mesh != nullptr && mesh->name != nullptr && mesh->name[0] != '\0'
        ? std::string{ mesh->name }
        : std::string{ fallback };
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

[[nodiscard]] const cgltf_accessor* TargetAttribute(
    const cgltf_morph_target& target,
    cgltf_attribute_type type) {
    for (cgltf_size index = 0U; index < target.attributes_count; ++index) {
        const cgltf_attribute& attribute = target.attributes[index];
        if (attribute.type == type) return attribute.data;
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

struct CoordinateConversion {
    std::array<std::size_t, 3U> sourceAxisForEngine{};
    std::array<float, 3U> signForEngine{};
    float unitScale = 1.0F;
    bool reversesWinding = false;
};

[[nodiscard]] std::optional<CoordinateConversion> MakeCoordinateConversion(
    const SkeletalMeshGltfCoordinateConversion& settings,
    std::string* error) {
    CoordinateConversion conversion{};
    if (!std::isfinite(settings.unitScale) || settings.unitScale <= 0.0F) {
        return Fail<CoordinateConversion>(error,
            "Skeletal glTF coordinate conversion has an invalid unit scale.");
    }
    std::array<bool, 3U> seen{};
    float determinant = 1.0F;
    for (std::size_t engineAxis = 0U; engineAxis < conversion.sourceAxisForEngine.size(); ++engineAxis) {
        const std::size_t sourceAxis = static_cast<std::size_t>(settings.engineAxes[engineAxis]);
        const float sign = settings.engineAxisSigns[engineAxis];
        if (sourceAxis >= seen.size() || seen[sourceAxis] ||
            !std::isfinite(sign) || (sign != -1.0F && sign != 1.0F)) {
            return Fail<CoordinateConversion>(error,
                "Skeletal glTF coordinate conversion must be a signed axis permutation.");
        }
        seen[sourceAxis] = true;
        conversion.sourceAxisForEngine[engineAxis] = sourceAxis;
        conversion.signForEngine[engineAxis] = sign;
        determinant *= sign;
        for (std::size_t later = engineAxis + 1U; later < conversion.sourceAxisForEngine.size(); ++later) {
            if (static_cast<std::size_t>(settings.engineAxes[later]) < sourceAxis) determinant = -determinant;
        }
    }
    conversion.unitScale = settings.unitScale;
    conversion.reversesWinding = determinant < 0.0F;
    return conversion;
}

[[nodiscard]] kb::math::Vec3 ConvertDirection(
    const CoordinateConversion& conversion,
    const kb::math::Vec3 source) noexcept {
    const std::array<float, 3U> values{ source.x, source.y, source.z };
    return {
        conversion.signForEngine[0U] * values[conversion.sourceAxisForEngine[0U]],
        conversion.signForEngine[1U] * values[conversion.sourceAxisForEngine[1U]],
        conversion.signForEngine[2U] * values[conversion.sourceAxisForEngine[2U]],
    };
}

[[nodiscard]] kb::math::Vec3 ConvertPosition(
    const CoordinateConversion& conversion,
    const kb::math::Vec3 source) noexcept {
    return ConvertDirection(conversion, source) * conversion.unitScale;
}

[[nodiscard]] kb::math::Quat ConvertRotation(
    const CoordinateConversion& conversion,
    kb::math::Quat source) noexcept {
    source = kb::math::Normalize(source);
    const float xx = source.x * source.x;
    const float yy = source.y * source.y;
    const float zz = source.z * source.z;
    const float xy = source.x * source.y;
    const float xz = source.x * source.z;
    const float yz = source.y * source.z;
    const float wx = source.w * source.x;
    const float wy = source.w * source.y;
    const float wz = source.w * source.z;
    const float sourceMatrix[3U][3U]{
        { 1.0F - 2.0F * (yy + zz), 2.0F * (xy - wz), 2.0F * (xz + wy) },
        { 2.0F * (xy + wz), 1.0F - 2.0F * (xx + zz), 2.0F * (yz - wx) },
        { 2.0F * (xz - wy), 2.0F * (yz + wx), 1.0F - 2.0F * (xx + yy) },
    };
    float matrix[3U][3U]{};
    for (std::size_t row = 0U; row < 3U; ++row) {
        for (std::size_t column = 0U; column < 3U; ++column) {
            matrix[row][column] = conversion.signForEngine[row] * conversion.signForEngine[column] *
                sourceMatrix[conversion.sourceAxisForEngine[row]][conversion.sourceAxisForEngine[column]];
        }
    }
    kb::math::Quat result{};
    const float trace = matrix[0U][0U] + matrix[1U][1U] + matrix[2U][2U];
    if (trace > 0.0F) {
        const float scale = std::sqrt(trace + 1.0F) * 2.0F;
        result = { (matrix[2U][1U] - matrix[1U][2U]) / scale,
            (matrix[0U][2U] - matrix[2U][0U]) / scale,
            (matrix[1U][0U] - matrix[0U][1U]) / scale, 0.25F * scale };
    } else if (matrix[0U][0U] > matrix[1U][1U] && matrix[0U][0U] > matrix[2U][2U]) {
        const float scale = std::sqrt(1.0F + matrix[0U][0U] - matrix[1U][1U] - matrix[2U][2U]) * 2.0F;
        result = { 0.25F * scale, (matrix[0U][1U] + matrix[1U][0U]) / scale,
            (matrix[0U][2U] + matrix[2U][0U]) / scale, (matrix[2U][1U] - matrix[1U][2U]) / scale };
    } else if (matrix[1U][1U] > matrix[2U][2U]) {
        const float scale = std::sqrt(1.0F + matrix[1U][1U] - matrix[0U][0U] - matrix[2U][2U]) * 2.0F;
        result = { (matrix[0U][1U] + matrix[1U][0U]) / scale, 0.25F * scale,
            (matrix[1U][2U] + matrix[2U][1U]) / scale, (matrix[0U][2U] - matrix[2U][0U]) / scale };
    } else {
        const float scale = std::sqrt(1.0F + matrix[2U][2U] - matrix[0U][0U] - matrix[1U][1U]) * 2.0F;
        result = { (matrix[0U][2U] + matrix[2U][0U]) / scale, (matrix[1U][2U] + matrix[2U][1U]) / scale,
            0.25F * scale, (matrix[1U][0U] - matrix[0U][1U]) / scale };
    }
    return kb::math::Normalize(result);
}

[[nodiscard]] kb::math::Vec3 ConvertScale(
    const CoordinateConversion& conversion,
    const kb::math::Vec3 source) noexcept {
    const std::array<float, 3U> values{ source.x, source.y, source.z };
    return { values[conversion.sourceAxisForEngine[0U]], values[conversion.sourceAxisForEngine[1U]],
        values[conversion.sourceAxisForEngine[2U]] };
}

[[nodiscard]] kb::math::Mat4 ConvertInverseBind(
    const CoordinateConversion& conversion,
    const std::array<cgltf_float, 16U>& source) noexcept {
    kb::math::Mat4 basis{};
    kb::math::Mat4 inverseBasis{};
    for (kb::math::Vec4& column : basis.columns) column = {};
    for (kb::math::Vec4& column : inverseBasis.columns) column = {};
    basis.columns[3U].w = 1.0F;
    inverseBasis.columns[3U].w = 1.0F;
    for (std::size_t engineAxis = 0U; engineAxis < 3U; ++engineAxis) {
        const std::size_t sourceAxis = conversion.sourceAxisForEngine[engineAxis];
        if (engineAxis == 0U) basis.columns[sourceAxis].x = conversion.signForEngine[engineAxis] * conversion.unitScale;
        else if (engineAxis == 1U) basis.columns[sourceAxis].y = conversion.signForEngine[engineAxis] * conversion.unitScale;
        else basis.columns[sourceAxis].z = conversion.signForEngine[engineAxis] * conversion.unitScale;
        if (sourceAxis == 0U) inverseBasis.columns[engineAxis].x = conversion.signForEngine[engineAxis] / conversion.unitScale;
        else if (sourceAxis == 1U) inverseBasis.columns[engineAxis].y = conversion.signForEngine[engineAxis] / conversion.unitScale;
        else inverseBasis.columns[engineAxis].z = conversion.signForEngine[engineAxis] / conversion.unitScale;
    }
    kb::math::Mat4 matrix{};
    for (std::size_t column = 0U; column < 4U; ++column) {
        matrix.columns[column] = { source[column * 4U], source[column * 4U + 1U],
            source[column * 4U + 2U], source[column * 4U + 3U] };
    }
    return basis * matrix * inverseBasis;
}

[[nodiscard]] std::optional<std::uint64_t> ResolveMaterialAssetId(
    const cgltf_primitive& primitive,
    const cgltf_data& data,
    const SkeletalMeshGltfImportOptions& options,
    std::string* error) {
    if (primitive.material == nullptr) return std::uint64_t{ 0U };
    if (options.materialResolver == nullptr) {
        return Fail<std::uint64_t>(error,
            "Skeletal glTF primitive references a material but no material resolver was supplied.");
    }
    const std::ptrdiff_t materialIndex = primitive.material - data.materials;
    if (materialIndex < 0 || static_cast<cgltf_size>(materialIndex) >= data.materials_count) {
        return Fail<std::uint64_t>(error, "Skeletal glTF primitive references an invalid material.");
    }
    const std::string materialName = primitive.material->name == nullptr || primitive.material->name[0] == '\0'
        ? "Material_" + std::to_string(materialIndex)
        : std::string{ primitive.material->name };
    const std::uint64_t materialAssetId = options.materialResolver(materialName, options.materialResolverUserData);
    if (materialAssetId == 0U) {
        return Fail<std::uint64_t>(error,
            "Skeletal glTF material resolver did not return an asset id for '" + materialName + "'.");
    }
    return materialAssetId;
}

} // namespace

std::optional<SkeletalMeshGltfImportResult> SkeletalMeshGltfImporter::Import(
    const std::filesystem::path& path,
    std::uint64_t skeletonAssetId,
    const SkeletalMeshGltfImportOptions& importOptions,
    std::string* error,
    SkeletalMeshGltfImportReport* report) {
    if (report != nullptr) report->diagnostics.clear();
    std::string reportError;
    if (error == nullptr && report != nullptr) error = &reportError;
    if (error != nullptr) error->clear();
    ErrorReporter errorReporter{ report, error, path };
    if (skeletonAssetId == 0U) {
        return Fail<SkeletalMeshGltfImportResult>(error,
            "Skeletal glTF import requires a valid Skeleton asset id.");
    }
    const auto conversion = MakeCoordinateConversion(importOptions.coordinateConversion, error);
    if (!conversion) return std::nullopt;

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
        if (node->has_matrix) return false;
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
        bone.referencePose.position = ConvertPosition(*conversion, {
            node->has_translation ? node->translation[0] : 0.0F,
            node->has_translation ? node->translation[1] : 0.0F,
            node->has_translation ? node->translation[2] : 0.0F,
        });
        bone.referencePose.rotation = ConvertRotation(*conversion, {
            node->has_rotation ? node->rotation[0] : 0.0F,
            node->has_rotation ? node->rotation[1] : 0.0F,
            node->has_rotation ? node->rotation[2] : 0.0F,
            node->has_rotation ? node->rotation[3] : 1.0F,
        });
        bone.referencePose.scale = ConvertScale(*conversion, {
            node->has_scale ? node->scale[0] : 1.0F,
            node->has_scale ? node->scale[1] : 1.0F,
            node->has_scale ? node->scale[2] : 1.0F,
        });
        std::array<cgltf_float, 16U> inverseBind{};
        if (!ReadFloat(skin.inverse_bind_matrices, sourceIndex, inverseBind.data(), inverseBind.size())) {
            return false;
        }
        bone.inverseBind = ConvertInverseBind(*conversion, inverseBind);
        emittedBone.emplace(node, static_cast<std::int32_t>(result.skeleton.bones.size()));
        result.skeleton.bones.push_back(std::move(bone));
        return true;
    };
    for (cgltf_size index = 0U; index < skin.joints_count; ++index) {
        if (!emitBone(emitBone, skin.joints[index])) {
            return Fail<SkeletalMeshGltfImportResult>(error,
                "Skeletal glTF import could not read an inverse bind matrix or uses unsupported matrix-authored joints.");
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
    const ImportContext meshContext{
        .mesh = NameOf(meshNode->mesh, "Mesh_0"),
        .node = NameOf(meshNode, "MeshNode"),
    };

    SkeletalMeshLod lod{};
    lod.minScreenCoverage = 0.0F;
    for (cgltf_size primitiveIndex = 0U;
         primitiveIndex < meshNode->mesh->primitives_count; ++primitiveIndex) {
        const cgltf_primitive& primitive = meshNode->mesh->primitives[primitiveIndex];
        if (primitive.type != cgltf_primitive_type_triangles) continue;
        ImportContext primitiveContext = meshContext;
        primitiveContext.primitiveIndex = static_cast<std::int32_t>(primitiveIndex);
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
            return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, primitiveContext,
                "Skeletal glTF primitive has incomplete POSITION, JOINTS_0, or WEIGHTS_0 data.");
        }
        if (normals == nullptr) {
            AddDiagnostic(report, SkeletalMeshGltfImportDiagnosticSeverity::Warning, path,
                primitiveContext, "Skeletal glTF primitive omits NORMAL; the canonical default normal was authored.");
        }
        if (tangents == nullptr) {
            AddDiagnostic(report, SkeletalMeshGltfImportDiagnosticSeverity::Warning, path,
                primitiveContext, "Skeletal glTF primitive omits TANGENT; the canonical default tangent was authored.");
        }
        const std::uint32_t baseVertex = static_cast<std::uint32_t>(lod.vertices.size());
        SkeletalMeshSection section{};
        section.firstIndex = static_cast<std::uint32_t>(lod.indices.size());
        const auto materialAssetId = ResolveMaterialAssetId(primitive, *data, importOptions, error);
        if (!materialAssetId) return std::nullopt;
        section.materialAssetId = *materialAssetId;
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
            vertex.position = ConvertPosition(*conversion, { position[0], position[1], position[2] });
            vertex.normal = ConvertDirection(*conversion, { normal[0], normal[1], normal[2] });
            const kb::math::Vec3 tangentDirection = ConvertDirection(*conversion,
                { tangent[0], tangent[1], tangent[2] });
            vertex.tangent = { tangentDirection.x, tangentDirection.y, tangentDirection.z,
                conversion->reversesWinding ? -tangent[3] : tangent[3] };
            vertex.uv = { uv[0], uv[1] };
            std::array<SkinInfluence, 8U> influences{};
            for (std::size_t influence = 0U; influence < joint.size(); ++influence) {
                if (joint[influence] >= skin.joints_count ||
                    joint[influence] > std::numeric_limits<std::uint16_t>::max() ||
                    !std::isfinite(weight[influence]) || weight[influence] < 0.0F) {
                    return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, primitiveContext,
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
                        return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, primitiveContext,
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
                return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, primitiveContext,
                    "Skeletal glTF primitive has a zero-weight skin binding.");
            }
            for (float& influenceWeight : vertex.jointWeights) {
                influenceWeight /= weightSum;
            }
            if (!IsFinite(vertex)) return Fail<SkeletalMeshGltfImportResult>(error, "Skeletal glTF primitive has non-finite vertex data.");
            lod.vertices.push_back(vertex);
        }
        while (result.mesh.morphTargets.size() < primitive.targets_count) {
            const std::size_t targetIndex = result.mesh.morphTargets.size();
            const char* targetName = targetIndex < meshNode->mesh->target_names_count
                ? meshNode->mesh->target_names[targetIndex]
                : nullptr;
            result.mesh.morphTargets.push_back({
                .name = targetName == nullptr || targetName[0] == '\0'
                    ? "Morph_" + std::to_string(targetIndex)
                    : std::string{ targetName },
                .lodIndex = 0U,
            });
        }
        for (cgltf_size targetIndex = 0U; targetIndex < primitive.targets_count; ++targetIndex) {
            const cgltf_morph_target& target = primitive.targets[targetIndex];
            const cgltf_accessor* targetPositions = TargetAttribute(target, cgltf_attribute_type_position);
            const cgltf_accessor* targetNormals = TargetAttribute(target, cgltf_attribute_type_normal);
            const cgltf_accessor* targetTangents = TargetAttribute(target, cgltf_attribute_type_tangent);
            if (targetPositions == nullptr || !EqualCount(targetPositions, positions->count) ||
                (targetNormals != nullptr && !EqualCount(targetNormals, positions->count)) ||
                (targetTangents != nullptr && !EqualCount(targetTangents, positions->count))) {
                return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, primitiveContext,
                    "Skeletal glTF morph target has incomplete POSITION, NORMAL, or TANGENT deltas.");
            }
            SkeletalMeshMorphTarget& morph = result.mesh.morphTargets[targetIndex];
            for (cgltf_size vertexIndex = 0U; vertexIndex < positions->count; ++vertexIndex) {
                std::array<cgltf_float, 3U> positionDelta{};
                std::array<cgltf_float, 3U> normalDelta{};
                std::array<cgltf_float, 3U> tangentDelta{};
                if (!ReadFloat(targetPositions, vertexIndex, positionDelta.data(), positionDelta.size()) ||
                    (targetNormals != nullptr && !ReadFloat(targetNormals, vertexIndex, normalDelta.data(), normalDelta.size())) ||
                    (targetTangents != nullptr && !ReadFloat(targetTangents, vertexIndex, tangentDelta.data(), tangentDelta.size()))) {
                    return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, primitiveContext,
                        "Skeletal glTF morph target has unreadable deltas.");
                }
                morph.deltas.push_back({
                    .vertexIndex = baseVertex + static_cast<std::uint32_t>(vertexIndex),
                    .positionDelta = ConvertPosition(*conversion,
                        { positionDelta[0], positionDelta[1], positionDelta[2] }),
                    .normalDelta = ConvertDirection(*conversion,
                        { normalDelta[0], normalDelta[1], normalDelta[2] }),
                    .tangentDelta = ConvertDirection(*conversion,
                        { tangentDelta[0], tangentDelta[1], tangentDelta[2] }),
                });
            }
        }
        const cgltf_size indexCount = primitive.indices == nullptr
            ? positions->count
            : primitive.indices->count;
        if (indexCount == 0U || indexCount % 3U != 0U) return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, primitiveContext, "Skeletal glTF primitive has non-triangle indices.");
        for (cgltf_size index = 0U; index < indexCount; ++index) {
            const cgltf_size sourceIndex = primitive.indices == nullptr
                ? index
                : cgltf_accessor_read_index(primitive.indices, index);
            if (sourceIndex >= positions->count) return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, primitiveContext, "Skeletal glTF primitive index is outside its vertex range.");
            lod.indices.push_back(baseVertex + static_cast<std::uint32_t>(sourceIndex));
        }
        if (conversion->reversesWinding) {
            for (std::size_t index = static_cast<std::size_t>(section.firstIndex);
                 index < lod.indices.size(); index += 3U) {
                std::swap(lod.indices[index + 1U], lod.indices[index + 2U]);
            }
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

    std::unordered_map<SkeletonBoneId, LocalTransform> referencePoses;
    referencePoses.reserve(result.skeleton.bones.size());
    for (const SkeletonBone& bone : result.skeleton.bones) {
        referencePoses.emplace(bone.id, bone.referencePose);
    }
    for (cgltf_size animationIndex = 0U; animationIndex < data->animations_count; ++animationIndex) {
        const cgltf_animation& sourceAnimation = data->animations[animationIndex];
        AnimationClip clip{};
        clip.durationSeconds = 0.0F;
        clip.targetSkeletonAssetId = skeletonAssetId;
        clip.targetSkeletonCompatibilitySignature = result.mesh.skeletonCompatibilitySignature;
        std::map<SkeletonBoneId, std::map<float, LocalTransform>> keys;
        std::map<SkeletonBoneId, std::vector<float>> boneChannelTimes;
        std::set<std::pair<SkeletonBoneId, cgltf_animation_path_type>> boneChannelTargets;
        std::map<std::string, std::vector<AnimationMorphKeyframe>> morphKeys;
        bool hasMorphChannel = false;
        for (cgltf_size channelIndex = 0U; channelIndex < sourceAnimation.channels_count; ++channelIndex) {
            const cgltf_animation_channel& channel = sourceAnimation.channels[channelIndex];
            ImportContext channelContext = meshContext;
            channelContext.channelIndex = static_cast<std::int32_t>(channelIndex);
            channelContext.node = NameOf(channel.target_node, "AnimationTarget");
            if (channel.target_path == cgltf_animation_path_type_weights) {
                if (channel.target_node != meshNode || channel.sampler == nullptr ||
                    channel.sampler->input == nullptr || channel.sampler->output == nullptr ||
                    channel.sampler->interpolation != cgltf_interpolation_type_linear ||
                    channel.sampler->input->count == 0U || result.mesh.morphTargets.empty() ||
                    channel.sampler->output->count != channel.sampler->input->count * result.mesh.morphTargets.size() ||
                    hasMorphChannel) {
                    return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, channelContext,
                        "Skeletal glTF animation has an invalid morph channel or unsupported interpolation.");
                }
                hasMorphChannel = true;
                float previousMorphTime = -1.0F;
                for (cgltf_size keyIndex = 0U; keyIndex < channel.sampler->input->count; ++keyIndex) {
                    std::array<cgltf_float, 1U> inputTime{};
                    if (!ReadFloat(channel.sampler->input, keyIndex, inputTime.data(), 1U) ||
                        !std::isfinite(inputTime[0]) || inputTime[0] < 0.0F) {
                        return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, channelContext,
                            "Skeletal glTF animation has unreadable or non-finite morph keyframes.");
                    }
                    if (inputTime[0] <= previousMorphTime) {
                        return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, channelContext,
                            "Skeletal glTF animation has unordered morph keyframes.");
                    }
                    previousMorphTime = inputTime[0];
                    for (std::size_t targetIndex = 0U; targetIndex < result.mesh.morphTargets.size(); ++targetIndex) {
                        std::array<cgltf_float, 1U> weight{};
                        if (!ReadFloat(channel.sampler->output,
                                keyIndex * result.mesh.morphTargets.size() + targetIndex,
                                weight.data(), 1U) || !std::isfinite(weight[0])) {
                            return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, channelContext,
                                "Skeletal glTF animation has unreadable or non-finite morph weights.");
                        }
                        morphKeys[result.mesh.morphTargets[targetIndex].name].push_back({
                            .timeSeconds = inputTime[0], .weight = weight[0],
                        });
                    }
                    clip.durationSeconds = std::max(clip.durationSeconds, inputTime[0]);
                }
                continue;
            }
            if (channel.target_path != cgltf_animation_path_type_translation &&
                channel.target_path != cgltf_animation_path_type_rotation &&
                channel.target_path != cgltf_animation_path_type_scale) {
                return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, channelContext,
                    "Skeletal glTF animation has an unsupported channel path.");
            }
            const auto target = jointSourceIndex.find(channel.target_node);
            if (target == jointSourceIndex.end() || channel.sampler == nullptr ||
                channel.sampler->input == nullptr || channel.sampler->output == nullptr ||
                channel.sampler->interpolation != cgltf_interpolation_type_linear ||
                channel.sampler->input->count == 0U ||
                channel.sampler->input->count != channel.sampler->output->count) {
                return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, channelContext,
                    "Skeletal glTF animation has an invalid bone channel or unsupported interpolation.");
            }
            const SkeletonBoneId boneId = static_cast<SkeletonBoneId>(target->second + 1U);
            channelContext.bone = NameOf(channel.target_node, "Joint_" + std::to_string(target->second));
            if (!boneChannelTargets.emplace(boneId, channel.target_path).second) {
                return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, channelContext,
                    "Skeletal glTF animation has duplicate bone channels.");
            }
            const LocalTransform referencePose = referencePoses.at(boneId);
            const cgltf_size componentCount = channel.target_path == cgltf_animation_path_type_rotation ? 4U : 3U;
            std::vector<float> channelTimes;
            channelTimes.reserve(static_cast<std::size_t>(channel.sampler->input->count));
            for (cgltf_size keyIndex = 0U; keyIndex < channel.sampler->input->count; ++keyIndex) {
                std::array<cgltf_float, 4U> inputTime{};
                std::array<cgltf_float, 4U> value{};
                if (!ReadFloat(channel.sampler->input, keyIndex, inputTime.data(), 1U) ||
                    !ReadFloat(channel.sampler->output, keyIndex, value.data(), componentCount) ||
                    !std::isfinite(inputTime[0]) || inputTime[0] < 0.0F) {
                    return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, channelContext,
                        "Skeletal glTF animation has unreadable or non-finite keyframes.");
                }
                if (!channelTimes.empty() && inputTime[0] <= channelTimes.back()) {
                    return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, channelContext,
                        "Skeletal glTF animation has unordered bone keyframes.");
                }
                channelTimes.push_back(inputTime[0]);
                LocalTransform& transform = keys[boneId].try_emplace(inputTime[0], referencePose).first->second;
                if (channel.target_path == cgltf_animation_path_type_translation) {
                    transform.position = ConvertPosition(*conversion, { value[0], value[1], value[2] });
                } else if (channel.target_path == cgltf_animation_path_type_rotation) {
                    transform.rotation = ConvertRotation(*conversion, { value[0], value[1], value[2], value[3] });
                } else {
                    transform.scale = ConvertScale(*conversion, { value[0], value[1], value[2] });
                }
                clip.durationSeconds = std::max(clip.durationSeconds, inputTime[0]);
            }
            const auto timeIt = boneChannelTimes.find(boneId);
            if (timeIt != boneChannelTimes.end() && timeIt->second != channelTimes) {
                return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, channelContext,
                    "Skeletal glTF animation bone channels must share one keyframe time grid.");
            }
            if (timeIt == boneChannelTimes.end()) {
                boneChannelTimes.emplace(boneId, std::move(channelTimes));
            }
        }
        if (keys.empty() && morphKeys.empty()) continue;
        clip.durationSeconds = std::max(clip.durationSeconds, 0.0001F);
        for (const auto& [boneId, boneKeys] : keys) {
            AnimationBoneTrack track{};
            track.boneId = boneId;
            for (const auto& [time, transform] : boneKeys) {
                track.keyframes.push_back({ .timeSeconds = time, .transform = transform });
            }
            clip.skeletalTracks.push_back(std::move(track));
        }
        for (auto& [morphTarget, morphTrackKeys] : morphKeys) {
            AnimationMorphTrack track{};
            track.morphTarget = std::move(morphTarget);
            track.keyframes = std::move(morphTrackKeys);
            clip.morphTracks.push_back(std::move(track));
        }
        result.clips.push_back(std::move(clip));
    }
    return result;
}

bool SkeletalMeshGltfImportReport::HasErrors() const noexcept {
    return std::ranges::any_of(diagnostics, [](const SkeletalMeshGltfImportDiagnostic& diagnostic) {
        return diagnostic.severity == SkeletalMeshGltfImportDiagnosticSeverity::Error;
    });
}

} // namespace kb::scene
