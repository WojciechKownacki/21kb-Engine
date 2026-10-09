#include "SceneTransformCpuReferenceOracle.hpp"

#include <algorithm>
#include <cmath>

namespace cpu_reference {
namespace {

using kb::scene::Quat;
using kb::scene::Vec3;

constexpr float kPi = 3.14159265358979323846F;
constexpr float kHalf = 100.0F;
constexpr std::size_t kVisible = 10000U;

} // namespace

float Lcg::Next01() {
    state = state * 1664525U + 1013904223U;
    return static_cast<float>(static_cast<double>(state) / 4294967296.0);
}

Row MakeInitialRow(Lcg& random, std::size_t index) {
    // Exact adapter seed/order/expressions. Creation converts EP/EQ; AdvanceRow does not convert yaw.
    const float px = random.Next01() * 2.0F * kHalf - kHalf;
    const float pz = random.Next01() * 2.0F * kHalf - kHalf;
    const float heading = random.Next01() * 2 * kPi;
    const float speed = 2.0F + random.Next01() * 2.0F;
    const float scale = index < kVisible ? 1.5F : 1.0F;
    const Quat specRotation{ 0.0F, std::sin(heading * 0.5F), 0.0F, std::cos(heading * 0.5F) };
    Row result;
    result.transform.localPosition = Vec3{ px, 0.5F, -pz };
    result.transform.localRotation = Quat{ -specRotation.x, -specRotation.y, specRotation.z, specRotation.w };
    result.transform.localScale = Vec3{ scale, scale, scale };
    result.agent = Agent{ .heading = heading, .speed = speed, .index = static_cast<std::uint32_t>(index) };
    return result;
}

// Frozen scalar root composition, independent of the engine setter/normalizer. Its grouping and identity
// shortcut reproduce the precise S0 operations, including signed zero. No candidate math helpers are called.
Quat NormalizeRoot(Quat value) {
    if (value.x == 0.0F && value.y == 0.0F && value.z == 0.0F && value.w == 1.0F) return value;
    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
    if (lengthSquared <= 0.000001F) return Quat{};
    const float invLength = 1.0F / std::sqrt(lengthSquared);
    return Quat{ value.x * invLength, value.y * invLength, value.z * invLength, value.w * invLength };
}

void ComposeRoot(kb::scene::TransformComponent& transform) {
    transform.worldPosition = transform.localPosition;
    transform.worldRotation = NormalizeRoot(transform.localRotation);
    transform.worldScale = transform.localScale;
    transform.parentVersion = 0U;
    ++transform.worldVersion;
    transform.worldDirty = false;
}

std::size_t AdvanceRow(Row& expected, std::uint64_t frame, float dt) {
    const float kDt = std::min(dt, 0.1F);
    const float t = static_cast<float>(frame) * kDt;
    std::size_t wraps = 0U;
    const float h = expected.agent.heading + 1.5F * std::sin(t * 0.5F + static_cast<float>(expected.agent.index) * 0.001F) * kDt;
    float px = expected.transform.localPosition.x + std::cos(h) * expected.agent.speed * kDt;
    float pz = expected.transform.localPosition.z + std::sin(h) * expected.agent.speed * kDt;
    if (px < -kHalf) { px += 2.0F * kHalf; ++wraps; }
    else if (px >= kHalf) { px -= 2.0F * kHalf; ++wraps; }
    if (pz < -kHalf) { pz += 2.0F * kHalf; ++wraps; }
    else if (pz >= kHalf) { pz -= 2.0F * kHalf; ++wraps; }
    expected.agent.heading = h;
    expected.transform.localPosition = Vec3{ px, 0.5F, pz };
    expected.transform.localRotation = Quat{ 0.0F, std::sin(h * 0.5F), 0.0F, std::cos(h * 0.5F) };
    ++expected.transform.localVersion;
    expected.transform.worldDirty = true;
    ComposeRoot(expected.transform);
    return wraps;
}

} // namespace cpu_reference
