#include "kb/render/particles/ParticleGpuEmitterSimulation.hpp"

#include "engine/scene/ParticleEffectAssetSchema.hpp"
#include "kb/render/ShaderLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderToolchain.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <cmath>

namespace kb::render {
namespace {

constexpr std::uint32_t kThreadGroupSize = 64U;
constexpr std::uint32_t kInstanceBytes = 80U;
constexpr std::uint32_t kStateBytes = 32U;
static_assert(sizeof(kb::particles::ParticleGpuSpawn) == kStateBytes);
constexpr std::uint32_t kSortKeyBytes = 16U;
// A strip vertex: position, colour and texture coordinate, one vec4 each (the layout of ParticleStripVertex padded).
constexpr std::uint32_t kStripVertexBytes = 48U;
constexpr std::uint32_t kStripVerticesPerSegment = 4U;
constexpr std::uint32_t kStripIndicesPerSegment = 6U;
// How far behind a visible surface (m) a particle still counts as having hit it.
constexpr float kDepthCollisionThickness = 0.5F;
constexpr float kDefaultDepthRestitution = 0.5F;
// The event buffer of the collide kernel: a four-word header (word 0 counts the hits), then records of ten words:
// position, normal and velocity before the bounce (float bits) and the index of the emitter.
constexpr std::uint32_t kEventHeaderWords = 4U;
constexpr std::uint32_t kEventRecordWords = 10U;
constexpr double kFixedStepSeconds = 1.0 / static_cast<double>(kb::scene::kParticleEffectFixedStepsPerSecond);

[[nodiscard]] bgfx::VertexLayout SpawnLayout() {
    bgfx::VertexLayout layout{};
    layout.begin()
        .add(bgfx::Attrib::TexCoord0, 4U, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 4U, bgfx::AttribType::Float)
        .end();
    return layout;
}

[[nodiscard]] bgfx::VertexLayout InstanceLayout() {
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

[[nodiscard]] bgfx::VertexLayout StripLayout() {
    bgfx::VertexLayout layout{};
    layout.begin()
        .add(bgfx::Attrib::Position, 4U, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4U, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 4U, bgfx::AttribType::Float)
        .end();
    return layout;
}

// Segments of the strip a ribbon (one per ring slot) or a beam emitter builds on the GPU; zero for the other outputs.
[[nodiscard]] std::uint32_t StripSegments(const kb::particles::ParticleGpuEmitterParams& params, std::uint32_t capacity) noexcept {
    if (params.output == kb::particles::ParticleRenderOutput::Ribbon) return capacity;
    if (params.output == kb::particles::ParticleRenderOutput::Beam) return params.beamSegments;
    return 0U;
}

// The rotation matrix of a quaternion (x, y, z, w) as three columns (padded to four floats).
[[nodiscard]] std::array<float, 12> BasisColumns(const std::array<float, 4>& q) noexcept {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    return {
        1.0F - 2.0F * (y * y + z * z), 2.0F * (x * y + w * z), 2.0F * (x * z - w * y), 0.0F,
        2.0F * (x * y - w * z), 1.0F - 2.0F * (x * x + z * z), 2.0F * (y * z + w * x), 0.0F,
        2.0F * (x * z + w * y), 2.0F * (y * z - w * x), 1.0F - 2.0F * (x * x + y * y), 0.0F };
}

[[nodiscard]] std::uint32_t InstancesPerSlot(const kb::particles::ParticleGpuEmitterParams& params) noexcept {
    return params.output == kb::particles::ParticleRenderOutput::Trail ? std::max(params.trailSegments, 1U) : 1U;
}

[[nodiscard]] std::uint32_t PaddedSortCount(std::uint32_t capacity) noexcept {
    std::uint32_t padded = 2U;
    while (padded < capacity) padded <<= 1U;
    return padded;
}

[[nodiscard]] const bgfx::Memory* CopyMemory(const void* source, std::uint32_t bytes) noexcept {
    const bgfx::Memory* memory = bgfx::alloc(bytes);
    if (memory == nullptr || memory->data == nullptr) return nullptr;
    std::memcpy(memory->data, source, bytes);
    return memory;
}

} // namespace

// Billboards follow the authored sort, except additive blending as on the CPU path.
// Mesh instances use the material's transparency; trail segments retain their strip order.
bool ParticleGpuEmitterSimulation::NeedsSort(const kb::particles::ParticleGpuEmitterParams& params, bool translucentMaterial) noexcept {
    if (params.sort == kb::particles::ParticleRenderSortMode::None) return false;
    if (params.output == kb::particles::ParticleRenderOutput::Mesh) return translucentMaterial;
    const bool billboard = params.output == kb::particles::ParticleRenderOutput::Billboard ||
        params.output == kb::particles::ParticleRenderOutput::StretchedBillboard;
    return billboard && params.blend != kb::particles::ParticleRenderBlendMode::Add;
}

bool ParticleGpuEmitterSimulation::Initialize(std::uint64_t maxBufferBytes, std::uint32_t collisionEventCapacity) {
    if (IsReady()) return true;
    collisionEventCapacity_ = std::max(collisionEventCapacity, 1U);
    maxBufferBytes_ = maxBufferBytes == 0U ? kb::scene::kParticleEffectMaxGpuResourceBytes
        : std::min(maxBufferBytes, kb::scene::kParticleEffectMaxGpuResourceBytes);
    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps == nullptr || (caps->supported & BGFX_CAPS_COMPUTE) == 0U) return false;
    processFrameUniform_ = bgfx::createUniform("u_particleProcessFrame", bgfx::UniformType::Vec4);
    processInverseUniform_ = bgfx::createUniform("u_particleProcessInverse", bgfx::UniformType::Mat4);
    program_ = ShaderLoader::LoadComputeProgram("cs_particle_gpu_emit.sc");
    motionUniform_ = bgfx::createUniform("u_gpuParticleMotion", bgfx::UniformType::Vec4);
    timeUniform_ = bgfx::createUniform("u_gpuParticleTime", bgfx::UniformType::Vec4);
    colorUniform_ = bgfx::createUniform("u_gpuParticleColor", bgfx::UniformType::Vec4,
        static_cast<std::uint16_t>(kb::particles::kParticleGpuCurveSamples));
    sizeUniform_ = bgfx::createUniform("u_gpuParticleSize", bgfx::UniformType::Vec4, 2U);
    outputUniform_ = bgfx::createUniform("u_gpuParticleOutput", bgfx::UniformType::Vec4);
    flipbookUniform_ = bgfx::createUniform("u_gpuParticleFlipbook", bgfx::UniformType::Vec4);
    spinUniform_ = bgfx::createUniform("u_gpuParticleSpin", bgfx::UniformType::Vec4, 4U);
    basisUniform_ = bgfx::createUniform("u_gpuParticleBasis", bgfx::UniformType::Vec4, 3U);
    forcesUniform_ = bgfx::createUniform("u_gpuParticleForces", bgfx::UniformType::Vec4, static_cast<std::uint16_t>(2U * kb::particles::kParticleGpuMaxMotionModules));
    fieldUniform_ = bgfx::createUniform("u_gpuParticleField", bgfx::UniformType::Vec4);
    noiseOriginUniform_ = bgfx::createUniform("u_gpuParticleNoiseOrigin", bgfx::UniformType::Vec4);
    eventUniform_ = bgfx::createUniform("u_gpuParticleEvent", bgfx::UniformType::Vec4);
    collideProgram_ = ShaderLoader::LoadComputeProgram("cs_particle_gpu_collide.sc");
    rebaseProgram_ = ShaderLoader::LoadComputeProgram("cs_particle_gpu_rebase.sc");
    rebaseUniform_ = bgfx::createUniform("u_gpuParticleRebase", bgfx::UniformType::Vec4);
    worldUniform_ = bgfx::createUniform("u_gpuParticleWorld", bgfx::UniformType::Mat4);
    worldInverseUniform_ = bgfx::createUniform("u_gpuParticleWorldInverse", bgfx::UniformType::Mat4);
    localUniform_ = bgfx::createUniform("u_gpuParticleLocal", bgfx::UniformType::Vec4);
    planeUniform_ = bgfx::createUniform("u_gpuParticlePlane", bgfx::UniformType::Vec4, 2U);
    collisionUniform_ = bgfx::createUniform("u_gpuParticleCollision", bgfx::UniformType::Vec4);
    depthBounceUniform_ = bgfx::createUniform("u_gpuParticleDepthBounce", bgfx::UniformType::Vec4);
    texelUniform_ = bgfx::createUniform("u_gpuParticleTexel", bgfx::UniformType::Vec4);
    viewProjectionUniform_ = bgfx::createUniform("u_gpuParticleViewProj", bgfx::UniformType::Mat4);
    depthSampler_ = bgfx::createUniform("s_particleDepth", bgfx::UniformType::Sampler);
    inverseViewProjectionUniform_ = bgfx::createUniform("u_deferredInverseViewProjection", bgfx::UniformType::Mat4);
    depthParamsUniform_ = bgfx::createUniform("u_deferredDepthParams", bgfx::UniformType::Vec4);
    cameraPositionUniform_ = bgfx::createUniform("u_deferredCameraPosition", bgfx::UniformType::Vec4);
    stripProgram_ = ShaderLoader::LoadComputeProgram("cs_particle_gpu_strip.sc");
    stripModeUniform_ = bgfx::createUniform("u_gpuStripMode", bgfx::UniformType::Vec4);
    stripBeamUniform_ = bgfx::createUniform("u_gpuStripBeam", bgfx::UniformType::Vec4, 3U);
    stripCameraUniform_ = bgfx::createUniform("u_gpuStripCamera", bgfx::UniformType::Vec4, 2U);
    sortKeysProgram_ = ShaderLoader::LoadComputeProgram("cs_particle_gpu_sort_keys.sc");
    sortStepProgram_ = ShaderLoader::LoadComputeProgram("cs_particle_gpu_sort_step.sc");
    sortGatherProgram_ = ShaderLoader::LoadComputeProgram("cs_particle_gpu_sort_gather.sc");
    sortCameraUniform_ = bgfx::createUniform("u_gpuSortCamera", bgfx::UniformType::Vec4);
    sortDepthUniform_ = bgfx::createUniform("u_gpuSortDepth", bgfx::UniformType::Vec4);
    sortParamsUniform_ = bgfx::createUniform("u_gpuSortParams", bgfx::UniformType::Vec4);
    const std::uint32_t zeroTexel = 0U;
    fallbackDepth_ = bgfx::createTexture2D(1U, 1U, false, 1U, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE, bgfx::copy(&zeroTexel, sizeof(zeroTexel)));
    if (!IsReady()) {
        Shutdown();
        return false;
    }
    return true;
}

bool ParticleGpuEmitterSimulation::IsReady() const noexcept {
    return bgfx::isValid(program_) && bgfx::isValid(motionUniform_) && bgfx::isValid(timeUniform_) &&
        bgfx::isValid(colorUniform_) && bgfx::isValid(sizeUniform_) && bgfx::isValid(collideProgram_) &&
        bgfx::isValid(forcesUniform_) && bgfx::isValid(fieldUniform_) && bgfx::isValid(noiseOriginUniform_) &&
        bgfx::isValid(eventUniform_) &&
        bgfx::isValid(flipbookUniform_) && bgfx::isValid(stripProgram_) && bgfx::isValid(stripModeUniform_) &&
        bgfx::isValid(stripBeamUniform_) && bgfx::isValid(stripCameraUniform_);
}

void ParticleGpuEmitterSimulation::Shutdown() noexcept {
    ReleaseAllScenes();
    // Teardown only: let bgfx finish the copies that still target the CPU storage before it is freed.
    if (!retiredReads_.empty()) {
        std::uint32_t lastReady = 0U;
        for (const auto& read : retiredReads_) lastReady = std::max(lastReady, read->readyFrame);
        for (std::uint32_t frame = bgfx::frame(); frame < lastReady; frame = bgfx::frame()) {}
        retiredReads_.clear();
    }
    for (bgfx::UniformHandle* handle : { &motionUniform_, &timeUniform_, &colorUniform_, &sizeUniform_, &worldUniform_, &worldInverseUniform_, &localUniform_, &planeUniform_,
             &collisionUniform_, &depthBounceUniform_, &texelUniform_, &viewProjectionUniform_, &depthSampler_,
             &inverseViewProjectionUniform_, &depthParamsUniform_, &cameraPositionUniform_, &sortCameraUniform_,
             &sortParamsUniform_, &sortDepthUniform_, &outputUniform_, &flipbookUniform_, &spinUniform_, &basisUniform_, &rebaseUniform_, &forcesUniform_,
             &noiseOriginUniform_, &eventUniform_, &fieldUniform_, &stripModeUniform_, &stripBeamUniform_, &stripCameraUniform_ }) {
        if (bgfx::isValid(*handle)) bgfx::destroy(*handle);
        *handle = BGFX_INVALID_HANDLE;
    }
    for (bgfx::ProgramHandle* handle : { &sortKeysProgram_, &sortStepProgram_, &sortGatherProgram_, &rebaseProgram_, &stripProgram_ }) {
        if (bgfx::isValid(*handle)) bgfx::destroy(*handle);
        *handle = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(fallbackDepth_)) bgfx::destroy(fallbackDepth_);
    fallbackDepth_ = BGFX_INVALID_HANDLE;
    if (bgfx::isValid(collideProgram_)) bgfx::destroy(collideProgram_);
    collideProgram_ = BGFX_INVALID_HANDLE;
    if (bgfx::isValid(processFrameUniform_)) bgfx::destroy(processFrameUniform_);
    if (bgfx::isValid(processInverseUniform_)) bgfx::destroy(processInverseUniform_);
    processFrameUniform_ = BGFX_INVALID_HANDLE; processInverseUniform_ = BGFX_INVALID_HANDLE;
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    program_ = BGFX_INVALID_HANDLE;
}

bool ParticleGpuEmitterSimulation::Create(Emitter& emitter, const kb::particles::ParticleGpuEmitterParams& params) noexcept {
    const std::uint32_t capacity = std::clamp(params.capacity, 1U, kb::particles::kParticleGpuMaxCapacity);
    const std::uint32_t perSlot = InstancesPerSlot(params);
    if (static_cast<std::uint64_t>(capacity) * perSlot > kb::particles::kParticleGpuMaxTrailSegments &&
        perSlot > 1U) return false;
    const std::uint32_t stripSegments = StripSegments(params, capacity);
    if (stripSegments > kb::scene::kParticleEffectMaxStripSegmentsPerEmitter ||
        (stripSegments == 0U && (params.output == kb::particles::ParticleRenderOutput::Ribbon ||
            params.output == kb::particles::ParticleRenderOutput::Beam))) return false;
    const std::uint64_t stripBytes = static_cast<std::uint64_t>(stripSegments) *
        (kStripVerticesPerSegment * kStripVertexBytes + kStripIndicesPerSegment * sizeof(std::uint16_t));
    const std::uint64_t bytes = static_cast<std::uint64_t>(capacity) *
        (2U * sizeof(kb::particles::ParticleGpuSpawn) + static_cast<std::uint64_t>(kInstanceBytes) * perSlot) + stripBytes;
    if (allocatedBytes_ + bytes > maxBufferBytes_) return false;
    emitter.params = params;
    emitter.params.capacity = capacity;
    emitter.capacity = capacity;
    emitter.perSlot = perSlot;
    emitter.stripSegments = stripSegments;
    emitter.bytes = bytes;
    // Zeroed birth records have lifetime 0, so every slot starts dead.
    const std::uint32_t spawnBytes = capacity * static_cast<std::uint32_t>(sizeof(kb::particles::ParticleGpuSpawn));
    const bgfx::Memory* zero = bgfx::alloc(spawnBytes);
    if (zero == nullptr || zero->data == nullptr) return false;
    std::memset(zero->data, 0, spawnBytes);
    // bgfx forbids CPU initialization and updates of compute-write buffers. Upload into a
    // CPU-writable source, then copy on the GPU before rebase and simulation.
    emitter.spawnUpload = bgfx::createDynamicVertexBuffer(zero, SpawnLayout(), BGFX_BUFFER_COMPUTE_READ);
    emitter.spawns = bgfx::createDynamicVertexBuffer(capacity, SpawnLayout(), BGFX_BUFFER_COMPUTE_READ_WRITE);
    emitter.instances = bgfx::createDynamicVertexBuffer(capacity * perSlot, InstanceLayout(), BGFX_BUFFER_COMPUTE_READ_WRITE);
    if (stripSegments != 0U) {
        // One quad per segment with its own four vertices (so a dead slot hides only its segment), indexed once.
        std::vector<std::uint16_t> indices;
        indices.reserve(static_cast<std::size_t>(stripSegments) * kStripIndicesPerSegment);
        for (std::uint32_t segment = 0U; segment < stripSegments; ++segment) {
            const auto at = static_cast<std::uint16_t>(segment * kStripVerticesPerSegment);
            indices.insert(indices.end(), {at, static_cast<std::uint16_t>(at + 1U), static_cast<std::uint16_t>(at + 3U),
                at, static_cast<std::uint16_t>(at + 3U), static_cast<std::uint16_t>(at + 2U)});
        }
        emitter.stripVertices = bgfx::createDynamicVertexBuffer(stripSegments * kStripVerticesPerSegment, StripLayout(),
            BGFX_BUFFER_COMPUTE_READ_WRITE);
        emitter.stripIndices = bgfx::createIndexBuffer(
            CopyMemory(indices.data(), static_cast<std::uint32_t>(indices.size() * sizeof(std::uint16_t))));
    }
    if (!bgfx::isValid(emitter.spawnUpload) || !bgfx::isValid(emitter.spawns) || !bgfx::isValid(emitter.instances) ||
        (stripSegments != 0U && (!bgfx::isValid(emitter.stripVertices) || !bgfx::isValid(emitter.stripIndices)))) {
        Destroy(emitter);
        return false;
    }
    allocatedBytes_ += bytes;
    if (params.NeedsState() && !EnsureState(emitter)) {
        allocatedBytes_ -= bytes;
        Destroy(emitter);
        return false;
    }
    // Sorting is an enhancement: without budget or kernels the emitter is simply drawn in ring order. (A mesh
    // emitter finds out whether its material is translucent at dispatch.)
    if (NeedsSort(params, false)) static_cast<void>(EnsureSort(emitter));
    return true;
}

bool ParticleGpuEmitterSimulation::EnsureSort(Emitter& emitter) noexcept {
    if (bgfx::isValid(emitter.sortedInstances)) return true;
    if (!bgfx::isValid(sortKeysProgram_) || !bgfx::isValid(sortStepProgram_) || !bgfx::isValid(sortGatherProgram_) ||
        !bgfx::isValid(sortCameraUniform_) || !bgfx::isValid(sortDepthUniform_) || !bgfx::isValid(sortParamsUniform_)) return false;
    const std::uint32_t padded = PaddedSortCount(emitter.capacity);
    const std::uint64_t sortBytes = static_cast<std::uint64_t>(padded) * kSortKeyBytes +
        static_cast<std::uint64_t>(emitter.capacity) * kInstanceBytes;
    if (allocatedBytes_ + sortBytes > maxBufferBytes_) return false;
    bgfx::VertexLayout keyLayout{};
    keyLayout.begin().add(bgfx::Attrib::TexCoord0, 4U, bgfx::AttribType::Float).end();
    emitter.sortKeys = bgfx::createDynamicVertexBuffer(padded, keyLayout, BGFX_BUFFER_COMPUTE_READ_WRITE);
    emitter.sortedInstances = bgfx::createDynamicVertexBuffer(emitter.capacity, InstanceLayout(), BGFX_BUFFER_COMPUTE_READ_WRITE);
    if (!bgfx::isValid(emitter.sortKeys) || !bgfx::isValid(emitter.sortedInstances)) {
        if (bgfx::isValid(emitter.sortKeys)) bgfx::destroy(emitter.sortKeys);
        if (bgfx::isValid(emitter.sortedInstances)) bgfx::destroy(emitter.sortedInstances);
        emitter.sortKeys = BGFX_INVALID_HANDLE;
        emitter.sortedInstances = BGFX_INVALID_HANDLE;
        return false;
    }
    emitter.bytes += sortBytes;
    allocatedBytes_ += sortBytes;
    return true;
}

void ParticleGpuEmitterSimulation::SortInstances(
    bgfx::ViewId viewId, const Emitter& emitter, const FrameContext& frame, float simTime) noexcept {
    const float meshLayout = emitter.params.output == kb::particles::ParticleRenderOutput::Mesh ? 1.0F : 0.0F;
    const std::uint32_t padded = PaddedSortCount(emitter.capacity);
    const std::array<float, 4> camera{ frame.cameraPosition[0], frame.cameraPosition[1], frame.cameraPosition[2], static_cast<float>(emitter.capacity) };
    const auto groups = [](std::uint32_t threads) { return (threads + kThreadGroupSize - 1U) / kThreadGroupSize; };
    bgfx::setBuffer(0U, emitter.instances, bgfx::Access::Read);
    bgfx::setBuffer(1U, emitter.sortKeys, bgfx::Access::Write);
    bgfx::setBuffer(2U, emitter.spawns, bgfx::Access::Read);
    bgfx::setUniform(sortCameraUniform_, camera.data());
    bgfx::setUniform(sortDepthUniform_, frame.cameraDepth.data());
    const std::array<float, 4> keyParams{ static_cast<float>(padded), meshLayout, static_cast<float>(emitter.params.sort), simTime };
    bgfx::setUniform(sortParamsUniform_, keyParams.data());
    bgfx::dispatch(viewId, sortKeysProgram_, groups(padded), 1U, 1U);
    // Bitonic sort, descending by the selected key.
    for (std::uint32_t block = 2U; block <= padded; block <<= 1U) {
        for (std::uint32_t distance = block >> 1U; distance > 0U; distance >>= 1U) {
            const std::array<float, 4> step{ static_cast<float>(padded), static_cast<float>(block), static_cast<float>(distance), 0.0F };
            bgfx::setBuffer(0U, emitter.sortKeys, bgfx::Access::ReadWrite);
            bgfx::setUniform(sortParamsUniform_, step.data());
            bgfx::dispatch(viewId, sortStepProgram_, groups(padded / 2U), 1U, 1U);
        }
    }
    bgfx::setBuffer(0U, emitter.instances, bgfx::Access::Read);
    bgfx::setBuffer(1U, emitter.sortKeys, bgfx::Access::Read);
    bgfx::setBuffer(2U, emitter.sortedInstances, bgfx::Access::Write);
    bgfx::setUniform(sortCameraUniform_, camera.data());
    bgfx::dispatch(viewId, sortGatherProgram_, groups(emitter.capacity), 1U, 1U);
}

void ParticleGpuEmitterSimulation::BuildStrip(
    bgfx::ViewId viewId, const Emitter& emitter, const FrameContext& frame, float simTime) noexcept {
    const bool beam = emitter.params.output == kb::particles::ParticleRenderOutput::Beam;
    const float width = beam ? emitter.params.beamWidth : emitter.params.ribbonWidth;
    const std::array<float, 4> mode{ beam ? 1.0F : 0.0F, static_cast<float>(emitter.stripSegments), width,
        static_cast<float>(emitter.nextSlot % emitter.capacity) };
    const std::array<float, 12> beamData{
        emitter.beamStart[0], emitter.beamStart[1], emitter.beamStart[2], emitter.params.beamIntensity,
        emitter.beamEnd[0], emitter.beamEnd[1], emitter.beamEnd[2], emitter.params.beamNoiseAmplitude,
        emitter.params.beamNoiseFrequency, simTime * static_cast<float>(kb::scene::kParticleEffectFixedStepsPerSecond), 0.0F, 0.0F };
    const std::array<float, 8> camera{ frame.cameraDepth[0], frame.cameraDepth[1], frame.cameraDepth[2], 0.0F,
        frame.cameraRight[0], frame.cameraRight[1], frame.cameraRight[2], 0.0F };
    bgfx::setBuffer(0U, emitter.instances, bgfx::Access::Read);
    bgfx::setBuffer(1U, emitter.stripVertices, bgfx::Access::Write);
    bgfx::setUniform(stripModeUniform_, mode.data());
    bgfx::setUniform(stripBeamUniform_, beamData.data(), 3U);
    bgfx::setUniform(stripCameraUniform_, camera.data(), 2U);
    bgfx::dispatch(viewId, stripProgram_, (emitter.stripSegments + kThreadGroupSize - 1U) / kThreadGroupSize, 1U, 1U);
}

bool ParticleGpuEmitterSimulation::EnsureState(Emitter& emitter) noexcept {
    if (bgfx::isValid(emitter.state)) return true;
    const std::uint64_t stateBytes = static_cast<std::uint64_t>(emitter.capacity) * kStateBytes;
    if (allocatedBytes_ + 2U * stateBytes > maxBufferBytes_) return false;
    // A zeroed record never matches a birth stamp, so every particle starts from its birth record.
    const bgfx::Memory* zero = bgfx::alloc(static_cast<std::uint32_t>(stateBytes));
    if (zero == nullptr || zero->data == nullptr) return false;
    std::memset(zero->data, 0, static_cast<std::size_t>(stateBytes));
    const auto stateZeros = bgfx::createDynamicVertexBuffer(zero, SpawnLayout(), BGFX_BUFFER_COMPUTE_READ);
    const auto state = bgfx::createDynamicVertexBuffer(emitter.capacity, SpawnLayout(), BGFX_BUFFER_COMPUTE_READ_WRITE);
    if (!bgfx::isValid(stateZeros) || !bgfx::isValid(state)) {
        if (bgfx::isValid(stateZeros)) bgfx::destroy(stateZeros);
        if (bgfx::isValid(state)) bgfx::destroy(state);
        return false;
    }
    emitter.stateZeros = stateZeros;
    emitter.state = state;
    emitter.initializeState = true;
    emitter.bytes += 2U * stateBytes;
    allocatedBytes_ += 2U * stateBytes;
    return true;
}

bool ParticleGpuEmitterSimulation::EnsureCustomProcess(Emitter& emitter, std::uint64_t assetId, kb::assets::AssetManager* assets) noexcept {
    try {
        if (assetId == 0U) {
            if (bgfx::isValid(emitter.customProgram)) bgfx::destroy(emitter.customProgram);
            if (bgfx::isValid(emitter.customState)) bgfx::destroy(emitter.customState);
            if (bgfx::isValid(emitter.customZeros)) bgfx::destroy(emitter.customZeros);
            emitter.customProgram = BGFX_INVALID_HANDLE; emitter.customState = BGFX_INVALID_HANDLE;
            emitter.customZeros = BGFX_INVALID_HANDLE; emitter.customAsset.reset();
            allocatedBytes_ -= emitter.customBytes; emitter.bytes -= emitter.customBytes; emitter.customBytes = 0U;
            return true;
        }
        if (!assets || bgfx::getCaps()->limits.maxComputeBindings < 4U || !bgfx::isValid(processFrameUniform_) || !bgfx::isValid(processInverseUniform_)) return false;
        auto asset = assets->Load<kb::particles::ParticleProcessAsset>(kb::assets::AssetId{assetId}).Shared();
        if (!asset) return false;
        if (asset == emitter.customAsset && bgfx::isValid(emitter.customProgram)) return true;
        const auto profile = RenderMaterialGraphShaderBackendName(RenderMaterialGraphShaderBackendForActiveRenderer());
        const auto variant = asset->programs.find(std::string{profile});
        if (variant == asset->programs.end()) return false;
        const auto memory = bgfx::copy(variant->second.data(), static_cast<std::uint32_t>(variant->second.size()));
        if (!memory) return false;
        auto shader = bgfx::createShader(memory);
        if (!bgfx::isValid(shader)) return false;
        auto program = bgfx::createProgram(shader, true);
        if (!bgfx::isValid(program)) return false;
        auto state = emitter.customState, zeros = emitter.customZeros;
        std::uint64_t bytes = 0U;
        if (!bgfx::isValid(state)) {
            bytes = static_cast<std::uint64_t>(emitter.capacity) * kStateBytes * 2U;
            if (allocatedBytes_ + bytes > maxBufferBytes_) { bgfx::destroy(program); return false; }
            auto zero = bgfx::alloc(emitter.capacity * kStateBytes);
            if (!zero) { bgfx::destroy(program); return false; }
            std::memset(zero->data, 0, zero->size);
            zeros = bgfx::createDynamicVertexBuffer(zero, SpawnLayout(), BGFX_BUFFER_COMPUTE_READ);
            state = bgfx::createDynamicVertexBuffer(emitter.capacity, SpawnLayout(), BGFX_BUFFER_COMPUTE_READ_WRITE);
            if (!bgfx::isValid(zeros) || !bgfx::isValid(state)) {
                if (bgfx::isValid(zeros)) bgfx::destroy(zeros);
                if (bgfx::isValid(state)) bgfx::destroy(state);
                bgfx::destroy(program); return false;
            }
        }
        if (bgfx::isValid(emitter.customProgram)) bgfx::destroy(emitter.customProgram);
        emitter.customProgram = program; emitter.customState = state; emitter.customZeros = zeros;
        emitter.customAsset = std::move(asset); emitter.initializeCustom = true;
        emitter.customBytes += bytes; emitter.bytes += bytes; allocatedBytes_ += bytes;
        return true;
    } catch (...) { return false; }
}

void ParticleGpuEmitterSimulation::Destroy(Emitter& emitter) noexcept {
    if (bgfx::isValid(emitter.customProgram)) bgfx::destroy(emitter.customProgram);
    if (bgfx::isValid(emitter.customState)) bgfx::destroy(emitter.customState);
    if (bgfx::isValid(emitter.customZeros)) bgfx::destroy(emitter.customZeros);
    emitter.customProgram = BGFX_INVALID_HANDLE; emitter.customState = BGFX_INVALID_HANDLE;
    emitter.customZeros = BGFX_INVALID_HANDLE; emitter.customAsset.reset();
    if (bgfx::isValid(emitter.spawns)) bgfx::destroy(emitter.spawns);
    if (bgfx::isValid(emitter.spawnUpload)) bgfx::destroy(emitter.spawnUpload);
    if (bgfx::isValid(emitter.instances)) bgfx::destroy(emitter.instances);
    if (bgfx::isValid(emitter.state)) bgfx::destroy(emitter.state);
    if (bgfx::isValid(emitter.stateZeros)) bgfx::destroy(emitter.stateZeros);
    if (bgfx::isValid(emitter.sortKeys)) bgfx::destroy(emitter.sortKeys);
    if (bgfx::isValid(emitter.sortedInstances)) bgfx::destroy(emitter.sortedInstances);
    if (bgfx::isValid(emitter.stripVertices)) bgfx::destroy(emitter.stripVertices);
    if (bgfx::isValid(emitter.stripIndices)) bgfx::destroy(emitter.stripIndices);
    emitter.stripVertices = BGFX_INVALID_HANDLE;
    emitter.stripIndices = BGFX_INVALID_HANDLE;
    emitter.sortKeys = BGFX_INVALID_HANDLE;
    emitter.sortedInstances = BGFX_INVALID_HANDLE;
    emitter.spawns = BGFX_INVALID_HANDLE;
    emitter.spawnUpload = BGFX_INVALID_HANDLE;
    emitter.instances = BGFX_INVALID_HANDLE;
    emitter.state = BGFX_INVALID_HANDLE;
    emitter.stateZeros = BGFX_INVALID_HANDLE;
}

void ParticleGpuEmitterSimulation::Upload(Emitter& emitter, std::span<const kb::particles::ParticleGpuSpawn> spawns) noexcept {
    if (spawns.empty()) return;
    // Only the newest `capacity` particles can still be alive in the ring.
    if (spawns.size() > emitter.capacity) {
        emitter.nextSlot += spawns.size() - emitter.capacity;
        spawns = spawns.last(emitter.capacity);
    }
    std::uint32_t uploaded = 0U;
    const std::uint32_t total = static_cast<std::uint32_t>(spawns.size());
    while (uploaded < total) {
        const std::uint32_t slot = static_cast<std::uint32_t>(emitter.nextSlot % emitter.capacity);
        const std::uint32_t chunk = std::min(total - uploaded, emitter.capacity - slot);
        const bgfx::Memory* memory = CopyMemory(
            spawns.data() + uploaded, chunk * static_cast<std::uint32_t>(sizeof(kb::particles::ParticleGpuSpawn)));
        if (memory == nullptr) return;
        bgfx::update(emitter.spawnUpload, slot, memory);
        emitter.pendingBirths = std::min(emitter.capacity, emitter.pendingBirths + chunk);
        emitter.nextSlot += chunk;
        uploaded += chunk;
    }
}

void ParticleGpuEmitterSimulation::Clear(Emitter& emitter) noexcept {
    const std::uint32_t bytes = emitter.capacity * static_cast<std::uint32_t>(sizeof(kb::particles::ParticleGpuSpawn));
    const bgfx::Memory* zero = bgfx::alloc(bytes);
    if (zero == nullptr || zero->data == nullptr) return;
    std::memset(zero->data, 0, bytes);
    bgfx::update(emitter.spawnUpload, 0U, zero);
    emitter.initializeSpawns = true;
    emitter.initializeState = bgfx::isValid(emitter.state);
    emitter.initializeCustom = bgfx::isValid(emitter.customState);
    emitter.pendingBirths = 0U;
}

void ParticleGpuEmitterSimulation::FlushUploads(bgfx::ViewId viewId, Emitter& emitter) noexcept {
    const auto copyRange = [viewId](bgfx::DynamicVertexBufferHandle destination,
                                  bgfx::DynamicVertexBufferHandle source, std::uint32_t slot, std::uint32_t count) {
        if (count == 0U) return;
        const std::uint32_t offset = slot * kStateBytes;
        bgfx::blit(viewId, bgfx::BufferRegion{.handle = destination, .offset = offset},
            bgfx::BufferRegion{.handle = source, .offset = offset, .size = count * kStateBytes});
    };
    if (emitter.initializeCustom) copyRange(emitter.customState, emitter.customZeros, 0U, emitter.capacity);
    if (emitter.initializeState) copyRange(emitter.state, emitter.stateZeros, 0U, emitter.capacity);
    const auto births = [&](std::uint32_t slot, std::uint32_t count) {
        copyRange(emitter.spawns, emitter.spawnUpload, slot, count);
        if (bgfx::isValid(emitter.customState) && !emitter.initializeCustom)
            copyRange(emitter.customState, emitter.customZeros, slot, count);
        // Invalidate live and contact-death stamps even when a recycled birth has the same time.
        if (bgfx::isValid(emitter.state) && !emitter.initializeState)
            copyRange(emitter.state, emitter.stateZeros, slot, count);
    };
    if (emitter.initializeSpawns) {
        births(0U, emitter.capacity);
    } else if (emitter.pendingBirths != 0U) {
        const auto first = static_cast<std::uint32_t>((emitter.nextSlot - emitter.pendingBirths) % emitter.capacity);
        const auto count = std::min(emitter.pendingBirths, emitter.capacity - first);
        births(first, count);
        births(0U, emitter.pendingBirths - count);
    }
    emitter.initializeSpawns = false;
    emitter.initializeState = false;
    emitter.initializeCustom = false;
    emitter.pendingBirths = 0U;
}

void ParticleGpuEmitterSimulation::SetRenderOrigin(std::uint64_t sceneId, const kb::math::DVec3& renderOrigin) noexcept {
    SceneClock& clock = scenes_[sceneId];
    if (clock.renderOrigin == renderOrigin) return;
    const kb::math::Vec3 shift = kb::math::RelativeTo(clock.renderOrigin, renderOrigin);
    clock.renderOrigin = renderOrigin;
    for (auto& [key, emitter] : emitters_) {
        if (key.sceneId != sceneId) continue;
        emitter.world[12] += shift.x;
        emitter.world[13] += shift.y;
        emitter.world[14] += shift.z;
        bx::mtxInverse(emitter.worldInverse.data(), emitter.world.data());
        emitter.origin = { emitter.origin[0] + shift.x, emitter.origin[1] + shift.y, emitter.origin[2] + shift.z };
        emitter.beamStart = { emitter.beamStart[0] + shift.x, emitter.beamStart[1] + shift.y, emitter.beamStart[2] + shift.z };
        emitter.beamEnd = { emitter.beamEnd[0] + shift.x, emitter.beamEnd[1] + shift.y, emitter.beamEnd[2] + shift.z };
    }
}

void ParticleGpuEmitterSimulation::Rebase(bgfx::ViewId viewId, std::uint64_t sceneId, SceneClock& clock) noexcept {
    if (clock.recordOrigin == clock.renderOrigin) return;
    // The origin moves by whole grid steps, so the move is exact for records near it.
    const kb::math::Vec3 shift = kb::math::RelativeTo(clock.recordOrigin, clock.renderOrigin);
    clock.recordOrigin = clock.renderOrigin;
    for (auto& [key, emitter] : emitters_) {
        if (key.sceneId != sceneId) continue;
        emitter.fieldOrigin = emitter.fieldOrigin + shift;
        if (emitter.params.localSpace) continue;
        emitter.params.plane.distance += kb::math::Dot(emitter.params.plane.normal, shift);
        if (!bgfx::isValid(rebaseProgram_)) {
            Clear(emitter);
            FlushUploads(viewId, emitter);
            continue;
        }
        const std::array<float, 4> rebase{ shift.x, shift.y, shift.z, static_cast<float>(emitter.capacity) };
        for (const bgfx::DynamicVertexBufferHandle records : { emitter.spawns, emitter.state }) {
            if (!bgfx::isValid(records)) continue;
            bgfx::setBuffer(0U, records, bgfx::Access::ReadWrite);
            bgfx::setUniform(rebaseUniform_, rebase.data());
            bgfx::dispatch(viewId, rebaseProgram_, (emitter.capacity + kThreadGroupSize - 1U) / kThreadGroupSize, 1U, 1U);
        }
    }
}

void ParticleGpuEmitterSimulation::Apply(
    std::uint64_t sceneId, std::span<const kb::particles::ParticleGpuEmitterCommand> commands, kb::assets::AssetManager* assets) noexcept {
    if (!IsReady()) return;
    SceneClock& clock = scenes_[sceneId];
    const auto clearRejection = [&](const kb::particles::ParticleGpuEmitterKey& key) {
        std::erase_if(clock.rejections, [&](const RejectedUpdate& rejection) { return rejection.key == key; });
    };
    const auto reject = [&](const kb::particles::ParticleGpuEmitterKey& key, ParticleGpuUpdateFailure failure) {
        const auto found = std::find_if(clock.rejections.begin(), clock.rejections.end(),
            [&](const RejectedUpdate& rejection) { return rejection.key == key; });
        if (found != clock.rejections.end()) found->failure = failure;
        else if (clock.rejections.size() < kb::scene::kParticleEffectMaxInstancesPerScene * kb::scene::kParticleEffectMaxEmitters)
            clock.rejections.push_back({key, failure});
        else clock.rejections.back() = {{}, ParticleGpuUpdateFailure::DiagnosticCapacity};
    };
    const auto invalidateDraw = [&](const kb::particles::ParticleGpuEmitterKey& key) {
        // Mesh collection can inspect the previous dispatch before this frame's dispatch.
        // Retire references before releasing their parameters or replacing their GPU buffers.
        std::erase_if(clock.draws, [&](const Draw& draw) { return draw.key == key; });
    };
    for (const kb::particles::ParticleGpuEmitterCommand& command : commands) {
        const Key key{ sceneId, command.key.instanceId, command.key.emitterId };
        clock.latest = std::max(clock.latest, command.simTime);
        // From the command's simulation space to render space (zero while the simulation follows the render origin),
        // and to the space of the GPU records, which the next Dispatch moves to the render origin.
        const kb::math::Vec3 offset = kb::math::RelativeTo(command.simulationOrigin, clock.renderOrigin);
        const kb::math::Vec3 recordOffset = kb::math::RelativeTo(command.simulationOrigin, clock.recordOrigin);
        auto found = emitters_.find(key);
        if (command.release) {
            clearRejection(command.key);
            invalidateDraw(command.key);
            if (found != emitters_.end()) {
                allocatedBytes_ -= found->second.bytes;
                Destroy(found->second);
                emitters_.erase(found);
            }
            continue;
        }
        if (command.hasParams) {
            kb::particles::ParticleGpuEmitterParams params = command.params;
            if (params.motionModuleCount > params.motionModules.size() || params.flipbookColumns == 0U ||
                params.flipbookRows == 0U || params.flipbookColumns > 256U || params.flipbookRows > 256U ||
                params.flipbookFrameCount == 0U || !std::isfinite(params.flipbookFramesPerSecond) ||
                params.flipbookFramesPerSecond < 0.0F ||
                static_cast<std::uint64_t>(params.flipbookStartFrame) + params.flipbookFrameCount >
                    static_cast<std::uint64_t>(params.flipbookColumns) * params.flipbookRows) {
                reject(command.key, ParticleGpuUpdateFailure::InvalidParameters);
                continue;
            }
            if (params.output == kb::particles::ParticleRenderOutput::Beam &&
                (params.beamSegments == 0U || params.beamSegments > kb::scene::kParticleEffectMaxStripSegmentsPerEmitter ||
                 !std::isfinite(params.beamWidth) || params.beamWidth <= 0.0F || !std::isfinite(params.beamIntensity) ||
                 !std::isfinite(params.beamNoiseAmplitude) || !std::isfinite(params.beamNoiseFrequency))) {
                reject(command.key, ParticleGpuUpdateFailure::InvalidParameters);
                continue;
            }
            if (params.output == kb::particles::ParticleRenderOutput::Ribbon && (!std::isfinite(params.ribbonWidth) || params.ribbonWidth <= 0.0F)) {
                reject(command.key, ParticleGpuUpdateFailure::InvalidParameters);
                continue;
            }
            if (params.customProcessAssetId != 0U && (params.output != kb::particles::ParticleRenderOutput::Billboard || !bgfx::isValid(collideProgram_))) {
                reject(command.key, ParticleGpuUpdateFailure::CustomProcess); continue;
            }
            params.plane.distance += kb::math::Dot(params.plane.normal, recordOffset);
            const std::uint32_t capacity = std::clamp(params.capacity, 1U, kb::particles::kParticleGpuMaxCapacity);
            if (found == emitters_.end() || found->second.capacity != capacity || found->second.perSlot != InstancesPerSlot(params) ||
                found->second.stripSegments != StripSegments(params, capacity)) {
                Emitter replacement{};
                if (!Create(replacement, params)) {
                    reject(command.key, ParticleGpuUpdateFailure::ResourceCreation);
                    continue;
                }
                if (!EnsureCustomProcess(replacement, params.customProcessAssetId, assets)) {
                    allocatedBytes_ -= replacement.bytes; Destroy(replacement);
                    reject(command.key, ParticleGpuUpdateFailure::CustomProcess); continue;
                }
                invalidateDraw(command.key);
                if (found == emitters_.end()) found = emitters_.emplace(key, std::move(replacement)).first;
                else {
                    allocatedBytes_ -= found->second.bytes;
                    Destroy(found->second);
                    found->second = std::move(replacement);
                }
            } else {
                const bool hadState = bgfx::isValid(found->second.state);
                if (params.NeedsState() && !EnsureState(found->second)) {
                    reject(command.key, ParticleGpuUpdateFailure::StateAllocation);
                    continue;
                }
                if (!EnsureCustomProcess(found->second, params.customProcessAssetId, assets)) {
                    if (!hadState && bgfx::isValid(found->second.state)) {
                        bgfx::destroy(found->second.state); bgfx::destroy(found->second.stateZeros);
                        found->second.state = BGFX_INVALID_HANDLE; found->second.stateZeros = BGFX_INVALID_HANDLE;
                        found->second.initializeState = false;
                        const auto bytes = static_cast<std::uint64_t>(found->second.capacity) * kStateBytes * 2U;
                        allocatedBytes_ -= bytes; found->second.bytes -= bytes;
                    }
                    reject(command.key, ParticleGpuUpdateFailure::CustomProcess); continue;
                }
                found->second.params = params;
                found->second.params.capacity = capacity;
                if (NeedsSort(params, false)) static_cast<void>(EnsureSort(found->second));
            }
            clearRejection(command.key);
        }
        if (found == emitters_.end()) continue;
        if (!command.hasParams && found->second.params.customProcessAssetId != 0U) {
            if (!EnsureCustomProcess(found->second, found->second.params.customProcessAssetId, assets))
                reject(command.key, ParticleGpuUpdateFailure::CustomProcess);
        }
        if (command.hasWorldMatrix) {
            found->second.world = command.worldMatrix;
            found->second.world[12] += offset.x;
            found->second.world[13] += offset.y;
            found->second.world[14] += offset.z;
            bx::mtxInverse(found->second.worldInverse.data(), found->second.world.data());
        }
        if (command.hasOrientation) {
            found->second.basis = BasisColumns(command.orientation);
            found->second.origin = { command.origin[0] + offset.x, command.origin[1] + offset.y, command.origin[2] + offset.z };
        }
        if (command.hasField) {
            found->second.fieldOrigin = command.fieldOrigin + recordOffset;
        }
        if (command.hasBeam) {
            found->second.beamStart = { command.beamStart.x + offset.x, command.beamStart.y + offset.y, command.beamStart.z + offset.z };
            found->second.beamEnd = { command.beamEnd.x + offset.x, command.beamEnd.y + offset.y, command.beamEnd.z + offset.z };
        }
        if (command.clear) Clear(found->second);
        if (found->second.params.localSpace || (recordOffset.x == 0.0F && recordOffset.y == 0.0F && recordOffset.z == 0.0F)) {
            // Local-space records are in the owner's frame, which the world matrix places.
            Upload(found->second, command.spawns);
        } else {
            spawnScratch_.assign(command.spawns.begin(), command.spawns.end());
            for (kb::particles::ParticleGpuSpawn& spawn : spawnScratch_) spawn.position = spawn.position + recordOffset;
            Upload(found->second, spawnScratch_);
        }
    }
    clock.now = std::max(clock.now, clock.latest - kFixedStepSeconds);
}

void ParticleGpuEmitterSimulation::Advance(std::uint64_t sceneId, float frameDeltaSeconds) noexcept {
    const auto found = scenes_.find(sceneId);
    if (found == scenes_.end()) return;
    SceneClock& clock = found->second;
    clock.now += std::max(frameDeltaSeconds, 0.0F);
    clock.now = std::clamp(clock.now, clock.latest - kFixedStepSeconds, clock.latest + kFixedStepSeconds);
}

void ParticleGpuEmitterSimulation::Dispatch(bgfx::ViewId viewId, std::uint64_t sceneId, const FrameContext& collision) noexcept {
    const auto sceneIt = scenes_.find(sceneId);
    if (!IsReady() || sceneIt == scenes_.end()) return;
    SceneClock& clock = sceneIt->second;
    clock.draws.clear();
    // Buffer blits execute before compute in this view, including the rebase kernels.
    for (auto& [key, emitter] : emitters_)
        if (key.sceneId == sceneId) FlushUploads(viewId, emitter);
    Rebase(viewId, sceneId, clock);
    // The colliding emitters append to one event buffer, which starts every frame with a cleared counter.
    clock.eventEmitters.clear();
    bool recordsReady = false;
    if (bgfx::isValid(collideProgram_)) {
        for (const auto& [key, emitter] : emitters_) {
            if (key.sceneId != sceneId || !bgfx::isValid(emitter.state)) continue;
            recordsReady = EnsureCollisionEvents(clock);
            break;
        }
    }
    if (recordsReady) {
        bgfx::blit(viewId, bgfx::BufferRegion{.handle = clock.eventRecords},
            bgfx::BufferRegion{.handle = clock.eventZeros, .size = kEventHeaderWords * static_cast<std::uint32_t>(sizeof(std::uint32_t))});
    }
    for (auto& [key, emitter] : emitters_) {
        if (key.sceneId != sceneId) continue;
        const kb::particles::ParticleGpuEmitterParams& params = emitter.params;
        const std::array<float, 4> motion{ params.acceleration.x, params.acceleration.y, params.acceleration.z, params.drag };
        const std::array<float, 4> time{
            static_cast<float>(clock.now), static_cast<float>(emitter.capacity),
            params.stretchVelocityScale, params.stretchMinimumLength };
        std::array<float, 4U * kb::particles::kParticleGpuCurveSamples> colors{};
        // The mesh pipeline expects the material's base colour already multiplied into the instance colour.
        std::array<float, 4> tint{ 1.0F, 1.0F, 1.0F, 1.0F };
        bool translucentMaterial = false;
        if (params.output == kb::particles::ParticleRenderOutput::Mesh && collision.resources != nullptr && collision.resourceMap != nullptr) {
            if (const RenderMaterialResource* material = collision.resources->FindMaterial(collision.resourceMap->ResolveMaterial(params.materialAssetId))) {
                tint = { material->baseColor[0], material->baseColor[1], material->baseColor[2], material->baseColor[3] };
                translucentMaterial = material->alphaMode == RenderMaterialAlphaMode::Blend;
            }
        }
        const bool sortNow = NeedsSort(params, translucentMaterial) && EnsureSort(emitter);
        for (std::size_t sample = 0U; sample < kb::particles::kParticleGpuCurveSamples; ++sample) {
            for (std::size_t channel = 0U; channel < 4U; ++channel) {
                colors[sample * 4U + channel] = params.color[sample][channel] * tint[channel];
            }
        }
        bgfx::setBuffer(0U, emitter.spawns, bgfx::Access::Read);
        bgfx::setBuffer(1U, emitter.instances, bgfx::Access::Write);
        // Keep integrating an allocated state after a live edit removes the last force or plane.
        // In particular, contact deaths must never revive through the closed-form path.
        const bool colliding = bgfx::isValid(emitter.state) && bgfx::isValid(collideProgram_);
        if (colliding) {
            std::array<float, 8U * kb::particles::kParticleGpuMaxMotionModules> forces{};
            const auto count = std::min(params.motionModuleCount, static_cast<std::uint32_t>(params.motionModules.size()));
            for (std::size_t index = 0U; index < count; ++index) {
                const auto& module = params.motionModules[index];
                const std::size_t base = index * 8U;
                forces[base] = static_cast<float>(module.type);
                forces[base + 1U] = module.strength;
                forces[base + 2U] = module.parameter;
                // Two exactly representable halves preserve every uint32 seed across all shader languages.
                forces[base + 3U] = static_cast<float>(module.seed & 0xFFFFU);
                forces[base + 4U] = module.vector.x;
                forces[base + 5U] = module.vector.y;
                forces[base + 6U] = module.vector.z;
                forces[base + 7U] = static_cast<float>(module.seed >> 16U);
            }
            const std::array<float, 4> field{emitter.fieldOrigin.x, emitter.fieldOrigin.y, emitter.fieldOrigin.z, static_cast<float>(count)};
            bgfx::setUniform(fieldUniform_, field.data());
            // Noise lies in the world: the records (world space) or the owner's matrix (local space) are relative
            // to an origin the kernel adds back.
            const kb::math::DVec3& noiseOrigin = params.localSpace ? clock.renderOrigin : clock.recordOrigin;
            const std::array<float, 4> noiseOriginData{
                static_cast<float>(noiseOrigin.x), static_cast<float>(noiseOrigin.y), static_cast<float>(noiseOrigin.z), 0.0F };
            bgfx::setUniform(forcesUniform_, forces.data(), static_cast<std::uint16_t>(2U * kb::particles::kParticleGpuMaxMotionModules));
            bgfx::setUniform(noiseOriginUniform_, noiseOriginData.data());
            const kb::particles::ParticleGpuCollisionPlane& plane = params.plane;
            const std::array<float, 8> planeData{
                plane.normal.x, plane.normal.y, plane.normal.z, plane.distance,
                plane.restitution, plane.friction, plane.killOnContact ? 1.0F : 0.0F, 0.0F };
            const bool depth = params.sceneDepthCollision && bgfx::isValid(collision.depthTexture);
            const std::array<float, 4> collisionParams{ params.hasPlane ? 1.0F : 0.0F, depth ? 1.0F : 0.0F, kDepthCollisionThickness, 0.0F };
            const std::array<float, 4> bounce{
                params.hasPlane ? plane.restitution : kDefaultDepthRestitution, params.hasPlane ? plane.friction : 0.0F, 0.0F, 0.0F };
            const std::array<float, 4> texel{ collision.texelSize[0], collision.texelSize[1], 0.0F, 0.0F };
            const std::array<float, 4> depthParams{ collision.homogeneousDepth ? 1.0F : 0.0F, 0.0F, 0.0F, 0.0F };
            bgfx::setBuffer(2U, emitter.state, bgfx::Access::ReadWrite);
            const bool report = recordsReady && params.collisionEvents && params.hasPlane;
            const std::array<float, 4> eventParams{ report ? static_cast<float>(clock.eventEmitters.size()) : 0.0F,
                static_cast<float>(collisionEventCapacity_), report ? 1.0F : 0.0F, 0.0F };
            if (report) clock.eventEmitters.push_back({ key.instanceId, key.emitterId });
            if (recordsReady) bgfx::setBuffer(4U, clock.eventRecords, bgfx::Access::ReadWrite);
            bgfx::setUniform(eventUniform_, eventParams.data());
            bgfx::setTexture(3U, depthSampler_, depth ? collision.depthTexture : fallbackDepth_);
            bgfx::setUniform(planeUniform_, planeData.data(), 2U);
            bgfx::setUniform(collisionUniform_, collisionParams.data());
            bgfx::setUniform(depthBounceUniform_, bounce.data());
            bgfx::setUniform(texelUniform_, texel.data());
            bgfx::setUniform(viewProjectionUniform_, collision.viewProjection.data());
            bgfx::setUniform(inverseViewProjectionUniform_, collision.inverseViewProjection.data());
            bgfx::setUniform(depthParamsUniform_, depthParams.data());
            bgfx::setUniform(cameraPositionUniform_, collision.cameraPosition.data());
        }
        const std::array<float, 4> local{ params.localSpace ? 1.0F : 0.0F, 0.0F, 0.0F, 0.0F };
        bgfx::setUniform(worldUniform_, emitter.world.data());
        bgfx::setUniform(worldInverseUniform_, emitter.worldInverse.data());
        bgfx::setUniform(localUniform_, local.data());
        bgfx::setUniform(motionUniform_, motion.data());
        bgfx::setUniform(timeUniform_, time.data());
        bgfx::setUniform(colorUniform_, colors.data(), static_cast<std::uint16_t>(kb::particles::kParticleGpuCurveSamples));
        // A ribbon has the width of its output: the size curve must not hide a slot (the strip kernel reads a live slot
        // as one with a size).
        std::array<float, kb::particles::kParticleGpuCurveSamples> sizes = params.size;
        if (params.output == kb::particles::ParticleRenderOutput::Ribbon) sizes.fill(1.0F);
        bgfx::setUniform(sizeUniform_, sizes.data(), 2U);
        const float outputMode = params.output == kb::particles::ParticleRenderOutput::Mesh ? 1.0F
            : params.output == kb::particles::ParticleRenderOutput::Trail ? 2.0F : 0.0F;
        const bool followsVelocity = params.output == kb::particles::ParticleRenderOutput::Mesh &&
            params.alignment == kb::particles::ParticleRenderAlignment::Velocity;
        const std::array<float, 4> output{ outputMode, static_cast<float>(emitter.perSlot),
            followsVelocity ? 1.0F : params.trailSegmentSeconds, params.trailWidth };
        bgfx::setUniform(outputUniform_, output.data());
        const std::array<float, 4> flipbook{static_cast<float>(params.flipbookStartFrame),
            static_cast<float>(params.flipbookFrameCount), params.flipbookFramesPerSecond, params.flipbookLooping ? 1.0F : 0.0F};
        bgfx::setUniform(flipbookUniform_, flipbook.data());
        const std::array<float, 16> spin{ params.spinMin.x, params.spinMin.y, params.spinMin.z, 0.0F,
            params.spinMax.x, params.spinMax.y, params.spinMax.z, 0.0F,
            params.spinRateMin.x, params.spinRateMin.y, params.spinRateMin.z, 0.0F,
            params.spinRateMax.x, params.spinRateMax.y, params.spinRateMax.z, 0.0F };
        bgfx::setUniform(spinUniform_, spin.data(), 4U);
        bgfx::setUniform(basisUniform_, emitter.basis.data(), 3U);
        const std::uint32_t threads = emitter.capacity * emitter.perSlot;
        // A beam has no particles to draw: its strip comes from its ends alone.
        if (params.output != kb::particles::ParticleRenderOutput::Beam)
            bgfx::dispatch(viewId, colliding ? collideProgram_ : program_, (threads + kThreadGroupSize - 1U) / kThreadGroupSize, 1U, 1U);
        if (bgfx::isValid(emitter.customProgram)) {
            const std::array<float, 4> processFrame{static_cast<float>(clock.now), static_cast<float>(emitter.capacity), params.localSpace ? 1.0F : 0.0F, 0.0F};
            bgfx::setUniform(processFrameUniform_, processFrame.data());
            bgfx::setUniform(processInverseUniform_, emitter.worldInverse.data());
            bgfx::setBuffer(0U, emitter.spawns, bgfx::Access::Read);
            bgfx::setBuffer(1U, emitter.instances, bgfx::Access::ReadWrite);
            bgfx::setBuffer(2U, emitter.state, bgfx::Access::ReadWrite);
            bgfx::setBuffer(3U, emitter.customState, bgfx::Access::ReadWrite);
            bgfx::dispatch(viewId, emitter.customProgram, (emitter.capacity + kThreadGroupSize - 1U) / kThreadGroupSize, 1U, 1U);
        }
        bgfx::DynamicVertexBufferHandle drawn = emitter.instances;
        if (sortNow && bgfx::isValid(emitter.sortedInstances)) {
            SortInstances(viewId, emitter, collision, static_cast<float>(clock.now));
            drawn = emitter.sortedInstances;
        }
        if (emitter.stripSegments != 0U) BuildStrip(viewId, emitter, collision, static_cast<float>(clock.now));
        clock.draws.push_back(Draw{ &emitter.params, drawn, emitter.capacity * emitter.perSlot, emitter.origin,
            {key.instanceId, key.emitterId}, emitter.bytes, emitter.stripVertices, emitter.stripIndices, emitter.stripSegments });
    }
    if (!clock.eventEmitters.empty()) ReadBackCollisionEvents(clock);
}

bool ParticleGpuEmitterSimulation::EnsureCollisionEvents(SceneClock& clock) noexcept {
    if (bgfx::isValid(clock.eventRecords) && bgfx::isValid(clock.eventZeros)) return true;
    constexpr std::array<std::uint32_t, kEventHeaderWords> cleared{};
    const bgfx::Memory* zeros = CopyMemory(cleared.data(), static_cast<std::uint32_t>(sizeof(cleared)));
    if (zeros == nullptr) return false;
    clock.eventRecords = bgfx::createDynamicIndexBuffer(
        kEventHeaderWords + collisionEventCapacity_ * kEventRecordWords, BGFX_BUFFER_INDEX32 | BGFX_BUFFER_COMPUTE_READ_WRITE);
    clock.eventZeros = bgfx::createIndexBuffer(zeros, BGFX_BUFFER_INDEX32 | BGFX_BUFFER_COMPUTE_READ);
    if (bgfx::isValid(clock.eventRecords) && bgfx::isValid(clock.eventZeros)) return true;
    DestroyCollisionEvents(clock);
    return false;
}

void ParticleGpuEmitterSimulation::ReadBackCollisionEvents(SceneClock& clock) noexcept {
    // bgfx fills the CPU storage when its frame completes, so a copy in flight keeps its slot until it is collected.
    EventReadback* free = nullptr;
    for (std::shared_ptr<EventReadback>& slot : clock.eventReads) {
        if (!slot) slot = std::make_shared<EventReadback>();
        if (!slot->pending) { free = slot.get(); break; }
    }
    if (free == nullptr) {
        ++clock.eventStats.skippedReads;
        return;
    }
    const std::uint32_t words = kEventHeaderWords + collisionEventCapacity_ * kEventRecordWords;
    free->words.assign(words, 0U);
    free->emitters = clock.eventEmitters;
    free->origin = clock.renderOrigin;
    free->readyFrame = bgfx::read(bgfx::BufferRegion{.handle = clock.eventRecords,
        .size = words * static_cast<std::uint32_t>(sizeof(std::uint32_t))}, free->words.data());
    free->pending = true;
    ++clock.eventStats.submittedReads;
}

void ParticleGpuEmitterSimulation::DestroyCollisionEvents(SceneClock& clock) noexcept {
    for (std::shared_ptr<EventReadback>& slot : clock.eventReads) {
        if (slot && slot->pending) retiredReads_.push_back(std::move(slot));
        slot.reset();
    }
    if (bgfx::isValid(clock.eventRecords)) bgfx::destroy(clock.eventRecords);
    if (bgfx::isValid(clock.eventZeros)) bgfx::destroy(clock.eventZeros);
    clock.eventRecords = BGFX_INVALID_HANDLE;
    clock.eventZeros = BGFX_INVALID_HANDLE;
}

void ParticleGpuEmitterSimulation::CollectCollisionEvents(
    std::uint64_t sceneId, std::uint32_t completedFrame, kb::particles::ParticleGpuCollisionBatch& output) noexcept {
    std::erase_if(retiredReads_, [&](const std::shared_ptr<EventReadback>& read) { return read->readyFrame <= completedFrame; });
    const auto found = scenes_.find(sceneId);
    if (found == scenes_.end()) return;
    SceneClock& clock = found->second;
    std::array<EventReadback*, kEventReadbackRing> ready{};
    std::size_t count = 0U;
    for (const std::shared_ptr<EventReadback>& slot : clock.eventReads) {
        if (slot && slot->pending && slot->readyFrame <= completedFrame) ready[count++] = slot.get();
    }
    std::sort(ready.begin(), ready.begin() + static_cast<std::ptrdiff_t>(count),
        [](const EventReadback* lhs, const EventReadback* rhs) { return lhs->readyFrame < rhs->readyFrame; });
    output.origin = clock.renderOrigin;
    for (std::size_t index = 0U; index < count; ++index) {
        EventReadback& read = *ready[index];
        read.pending = false;
        const std::uint32_t recorded = read.words[0];
        const std::uint32_t stored = std::min(recorded, collisionEventCapacity_);
        const kb::math::Vec3 shift = kb::math::RelativeTo(read.origin, clock.renderOrigin);
        std::uint32_t appended = 0U;
        for (std::uint32_t record = 0U; record < stored; ++record) {
            const std::uint32_t* words = read.words.data() + kEventHeaderWords + record * kEventRecordWords;
            if (words[9] >= read.emitters.size()) continue;
            const auto at = [&](std::size_t lane) { return std::bit_cast<float>(words[lane]); };
            output.events.push_back({ read.emitters[words[9]], kb::math::Vec3{ at(0), at(1), at(2) } + shift,
                { at(3), at(4), at(5) }, { at(6), at(7), at(8) } });
            ++appended;
        }
        clock.eventStats.recorded += recorded;
        clock.eventStats.delivered += appended;
        clock.eventStats.lost += recorded - appended;
        output.lost += recorded - appended;
    }
}

ParticleGpuEmitterSimulation::CollisionEventStats ParticleGpuEmitterSimulation::CollisionStats(std::uint64_t sceneId) const noexcept {
    const auto found = scenes_.find(sceneId);
    if (found == scenes_.end()) return {};
    CollisionEventStats stats = found->second.eventStats;
    for (const std::shared_ptr<EventReadback>& slot : found->second.eventReads) stats.pendingReads += slot && slot->pending ? 1U : 0U;
    return stats;
}

std::span<const ParticleGpuEmitterSimulation::Draw> ParticleGpuEmitterSimulation::Draws(std::uint64_t sceneId) const noexcept {
    const auto found = scenes_.find(sceneId);
    return found == scenes_.end() ? std::span<const Draw>{} : std::span<const Draw>{ found->second.draws };
}

std::span<const ParticleGpuEmitterSimulation::RejectedUpdate> ParticleGpuEmitterSimulation::RejectedUpdates(std::uint64_t sceneId) const noexcept {
    const auto found = scenes_.find(sceneId);
    return found == scenes_.end() ? std::span<const RejectedUpdate>{} : std::span<const RejectedUpdate>{found->second.rejections};
}

bool ParticleGpuEmitterSimulation::HasEmitters(std::uint64_t sceneId) const noexcept {
    for (const auto& [key, emitter] : emitters_) {
        static_cast<void>(emitter);
        if (key.sceneId == sceneId) return true;
    }
    return false;
}

void ParticleGpuEmitterSimulation::ReleaseScene(std::uint64_t sceneId) noexcept {
    if (const auto scene = scenes_.find(sceneId); scene != scenes_.end()) DestroyCollisionEvents(scene->second);
    for (auto it = emitters_.begin(); it != emitters_.end();) {
        if (it->first.sceneId == sceneId) {
            allocatedBytes_ -= it->second.bytes;
            Destroy(it->second);
            it = emitters_.erase(it);
        } else {
            ++it;
        }
    }
    scenes_.erase(sceneId);
}

void ParticleGpuEmitterSimulation::ReleaseAllScenes() noexcept {
    for (auto& [key, emitter] : emitters_) {
        static_cast<void>(key);
        Destroy(emitter);
    }
    for (auto& [sceneId, clock] : scenes_) {
        static_cast<void>(sceneId);
        DestroyCollisionEvents(clock);
    }
    emitters_.clear();
    scenes_.clear();
    allocatedBytes_ = 0U;
}

} // namespace kb::render
