#include "engine/scene/SkeletonAsset.hpp"

#include <cmath>
#include <type_traits>
#include <unordered_set>

namespace kb::scene {
namespace {

constexpr float kQuaternionUnitTolerance = 0.0001F;

[[nodiscard]] bool IsFinite(float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool IsFinite(const Vec3& value) noexcept {
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

[[nodiscard]] bool IsFinite(const Quat& value) noexcept {
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) && IsFinite(value.w);
}

[[nodiscard]] bool IsUnitQuaternion(const Quat& value) noexcept {
    const float squaredLength = value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
    return IsFinite(squaredLength) && std::fabs(squaredLength - 1.0F) <= kQuaternionUnitTolerance;
}

[[nodiscard]] bool IsFinite(const kb::math::Mat4& value) noexcept {
    for (const kb::math::Vec4& column : value.columns) {
        if (!IsFinite(column.x) || !IsFinite(column.y) || !IsFinite(column.z) || !IsFinite(column.w)) {
            return false;
        }
    }
    return true;
}

void HashBytes(std::uint64_t& hash, const void* data, std::size_t count) noexcept {
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0U; index < count; ++index) {
        hash ^= bytes[index];
        hash *= kPrime;
    }
}

template <typename T>
void HashValue(std::uint64_t& hash, const T& value) noexcept {
    static_assert(std::is_trivially_copyable_v<T>);
    HashBytes(hash, &value, sizeof(value));
}

} // namespace

SkeletonAssetValidationResult ValidateSkeletonAsset(const SkeletonAsset& asset) {
    if (asset.bones.empty()) {
        return { false, "Skeleton has no bones." };
    }

    std::unordered_set<SkeletonBoneId> ids;
    std::unordered_set<std::string> names;
    for (std::size_t index = 0U; index < asset.bones.size(); ++index) {
        const SkeletonBone& bone = asset.bones[index];
        if (bone.id == 0U || !ids.insert(bone.id).second) {
            return { false, "Skeleton bone " + std::to_string(index) + " has an invalid or duplicate stable id." };
        }
        if (bone.name.empty() || !names.insert(bone.name).second) {
            return { false, "Skeleton bone " + std::to_string(index) + " has an empty or duplicate name." };
        }
        if (bone.parentIndex < -1 || bone.parentIndex >= static_cast<std::int32_t>(index)) {
            return { false, "Skeleton bone '" + bone.name + "' must reference an earlier parent index or -1." };
        }
        if (!IsFinite(bone.referencePose.position) || !IsFinite(bone.referencePose.scale) ||
            !IsFinite(bone.referencePose.rotation) || !IsUnitQuaternion(bone.referencePose.rotation)) {
            return { false, "Skeleton bone '" + bone.name + "' has a non-finite or non-unit reference rotation." };
        }
        if (!IsFinite(bone.inverseBind)) {
            return { false, "Skeleton bone '" + bone.name + "' has a non-finite inverse bind matrix." };
        }
    }
    return { true, {} };
}

std::uint64_t SkeletonCompatibilitySignature(const SkeletonAsset& asset) {
    if (!ValidateSkeletonAsset(asset).valid) {
        return 0U;
    }

    std::uint64_t hash = 1469598103934665603ULL;
    const std::uint64_t boneCount = asset.bones.size();
    HashValue(hash, boneCount);
    for (const SkeletonBone& bone : asset.bones) {
        HashValue(hash, bone.id);
        HashValue(hash, bone.parentIndex);
        const std::uint64_t nameSize = bone.name.size();
        HashValue(hash, nameSize);
        HashBytes(hash, bone.name.data(), bone.name.size());
        HashValue(hash, bone.referencePose.position);
        HashValue(hash, bone.referencePose.rotation);
        HashValue(hash, bone.referencePose.scale);
        HashValue(hash, bone.inverseBind);
    }
    return hash;
}

} // namespace kb::scene
