#pragma once
#include "kb/render/scene/SceneRenderTypes.hpp"
#include <algorithm>
#include <cmath>

namespace kb::render {
class GeometrySwarmVisibilityClusters {
public:
    // Called only when a generated range changes, never per camera or frame.
    static void Append(SceneRenderDrawGroup& group, std::size_t first) {
        constexpr std::size_t grain = 128U;
        for (; first < group.instances.size(); first += grain) {
            const auto end = std::min(first + grain, group.instances.size());
            const auto& initial = group.instances[first].model;
            std::array<float, 3> minimum{initial[12], initial[13], initial[14]}, maximum = minimum;
            float scaleSquared = 0.0F;
            for (auto index = first; index < end; ++index) {
                const auto& model = group.instances[index].model;
                float matrixSquared = 0.0F;
                for (std::size_t axis = 0U; axis < 3U; ++axis) {
                    minimum[axis] = std::min(minimum[axis], model[12U + axis]);
                    maximum[axis] = std::max(maximum[axis], model[12U + axis]);
                    const auto column = axis * 4U;
                    matrixSquared += model[column]*model[column] +
                        model[column+1U]*model[column+1U] + model[column+2U]*model[column+2U];
                }
                // Frobenius norm also bounds off-centre sections under shear.
                scaleSquared = std::max(scaleSquared, matrixSquared);
            }
            std::array<float, 3> center{};
            float radiusSquared = 0.0F;
            for (std::size_t axis = 0U; axis < 3U; ++axis) {
                center[axis] = (minimum[axis] + maximum[axis]) * 0.5F;
                const float extent = (maximum[axis] - minimum[axis]) * 0.5F;
                radiusSquared += extent * extent;
            }
            group.visibilityClusters.push_back({.firstInstance = static_cast<std::uint32_t>(first),
                .instanceCount = static_cast<std::uint32_t>(end-first),
                .origins = {.center = center, .radius = std::sqrt(radiusSquared)},
                .maximumScale = std::sqrt(scaleSquared)});
        }
    }
};
}
