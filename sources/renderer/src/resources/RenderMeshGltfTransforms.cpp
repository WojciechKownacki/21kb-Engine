#include "resources/RenderMeshGltfTransforms.hpp"

#include <cmath>

namespace kb::render {
namespace {

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

} // namespace

std::array<float, 3> RenderMeshGltfTransforms::TransformPosition(const float matrix[16], float x, float y, float z) noexcept {
    return {
        matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12],
        matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13],
        matrix[2] * x + matrix[6] * y + matrix[10] * z + matrix[14],
    };
}

std::array<float, 3> RenderMeshGltfTransforms::TransformDirection(const float matrix[16], float x, float y, float z) noexcept {
    return NormalizeVector(
        std::array<float, 3>{
            matrix[0] * x + matrix[4] * y + matrix[8] * z,
            matrix[1] * x + matrix[5] * y + matrix[9] * z,
            matrix[2] * x + matrix[6] * y + matrix[10] * z,
        },
        std::array<float, 3>{ 1.0F, 0.0F, 0.0F });
}

std::array<float, 3> RenderMeshGltfTransforms::TransformSurfaceNormal(const float matrix[16], float x, float y, float z) noexcept {
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

} // namespace kb::render
