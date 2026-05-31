#include "scene/prefab/ScenePrefabHashBuilder.hpp"

#include <cstring>

namespace kb::scene {
namespace {

constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

} // namespace

void ScenePrefabHashBuilder::Mix(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (int byte = 0; byte < 8; ++byte) {
        hash ^= (value >> (byte * 8)) & 0xffU;
        hash *= kFnvPrime;
    }
}

void ScenePrefabHashBuilder::MixString(std::uint64_t& hash, std::string_view value) noexcept {
    for (const char character : value) {
        hash ^= static_cast<unsigned char>(character);
        hash *= kFnvPrime;
    }
    Mix(hash, value.size());
}

void ScenePrefabHashBuilder::MixFloat(std::uint64_t& hash, float value) noexcept {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    Mix(hash, bits);
}

void ScenePrefabHashBuilder::MixVec3(std::uint64_t& hash, const Vec3& value) noexcept {
    MixFloat(hash, value.x);
    MixFloat(hash, value.y);
    MixFloat(hash, value.z);
}

void ScenePrefabHashBuilder::MixQuat(std::uint64_t& hash, const Quat& value) noexcept {
    MixFloat(hash, value.x);
    MixFloat(hash, value.y);
    MixFloat(hash, value.z);
    MixFloat(hash, value.w);
}

void ScenePrefabHashBuilder::MixTransform(std::uint64_t& hash, const TransformComponent& transform) noexcept {
    MixVec3(hash, transform.localPosition);
    MixQuat(hash, transform.localRotation);
    MixVec3(hash, transform.localScale);
}

} // namespace kb::scene
