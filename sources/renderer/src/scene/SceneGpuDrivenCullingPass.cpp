#include "kb/render/scene/SceneGpuDrivenCullingPass.hpp"

#include "kb/render/ShaderLoader.hpp"
#include "kb/render/scene/MeshPipeline.hpp"
#include "kb/render/scene/RenderInstanceBuffer.hpp"
#include "scene/pipeline/MeshPipelineVisibility.hpp"
#include <cstring>

#include <algorithm>
#include <array>

namespace kb::render {
namespace {

constexpr std::uint32_t kInstanceCullThreadGroupSize = 64U;

[[nodiscard]] std::uint32_t DispatchGroupCount(std::uint32_t candidateCount) noexcept {
    return std::max(1U, (candidateCount + kInstanceCullThreadGroupSize - 1U) / kInstanceCullThreadGroupSize);
}

[[nodiscard]] std::array<std::array<float, 4>, 6> BuildFrustumPlanes(const SceneRenderCamera* camera) noexcept {
    const auto frustum = MeshPipelineVisibility::BuildFrustum(camera);
    std::array<std::array<float, 4>, 6> planes{};
    for (std::size_t i = 0U; i < planes.size(); ++i) {
        const auto& p = frustum.planes[i];
        planes[i] = {p.x, p.y, p.z, p.w};
    }
    return planes;
}

} // namespace

bool SceneGpuDrivenCullingPass::Initialize() {
    if (IsInitialized()) {
        return true;
    }
    if ((bgfx::getCaps()->supported & (BGFX_CAPS_COMPUTE | BGFX_CAPS_DRAW_INDIRECT)) !=
        (BGFX_CAPS_COMPUTE | BGFX_CAPS_DRAW_INDIRECT)) {
        return false;
    }

    compactClearProgram_ = ShaderLoader::LoadComputeProgram("cs_instance_compact_clear.sc");
    compactProgram_ = ShaderLoader::LoadComputeProgram("cs_instance_compact.sc");
    compactParams_ = bgfx::createUniform("u_gpuCompactParams", bgfx::UniformType::Vec4);
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
    for (auto& view : drawViews_) for (auto& slot : view.slots) slot.Destroy();
    drawViews_.clear();
    if (bgfx::isValid(compactProgram_)) bgfx::destroy(compactProgram_);
    if (bgfx::isValid(compactClearProgram_)) bgfx::destroy(compactClearProgram_);
    if (bgfx::isValid(compactParams_)) bgfx::destroy(compactParams_);
    compactProgram_ = BGFX_INVALID_HANDLE;
    compactClearProgram_ = BGFX_INVALID_HANDLE;
    compactParams_ = BGFX_INVALID_HANDLE;
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
    stats.gpuDrivenBufferCapacity = desc.batch.capacity;
    stats.gpuDrivenUploadBytes = desc.batch.uploadBytes;
    return stats;
}

bool SceneGpuDrivenCullingPass::IsInitialized() const noexcept {
    return bgfx::isValid(compactProgram_) && bgfx::isValid(compactClearProgram_) &&
        bgfx::isValid(compactParams_) && bgfx::isValid(clearProgram_) &&
        bgfx::isValid(cullProgram_) &&
        bgfx::isValid(finalizeProgram_) &&
        bgfx::isValid(frustumPlanesUniform_) &&
        bgfx::isValid(cullingParamsUniform_);
}

void SceneGpuDrivenCullingPass::DrawSlot::Destroy() noexcept {
    if (bgfx::isValid(input)) bgfx::destroy(input);
    if (bgfx::isValid(output)) bgfx::destroy(output);
    if (bgfx::isValid(indirect)) bgfx::destroy(indirect);
    input = BGFX_INVALID_HANDLE;
    output = BGFX_INVALID_HANDLE;
    indirect = BGFX_INVALID_HANDLE;
    capacity = 0U;
    revision = 0U;
}

void SceneGpuDrivenCullingPass::EndFrame() const noexcept {
    for (auto& view : drawViews_) view.used = 0U;
}

SceneGpuCulledDraw SceneGpuDrivenCullingPass::Compact(bgfx::ViewId viewId,
    const MeshDrawCommand& command, const SceneRenderCamera* camera) const {
    if (!IsInitialized() || command.instances.empty() ||
        command.instances.size() > 65535U * kInstanceCullThreadGroupSize || command.indexCount > (1U << 24U) ||
        command.indexStart > (1U << 24U)) return {};
    const auto count = static_cast<std::uint32_t>(command.instances.size());
    auto found = std::find_if(drawViews_.begin(), drawViews_.end(),
        [viewId](const ViewSlots& v) { return v.view == viewId; });
    if (found == drawViews_.end()) {
        drawViews_.push_back(ViewSlots{.view = viewId});
        found = drawViews_.end() - 1;
    }
    if (found->used == found->slots.size()) found->slots.emplace_back();
    auto& slot = found->slots[found->used++];
    if (slot.capacity < count) {
        slot.Destroy();
        std::uint32_t capacity = 64U;
        while (capacity < count) capacity *= 2U;
        // A vec4 storage layout lets compute address the five rows of the
        // existing instance ABI without introducing a shader permutation.
        bgfx::VertexLayout layout;
        layout.begin().add(bgfx::Attrib::Position, 4U, bgfx::AttribType::Float).end();
        slot.input = bgfx::createDynamicVertexBuffer(capacity * 6U, layout, BGFX_BUFFER_COMPUTE_READ);
        bgfx::VertexLayout instances;
        instances.begin().add(bgfx::Attrib::TexCoord0, 4U, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord1, 4U, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord2, 4U, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord3, 4U, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord4, 4U, bgfx::AttribType::Float).end();
        slot.output = bgfx::createDynamicVertexBuffer(capacity, instances, BGFX_BUFFER_COMPUTE_READ_WRITE);
        slot.indirect = bgfx::createIndirectBuffer(1U);
        if (!bgfx::isValid(slot.input) || !bgfx::isValid(slot.output) || !bgfx::isValid(slot.indirect)) {
            slot.Destroy();
            return {};
        }
        slot.capacity = capacity;
    }
    struct Input {
        std::array<float, 4> sphere;
        RenderInstanceData instance;
    };
    static_assert(sizeof(Input) == 6U * 16U);
    const auto bytes = count * static_cast<std::uint32_t>(sizeof(Input));
    const bool upload = command.instanceRevision == 0U || slot.revision != command.instanceRevision;
    if (upload) {
    const auto memory = bgfx::alloc(bytes);
    for (std::uint32_t i = 0U; i < count; ++i) {
        const auto& instance = command.instances[i];
        const Input row{
            {instance.worldBounds.center[0], instance.worldBounds.center[1], instance.worldBounds.center[2], instance.worldBounds.radius},
            RenderInstanceBuffer::Pack(instance, command.materialResource, command.pass != MeshPassType::ShadowDepth)};
        std::memcpy(memory->data + i * sizeof(Input), &row, sizeof(row));
    }
    bgfx::update(slot.input, 0U, memory);
    slot.revision = command.instanceRevision;
    }
    const std::array<float, 4> params{static_cast<float>(count), static_cast<float>(command.indexCount),
        static_cast<float>(command.indexStart), 0.0F};
    const auto planes = BuildFrustumPlanes(camera);
    if (command.meshResource != nullptr && bgfx::isValid(command.meshResource->indexBuffer)) {
        bgfx::setIndexBuffer(command.meshResource->indexBuffer, 0U, command.meshResource->indexCount);
    }
    bgfx::setUniform(compactParams_, params.data());
    bgfx::setBuffer(3U, slot.indirect, bgfx::Access::Write);
    bgfx::dispatch(viewId, compactClearProgram_);
    // bgfx orders dispatches and transitions UAV -> indirect/instance reads.
    // Each draw owns a slot, so sorted draws cannot observe another camera's data.
    bgfx::setUniform(compactParams_, params.data());
    bgfx::setUniform(frustumPlanesUniform_, planes.data(), 6U);
    bgfx::setBuffer(0U, slot.input, bgfx::Access::Read);
    bgfx::setBuffer(1U, slot.output, bgfx::Access::Write);
    bgfx::setBuffer(3U, slot.indirect, bgfx::Access::ReadWrite);
    bgfx::dispatch(viewId, compactProgram_, DispatchGroupCount(count));
    return {.instances = slot.output, .indirect = slot.indirect, .capacity = slot.capacity, .uploadBytes = upload ? bytes : 0U};
}

} // namespace kb::render
