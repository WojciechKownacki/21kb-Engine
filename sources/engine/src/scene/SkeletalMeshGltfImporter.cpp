#include "engine/scene/SkeletalMeshGltfImporter.hpp"

#include "engine/assets/GltfExternalResources.hpp"

#define CGLTF_IMPLEMENTATION
#include <cgltf/cgltf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
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

// The whole file through std::filesystem, so a name the Windows code page cannot spell still opens.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> ReadSourceBytes(const std::filesystem::path& path) {
    std::error_code sizeError;
    const std::uintmax_t size = std::filesystem::file_size(path, sizeError);
    constexpr std::uintmax_t kMaximumSourceBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    if (sizeError || size == 0U || size > kMaximumSourceBytes) return std::nullopt;
    std::ifstream input{ path, std::ios::binary };
    if (!input) return std::nullopt;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size())) return std::nullopt;
    return bytes;
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

[[nodiscard]] bool SameWorldTransform(const cgltf_node& lhs, const cgltf_node& rhs) noexcept {
    std::array<cgltf_float, 16U> lhsMatrix{};
    std::array<cgltf_float, 16U> rhsMatrix{};
    cgltf_node_transform_world(&lhs, lhsMatrix.data());
    cgltf_node_transform_world(&rhs, rhsMatrix.data());
    constexpr cgltf_float epsilon = 0.00001F;
    for (std::size_t index = 0U; index < lhsMatrix.size(); ++index) {
        if (!std::isfinite(lhsMatrix[index]) || !std::isfinite(rhsMatrix[index]) ||
            std::abs(lhsMatrix[index] - rhsMatrix[index]) > epsilon) return false;
    }
    return true;
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
    SkeletalMeshGltfImportReport* report,
    const std::filesystem::path& sourcePath,
    const ImportContext& context,
    std::string* error) {
    if (primitive.material == nullptr) return std::uint64_t{ 0U };
    if (options.materialResolver == nullptr) {
        AddDiagnostic(report, SkeletalMeshGltfImportDiagnosticSeverity::Warning,
            sourcePath, context,
            "Skeletal glTF primitive material was imported as an unassigned material slot because no material resolver was supplied.");
        return std::uint64_t{ 0U };
    }
    const std::ptrdiff_t materialIndex = primitive.material - data.materials;
    if (materialIndex < 0 || static_cast<cgltf_size>(materialIndex) >= data.materials_count) {
        return Fail<std::uint64_t>(error, "Skeletal glTF primitive references an invalid material.");
    }
    const std::string materialName = kb::assets::GltfMaterialSlotName(data, static_cast<std::size_t>(materialIndex));
    const std::uint64_t materialAssetId = options.materialResolver(materialName, options.materialResolverUserData);
    if (materialAssetId == 0U) {
        return Fail<std::uint64_t>(error,
            "Skeletal glTF material resolver did not return an asset id for '" + materialName + "'.");
    }
    return materialAssetId;
}

// Animations-only sources: a stepped key holds until this long before the next key, where the
// clip's linear sampling takes over.
constexpr float kStepHoldSeconds = 0.0001F;

// The same joints bound at the same pose: what an exporter writes for each mesh object of one
// character. Different bind matrices would deform one of the meshes wrongly, so they differ.
[[nodiscard]] bool SameJoints(const cgltf_skin& lhs, const cgltf_skin& rhs) noexcept {
    if (&lhs == &rhs) return true;
    if (lhs.joints_count != rhs.joints_count) return false;
    for (cgltf_size index = 0U; index < lhs.joints_count; ++index) {
        if (lhs.joints[index] != rhs.joints[index]) return false;
        std::array<cgltf_float, 16U> a{};
        std::array<cgltf_float, 16U> b{};
        if ((lhs.inverse_bind_matrices == nullptr) != (rhs.inverse_bind_matrices == nullptr)) return false;
        if (lhs.inverse_bind_matrices == nullptr) continue;
        if (!ReadFloat(lhs.inverse_bind_matrices, index, a.data(), a.size()) ||
            !ReadFloat(rhs.inverse_bind_matrices, index, b.data(), b.size())) {
            return false;
        }
        for (std::size_t element = 0U; element < a.size(); ++element) {
            if (std::abs(a[element] - b[element]) > 0.00001F) return false;
        }
    }
    return true;
}

// A source without a skin: the bones are the nodes its animations move, with their ancestors,
// in document order. Nodes carrying a mesh or a camera are never bones.
[[nodiscard]] std::vector<const cgltf_node*> AnimatedArmatureNodes(const cgltf_data& data) {
    std::unordered_set<const cgltf_node*> selected;
    for (cgltf_size animation = 0U; animation < data.animations_count; ++animation) {
        for (cgltf_size channel = 0U; channel < data.animations[animation].channels_count; ++channel) {
            const cgltf_animation_channel& source = data.animations[animation].channels[channel];
            if (source.target_path == cgltf_animation_path_type_weights) continue;
            for (const cgltf_node* node = source.target_node; node != nullptr; node = node->parent) {
                if (node->mesh != nullptr || node->camera != nullptr) break;
                selected.insert(node);
            }
        }
    }
    std::vector<const cgltf_node*> nodes;
    for (cgltf_size index = 0U; index < data.nodes_count; ++index) {
        if (selected.contains(&data.nodes[index])) nodes.push_back(&data.nodes[index]);
    }
    return nodes;
}

// Bind data of a skeleton that skins nothing: each bone bound where its reference pose puts it.
void BindFromReferencePose(SkeletonAsset& skeleton) {
    std::vector<LocalTransform> world(skeleton.bones.size());
    for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
        const SkeletonBone& bone = skeleton.bones[index];
        const LocalTransform& local = bone.referencePose;
        if (bone.parentIndex < 0) {
            world[index] = local;
        } else {
            const LocalTransform& parent = world[static_cast<std::size_t>(bone.parentIndex)];
            world[index] = {
                .position = parent.position + kb::math::Rotate(parent.rotation,
                    kb::math::Vec3{ parent.scale.x * local.position.x, parent.scale.y * local.position.y,
                        parent.scale.z * local.position.z }),
                .rotation = kb::math::Normalize(parent.rotation * local.rotation),
                .scale = { parent.scale.x * local.scale.x, parent.scale.y * local.scale.y, parent.scale.z * local.scale.z },
            };
        }
    }
    for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
        const LocalTransform& bound = world[index];
        // A bone's world scale is uniform in practice; the inverse takes its mean.
        const float scale = std::max((bound.scale.x + bound.scale.y + bound.scale.z) / 3.0F, 0.000001F);
        const kb::math::Quat inverseRotation = kb::math::Inverse(bound.rotation);
        const kb::math::Vec3 inversePosition = kb::math::Rotate(inverseRotation, bound.position) * (-1.0F / scale);
        skeleton.bones[index].inverseBind = kb::math::FromTRS(
            inversePosition, inverseRotation, { 1.0F / scale, 1.0F / scale, 1.0F / scale });
    }
}

struct GltfChannelKeys {
    cgltf_animation_path_type path = cgltf_animation_path_type_invalid;
    cgltf_interpolation_type interpolation = cgltf_interpolation_type_linear;
    std::vector<float> times;
    // Per key: the value, and for a cubic spline its in and out tangents.
    std::vector<std::array<float, 4U>> values;
    std::vector<std::array<float, 4U>> inTangents;
    std::vector<std::array<float, 4U>> outTangents;
};

[[nodiscard]] std::optional<GltfChannelKeys> ReadChannelKeys(const cgltf_animation_channel& channel) {
    const cgltf_animation_sampler& sampler = *channel.sampler;
    GltfChannelKeys keys{};
    keys.path = channel.target_path;
    keys.interpolation = sampler.interpolation;
    const cgltf_size count = sampler.input->count;
    const bool cubic = sampler.interpolation == cgltf_interpolation_type_cubic_spline;
    if (sampler.output->count != (cubic ? count * 3U : count)) return std::nullopt;
    const cgltf_size components = channel.target_path == cgltf_animation_path_type_rotation ? 4U : 3U;
    keys.times.reserve(static_cast<std::size_t>(count));
    for (cgltf_size key = 0U; key < count; ++key) {
        std::array<cgltf_float, 1U> time{};
        if (!ReadFloat(sampler.input, key, time.data(), 1U) || !std::isfinite(time[0]) || time[0] < 0.0F ||
            (!keys.times.empty() && time[0] <= keys.times.back())) {
            return std::nullopt;
        }
        keys.times.push_back(time[0]);
        const auto read = [&](cgltf_size element, std::vector<std::array<float, 4U>>& into) {
            std::array<cgltf_float, 4U> value{ 0.0F, 0.0F, 0.0F, 1.0F };
            if (!ReadFloat(sampler.output, element, value.data(), components)) return false;
            for (const float component : value) if (!std::isfinite(component)) return false;
            into.push_back(value);
            return true;
        };
        if (cubic) {
            if (!read(key * 3U, keys.inTangents) || !read(key * 3U + 1U, keys.values) || !read(key * 3U + 2U, keys.outTangents)) {
                return std::nullopt;
            }
        } else if (!read(key, keys.values)) {
            return std::nullopt;
        }
    }
    return keys;
}

// The channel's value at `time`, in the source's own space, with its own interpolation.
[[nodiscard]] std::array<float, 4U> EvaluateChannel(const GltfChannelKeys& keys, float time) {
    if (time <= keys.times.front()) return keys.values.front();
    if (time >= keys.times.back()) return keys.values.back();
    const std::size_t next = static_cast<std::size_t>(
        std::upper_bound(keys.times.begin(), keys.times.end(), time) - keys.times.begin());
    const std::size_t previous = next - 1U;
    if (keys.interpolation == cgltf_interpolation_type_step) return keys.values[previous];
    const float span = keys.times[next] - keys.times[previous];
    const float t = (time - keys.times[previous]) / span;
    std::array<float, 4U> value{};
    const bool rotation = keys.path == cgltf_animation_path_type_rotation;
    if (keys.interpolation == cgltf_interpolation_type_cubic_spline) {
        const float t2 = t * t;
        const float t3 = t2 * t;
        for (std::size_t component = 0U; component < 4U; ++component) {
            value[component] = (2.0F * t3 - 3.0F * t2 + 1.0F) * keys.values[previous][component] +
                (t3 - 2.0F * t2 + t) * span * keys.outTangents[previous][component] +
                (-2.0F * t3 + 3.0F * t2) * keys.values[next][component] +
                (t3 - t2) * span * keys.inTangents[next][component];
        }
    } else if (rotation) {
        const kb::math::Quat a{ keys.values[previous][0], keys.values[previous][1], keys.values[previous][2], keys.values[previous][3] };
        const kb::math::Quat b{ keys.values[next][0], keys.values[next][1], keys.values[next][2], keys.values[next][3] };
        const kb::math::Quat q = kb::math::Slerp(a, b, t);
        return { q.x, q.y, q.z, q.w };
    } else {
        for (std::size_t component = 0U; component < 4U; ++component) {
            value[component] = keys.values[previous][component] + (keys.values[next][component] - keys.values[previous][component]) * t;
        }
    }
    if (rotation) {
        const kb::math::Quat q = kb::math::Normalize(kb::math::Quat{ value[0], value[1], value[2], value[3] });
        return { q.x, q.y, q.z, q.w };
    }
    return value;
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

    // The document's bytes stay alive for the whole import: a GLB's binary chunk points into them.
    const std::optional<std::vector<std::uint8_t>> sourceBytes = ReadSourceBytes(path);
    if (!sourceBytes) {
        return Fail<SkeletalMeshGltfImportResult>(error,
            "Skeletal glTF import could not read the source file.");
    }
    cgltf_options options{};
    cgltf_data* rawData = nullptr;
    if (cgltf_parse(&options, sourceBytes->data(), sourceBytes->size(), &rawData) != cgltf_result_success ||
        rawData == nullptr) {
        return Fail<SkeletalMeshGltfImportResult>(error,
            "Skeletal glTF import could not parse the source file.");
    }
    GltfData data{ rawData };
    std::string bufferError;
    if (!kb::assets::LoadGltfBuffers(options, *data, path.has_parent_path() ? path.parent_path() : std::filesystem::path{ "." },
            {}, &bufferError)) {
        return Fail<SkeletalMeshGltfImportResult>(error,
            "Skeletal glTF import could not load source buffers: " + bufferError);
    }
    // One skeleton per source. An exporter writes one skin per mesh object, so several skins are
    // accepted when they all bind the same joints.
    const cgltf_skin* sourceSkin = data->skins_count == 0U ? nullptr : &data->skins[0];
    for (cgltf_size index = 1U; index < data->skins_count; ++index) {
        if (!SameJoints(data->skins[index], *sourceSkin)) {
            return Fail<SkeletalMeshGltfImportResult>(error,
                "Skeletal glTF source holds more than one skeleton; export one character per file.");
        }
    }
    if (sourceSkin != nullptr && (sourceSkin->joints_count == 0U || sourceSkin->inverse_bind_matrices == nullptr ||
            sourceSkin->inverse_bind_matrices->count != sourceSkin->joints_count)) {
        return Fail<SkeletalMeshGltfImportResult>(error,
            "Skeletal glTF import requires one inverse bind matrix per skin joint.");
    }
    static const cgltf_skin kNoSkin{};
    const cgltf_skin& skin = sourceSkin != nullptr ? *sourceSkin : kNoSkin;
    std::vector<const cgltf_node*> jointNodes;
    if (sourceSkin != nullptr) {
        jointNodes.assign(sourceSkin->joints, sourceSkin->joints + sourceSkin->joints_count);
    } else {
        jointNodes = AnimatedArmatureNodes(*data);
        if (jointNodes.empty()) {
            return Fail<SkeletalMeshGltfImportResult>(error,
                "Skeletal glTF source has neither a skinned mesh nor animated bones.");
        }
    }

    std::unordered_map<const cgltf_node*, std::size_t> jointSourceIndex;
    jointSourceIndex.reserve(jointNodes.size());
    for (std::size_t index = 0U; index < jointNodes.size(); ++index) {
        if (jointNodes[index] == nullptr || !jointSourceIndex.emplace(jointNodes[index], index).second) {
            return Fail<SkeletalMeshGltfImportResult>(error,
                "Skeletal glTF skin has null or duplicate joint nodes.");
        }
    }

    SkeletalMeshGltfImportResult result{};
    std::unordered_map<const cgltf_node*, std::int32_t> emittedBone;
    emittedBone.reserve(jointSourceIndex.size());
    std::unordered_set<const cgltf_node*> visitingBone;
    visitingBone.reserve(jointSourceIndex.size());
    const auto emitBone = [&](const auto& self, const cgltf_node* node) -> bool {
        if (emittedBone.contains(node)) return true;
        if (!visitingBone.insert(node).second) return false;
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
        if (sourceSkin != nullptr) {
            std::array<cgltf_float, 16U> inverseBind{};
            if (!ReadFloat(skin.inverse_bind_matrices, sourceIndex, inverseBind.data(), inverseBind.size())) {
                return false;
            }
            bone.inverseBind = ConvertInverseBind(*conversion, inverseBind);
        }
        emittedBone.emplace(node, static_cast<std::int32_t>(result.skeleton.bones.size()));
        result.skeleton.bones.push_back(std::move(bone));
        visitingBone.erase(node);
        return true;
    };
    for (const cgltf_node* joint : jointNodes) {
        if (!emitBone(emitBone, joint)) {
            return Fail<SkeletalMeshGltfImportResult>(error,
                "Skeletal glTF import could not read an inverse bind matrix or uses unsupported matrix-authored joints.");
        }
    }
    if (sourceSkin == nullptr) BindFromReferencePose(result.skeleton);
    if (!ValidateSkeletonAsset(result.skeleton).valid) {
        return Fail<SkeletalMeshGltfImportResult>(error,
            "Skeletal glTF import produced an invalid Skeleton hierarchy.");
    }

    std::vector<const cgltf_node*> meshNodes;
    for (cgltf_size index = 0U; index < data->nodes_count; ++index) {
        const cgltf_node& node = data->nodes[index];
        if (sourceSkin != nullptr && node.mesh != nullptr && node.skin != nullptr && SameJoints(*node.skin, skin)) {
            meshNodes.push_back(&node);
        }
    }
    if (meshNodes.empty() && data->animations_count == 0U) {
        return Fail<SkeletalMeshGltfImportResult>(error,
            "Skeletal glTF source has a skeleton but neither a mesh bound to it nor animations.");
    }
    const cgltf_node* meshNode = meshNodes.empty() ? nullptr : meshNodes.front();
    const ImportContext meshContext{
        .mesh = meshNode == nullptr ? std::string{} : NameOf(meshNode->mesh, "Mesh_0"),
        .node = meshNode == nullptr ? std::string{} : NameOf(meshNode, "MeshNode"),
    };
    // A source without a mesh carries animations for a character imported from another file.
    if (meshNode != nullptr) {
    if (!importOptions.combineMeshes && meshNodes.size() != 1U) {
        return Fail<SkeletalMeshGltfImportResult>(error,
            "Skeletal glTF source contains multiple mesh nodes; enable Combine meshes to import them as one asset.");
    }
    if (meshNodes.size() > 1U) {
        for (const cgltf_node* node : meshNodes) {
            if (!SameWorldTransform(*meshNodes.front(), *node)) {
                return Fail<SkeletalMeshGltfImportResult>(error,
                    "Combining glTF mesh nodes with different world transforms is not representable by one skeletal mesh asset.");
            }
            for (cgltf_size primitiveIndex = 0U; primitiveIndex < node->mesh->primitives_count; ++primitiveIndex) {
                if (node->mesh->primitives[primitiveIndex].targets_count != 0U) {
                    return Fail<SkeletalMeshGltfImportResult>(error,
                        "Combining glTF mesh nodes with morph targets is not representable as one canonical morph set.");
                }
            }
        }
    }
    SkeletalMeshLod lod{};
    lod.minScreenCoverage = 0.0F;
    for (const cgltf_node* currentMeshNode : meshNodes) {
      const ImportContext currentMeshContext{
          .mesh = NameOf(currentMeshNode->mesh, "Mesh_0"),
          .node = NameOf(currentMeshNode, "MeshNode"),
      };
      for (cgltf_size primitiveIndex = 0U;
         primitiveIndex < currentMeshNode->mesh->primitives_count; ++primitiveIndex) {
        const cgltf_primitive& primitive = currentMeshNode->mesh->primitives[primitiveIndex];
        if (primitive.type != cgltf_primitive_type_triangles) continue;
        ImportContext primitiveContext = currentMeshContext;
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
        const auto materialAssetId = ResolveMaterialAssetId(
            primitive, *data, importOptions, report, path, primitiveContext, error);
        if (!materialAssetId) return std::nullopt;
        section.materialAssetId = *materialAssetId;
        section.boneMap.reserve(static_cast<std::size_t>(skin.joints_count));
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
            const char* targetName = targetIndex < currentMeshNode->mesh->target_names_count
                ? currentMeshNode->mesh->target_names[targetIndex]
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

        // glTF joints address the complete skin. Keep only the influences this
        // section needs and remap into its compact local bone map. The renderer
        // later maps those entries into a stable mesh-wide palette, so LOD
        // changes cannot reinterpret a joint index.
        std::vector<std::uint16_t> remap(static_cast<std::size_t>(skin.joints_count), UINT16_MAX);
        for (std::uint32_t vertexOffset = 0U; vertexOffset < positions->count; ++vertexOffset) {
            SkeletalMeshVertex& vertex = lod.vertices[baseVertex + vertexOffset];
            for (std::size_t influence = 0U; influence < vertex.jointIndices.size(); ++influence) {
                if (vertex.jointWeights[influence] <= 0.0F) continue;
                const std::uint16_t joint = vertex.jointIndices[influence];
                if (joint >= remap.size()) {
                    return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, primitiveContext,
                        "Skeletal glTF primitive has a joint outside the skin joint array.");
                }
                if (remap[joint] == UINT16_MAX) {
                    if (section.boneMap.size() >= static_cast<std::size_t>(UINT16_MAX) + 1U) {
                        return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, primitiveContext,
                            "Skeletal glTF section exceeds the representable joint palette size.");
                    }
                    remap[joint] = static_cast<std::uint16_t>(section.boneMap.size());
                    section.boneMap.push_back(static_cast<SkeletonBoneId>(joint + 1U));
                }
                vertex.jointIndices[influence] = remap[joint];
            }
            for (std::size_t influence = 0U; influence < vertex.jointIndices.size(); ++influence) {
                if (vertex.jointWeights[influence] <= 0.0F) vertex.jointIndices[influence] = 0U;
            }
        }
        lod.sections.push_back(std::move(section));
      }
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
    BuildSkeletalMeshLodBoneBounds(result.mesh.lods.back());
    kb::math::Vec3 minimum = result.mesh.lods[0].vertices.front().position;
    kb::math::Vec3 maximum = minimum;
    for (const SkeletalMeshVertex& vertex : result.mesh.lods[0].vertices) {
        minimum.x = std::min(minimum.x, vertex.position.x); minimum.y = std::min(minimum.y, vertex.position.y); minimum.z = std::min(minimum.z, vertex.position.z);
        maximum.x = std::max(maximum.x, vertex.position.x); maximum.y = std::max(maximum.y, vertex.position.y); maximum.z = std::max(maximum.z, vertex.position.z);
    }
    result.mesh.conservativeBounds.center = { (minimum.x + maximum.x) * 0.5F, (minimum.y + maximum.y) * 0.5F, (minimum.z + maximum.z) * 0.5F };
    result.mesh.conservativeBounds.extents = { (maximum.x - minimum.x) * 0.5F, (maximum.y - minimum.y) * 0.5F, (maximum.z - minimum.z) * 0.5F };
    result.mesh.fixedBounds = result.mesh.conservativeBounds;
    const SkeletalMeshAssetValidationResult validation = ValidateSkeletalMeshAsset(result.mesh);
    if (!validation.valid) return Fail<SkeletalMeshGltfImportResult>(error, validation.error);
    }
    result.mesh.skeletonAssetId = skeletonAssetId;
    result.mesh.skeletonCompatibilitySignature = SkeletonCompatibilitySignature(result.skeleton);

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
        std::map<SkeletonBoneId, std::vector<GltfChannelKeys>> boneChannels;
        std::set<std::pair<SkeletonBoneId, cgltf_animation_path_type>> boneChannelTargets;
        std::map<std::string, std::vector<AnimationMorphKeyframe>> morphKeys;
        bool hasMorphChannel = false;
        for (cgltf_size channelIndex = 0U; channelIndex < sourceAnimation.channels_count; ++channelIndex) {
            const cgltf_animation_channel& channel = sourceAnimation.channels[channelIndex];
            ImportContext channelContext = meshContext;
            channelContext.channelIndex = static_cast<std::int32_t>(channelIndex);
            channelContext.node = NameOf(channel.target_node, "AnimationTarget");
            const cgltf_animation_sampler* sampler = channel.sampler;
            if (sampler == nullptr || sampler->input == nullptr || sampler->output == nullptr ||
                sampler->input->count == 0U) {
                return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, channelContext,
                    "Skeletal glTF animation has a channel without keyframes.");
            }
            if (channel.target_path == cgltf_animation_path_type_weights) {
                if (meshNode == nullptr || channel.target_node != meshNode || result.mesh.morphTargets.empty()) {
                    AddDiagnostic(report, SkeletalMeshGltfImportDiagnosticSeverity::Warning, path, channelContext,
                        "Skeletal glTF animation moves morph weights of a mesh this import does not keep; the channel was skipped.");
                    continue;
                }
                if (sampler->interpolation != cgltf_interpolation_type_linear ||
                    sampler->output->count != sampler->input->count * result.mesh.morphTargets.size() ||
                    hasMorphChannel) {
                    return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, channelContext,
                        "Skeletal glTF animation has an invalid morph channel or unsupported interpolation.");
                }
                hasMorphChannel = true;
                float previousMorphTime = -1.0F;
                for (cgltf_size keyIndex = 0U; keyIndex < sampler->input->count; ++keyIndex) {
                    std::array<cgltf_float, 1U> inputTime{};
                    if (!ReadFloat(sampler->input, keyIndex, inputTime.data(), 1U) ||
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
                        if (!ReadFloat(sampler->output,
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
                AddDiagnostic(report, SkeletalMeshGltfImportDiagnosticSeverity::Warning, path, channelContext,
                    "Skeletal glTF animation channel animates something other than a transform; it was skipped.");
                continue;
            }
            const auto target = jointSourceIndex.find(channel.target_node);
            if (target == jointSourceIndex.end()) {
                // An exporter also keys the armature or mesh object itself; only bones move a skeleton.
                AddDiagnostic(report, SkeletalMeshGltfImportDiagnosticSeverity::Warning, path, channelContext,
                    "Skeletal glTF animation moves a node that is not a bone of the skeleton; the channel was skipped.");
                continue;
            }
            const SkeletonBoneId boneId = static_cast<SkeletonBoneId>(target->second + 1U);
            channelContext.bone = NameOf(channel.target_node, "Joint_" + std::to_string(target->second));
            if (!boneChannelTargets.emplace(boneId, channel.target_path).second) {
                return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, channelContext,
                    "Skeletal glTF animation has duplicate bone channels.");
            }
            std::optional<GltfChannelKeys> keys = ReadChannelKeys(channel);
            if (!keys) {
                return FailWithDiagnostic<SkeletalMeshGltfImportResult>(error, report, path, channelContext,
                    "Skeletal glTF animation has unreadable, unordered or non-finite bone keyframes.");
            }
            clip.durationSeconds = std::max(clip.durationSeconds, keys->times.back());
            boneChannels[boneId].push_back(std::move(*keys));
        }
        if (boneChannels.empty() && morphKeys.empty()) continue;
        clip.durationSeconds = std::max(clip.durationSeconds, 0.0001F);
        for (const auto& [boneId, channels] : boneChannels) {
            // Channels of one bone may key different instants (an exporter drops redundant keys
            // per channel) and interpolate differently: each is evaluated at every instant any
            // of them keys, and a stepped one also just before its next key.
            std::vector<float> times;
            for (const GltfChannelKeys& keys : channels) {
                times.insert(times.end(), keys.times.begin(), keys.times.end());
                if (keys.interpolation == cgltf_interpolation_type_step) {
                    for (std::size_t key = 1U; key < keys.times.size(); ++key) {
                        const float hold = keys.times[key] - kStepHoldSeconds;
                        if (hold > keys.times[key - 1U]) times.push_back(hold);
                    }
                }
            }
            std::sort(times.begin(), times.end());
            times.erase(std::unique(times.begin(), times.end()), times.end());
            AnimationBoneTrack track{};
            track.boneId = boneId;
            track.keyframes.reserve(times.size());
            for (const float time : times) {
                LocalTransform transform = referencePoses.at(boneId);
                for (const GltfChannelKeys& keys : channels) {
                    const std::array<float, 4U> value = EvaluateChannel(keys, time);
                    if (keys.path == cgltf_animation_path_type_translation) {
                        transform.position = ConvertPosition(*conversion, { value[0], value[1], value[2] });
                    } else if (keys.path == cgltf_animation_path_type_rotation) {
                        transform.rotation = ConvertRotation(*conversion, { value[0], value[1], value[2], value[3] });
                    } else {
                        transform.scale = ConvertScale(*conversion, { value[0], value[1], value[2] });
                    }
                }
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
        result.clipNames.push_back(sourceAnimation.name != nullptr && sourceAnimation.name[0] != '\0'
            ? std::string{ sourceAnimation.name }
            : "Animation_" + std::to_string(animationIndex));
    }
    if (meshNode == nullptr && result.clips.empty()) {
        return Fail<SkeletalMeshGltfImportResult>(error,
            "Skeletal glTF source has no mesh bound to its skeleton and no animation of its bones.");
    }
    return result;
}

SkeletalSourceSummary SkeletalMeshGltfImporter::Probe(const std::filesystem::path& path) {
    SkeletalSourceSummary summary{};
    const std::optional<std::vector<std::uint8_t>> bytes = ReadSourceBytes(path);
    if (!bytes) return summary;
    cgltf_options options{};
    cgltf_data* rawData = nullptr;
    if (cgltf_parse(&options, bytes->data(), bytes->size(), &rawData) != cgltf_result_success || rawData == nullptr) {
        return summary;
    }
    const GltfData data{ rawData };
    summary.readable = true;
    summary.animationCount = static_cast<std::size_t>(data->animations_count);
    for (cgltf_size index = 0U; index < data->nodes_count; ++index) {
        const cgltf_node& node = data->nodes[index];
        if (node.mesh != nullptr && node.skin != nullptr) summary.skinned = true;
    }
    if (data->skins_count != 0U) {
        summary.boneCount = static_cast<std::size_t>(data->skins[0].joints_count);
    } else {
        // An armature exported without its mesh: the nodes its animations move.
        std::unordered_set<const cgltf_node*> animated;
        for (cgltf_size animation = 0U; animation < data->animations_count; ++animation) {
            for (cgltf_size channel = 0U; channel < data->animations[animation].channels_count; ++channel) {
                const cgltf_animation_channel& source = data->animations[animation].channels[channel];
                if (source.target_node != nullptr && source.target_node->mesh == nullptr) animated.insert(source.target_node);
            }
        }
        summary.boneCount = animated.size();
    }
    return summary;
}

bool SkeletalMeshGltfImportReport::HasErrors() const noexcept {
    return std::ranges::any_of(diagnostics, [](const SkeletalMeshGltfImportDiagnostic& diagnostic) {
        return diagnostic.severity == SkeletalMeshGltfImportDiagnosticSeverity::Error;
    });
}

} // namespace kb::scene
