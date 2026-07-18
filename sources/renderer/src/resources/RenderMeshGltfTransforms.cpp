#include "resources/RenderMeshGltfTransforms.hpp"

#include "engine/math/EngineMath.hpp"

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
    // LIB-043: the surface-normal transform is the inverse-transpose (normal
    // matrix) of the model transform's upper-left 3x3 — extract that 3x3 from
    // the column-major float[16] and delegate the math to kb::math::Mat3
    // (InverseTranspose/Determinant) instead of re-expanding nine cofactors by
    // hand here. Behaviour is preserved exactly, including the singular-matrix
    // fallback to +Y and the final renormalization.
    const kb::math::Mat3 upperLeft{ {
        kb::math::Vec3{ matrix[0], matrix[1], matrix[2] },
        kb::math::Vec3{ matrix[4], matrix[5], matrix[6] },
        kb::math::Vec3{ matrix[8], matrix[9], matrix[10] },
    } };

    if (std::fabs(kb::math::Determinant(upperLeft)) <= 0.0001F) {
        return { 0.0F, 1.0F, 0.0F };
    }

    const kb::math::Vec3 transformed = kb::math::InverseTranspose(upperLeft) * kb::math::Vec3{ x, y, z };
    return NormalizeVector(std::array<float, 3>{ transformed.x, transformed.y, transformed.z }, std::array<float, 3>{ 0.0F, 1.0F, 0.0F });
}

} // namespace kb::render
