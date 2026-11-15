#include "kb/render/overlay/EditorLightWireframe.hpp"

#include <algorithm>
#include <cmath>

namespace kb::render {
namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr std::uint32_t kCircleSegments = 64U;
constexpr float kMinRange = 0.01F;
constexpr std::uint32_t kSpotEdgeCount = 4U;

[[nodiscard]] std::array<float, 3> Add(std::array<float, 3> lhs, std::array<float, 3> rhs) noexcept {
    return {lhs[0] + rhs[0], lhs[1] + rhs[1], lhs[2] + rhs[2]};
}

[[nodiscard]] std::array<float, 3> Mul(std::array<float, 3> value, float scale) noexcept {
    return {value[0] * scale, value[1] * scale, value[2] * scale};
}

[[nodiscard]] std::array<float, 3> CirclePoint(
    std::array<float, 3> center, std::array<float, 3> axisA, std::array<float, 3> axisB, float radius, float angle) noexcept {
    return Add(center, Add(Mul(axisA, std::cos(angle) * radius), Mul(axisB, std::sin(angle) * radius)));
}

void AppendCircle(
    std::vector<EditorLightWireframeLine>& output,
    std::array<float, 3> center,
    std::array<float, 3> axisA,
    std::array<float, 3> axisB,
    float radius,
    float alpha) {
    const float clampedRadius = std::max(kMinRange, radius);
    for (std::uint32_t i = 0; i < kCircleSegments; ++i) {
        const float a0 = static_cast<float>(i) / static_cast<float>(kCircleSegments) * 2.0F * kPi;
        const float a1 = static_cast<float>(i + 1U) / static_cast<float>(kCircleSegments) * 2.0F * kPi;
        output.push_back(EditorLightWireframeLine{
            .from = CirclePoint(center, axisA, axisB, clampedRadius, a0),
            .to = CirclePoint(center, axisA, axisB, clampedRadius, a1),
            .planeA = axisA,
            .planeB = axisB,
            .alpha = alpha,
        });
    }
}

} // namespace

void BuildEditorLightWireframeLines(
    const EditorLightWireframeDesc& light,
    std::vector<EditorLightWireframeLine>& output) {
    if (light.kind == EditorLightWireframeKind::Directional || light.range <= 0.0F) {
        return;
    }
    if (light.kind == EditorLightWireframeKind::Point) {
        constexpr std::array<float, 3> xAxis{1.0F, 0.0F, 0.0F};
        constexpr std::array<float, 3> yAxis{0.0F, 1.0F, 0.0F};
        constexpr std::array<float, 3> zAxis{0.0F, 0.0F, 1.0F};
        AppendCircle(output, light.position, xAxis, yAxis, light.range, 0.78F);
        AppendCircle(output, light.position, xAxis, zAxis, light.range, 0.50F);
        AppendCircle(output, light.position, yAxis, zAxis, light.range, 0.50F);
        return;
    }

    const float range = std::max(kMinRange, light.range);
    const float coneRadians = std::clamp(light.outerConeDegrees, 0.0F, 179.0F) * 0.5F * kPi / 180.0F;
    const float radius = std::tan(coneRadians) * range;
    const std::array<float, 3> endCenter = Add(light.position, Mul(light.forward, range));
    AppendCircle(output, endCenter, light.right, light.up, radius, 0.78F);
    for (std::uint32_t i = 0; i < kSpotEdgeCount; ++i) {
        const float angle = static_cast<float>(i) * kPi * 0.5F + kPi * 0.25F;
        output.push_back(EditorLightWireframeLine{
            .from = light.position,
            .to = CirclePoint(endCenter, light.right, light.up, radius, angle),
            .planeA = light.right,
            .planeB = light.up,
            .alpha = 0.82F,
        });
    }
}

} // namespace kb::render
