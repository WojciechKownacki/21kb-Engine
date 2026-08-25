#include "kb/render/particles/ParticleGpuVisualSimulation.hpp"

#include "kb/render/ShaderLoader.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace kb::render {
namespace {

constexpr std::uint32_t kThreadGroupSize = 64U;

[[nodiscard]] bgfx::VertexLayout ParticleInstanceLayout() {
    bgfx::VertexLayout layout{};
    layout.begin()
        .add(bgfx::Attrib::TexCoord0, 4U, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 4U, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord2, 4U, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord3, 4U, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord4, 4U, bgfx::AttribType::Float)
        .end();
    return layout;
}

[[nodiscard]] bgfx::VertexLayout Vec4Layout() {
    bgfx::VertexLayout layout{};
    layout.begin().add(bgfx::Attrib::TexCoord0, 4U, bgfx::AttribType::Float).end();
    return layout;
}

[[nodiscard]] std::uint32_t NextCapacity(std::uint32_t requested) noexcept {
    std::uint32_t capacity = 64U;
    while (capacity < requested && capacity < kb::scene::kParticleEffectMaxGpuParticlesPerScene / 2U) {
        capacity *= 2U;
    }
    return capacity < requested ? requested : capacity;
}

[[nodiscard]] bool FitsGpuResourceBudget(std::uint32_t capacity) noexcept {
    constexpr std::uint64_t kBytesPerParticle =
        2U * sizeof(ParticleGpuInstance) + 4U * sizeof(std::array<float, 4U>);
    return static_cast<std::uint64_t>(capacity) * kBytesPerParticle <=
        kb::scene::kParticleEffectMaxGpuResourceBytes;
}

[[nodiscard]] const bgfx::Memory* CopyMemory(const void* source, std::uint32_t byteCount) noexcept {
    const bgfx::Memory* memory = bgfx::alloc(byteCount);
    if (memory == nullptr || memory->data == nullptr) return nullptr;
    std::memcpy(memory->data, source, byteCount);
    return memory;
}

} // namespace

bool ParticleGpuVisualSimulation::Initialize() {
    if (bgfx::isValid(program_)) return true;
    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps == nullptr || (caps->supported & BGFX_CAPS_COMPUTE) == 0U ||
        (caps->supported & BGFX_CAPS_DRAW_INDIRECT) == 0U) {
        return false;
    }
    program_ = ShaderLoader::LoadComputeProgram("cs_particle_visual_integrate.sc");
    paramsUniform_ = bgfx::createUniform("u_particleVisualParams", bgfx::UniformType::Vec4);
    if (!bgfx::isValid(program_) || !bgfx::isValid(paramsUniform_)) {
        Shutdown();
        return false;
    }
    if (!EnsureCapacity(64U)) {
        Shutdown();
        return false;
    }
    return true;
}

void ParticleGpuVisualSimulation::Shutdown() noexcept {
    DestroyBuffers();
    if (bgfx::isValid(paramsUniform_)) bgfx::destroy(paramsUniform_);
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    paramsUniform_ = BGFX_INVALID_HANDLE;
    program_ = BGFX_INVALID_HANDLE;
    preparedSceneId_ = 0U;
    preparedFixedStepIndex_ = 0U;
    preparedViewId_ = UINT16_MAX;
}

bool ParticleGpuVisualSimulation::Prepare(
    bgfx::ViewId viewId,
    std::uint64_t sceneId,
    std::uint64_t fixedStepIndex,
    std::span<const ParticleGpuInstance> source,
    std::span<const std::uint32_t> visualMask) noexcept {
    if (!bgfx::isValid(program_) || !bgfx::isValid(paramsUniform_) || sceneId == 0U ||
        fixedStepIndex == 0U || source.empty() || source.size() != visualMask.size() ||
        source.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    if (preparedSceneId_ == sceneId && preparedFixedStepIndex_ == fixedStepIndex) {
        return preparedViewId_ == viewId;
    }
    const std::uint32_t count = static_cast<std::uint32_t>(source.size());
    if (!EnsureCapacity(count)) return false;
    const std::uint64_t byteCount64 = static_cast<std::uint64_t>(count) * sizeof(ParticleGpuInstance);
    if (byteCount64 > std::numeric_limits<std::uint32_t>::max()) return false;
    const bgfx::Memory* memory = CopyMemory(source.data(), static_cast<std::uint32_t>(byteCount64));
    if (memory == nullptr) return false;
    constexpr std::array<std::uint32_t, 4U> clearedCounters{};
    const bgfx::Memory* counterMemory = CopyMemory(clearedCounters.data(),
        static_cast<std::uint32_t>(sizeof(clearedCounters)));
    if (counterMemory == nullptr) return false;
    const std::uint64_t maskByteCount64 = static_cast<std::uint64_t>(count) * sizeof(std::uint32_t);
    if (maskByteCount64 > std::numeric_limits<std::uint32_t>::max()) return false;
    const bgfx::Memory* maskMemory = CopyMemory(visualMask.data(), static_cast<std::uint32_t>(maskByteCount64));
    if (maskMemory == nullptr) return false;
    bgfx::update(sourceBuffer_, 0U, memory);
    bgfx::update(visualMask_, 0U, maskMemory);
    bgfx::update(aliveCounters_, 0U, counterMemory);
    const bool continuesPriorVisualStep = preparedSceneId_ == sceneId &&
        fixedStepIndex > preparedFixedStepIndex_;
    const std::array<float, 4U> params{
        static_cast<float>(count),
        1.0F / static_cast<float>(kb::scene::kParticleEffectFixedStepsPerSecond),
        continuesPriorVisualStep ? 1.0F : 0.0F,
        0.0F,
    };
    const std::uint8_t writeIndex = static_cast<std::uint8_t>(stateReadIndex_ ^ 1U);
    bgfx::setBuffer(0U, sourceBuffer_, bgfx::Access::Read);
    bgfx::setBuffer(1U, positionBuffers_[stateReadIndex_], bgfx::Access::Read);
    bgfx::setBuffer(2U, velocityBuffers_[stateReadIndex_], bgfx::Access::Read);
    bgfx::setBuffer(3U, positionBuffers_[writeIndex], bgfx::Access::Write);
    bgfx::setBuffer(4U, velocityBuffers_[writeIndex], bgfx::Access::Write);
    bgfx::setBuffer(5U, outputBuffer_, bgfx::Access::Write);
    bgfx::setBuffer(6U, aliveCounters_, bgfx::Access::ReadWrite);
    bgfx::setBuffer(7U, indirectArgs_, bgfx::Access::ReadWrite);
    bgfx::setBuffer(8U, visualMask_, bgfx::Access::Read);
    bgfx::setUniform(paramsUniform_, params.data());
    bgfx::dispatch(viewId, program_, (count + kThreadGroupSize - 1U) / kThreadGroupSize, 1U, 1U);
    preparedSceneId_ = sceneId;
    preparedFixedStepIndex_ = fixedStepIndex;
    preparedViewId_ = viewId;
    stateReadIndex_ = writeIndex;
    return true;
}

bool ParticleGpuVisualSimulation::HasPreparedView(
    bgfx::ViewId viewId,
    std::uint64_t sceneId,
    std::uint64_t fixedStepIndex) const noexcept {
    return preparedViewId_ == viewId && preparedSceneId_ == sceneId &&
        preparedFixedStepIndex_ == fixedStepIndex && bgfx::isValid(outputBuffer_);
}

bgfx::DynamicVertexBufferHandle ParticleGpuVisualSimulation::InstanceBuffer() const noexcept {
    return outputBuffer_;
}

kb::particles::ParticleGpuVisualAvailability ParticleGpuVisualSimulation::Availability() const noexcept {
    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps == nullptr) return kb::particles::ParticleGpuVisualAvailability::RendererUnavailable;
    if ((caps->supported & BGFX_CAPS_COMPUTE) == 0U) {
        return kb::particles::ParticleGpuVisualAvailability::ComputeUnsupported;
    }
    if ((caps->supported & BGFX_CAPS_DRAW_INDIRECT) == 0U) {
        return kb::particles::ParticleGpuVisualAvailability::ResourceUnavailable;
    }
    if (!bgfx::isValid(program_) || !bgfx::isValid(paramsUniform_)) {
        return kb::particles::ParticleGpuVisualAvailability::ShaderUnavailable;
    }
    if (!bgfx::isValid(sourceBuffer_) || !bgfx::isValid(outputBuffer_) ||
        !bgfx::isValid(positionBuffers_[0]) || !bgfx::isValid(positionBuffers_[1]) ||
        !bgfx::isValid(velocityBuffers_[0]) || !bgfx::isValid(velocityBuffers_[1]) ||
        !bgfx::isValid(visualMask_) || !bgfx::isValid(aliveCounters_) || !bgfx::isValid(indirectArgs_)) {
        return kb::particles::ParticleGpuVisualAvailability::ResourceUnavailable;
    }
    return kb::particles::ParticleGpuVisualAvailability::Ready;
}

void ParticleGpuVisualSimulation::ReleaseScene(std::uint64_t sceneId) noexcept {
    if (preparedSceneId_ == sceneId) {
        DestroyBuffers();
        preparedSceneId_ = 0U;
        preparedFixedStepIndex_ = 0U;
        preparedViewId_ = UINT16_MAX;
    }
}

void ParticleGpuVisualSimulation::ReleaseAllScenes() noexcept {
    DestroyBuffers();
    preparedSceneId_ = 0U;
    preparedFixedStepIndex_ = 0U;
    preparedViewId_ = UINT16_MAX;
}

bool ParticleGpuVisualSimulation::EnsureCapacity(std::uint32_t requestedCapacity) noexcept {
    if (requestedCapacity <= capacity_ && bgfx::isValid(sourceBuffer_) && bgfx::isValid(outputBuffer_) &&
        bgfx::isValid(positionBuffers_[0]) && bgfx::isValid(positionBuffers_[1]) &&
        bgfx::isValid(velocityBuffers_[0]) && bgfx::isValid(velocityBuffers_[1]) &&
        bgfx::isValid(visualMask_) && bgfx::isValid(aliveCounters_) && bgfx::isValid(indirectArgs_)) {
        return true;
    }
    DestroyBuffers();
    capacity_ = NextCapacity(requestedCapacity);
    if (capacity_ > kb::scene::kParticleEffectMaxGpuParticlesPerScene || !FitsGpuResourceBudget(capacity_)) {
        DestroyBuffers();
        return false;
    }
    const bgfx::VertexLayout layout = ParticleInstanceLayout();
    const bgfx::VertexLayout vec4Layout = Vec4Layout();
    sourceBuffer_ = bgfx::createDynamicVertexBuffer(capacity_, layout, BGFX_BUFFER_COMPUTE_READ);
    outputBuffer_ = bgfx::createDynamicVertexBuffer(capacity_, layout, BGFX_BUFFER_COMPUTE_READ_WRITE);
    positionBuffers_[0] = bgfx::createDynamicVertexBuffer(capacity_, vec4Layout, BGFX_BUFFER_COMPUTE_READ_WRITE);
    positionBuffers_[1] = bgfx::createDynamicVertexBuffer(capacity_, vec4Layout, BGFX_BUFFER_COMPUTE_READ_WRITE);
    velocityBuffers_[0] = bgfx::createDynamicVertexBuffer(capacity_, vec4Layout, BGFX_BUFFER_COMPUTE_READ_WRITE);
    velocityBuffers_[1] = bgfx::createDynamicVertexBuffer(capacity_, vec4Layout, BGFX_BUFFER_COMPUTE_READ_WRITE);
    visualMask_ = bgfx::createDynamicIndexBuffer(capacity_, BGFX_BUFFER_INDEX32 | BGFX_BUFFER_COMPUTE_READ);
    aliveCounters_ = bgfx::createDynamicIndexBuffer(4U, BGFX_BUFFER_INDEX32 | BGFX_BUFFER_COMPUTE_READ_WRITE);
    indirectArgs_ = bgfx::createIndirectBuffer(1U);
    if (!bgfx::isValid(sourceBuffer_) || !bgfx::isValid(outputBuffer_) ||
        !bgfx::isValid(positionBuffers_[0]) || !bgfx::isValid(positionBuffers_[1]) ||
        !bgfx::isValid(velocityBuffers_[0]) || !bgfx::isValid(velocityBuffers_[1]) ||
        !bgfx::isValid(visualMask_) || !bgfx::isValid(aliveCounters_) || !bgfx::isValid(indirectArgs_)) {
        DestroyBuffers();
        return false;
    }
    return true;
}

void ParticleGpuVisualSimulation::DestroyBuffers() noexcept {
    if (bgfx::isValid(indirectArgs_)) bgfx::destroy(indirectArgs_);
    if (bgfx::isValid(aliveCounters_)) bgfx::destroy(aliveCounters_);
    if (bgfx::isValid(visualMask_)) bgfx::destroy(visualMask_);
    if (bgfx::isValid(outputBuffer_)) bgfx::destroy(outputBuffer_);
    if (bgfx::isValid(sourceBuffer_)) bgfx::destroy(sourceBuffer_);
    for (bgfx::DynamicVertexBufferHandle& buffer : velocityBuffers_) {
        if (bgfx::isValid(buffer)) bgfx::destroy(buffer);
        buffer = BGFX_INVALID_HANDLE;
    }
    for (bgfx::DynamicVertexBufferHandle& buffer : positionBuffers_) {
        if (bgfx::isValid(buffer)) bgfx::destroy(buffer);
        buffer = BGFX_INVALID_HANDLE;
    }
    indirectArgs_ = BGFX_INVALID_HANDLE;
    aliveCounters_ = BGFX_INVALID_HANDLE;
    visualMask_ = BGFX_INVALID_HANDLE;
    outputBuffer_ = BGFX_INVALID_HANDLE;
    sourceBuffer_ = BGFX_INVALID_HANDLE;
    capacity_ = 0U;
    stateReadIndex_ = 0U;
}

} // namespace kb::render
