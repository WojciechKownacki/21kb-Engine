#include "kb/render/particles/ParticleGpuEmitterSimulation.hpp"

#include "engine/scene/ParticleEffectAssetSchema.hpp"
#include "kb/render/ShaderLoader.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace kb::render {
namespace {

constexpr std::uint32_t kThreadGroupSize = 64U;
constexpr std::uint32_t kInstanceBytes = 80U;
constexpr std::uint32_t kStateBytes = 32U;
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

[[nodiscard]] const bgfx::Memory* CopyMemory(const void* source, std::uint32_t bytes) noexcept {
    const bgfx::Memory* memory = bgfx::alloc(bytes);
    if (memory == nullptr || memory->data == nullptr) return nullptr;
    std::memcpy(memory->data, source, bytes);
    return memory;
}

} // namespace

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
    collideProgram_ = ShaderLoader::LoadComputeProgram("cs_particle_gpu_collide.sc");
    planeUniform_ = bgfx::createUniform("u_gpuParticlePlane", bgfx::UniformType::Vec4, 2U);
    collisionUniform_ = bgfx::createUniform("u_gpuParticleCollision", bgfx::UniformType::Vec4);
    depthBounceUniform_ = bgfx::createUniform("u_gpuParticleDepthBounce", bgfx::UniformType::Vec4);
    texelUniform_ = bgfx::createUniform("u_gpuParticleTexel", bgfx::UniformType::Vec4);
    viewProjectionUniform_ = bgfx::createUniform("u_gpuParticleViewProj", bgfx::UniformType::Mat4);
    depthSampler_ = bgfx::createUniform("s_particleDepth", bgfx::UniformType::Sampler);
    inverseViewProjectionUniform_ = bgfx::createUniform("u_deferredInverseViewProjection", bgfx::UniformType::Mat4);
    depthParamsUniform_ = bgfx::createUniform("u_deferredDepthParams", bgfx::UniformType::Vec4);
    cameraPositionUniform_ = bgfx::createUniform("u_deferredCameraPosition", bgfx::UniformType::Vec4);
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
    for (bgfx::UniformHandle* handle : { &motionUniform_, &timeUniform_, &colorUniform_, &sizeUniform_, &planeUniform_,
             &collisionUniform_, &depthBounceUniform_, &texelUniform_, &viewProjectionUniform_, &depthSampler_,
             &inverseViewProjectionUniform_, &depthParamsUniform_, &cameraPositionUniform_ }) {
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
    const std::uint64_t bytes =
        static_cast<std::uint64_t>(capacity) * (sizeof(kb::particles::ParticleGpuSpawn) + kInstanceBytes);
    if (allocatedBytes_ + bytes > kb::scene::kParticleEffectMaxGpuResourceBytes) return false;
    Emitter emitter{};
    emitter.params = params;
    emitter.params.capacity = capacity;
    emitter.capacity = capacity;
    emitter.bytes = bytes;
    // Zeroed birth records have lifetime 0, so every slot starts dead.
    const std::uint32_t spawnBytes = capacity * static_cast<std::uint32_t>(sizeof(kb::particles::ParticleGpuSpawn));
    const bgfx::Memory* zero = bgfx::alloc(spawnBytes);
    if (zero == nullptr || zero->data == nullptr) return false;
    std::memset(zero->data, 0, spawnBytes);
    emitter.spawns = bgfx::createDynamicVertexBuffer(zero, SpawnLayout(), BGFX_BUFFER_COMPUTE_READ);
    emitter.instances = bgfx::createDynamicVertexBuffer(capacity, InstanceLayout(), BGFX_BUFFER_COMPUTE_READ_WRITE);
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
    emitters_[key] = emitter;
    return true;
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
            if (found != emitters_.end() && found->second.capacity != capacity) {
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
                if (command.params.HasCollision() && !EnsureState(found->second)) {
                    // No budget left for the state: the emitter keeps simulating without collisions.
                    found->second.params.hasPlane = false;
                    found->second.params.sceneDepthCollision = false;
                }
            }
        }
        if (found == emitters_.end()) continue;
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

void ParticleGpuEmitterSimulation::Dispatch(bgfx::ViewId viewId, std::uint64_t sceneId, const CollisionContext& collision) noexcept {
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
        for (std::size_t sample = 0U; sample < kb::particles::kParticleGpuCurveSamples; ++sample) {
            std::copy(params.color[sample].begin(), params.color[sample].end(), colors.begin() + sample * 4U);
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
        bgfx::setUniform(motionUniform_, motion.data());
        bgfx::setUniform(timeUniform_, time.data());
        bgfx::setUniform(colorUniform_, colors.data(), static_cast<std::uint16_t>(kb::particles::kParticleGpuCurveSamples));
        bgfx::setUniform(sizeUniform_, params.size.data(), 2U);
        bgfx::dispatch(viewId, colliding ? collideProgram_ : program_, (emitter.capacity + kThreadGroupSize - 1U) / kThreadGroupSize, 1U, 1U);
        clock.draws.push_back(Draw{ &emitter.params, emitter.instances, emitter.capacity });
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
