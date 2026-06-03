#include "kb/render/scene/SceneGpuDrivenCullingPass.hpp"

#include "kb/render/ShaderLoader.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace kb::render {
namespace {

constexpr std::uint32_t kInstanceCullThreadGroupSize = 64U;

[[nodiscard]] std::uint32_t DispatchGroupCount(std::uint32_t candidateCount) noexcept {
    return std::max(1U, (candidateCount + kInstanceCullThreadGroupSize - 1U) / kInstanceCullThreadGroupSize);
}

[[nodiscard]] float Length3(float x, float y, float z) noexcept {
    return std::sqrt(x * x + y * y + z * z);
}

[[nodiscard]] std::array<float, 4> NormalizePlane(std::array<float, 4> plane) noexcept {
    const float length = Length3(plane[0], plane[1], plane[2]);
    if (length <= 0.00001F) {
        return {};
    }
    const float invLength = 1.0F / length;
    return {
        plane[0] * invLength,
        plane[1] * invLength,
        plane[2] * invLength,
        plane[3] * invLength,
    };
}

[[nodiscard]] std::array<float, 16> MultiplyColumnMajor(const std::array<float, 16>& lhs, const std::array<float, 16>& rhs) noexcept {
    std::array<float, 16> result{};
    for (std::uint32_t column = 0U; column < 4U; ++column) {
        for (std::uint32_t row = 0U; row < 4U; ++row) {
            float value = 0.0F;
            for (std::uint32_t k = 0U; k < 4U; ++k) {
                value += lhs[k * 4U + row] * rhs[column * 4U + k];
            }
            result[column * 4U + row] = value;
        }
    }
    return result;
}

[[nodiscard]] std::array<std::array<float, 4>, 6> BuildFrustumPlanes(const SceneRenderCamera* camera) noexcept {
    if (camera == nullptr) {
        return {};
    }

    const std::array<float, 16> clip = MultiplyColumnMajor(camera->projection, camera->view);
    const std::array<float, 4> row0{ clip[0], clip[4], clip[8], clip[12] };
    const std::array<float, 4> row1{ clip[1], clip[5], clip[9], clip[13] };
    const std::array<float, 4> row2{ clip[2], clip[6], clip[10], clip[14] };
    const std::array<float, 4> row3{ clip[3], clip[7], clip[11], clip[15] };
    return {
        NormalizePlane({ row3[0] + row0[0], row3[1] + row0[1], row3[2] + row0[2], row3[3] + row0[3] }),
        NormalizePlane({ row3[0] - row0[0], row3[1] - row0[1], row3[2] - row0[2], row3[3] - row0[3] }),
        NormalizePlane({ row3[0] + row1[0], row3[1] + row1[1], row3[2] + row1[2], row3[3] + row1[3] }),
        NormalizePlane({ row3[0] - row1[0], row3[1] - row1[1], row3[2] - row1[2], row3[3] - row1[3] }),
        NormalizePlane({ row3[0] + row2[0], row3[1] + row2[1], row3[2] + row2[2], row3[3] + row2[3] }),
        NormalizePlane({ row3[0] - row2[0], row3[1] - row2[1], row3[2] - row2[2], row3[3] - row2[3] }),
    };
}

} // namespace

bool SceneGpuDrivenCullingPass::Initialize() {
    if (IsInitialized()) {
        return true;
    }

    clearProgram_ = ShaderLoader::LoadComputeProgram("cs_instance_cull_clear.sc");
    cullProgram_ = ShaderLoader::LoadComputeProgram("cs_instance_cull.sc");
    finalizeProgram_ = ShaderLoader::LoadComputeProgram("cs_instance_cull_finalize.sc");
    frustumPlanesUniform_ = bgfx::createUniform("u_gpuCullFrustum", bgfx::UniformType::Vec4, 6U);
    cullingParamsUniform_ = bgfx::createUniform("u_gpuCullParams", bgfx::UniformType::Vec4);
    if (!IsInitialized()) {
        Shutdown();
        return false;
    }
    return true;
}

void SceneGpuDrivenCullingPass::Shutdown() noexcept {
    if (bgfx::isValid(finalizeProgram_)) {
        bgfx::destroy(finalizeProgram_);
        finalizeProgram_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(cullProgram_)) {
        bgfx::destroy(cullProgram_);
        cullProgram_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(clearProgram_)) {
        bgfx::destroy(clearProgram_);
        clearProgram_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(cullingParamsUniform_)) {
        bgfx::destroy(cullingParamsUniform_);
        cullingParamsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(frustumPlanesUniform_)) {
        bgfx::destroy(frustumPlanesUniform_);
        frustumPlanesUniform_ = BGFX_INVALID_HANDLE;
    }
}

SceneRenderSubmitStats SceneGpuDrivenCullingPass::Submit(const SceneGpuDrivenCullingPassDesc& desc) const noexcept {
    SceneRenderSubmitStats stats{};
    if (!IsInitialized() || !desc.IsValid() || !desc.batch.IsValid()) {
        return stats;
    }

    const std::array<std::array<float, 4>, 6> frustumPlanes = BuildFrustumPlanes(desc.camera);
    const std::array<float, 4> params{
        static_cast<float>(desc.batch.instanceCount),
        static_cast<float>(desc.batch.capacity),
        0.0F,
        0.0F,
    };

    bgfx::setViewName(desc.viewId, "KB GPU Driven Instance Culling");
    bgfx::setViewMode(desc.viewId, bgfx::ViewMode::Sequential);

    bgfx::setBuffer(2U, desc.batch.predicateBuffer, bgfx::Access::Write);
    bgfx::setBuffer(3U, desc.batch.visibleListBuffer, bgfx::Access::Write);
    bgfx::setBuffer(4U, desc.batch.counterBuffer, bgfx::Access::Write);
    bgfx::dispatch(desc.viewId, clearProgram_, 1U, 1U, 1U);

    bgfx::setBuffer(0U, desc.batch.boundsBuffer, bgfx::Access::Read);
    bgfx::setBuffer(1U, desc.batch.metadataBuffer, bgfx::Access::Read);
    bgfx::setBuffer(2U, desc.batch.predicateBuffer, bgfx::Access::ReadWrite);
    bgfx::setBuffer(3U, desc.batch.visibleListBuffer, bgfx::Access::ReadWrite);
    bgfx::setBuffer(4U, desc.batch.counterBuffer, bgfx::Access::ReadWrite);
    bgfx::setUniform(frustumPlanesUniform_, frustumPlanes.data(), 6U);
    bgfx::setUniform(cullingParamsUniform_, params.data());
    bgfx::dispatch(desc.viewId, cullProgram_, DispatchGroupCount(desc.batch.instanceCount), 1U, 1U);

    bgfx::setBuffer(4U, desc.batch.counterBuffer, bgfx::Access::ReadWrite);
    bgfx::setUniform(cullingParamsUniform_, params.data());
    bgfx::dispatch(desc.viewId, finalizeProgram_, 1U, 1U, 1U);

    stats.gpuDrivenFeatureState = desc.featureState;
    stats.gpuDrivenCounterSource = SceneGpuDrivenCounterSource::GpuDispatchCounters;
    stats.gpuCullingDispatchCount = 3U;
    stats.gpuDrivenInputInstanceCount = desc.batch.instanceCount;
    stats.gpuDrivenBufferCapacity = desc.batch.capacity;
    stats.gpuDrivenUploadBytes = desc.batch.uploadBytes;
    return stats;
}

bool SceneGpuDrivenCullingPass::IsInitialized() const noexcept {
    return bgfx::isValid(clearProgram_) &&
        bgfx::isValid(cullProgram_) &&
        bgfx::isValid(finalizeProgram_) &&
        bgfx::isValid(frustumPlanesUniform_) &&
        bgfx::isValid(cullingParamsUniform_);
}

} // namespace kb::render
