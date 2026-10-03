#include "kb/render/particles/ParticleGpuEmitterSimulation.hpp"

#include "engine/scene/ParticleEffectAssetSchema.hpp"
#include "kb/render/ShaderLoader.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace kb::render {
namespace {

constexpr std::uint32_t kThreadGroupSize = 64U;
constexpr std::uint32_t kInstanceBytes = 80U;
constexpr std::uint32_t kStateBytes = 32U;
constexpr std::uint32_t kSortKeyBytes = 16U;
// How far behind a visible surface (m) a particle still counts as having hit it.
constexpr float kDepthCollisionThickness = 0.5F;
constexpr float kDefaultDepthRestitution = 0.5F;
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

// Alpha and premultiplied blending depend on draw order; the other modes are commutative. Billboards follow the
// emitter's blend mode, mesh instances their material's; trail segments follow each other in the buffer.
bool ParticleGpuEmitterSimulation::NeedsSort(const kb::particles::ParticleGpuEmitterParams& params, bool translucentMaterial) noexcept {
    if (params.output == kb::particles::ParticleRenderOutput::Mesh) return translucentMaterial;
    const bool billboard = params.output == kb::particles::ParticleRenderOutput::Billboard ||
        params.output == kb::particles::ParticleRenderOutput::StretchedBillboard;
    return billboard && (params.blend == kb::particles::ParticleRenderBlendMode::Alpha ||
        params.blend == kb::particles::ParticleRenderBlendMode::Premultiplied);
}

bool ParticleGpuEmitterSimulation::Initialize() {
    if (IsReady()) return true;
    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps == nullptr || (caps->supported & BGFX_CAPS_COMPUTE) == 0U) return false;
    program_ = ShaderLoader::LoadComputeProgram("cs_particle_gpu_emit.sc");
    motionUniform_ = bgfx::createUniform("u_gpuParticleMotion", bgfx::UniformType::Vec4);
    timeUniform_ = bgfx::createUniform("u_gpuParticleTime", bgfx::UniformType::Vec4);
    colorUniform_ = bgfx::createUniform("u_gpuParticleColor", bgfx::UniformType::Vec4,
        static_cast<std::uint16_t>(kb::particles::kParticleGpuCurveSamples));
    sizeUniform_ = bgfx::createUniform("u_gpuParticleSize", bgfx::UniformType::Vec4, 2U);
    outputUniform_ = bgfx::createUniform("u_gpuParticleOutput", bgfx::UniformType::Vec4);
    spinUniform_ = bgfx::createUniform("u_gpuParticleSpin", bgfx::UniformType::Vec4, 4U);
    basisUniform_ = bgfx::createUniform("u_gpuParticleBasis", bgfx::UniformType::Vec4, 3U);
    collideProgram_ = ShaderLoader::LoadComputeProgram("cs_particle_gpu_collide.sc");
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
    sortKeysProgram_ = ShaderLoader::LoadComputeProgram("cs_particle_gpu_sort_keys.sc");
    sortStepProgram_ = ShaderLoader::LoadComputeProgram("cs_particle_gpu_sort_step.sc");
    sortGatherProgram_ = ShaderLoader::LoadComputeProgram("cs_particle_gpu_sort_gather.sc");
    sortCameraUniform_ = bgfx::createUniform("u_gpuSortCamera", bgfx::UniformType::Vec4);
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
        bgfx::isValid(colorUniform_) && bgfx::isValid(sizeUniform_);
}

void ParticleGpuEmitterSimulation::Shutdown() noexcept {
    ReleaseAllScenes();
    for (bgfx::UniformHandle* handle : { &motionUniform_, &timeUniform_, &colorUniform_, &sizeUniform_, &worldUniform_, &worldInverseUniform_, &localUniform_, &planeUniform_,
             &collisionUniform_, &depthBounceUniform_, &texelUniform_, &viewProjectionUniform_, &depthSampler_,
             &inverseViewProjectionUniform_, &depthParamsUniform_, &cameraPositionUniform_, &sortCameraUniform_,
             &sortParamsUniform_, &outputUniform_, &spinUniform_, &basisUniform_ }) {
        if (bgfx::isValid(*handle)) bgfx::destroy(*handle);
        *handle = BGFX_INVALID_HANDLE;
    }
    for (bgfx::ProgramHandle* handle : { &sortKeysProgram_, &sortStepProgram_, &sortGatherProgram_ }) {
        if (bgfx::isValid(*handle)) bgfx::destroy(*handle);
        *handle = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(fallbackDepth_)) bgfx::destroy(fallbackDepth_);
    fallbackDepth_ = BGFX_INVALID_HANDLE;
    if (bgfx::isValid(collideProgram_)) bgfx::destroy(collideProgram_);
    collideProgram_ = BGFX_INVALID_HANDLE;
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    program_ = BGFX_INVALID_HANDLE;
}

bool ParticleGpuEmitterSimulation::Create(const Key& key, const kb::particles::ParticleGpuEmitterParams& params) noexcept {
    const std::uint32_t capacity = std::clamp(params.capacity, 1U, kb::particles::kParticleGpuMaxCapacity);
    const std::uint32_t perSlot = InstancesPerSlot(params);
    if (static_cast<std::uint64_t>(capacity) * perSlot > kb::particles::kParticleGpuMaxTrailSegments &&
        perSlot > 1U) return false;
    const std::uint64_t bytes = static_cast<std::uint64_t>(capacity) *
        (sizeof(kb::particles::ParticleGpuSpawn) + static_cast<std::uint64_t>(kInstanceBytes) * perSlot);
    if (allocatedBytes_ + bytes > kb::scene::kParticleEffectMaxGpuResourceBytes) return false;
    Emitter emitter{};
    emitter.params = params;
    emitter.params.capacity = capacity;
    emitter.capacity = capacity;
    emitter.perSlot = perSlot;
    emitter.bytes = bytes;
    // Zeroed birth records have lifetime 0, so every slot starts dead.
    const std::uint32_t spawnBytes = capacity * static_cast<std::uint32_t>(sizeof(kb::particles::ParticleGpuSpawn));
    const bgfx::Memory* zero = bgfx::alloc(spawnBytes);
    if (zero == nullptr || zero->data == nullptr) return false;
    std::memset(zero->data, 0, spawnBytes);
    emitter.spawns = bgfx::createDynamicVertexBuffer(zero, SpawnLayout(), BGFX_BUFFER_COMPUTE_READ);
    emitter.instances = bgfx::createDynamicVertexBuffer(capacity * perSlot, InstanceLayout(), BGFX_BUFFER_COMPUTE_READ_WRITE);
    if (!bgfx::isValid(emitter.spawns) || !bgfx::isValid(emitter.instances)) {
        Destroy(emitter);
        return false;
    }
    allocatedBytes_ += bytes;
    if (params.HasCollision() && !EnsureState(emitter)) {
        allocatedBytes_ -= bytes;
        Destroy(emitter);
        return false;
    }
    // Sorting is an enhancement: without budget or kernels the emitter is simply drawn in ring order. (A mesh
    // emitter finds out whether its material is translucent at dispatch.)
    if (NeedsSort(params, false)) static_cast<void>(EnsureSort(emitter));
    emitters_[key] = emitter;
    return true;
}

bool ParticleGpuEmitterSimulation::EnsureSort(Emitter& emitter) noexcept {
    if (bgfx::isValid(emitter.sortedInstances)) return true;
    if (!bgfx::isValid(sortKeysProgram_) || !bgfx::isValid(sortStepProgram_) || !bgfx::isValid(sortGatherProgram_)) return false;
    const std::uint32_t padded = PaddedSortCount(emitter.capacity);
    const std::uint64_t sortBytes = static_cast<std::uint64_t>(padded) * kSortKeyBytes +
        static_cast<std::uint64_t>(emitter.capacity) * kInstanceBytes;
    if (allocatedBytes_ + sortBytes > kb::scene::kParticleEffectMaxGpuResourceBytes) return false;
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
    bgfx::ViewId viewId, const Emitter& emitter, const std::array<float, 4>& cameraPosition) noexcept {
    const float meshLayout = emitter.params.output == kb::particles::ParticleRenderOutput::Mesh ? 1.0F : 0.0F;
    const std::uint32_t padded = PaddedSortCount(emitter.capacity);
    const std::array<float, 4> camera{ cameraPosition[0], cameraPosition[1], cameraPosition[2], static_cast<float>(emitter.capacity) };
    const auto groups = [](std::uint32_t threads) { return (threads + kThreadGroupSize - 1U) / kThreadGroupSize; };
    bgfx::setBuffer(0U, emitter.instances, bgfx::Access::Read);
    bgfx::setBuffer(1U, emitter.sortKeys, bgfx::Access::Write);
    bgfx::setUniform(sortCameraUniform_, camera.data());
    const std::array<float, 4> keyParams{ static_cast<float>(padded), meshLayout, 0.0F, 0.0F };
    bgfx::setUniform(sortParamsUniform_, keyParams.data());
    bgfx::dispatch(viewId, sortKeysProgram_, groups(padded), 1U, 1U);
    // Bitonic sort, farthest first.
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

bool ParticleGpuEmitterSimulation::EnsureState(Emitter& emitter) noexcept {
    if (bgfx::isValid(emitter.state)) return true;
    const std::uint64_t stateBytes = static_cast<std::uint64_t>(emitter.capacity) * kStateBytes;
    if (allocatedBytes_ + stateBytes > kb::scene::kParticleEffectMaxGpuResourceBytes) return false;
    // A zeroed record never matches a birth stamp, so every particle starts from its birth record.
    const bgfx::Memory* zero = bgfx::alloc(static_cast<std::uint32_t>(stateBytes));
    if (zero == nullptr || zero->data == nullptr) return false;
    std::memset(zero->data, 0, static_cast<std::size_t>(stateBytes));
    emitter.state = bgfx::createDynamicVertexBuffer(zero, SpawnLayout(), BGFX_BUFFER_COMPUTE_READ_WRITE);
    if (!bgfx::isValid(emitter.state)) return false;
    emitter.bytes += stateBytes;
    allocatedBytes_ += stateBytes;
    return true;
}

void ParticleGpuEmitterSimulation::Destroy(Emitter& emitter) noexcept {
    if (bgfx::isValid(emitter.spawns)) bgfx::destroy(emitter.spawns);
    if (bgfx::isValid(emitter.instances)) bgfx::destroy(emitter.instances);
    if (bgfx::isValid(emitter.state)) bgfx::destroy(emitter.state);
    if (bgfx::isValid(emitter.sortKeys)) bgfx::destroy(emitter.sortKeys);
    if (bgfx::isValid(emitter.sortedInstances)) bgfx::destroy(emitter.sortedInstances);
    emitter.sortKeys = BGFX_INVALID_HANDLE;
    emitter.sortedInstances = BGFX_INVALID_HANDLE;
    emitter.spawns = BGFX_INVALID_HANDLE;
    emitter.instances = BGFX_INVALID_HANDLE;
    emitter.state = BGFX_INVALID_HANDLE;
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
        bgfx::update(emitter.spawns, slot, memory);
        emitter.nextSlot += chunk;
        uploaded += chunk;
    }
}

void ParticleGpuEmitterSimulation::Clear(Emitter& emitter) noexcept {
    const std::uint32_t bytes = emitter.capacity * static_cast<std::uint32_t>(sizeof(kb::particles::ParticleGpuSpawn));
    const bgfx::Memory* zero = bgfx::alloc(bytes);
    if (zero == nullptr || zero->data == nullptr) return;
    std::memset(zero->data, 0, bytes);
    bgfx::update(emitter.spawns, 0U, zero);
}

void ParticleGpuEmitterSimulation::Apply(
    std::uint64_t sceneId, std::span<const kb::particles::ParticleGpuEmitterCommand> commands) noexcept {
    if (!IsReady()) return;
    SceneClock& clock = scenes_[sceneId];
    for (const kb::particles::ParticleGpuEmitterCommand& command : commands) {
        const Key key{ sceneId, command.key.instanceId, command.key.emitterId };
        clock.latest = std::max(clock.latest, command.simTime);
        auto found = emitters_.find(key);
        if (command.release) {
            if (found != emitters_.end()) {
                allocatedBytes_ -= found->second.bytes;
                Destroy(found->second);
                emitters_.erase(found);
            }
            continue;
        }
        if (command.hasParams) {
            const std::uint32_t capacity = std::clamp(command.params.capacity, 1U, kb::particles::kParticleGpuMaxCapacity);
            if (found != emitters_.end() && (found->second.capacity != capacity || found->second.perSlot != InstancesPerSlot(command.params))) {
                allocatedBytes_ -= found->second.bytes;
                Destroy(found->second);
                emitters_.erase(found);
                found = emitters_.end();
            }
            if (found == emitters_.end()) {
                if (!Create(key, command.params)) continue;
                found = emitters_.find(key);
            } else {
                found->second.params = command.params;
                found->second.params.capacity = capacity;
                if (NeedsSort(command.params, false)) static_cast<void>(EnsureSort(found->second));
                if (command.params.HasCollision() && !EnsureState(found->second)) {
                    // No budget left for the state: the emitter keeps simulating without collisions.
                    found->second.params.hasPlane = false;
                    found->second.params.sceneDepthCollision = false;
                }
            }
        }
        if (found == emitters_.end()) continue;
        if (command.hasWorldMatrix) {
            found->second.world = command.worldMatrix;
            bx::mtxInverse(found->second.worldInverse.data(), command.worldMatrix.data());
        }
        if (command.hasOrientation) {
            found->second.basis = BasisColumns(command.orientation);
            found->second.origin = command.origin;
        }
        if (command.clear) Clear(found->second);
        Upload(found->second, command.spawns);
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
        const bool colliding = params.HasCollision() && bgfx::isValid(emitter.state) && bgfx::isValid(collideProgram_);
        if (colliding) {
            const kb::particles::ParticleGpuCollisionPlane& plane = params.plane;
            const std::array<float, 8> planeData{
                plane.normal.x, plane.normal.y, plane.normal.z, plane.distance, plane.restitution, plane.friction, 0.0F, 0.0F };
            const bool depth = params.sceneDepthCollision && bgfx::isValid(collision.depthTexture);
            const std::array<float, 4> collisionParams{ params.hasPlane ? 1.0F : 0.0F, depth ? 1.0F : 0.0F, kDepthCollisionThickness, 0.0F };
            const std::array<float, 4> bounce{
                params.hasPlane ? plane.restitution : kDefaultDepthRestitution, params.hasPlane ? plane.friction : 0.0F, 0.0F, 0.0F };
            const std::array<float, 4> texel{ collision.texelSize[0], collision.texelSize[1], 0.0F, 0.0F };
            const std::array<float, 4> depthParams{ collision.homogeneousDepth ? 1.0F : 0.0F, 0.0F, 0.0F, 0.0F };
            bgfx::setBuffer(2U, emitter.state, bgfx::Access::ReadWrite);
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
        bgfx::setUniform(sizeUniform_, params.size.data(), 2U);
        const float outputMode = params.output == kb::particles::ParticleRenderOutput::Mesh ? 1.0F
            : params.output == kb::particles::ParticleRenderOutput::Trail ? 2.0F : 0.0F;
        const std::array<float, 4> output{ outputMode, static_cast<float>(emitter.perSlot), params.trailSegmentSeconds, params.trailWidth };
        bgfx::setUniform(outputUniform_, output.data());
        const std::array<float, 16> spin{ params.spinMin.x, params.spinMin.y, params.spinMin.z, 0.0F,
            params.spinMax.x, params.spinMax.y, params.spinMax.z, 0.0F,
            params.spinRateMin.x, params.spinRateMin.y, params.spinRateMin.z, 0.0F,
            params.spinRateMax.x, params.spinRateMax.y, params.spinRateMax.z, 0.0F };
        bgfx::setUniform(spinUniform_, spin.data(), 4U);
        bgfx::setUniform(basisUniform_, emitter.basis.data(), 3U);
        const std::uint32_t threads = emitter.capacity * emitter.perSlot;
        bgfx::dispatch(viewId, colliding ? collideProgram_ : program_, (threads + kThreadGroupSize - 1U) / kThreadGroupSize, 1U, 1U);
        bgfx::DynamicVertexBufferHandle drawn = emitter.instances;
        if (sortNow && bgfx::isValid(emitter.sortedInstances)) {
            SortInstances(viewId, emitter, collision.cameraPosition);
            drawn = emitter.sortedInstances;
        }
        clock.draws.push_back(Draw{ &emitter.params, drawn, emitter.capacity * emitter.perSlot, emitter.origin });
    }
}

std::span<const ParticleGpuEmitterSimulation::Draw> ParticleGpuEmitterSimulation::Draws(std::uint64_t sceneId) noexcept {
    const auto found = scenes_.find(sceneId);
    return found == scenes_.end() ? std::span<const Draw>{} : std::span<const Draw>{ found->second.draws };
}

bool ParticleGpuEmitterSimulation::HasEmitters(std::uint64_t sceneId) const noexcept {
    for (const auto& [key, emitter] : emitters_) {
        static_cast<void>(emitter);
        if (key.sceneId == sceneId) return true;
    }
    return false;
}

void ParticleGpuEmitterSimulation::ReleaseScene(std::uint64_t sceneId) noexcept {
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
    emitters_.clear();
    scenes_.clear();
    allocatedBytes_ = 0U;
}

} // namespace kb::render
