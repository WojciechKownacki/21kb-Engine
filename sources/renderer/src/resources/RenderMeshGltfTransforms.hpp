#pragma once

#include <array>

namespace kb::render {

class RenderMeshGltfTransforms final {
public:
    [[nodiscard]] static std::array<float, 3> TransformPosition(const float matrix[16], float x, float y, float z) noexcept;
    [[nodiscard]] static std::array<float, 3> TransformDirection(const float matrix[16], float x, float y, float z) noexcept;
    [[nodiscard]] static std::array<float, 3> TransformSurfaceNormal(const float matrix[16], float x, float y, float z) noexcept;
};

} // namespace kb::render
