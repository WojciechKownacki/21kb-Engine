#include "engine/particles/ParticleCompiledEffectCache.hpp"

#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <vector>

namespace kb::particles {
namespace {

using namespace kb::scene::SceneAssetBinaryIO;
constexpr std::uint64_t kMagic = 0x3143464550424B32ULL;
constexpr std::size_t kHeaderBytes = 65U;

void WriteVec3(std::vector<std::uint8_t>& bytes, kb::math::Vec3 value) {
    WriteFloat(bytes, value.x); WriteFloat(bytes, value.y); WriteFloat(bytes, value.z);
}
void WriteQuat(std::vector<std::uint8_t>& bytes, kb::math::Quat value) {
    WriteFloat(bytes, value.x); WriteFloat(bytes, value.y); WriteFloat(bytes, value.z); WriteFloat(bytes, value.w);
}
void WriteColor(std::vector<std::uint8_t>& bytes, kb::math::Color value) {
    WriteFloat(bytes, value.r); WriteFloat(bytes, value.g); WriteFloat(bytes, value.b); WriteFloat(bytes, value.a);
}
void WriteCurveKey(std::vector<std::uint8_t>& bytes, const ParticleCompiledCurveKey& key) {
    WriteFloat(bytes, key.time); WriteFloat(bytes, key.value); WriteUInt8(bytes, static_cast<std::uint8_t>(key.easing));
}
void WriteCurve(std::vector<std::uint8_t>& bytes, const ParticleCompiledCurve& curve) {
    WriteUInt8(bytes, curve.keyCount);
    for (std::uint8_t index = 0U; index < curve.keyCount; ++index) WriteCurveKey(bytes, curve.keys[index]);
}
void WriteGradient(std::vector<std::uint8_t>& bytes, const ParticleCompiledGradient& gradient) {
    WriteUInt8(bytes, gradient.stopCount);
    for (std::uint8_t index = 0U; index < gradient.stopCount; ++index) {
        WriteFloat(bytes, gradient.stops[index].time); WriteColor(bytes, gradient.stops[index].color);
    }
}
void WriteInitialVelocity(std::vector<std::uint8_t>& bytes, const kb::scene::ParticleInitialVelocityModule& value) {
    WriteVec3(bytes, value.direction); WriteFloat(bytes, value.speedMin); WriteFloat(bytes, value.speedMax);
    WriteFloat(bytes, value.randomization); WriteFloat(bytes, value.spreadDegrees);
}

void WriteEffect(std::vector<std::uint8_t>& bytes, const ParticleCompiledEffect& effect) {
    WriteUInt64(bytes, effect.determinismSeed); WriteFloat(bytes, effect.durationSeconds); WriteBool(bytes, effect.looping);
    WriteUInt8(bytes, static_cast<std::uint8_t>(effect.backendPolicy));
    WriteUInt8(bytes, static_cast<std::uint8_t>(effect.gpuCatchupPolicy));
    WriteUInt8(bytes, effect.emitterCount); WriteUInt8(bytes, effect.eventBindingCount);
    for (std::uint8_t emitterIndex = 0U; emitterIndex < effect.emitterCount; ++emitterIndex) {
        const auto& e = effect.emitters[emitterIndex];
        WriteUInt64(bytes, e.emitterId); WriteUInt8(bytes, static_cast<std::uint8_t>(e.outputType));
        WriteUInt64(bytes, e.materialAssetId); WriteUInt64(bytes, e.meshAssetId); WriteUInt64(bytes, e.textureAtlasAssetId);
        WriteFloat(bytes, e.meshLodBias); WriteBool(bytes, e.meshCastsShadow); WriteBool(bytes, e.meshReceivesShadow);
        WriteUInt8(bytes, static_cast<std::uint8_t>(e.blendMode)); WriteUInt8(bytes, static_cast<std::uint8_t>(e.sortMode));
        WriteUInt8(bytes, static_cast<std::uint8_t>(e.alignment)); WriteBool(bytes, e.depthTest); WriteBool(bytes, e.depthWrite);
        WriteBool(bytes, e.softParticles); WriteBool(bytes, e.antiAliasing); WriteUInt32(bytes, e.flipbookColumns);
        WriteUInt32(bytes, e.flipbookRows); WriteUInt32(bytes, e.flipbookFrameCount); WriteFloat(bytes, e.flipbookFramesPerSecond);
        WriteBool(bytes, e.flipbookLooping); WriteFloat(bytes, e.stretchVelocityScale); WriteFloat(bytes, e.stretchMinimumLength);
        WriteFloat(bytes, e.pointSpriteDiameter);
        WriteFloat(bytes, e.trailSampleIntervalSeconds); WriteFloat(bytes, e.trailMinimumDistance);
        WriteUInt32(bytes, e.trailMaxSamplesPerParticle); WriteFloat(bytes, e.trailWidth);
        WriteUInt32(bytes, e.ribbonMaxSegments); WriteFloat(bytes, e.ribbonWidth); WriteBool(bytes, e.ribbonBreakOnDeath);
        WriteVec3(bytes, e.beamLocalEnd); WriteUInt32(bytes, e.beamSegments); WriteFloat(bytes, e.beamWidth);
        WriteFloat(bytes, e.beamNoiseAmplitude); WriteFloat(bytes, e.beamNoiseFrequency);
        WriteUInt8(bytes, static_cast<std::uint8_t>(e.simulationSpace));
        WriteBool(bytes, e.enabled); WriteUInt8(bytes, static_cast<std::uint8_t>(e.mode)); WriteUInt32(bytes, e.maxParticles);
        WriteVec3(bytes, e.localPosition); WriteQuat(bytes, e.localRotation); WriteInitialVelocity(bytes, e.initialVelocity);
        WriteFloat(bytes, e.lifetimeMin); WriteFloat(bytes, e.lifetimeMax); WriteFloat(bytes, e.prewarmSeconds);
        WriteUInt8(bytes, e.rateKeyCount); WriteUInt8(bytes, e.burstCount);
        for (std::uint8_t index = 0U; index < e.rateKeyCount; ++index) WriteCurveKey(bytes, e.rateKeys[index]);
        for (std::uint8_t index = 0U; index < e.burstCount; ++index) {
            WriteFloat(bytes, e.bursts[index].timeSeconds); WriteUInt32(bytes, e.bursts[index].count);
        }
        WriteGradient(bytes, e.colorOverLife); WriteCurve(bytes, e.sizeOverLife); WriteCurve(bytes, e.alphaOverLife);
        WriteUInt8(bytes, e.moduleCount);
        for (std::uint8_t index = 0U; index < e.moduleCount; ++index) {
            const auto& module = e.modules[index];
            WriteUInt64(bytes, module.moduleId); WriteUInt8(bytes, static_cast<std::uint8_t>(module.type));
            WriteBool(bytes, module.enabled);
            switch (module.type) {
            case kb::scene::ParticleModuleType::InitialVelocity:
                WriteInitialVelocity(bytes, std::get<kb::scene::ParticleInitialVelocityModule>(module.payload)); break;
            case kb::scene::ParticleModuleType::Gravity: {
                const auto& value = std::get<kb::scene::ParticleGravityModule>(module.payload);
                WriteVec3(bytes, value.acceleration); WriteFloat(bytes, value.sceneGravityScale); break;
            }
            case kb::scene::ParticleModuleType::Wind:
                WriteVec3(bytes, std::get<kb::scene::ParticleWindModule>(module.payload).acceleration); break;
            case kb::scene::ParticleModuleType::Drag:
                WriteFloat(bytes, std::get<kb::scene::ParticleDragModule>(module.payload).coefficient); break;
            case kb::scene::ParticleModuleType::CollisionPlane: {
                const auto& value = std::get<kb::scene::ParticleCollisionPlaneModule>(module.payload);
                WriteVec3(bytes, value.normal); WriteFloat(bytes, value.distance); WriteFloat(bytes, value.restitution);
                WriteFloat(bytes, value.friction); WriteUInt32(bytes, value.maxEventsPerStep); break;
            }
            case kb::scene::ParticleModuleType::SubEmitter: {
                const auto& value = std::get<kb::scene::ParticleSubEmitterModule>(module.payload);
                WriteUInt64(bytes, value.targetEmitterId); WriteUInt8(bytes, static_cast<std::uint8_t>(value.trigger));
                WriteUInt32(bytes, value.count); WriteUInt32(bytes, value.maxDepth); break;
            }
            case kb::scene::ParticleModuleType::ColorOverLife:
            case kb::scene::ParticleModuleType::SizeOverLife:
            case kb::scene::ParticleModuleType::AlphaOverLife: break;
            }
        }
    }
    for (std::uint8_t index = 0U; index < effect.eventBindingCount; ++index) {
        const auto& binding = effect.eventBindings[index];
        WriteUInt8(bytes, binding.sourceEmitterIndex); WriteUInt8(bytes, static_cast<std::uint8_t>(binding.trigger));
        WriteUInt64(bytes, binding.sourceModuleId); WriteUInt8(bytes, binding.targetEmitterIndex);
        WriteUInt32(bytes, binding.count); WriteUInt8(bytes, binding.maxDepth); WriteUInt32(bytes, binding.perStepBudget);
    }
}

class Reader {
  public:
    explicit Reader(std::vector<std::uint8_t> bytes) : reader_(std::move(bytes)) {}
    bool U8(std::uint8_t& value) { return reader_.ReadUInt8(value); }
    bool U32(std::uint32_t& value) { return reader_.ReadUInt32(value); }
    bool U64(std::uint64_t& value) { return reader_.ReadUInt64(value); }
    bool Bool(bool& value) { return reader_.ReadBool(value); }
    bool F(float& value) { return reader_.ReadFloat(value) && std::isfinite(value); }
    bool Vec3(kb::math::Vec3& value) { return F(value.x) && F(value.y) && F(value.z); }
    bool Quat(kb::math::Quat& value) { return F(value.x) && F(value.y) && F(value.z) && F(value.w); }
    bool Color(kb::math::Color& value) { return F(value.r) && F(value.g) && F(value.b) && F(value.a); }
    bool CurveKey(ParticleCompiledCurveKey& value) {
        std::uint8_t easing = 0U;
        if (!F(value.time) || !F(value.value) || !U8(easing) || easing > static_cast<std::uint8_t>(kb::math::Easing::InOutBounce)) return false;
        value.easing = static_cast<kb::math::Easing>(easing); return true;
    }
    bool Curve(ParticleCompiledCurve& value) {
        if (!U8(value.keyCount) || value.keyCount > kb::scene::kParticleEffectMaxCurveKeys) return false;
        for (std::uint8_t index = 0U; index < value.keyCount; ++index) if (!CurveKey(value.keys[index])) return false;
        return true;
    }
    bool Gradient(ParticleCompiledGradient& value) {
        if (!U8(value.stopCount) || value.stopCount > kb::scene::kParticleEffectMaxGradientStops) return false;
        for (std::uint8_t index = 0U; index < value.stopCount; ++index)
            if (!F(value.stops[index].time) || !Color(value.stops[index].color)) return false;
        return true;
    }
    bool InitialVelocity(kb::scene::ParticleInitialVelocityModule& value) {
        return Vec3(value.direction) && F(value.speedMin) && F(value.speedMax) && F(value.randomization) && F(value.spreadDegrees);
    }
    [[nodiscard]] bool Exhausted() const noexcept { return reader_.Exhausted(); }
  private:
    ByteReader reader_;
};

bool ReadEffect(Reader& reader, ParticleCompiledEffect& effect) {
    std::uint8_t backendPolicy = 0U, gpuCatchupPolicy = 0U;
    if (!reader.U64(effect.determinismSeed) || !reader.F(effect.durationSeconds) || !reader.Bool(effect.looping) ||
        !reader.U8(backendPolicy) || backendPolicy > static_cast<std::uint8_t>(kb::scene::ParticleBackendPolicy::GpuVisualRequired) ||
        !reader.U8(gpuCatchupPolicy) || gpuCatchupPolicy > static_cast<std::uint8_t>(kb::scene::ParticleGpuCatchupPolicy::BoundedWarmup) ||
        !reader.U8(effect.emitterCount) || effect.emitterCount > kb::scene::kParticleEffectMaxEmitters ||
        !reader.U8(effect.eventBindingCount) || effect.eventBindingCount > kb::scene::kParticleEffectMaxEventBindings) return false;
    effect.backendPolicy = static_cast<kb::scene::ParticleBackendPolicy>(backendPolicy);
    effect.gpuCatchupPolicy = static_cast<kb::scene::ParticleGpuCatchupPolicy>(gpuCatchupPolicy);
    for (std::uint8_t emitterIndex = 0U; emitterIndex < effect.emitterCount; ++emitterIndex) {
        auto& e = effect.emitters[emitterIndex];
        std::uint8_t output = 0U, blend = 0U, sort = 0U, alignment = 0U, space = 0U, mode = 0U;
        std::uint32_t columns = 0U, rows = 0U;
        if (!reader.U64(e.emitterId) || !reader.U8(output) || output > static_cast<std::uint8_t>(kb::scene::ParticleOutputType::Volumetric) ||
            !reader.U64(e.materialAssetId) || !reader.U64(e.meshAssetId) || !reader.U64(e.textureAtlasAssetId) ||
            !reader.F(e.meshLodBias) || !reader.Bool(e.meshCastsShadow) || !reader.Bool(e.meshReceivesShadow) ||
            !reader.U8(blend) || blend > static_cast<std::uint8_t>(kb::scene::ParticleBlendMode::Premultiplied) ||
            !reader.U8(sort) || sort > static_cast<std::uint8_t>(kb::scene::ParticleSortMode::Age) ||
            !reader.U8(alignment) || alignment > static_cast<std::uint8_t>(kb::scene::ParticleAlignment::Local) ||
            !reader.Bool(e.depthTest) || !reader.Bool(e.depthWrite) || !reader.Bool(e.softParticles) ||
            !reader.Bool(e.antiAliasing) || !reader.U32(columns) || columns > UINT16_MAX || !reader.U32(rows) || rows > UINT16_MAX ||
            !reader.U32(e.flipbookFrameCount) || !reader.F(e.flipbookFramesPerSecond) || !reader.Bool(e.flipbookLooping) ||
            !reader.F(e.stretchVelocityScale) || !reader.F(e.stretchMinimumLength) || !reader.F(e.pointSpriteDiameter) ||
            !reader.F(e.trailSampleIntervalSeconds) || !reader.F(e.trailMinimumDistance) ||
            !reader.U32(e.trailMaxSamplesPerParticle) || !reader.F(e.trailWidth) ||
            !reader.U32(e.ribbonMaxSegments) || !reader.F(e.ribbonWidth) || !reader.Bool(e.ribbonBreakOnDeath) ||
            !reader.Vec3(e.beamLocalEnd) || !reader.U32(e.beamSegments) || !reader.F(e.beamWidth) ||
            !reader.F(e.beamNoiseAmplitude) || !reader.F(e.beamNoiseFrequency) ||
            !reader.U8(space) || space > static_cast<std::uint8_t>(kb::scene::ParticleSimulationSpace::World) ||
            !reader.Bool(e.enabled) || !reader.U8(mode) || mode > static_cast<std::uint8_t>(kb::scene::ParticleSpawnMode::Burst) ||
            !reader.U32(e.maxParticles) || !reader.Vec3(e.localPosition) || !reader.Quat(e.localRotation) ||
            !reader.InitialVelocity(e.initialVelocity) || !reader.F(e.lifetimeMin) || !reader.F(e.lifetimeMax) ||
            !reader.F(e.prewarmSeconds) || !reader.U8(e.rateKeyCount) || e.rateKeyCount > kb::scene::kParticleEffectMaxCurveKeys ||
            !reader.U8(e.burstCount) || e.burstCount > kb::scene::kParticleEffectMaxBursts) return false;
        e.outputType = static_cast<kb::scene::ParticleOutputType>(output); e.blendMode = static_cast<kb::scene::ParticleBlendMode>(blend);
        e.sortMode = static_cast<kb::scene::ParticleSortMode>(sort); e.alignment = static_cast<kb::scene::ParticleAlignment>(alignment);
        e.flipbookColumns = static_cast<std::uint16_t>(columns); e.flipbookRows = static_cast<std::uint16_t>(rows);
        e.simulationSpace = static_cast<kb::scene::ParticleSimulationSpace>(space); e.mode = static_cast<kb::scene::ParticleSpawnMode>(mode);
        for (std::uint8_t index = 0U; index < e.rateKeyCount; ++index) if (!reader.CurveKey(e.rateKeys[index])) return false;
        for (std::uint8_t index = 0U; index < e.burstCount; ++index)
            if (!reader.F(e.bursts[index].timeSeconds) || !reader.U32(e.bursts[index].count)) return false;
        if (!reader.Gradient(e.colorOverLife) || !reader.Curve(e.sizeOverLife) || !reader.Curve(e.alphaOverLife) ||
            !reader.U8(e.moduleCount) || e.moduleCount > kb::scene::kParticleEffectMaxModulesPerEmitter) return false;
        for (std::uint8_t index = 0U; index < e.moduleCount; ++index) {
            auto& module = e.modules[index]; std::uint8_t type = 0U;
            if (!reader.U64(module.moduleId) || !reader.U8(type) || type > static_cast<std::uint8_t>(kb::scene::ParticleModuleType::SubEmitter) ||
                !reader.Bool(module.enabled)) return false;
            module.type = static_cast<kb::scene::ParticleModuleType>(type);
            switch (module.type) {
            case kb::scene::ParticleModuleType::InitialVelocity: { kb::scene::ParticleInitialVelocityModule value; if (!reader.InitialVelocity(value)) return false; module.payload = value; break; }
            case kb::scene::ParticleModuleType::Gravity: { kb::scene::ParticleGravityModule value; if (!reader.Vec3(value.acceleration) || !reader.F(value.sceneGravityScale)) return false; module.payload = value; break; }
            case kb::scene::ParticleModuleType::Wind: { kb::scene::ParticleWindModule value; if (!reader.Vec3(value.acceleration)) return false; module.payload = value; break; }
            case kb::scene::ParticleModuleType::Drag: { kb::scene::ParticleDragModule value; if (!reader.F(value.coefficient)) return false; module.payload = value; break; }
            case kb::scene::ParticleModuleType::CollisionPlane: { kb::scene::ParticleCollisionPlaneModule value; if (!reader.Vec3(value.normal) || !reader.F(value.distance) || !reader.F(value.restitution) || !reader.F(value.friction) || !reader.U32(value.maxEventsPerStep)) return false; module.payload = value; break; }
            case kb::scene::ParticleModuleType::SubEmitter: { kb::scene::ParticleSubEmitterModule value; std::uint8_t trigger = 0U; if (!reader.U64(value.targetEmitterId) || !reader.U8(trigger) || trigger > static_cast<std::uint8_t>(kb::scene::ParticleEventTrigger::Collision) || !reader.U32(value.count) || !reader.U32(value.maxDepth)) return false; value.trigger = static_cast<kb::scene::ParticleEventTrigger>(trigger); module.payload = value; break; }
            case kb::scene::ParticleModuleType::ColorOverLife:
            case kb::scene::ParticleModuleType::SizeOverLife:
            case kb::scene::ParticleModuleType::AlphaOverLife: break;
            }
        }
    }
    for (std::uint8_t index = 0U; index < effect.eventBindingCount; ++index) {
        auto& binding = effect.eventBindings[index]; std::uint8_t trigger = 0U;
        if (!reader.U8(binding.sourceEmitterIndex) || binding.sourceEmitterIndex >= effect.emitterCount ||
            !reader.U8(trigger) || trigger > static_cast<std::uint8_t>(kb::scene::ParticleEventTrigger::Collision) ||
            !reader.U64(binding.sourceModuleId) || !reader.U8(binding.targetEmitterIndex) || binding.targetEmitterIndex >= effect.emitterCount ||
            !reader.U32(binding.count) || !reader.U8(binding.maxDepth) || !reader.U32(binding.perStepBudget)) return false;
        binding.trigger = static_cast<kb::scene::ParticleEventTrigger>(trigger);
    }
    return reader.Exhausted();
}

[[nodiscard]] bool ValidEffect(const ParticleCompiledEffect& effect) noexcept {
    if (effect.emitterCount == 0U || effect.durationSeconds < 0.0F) return false;
    std::uint64_t totalCapacity = 0U;
    kb::scene::ParticleStableId previousEmitterId = 0U;
    for (std::uint8_t emitterIndex = 0U; emitterIndex < effect.emitterCount; ++emitterIndex) {
        const auto& emitter = effect.emitters[emitterIndex];
        if (emitter.emitterId == 0U || emitter.emitterId <= previousEmitterId || emitter.materialAssetId == 0U ||
            ((emitter.outputType == kb::scene::ParticleOutputType::Mesh) != (emitter.meshAssetId != 0U)) ||
            emitter.maxParticles == 0U ||
            emitter.maxParticles > kb::scene::kParticleEffectMaxCpuParticlesPerEmitter || emitter.rateKeyCount == 0U ||
            emitter.lifetimeMin <= 0.0F || emitter.lifetimeMax < emitter.lifetimeMin ||
            emitter.prewarmSeconds < 0.0F || emitter.prewarmSeconds > kb::scene::kParticleEffectMaxPrewarmSeconds ||
            emitter.flipbookColumns == 0U || emitter.flipbookRows == 0U || emitter.flipbookFrameCount == 0U ||
            emitter.pointSpriteDiameter <= 0.0F ||
            emitter.trailSampleIntervalSeconds <= 0.0F || emitter.trailMinimumDistance < 0.0F ||
            emitter.trailMaxSamplesPerParticle == 0U ||
            emitter.trailMaxSamplesPerParticle > kb::scene::kParticleEffectMaxTrailSamplesPerParticle ||
            emitter.trailWidth <= 0.0F || emitter.ribbonMaxSegments == 0U ||
            emitter.ribbonMaxSegments > kb::scene::kParticleEffectMaxStripSegmentsPerEmitter ||
            emitter.ribbonWidth <= 0.0F ||
            kb::math::Dot(emitter.beamLocalEnd, emitter.beamLocalEnd) <= 0.000001F ||
            emitter.beamSegments == 0U || emitter.beamSegments > kb::scene::kParticleEffectMaxStripSegmentsPerEmitter ||
            emitter.beamWidth <= 0.0F || emitter.beamNoiseAmplitude < 0.0F || emitter.beamNoiseFrequency < 0.0F)
            return false;
        previousEmitterId = emitter.emitterId;
        totalCapacity += emitter.maxParticles;
        for (std::uint8_t index = 1U; index < emitter.rateKeyCount; ++index)
            if (emitter.rateKeys[index].time <= emitter.rateKeys[index - 1U].time) return false;
        for (std::uint8_t index = 1U; index < emitter.burstCount; ++index)
            if (emitter.bursts[index].timeSeconds < emitter.bursts[index - 1U].timeSeconds) return false;
        kb::scene::ParticleStableId previousModuleId = 0U;
        for (std::uint8_t index = 0U; index < emitter.moduleCount; ++index) {
            const auto& module = emitter.modules[index];
            if (module.moduleId == 0U || module.moduleId <= previousModuleId) return false;
            previousModuleId = module.moduleId;
        }
    }
    if (totalCapacity > kb::scene::kParticleEffectMaxCpuParticlesPerScene) return false;
    for (std::uint8_t index = 0U; index < effect.eventBindingCount; ++index) {
        const auto& binding = effect.eventBindings[index];
        if (binding.count == 0U || binding.count > kb::scene::kParticleEffectMaxSpawnsPerStep ||
            binding.maxDepth == 0U || binding.maxDepth > kb::scene::kParticleEffectMaxSubEmitterDepth ||
            binding.perStepBudget == 0U || binding.perStepBudget > kb::scene::kParticleEffectMaxEventsPerStep)
            return false;
    }
    return true;
}

} // namespace

std::uint64_t ParticleCompiledEffectCache::HashBytes(std::span<const std::uint8_t> bytes) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::uint8_t byte : bytes) { hash ^= byte; hash *= 1099511628211ULL; }
    return hash;
}
std::uint64_t ParticleCompiledEffectCache::HashText(std::string_view text) noexcept {
    return HashBytes({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}
std::uint64_t ParticleCompiledEffectCacheKey::StableHash() const noexcept {
    std::array<std::uint64_t, 5> values{sourceHash, dependencyHash, compilerVersion,
        static_cast<std::uint64_t>(platform), capabilityKey};
    std::array<std::uint8_t, sizeof(values)> bytes{};
    for (std::size_t index = 0U; index < values.size(); ++index)
        for (std::uint32_t shift = 0U; shift < 64U; shift += 8U)
            bytes[index * sizeof(std::uint64_t) + shift / 8U] =
                static_cast<std::uint8_t>((values[index] >> shift) & 0xFFU);
    return ParticleCompiledEffectCache::HashBytes(bytes);
}
std::filesystem::path ParticleCompiledEffectCache::PathFor(const std::filesystem::path& cacheRoot,
                                                           const ParticleCompiledEffectCacheKey& key) {
    std::ostringstream name; name << std::hex << std::setfill('0') << std::setw(16) << key.StableHash() << ".kbvfxc";
    return cacheRoot / std::to_string(key.compilerVersion) / name.str();
}

ParticleCompiledEffectCacheResult ParticleCompiledEffectCache::Load(const std::filesystem::path& path,
                                                                    const ParticleCompiledEffectCacheKey& expectedKey) {
    std::error_code error;
    if (!std::filesystem::exists(path, error))
        return {.status = error ? ParticleCompiledEffectCacheStatus::FileAccessFailed : ParticleCompiledEffectCacheStatus::Missing};
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) return {.status = ParticleCompiledEffectCacheStatus::FileAccessFailed};
    if (size > kParticleCompiledEffectCacheMaxBytes) return {.status = ParticleCompiledEffectCacheStatus::SourceTooLarge};
    std::vector<std::uint8_t> bytes = ReadAllBytes(path);
    if (bytes.size() != size) return {.status = ParticleCompiledEffectCacheStatus::FileAccessFailed};
    if (bytes.size() < kHeaderBytes) return {.status = ParticleCompiledEffectCacheStatus::InvalidCache};
    const std::uint64_t actualPayloadHash = HashBytes(std::span<const std::uint8_t>{bytes}.subspan(kHeaderBytes));
    Reader reader{std::move(bytes)};
    std::uint64_t magic = 0U, format = 0U, artifactVersion = 0U;
    ParticleCompiledEffectCacheKey key; std::uint8_t platform = 0U; std::uint64_t payloadHash = 0U;
    if (!reader.U64(magic) || magic != kMagic || !reader.U64(format) || format != kParticleCompiledEffectCacheFormatVersion ||
        !reader.U64(artifactVersion) || artifactVersion != kParticleCompiledEffectVersion ||
        !reader.U64(key.sourceHash) || !reader.U64(key.dependencyHash) || !reader.U64(key.compilerVersion) ||
        !reader.U8(platform) || platform > static_cast<std::uint8_t>(ParticleCompilePlatform::WindowsVulkan) ||
        !reader.U64(key.capabilityKey) || !reader.U64(payloadHash) || payloadHash != actualPayloadHash)
        return {.status = ParticleCompiledEffectCacheStatus::InvalidCache};
    key.platform = static_cast<ParticleCompilePlatform>(platform);
    if (!(key == expectedKey)) return {.status = ParticleCompiledEffectCacheStatus::InvalidCache};
    ParticleCompiledEffect effect;
    if (!ReadEffect(reader, effect) || !ValidEffect(effect))
        return {.status = ParticleCompiledEffectCacheStatus::InvalidCache};
    return {.status = ParticleCompiledEffectCacheStatus::Success, .effect = MakeParticleCompiledEffect(std::move(effect))};
}

ParticleCompiledEffectCacheStatus ParticleCompiledEffectCache::Save(const std::filesystem::path& path,
                                                                    const ParticleCompiledEffectCacheKey& key,
                                                                    const ParticleCompiledEffect& effect) {
    std::vector<std::uint8_t> payload; payload.reserve(64U * 1024U); WriteEffect(payload, effect);
    std::vector<std::uint8_t> bytes; bytes.reserve(kHeaderBytes + payload.size());
    WriteUInt64(bytes, kMagic); WriteUInt64(bytes, kParticleCompiledEffectCacheFormatVersion);
    WriteUInt64(bytes, kParticleCompiledEffectVersion); WriteUInt64(bytes, key.sourceHash);
    WriteUInt64(bytes, key.dependencyHash); WriteUInt64(bytes, key.compilerVersion);
    WriteUInt8(bytes, static_cast<std::uint8_t>(key.platform)); WriteUInt64(bytes, key.capabilityKey);
    WriteUInt64(bytes, HashBytes(payload)); WriteRaw(bytes, payload.data(), payload.size());
    if (bytes.size() > kParticleCompiledEffectCacheMaxBytes) return ParticleCompiledEffectCacheStatus::SourceTooLarge;
    std::error_code error; std::filesystem::create_directories(path.parent_path(), error);
    if (error) return ParticleCompiledEffectCacheStatus::FileAccessFailed;
    return WriteBytesAtomically(path, bytes) ? ParticleCompiledEffectCacheStatus::Success
                                             : ParticleCompiledEffectCacheStatus::AtomicWriteFailed;
}

} // namespace kb::particles
