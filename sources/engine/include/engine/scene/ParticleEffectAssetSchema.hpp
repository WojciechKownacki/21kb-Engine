#pragma once

#include "engine/math/EngineMath.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace kb::scene {

inline constexpr std::uint32_t kParticleEffectFormatVersion = 2U;
inline constexpr std::size_t kParticleEffectMaxSourceBytes = 512U * 1024U;
inline constexpr std::size_t kParticleEffectMaxInstancesPerScene = 256U;
inline constexpr std::size_t kParticleEffectMaxEmitters = 8U;
inline constexpr std::size_t kParticleEffectMaxModulesPerEmitter = 16U;
inline constexpr std::size_t kParticleEffectMaxEventBindings = 32U;
inline constexpr std::size_t kParticleEffectMaxCurveKeys = 64U;
inline constexpr std::size_t kParticleEffectMaxGradientStops = 64U;
inline constexpr std::size_t kParticleEffectMaxBursts = 64U;
inline constexpr std::size_t kParticleEffectMaxCurveRecords = 1U + 3U * kParticleEffectMaxCurveKeys;
inline constexpr std::size_t kParticleEffectMaxGradientRecords = 1U + 3U * kParticleEffectMaxGradientStops;
inline constexpr std::size_t kParticleEffectMaxBurstRecords = 1U + 2U * kParticleEffectMaxBursts;
inline constexpr std::size_t kParticleEffectMaxModuleRecords = 4U + kParticleEffectMaxGradientRecords;
inline constexpr std::size_t kParticleEffectMaxEmitterRecords =
    64U + kParticleEffectMaxCurveRecords + kParticleEffectMaxBurstRecords +
    kParticleEffectMaxModulesPerEmitter * kParticleEffectMaxModuleRecords;
inline constexpr std::size_t kParticleEffectMaxEventBindingRecords = 10U;
inline constexpr std::size_t kParticleEffectMaxRecords =
    16U + kParticleEffectMaxEmitters * kParticleEffectMaxEmitterRecords +
    kParticleEffectMaxEventBindings * kParticleEffectMaxEventBindingRecords;
inline constexpr std::size_t kParticleEffectMaxDiagnostics = 128U;
inline constexpr std::size_t kParticleEffectMaxDependencyAssets = 256U;
inline constexpr std::size_t kParticleEffectMaxStringBytes = 4U * 1024U;
inline constexpr std::size_t kParticleEffectMaxRuntimeParametersPerInstance = 32U;
inline constexpr std::size_t kParticleEffectMaxRuntimeParameterNameBytes = 128U;
inline constexpr std::uint32_t kParticleEffectMaxCpuParticlesPerEmitter = 65'536U;
inline constexpr std::uint32_t kParticleEffectMaxCpuParticlesPerScene = 262'144U;
inline constexpr std::uint32_t kParticleEffectMaxGpuParticlesPerScene = 1'048'576U;
inline constexpr std::uint32_t kParticleEffectMaxCommandsPerStep = 8'192U;
inline constexpr std::uint32_t kParticleEffectMaxSpawnsPerStep = 65'536U;
inline constexpr std::uint32_t kParticleEffectMaxEventsPerStep = 8'192U;
inline constexpr std::uint32_t kParticleEffectMaxPrewarmSteps = 65'536U;
inline constexpr std::uint32_t kParticleEffectFixedStepsPerSecond = 60U;
inline constexpr float kParticleEffectMaxPrewarmSeconds =
    static_cast<float>(kParticleEffectMaxPrewarmSteps) /
    static_cast<float>(kParticleEffectFixedStepsPerSecond);
inline constexpr float kParticleEffectMaxContinuousRatePerSecond =
    static_cast<float>(kParticleEffectMaxSpawnsPerStep) *
    static_cast<float>(kParticleEffectFixedStepsPerSecond);
inline constexpr std::uint32_t kParticleEffectMaxSubEmitterDepth = 3U;
inline constexpr std::uint32_t kParticleEffectMaxTrailSamplesPerParticle = 64U;
inline constexpr std::uint32_t kParticleEffectMaxStripSegmentsPerEmitter = 4'096U;
inline constexpr std::uint32_t kParticleEffectRetainedSnapshotSlots = 4U;
inline constexpr std::uint32_t kParticleEffectRetainedGpuSteps = 64U;
inline constexpr std::uint64_t kParticleEffectMaxCpuSnapshotBytes = 64ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kParticleEffectMaxGpuResourceBytes = 256ULL * 1024ULL * 1024ULL;
inline constexpr kb::math::Vec3 kParticleEffectDefaultSceneGravity{0.0F, -9.81F, 0.0F};

using ParticleStableId = std::uint64_t;

enum class ParticleBackendPolicy : std::uint8_t { CpuDeterministic, GpuVisualPreferred, GpuVisualRequired };
enum class ParticleGpuCatchupPolicy : std::uint8_t { RestartFromSeed, BoundedWarmup };
enum class ParticleSimulationSpace : std::uint8_t { Local, World };
enum class ParticleSpawnMode : std::uint8_t { Continuous, Burst };
enum class ParticleModuleType : std::uint8_t {
    InitialVelocity,
    Gravity,
    Wind,
    Drag,
    ColorOverLife,
    SizeOverLife,
    AlphaOverLife,
    CollisionPlane,
    SubEmitter,
};
enum class ParticleEventTrigger : std::uint8_t { Birth, Death, Collision };
enum class ParticleEventAction : std::uint8_t { EmitTargetEmitter, EmitEffectAsset };
enum class ParticleOutputType : std::uint8_t {
    Billboard,
    StretchedBillboard,
    PointSprite,
    Mesh,
    Trail,
    Ribbon,
    Beam,
    Volumetric
};
enum class ParticleBlendMode : std::uint8_t { Opaque, Alpha, Add, Multiply, Subtract, Premultiplied };
enum class ParticleSortMode : std::uint8_t { None, BackToFront, FrontToBack, Distance, Age };
enum class ParticleAlignment : std::uint8_t { CameraFacing, Velocity, WorldUp, Local };

struct ParticleAssetReference {
    std::uint64_t assetId = 0U;
    std::string virtualPath;

    [[nodiscard]] bool Empty() const noexcept {
        return assetId == 0U && virtualPath.empty();
    }
};

struct ParticleBurstAsset {
    float timeSeconds = 0.0F;
    std::uint32_t count = 1U;
};

struct ParticleSpawnAsset {
    ParticleSpawnMode mode = ParticleSpawnMode::Continuous;
    kb::math::Curve rateOverTime{.keyframes = {kb::math::CurveKeyframe{.time = 0.0F, .value = 10.0F}}};
    std::vector<ParticleBurstAsset> bursts;
    float lifetimeMin = 1.0F;
    float lifetimeMax = 1.0F;
    float speedMin = 1.0F;
    float speedMax = 2.0F;
    kb::math::Vec3 direction{0.0F, 1.0F, 0.0F};
    float spreadDegrees = 15.0F;
    float randomization = 1.0F;
    float prewarmSeconds = 0.0F;
};

struct ParticleInitialVelocityModule {
    kb::math::Vec3 direction{0.0F, 1.0F, 0.0F};
    float speedMin = 1.0F;
    float speedMax = 2.0F;
    float randomization = 1.0F;
    float spreadDegrees = 15.0F;
};
struct ParticleGravityModule {
    kb::math::Vec3 acceleration{0.0F, -9.81F, 0.0F};
    float sceneGravityScale = 0.0F;
};
struct ParticleWindModule {
    kb::math::Vec3 acceleration{};
};
struct ParticleDragModule {
    float coefficient = 0.0F;
};
struct ParticleColorOverLifeModule {
    kb::math::Gradient gradient;
};
struct ParticleSizeOverLifeModule {
    kb::math::Curve curve{.keyframes = {kb::math::CurveKeyframe{.time = 0.0F, .value = 1.0F}}};
};
struct ParticleAlphaOverLifeModule {
    kb::math::Curve curve{.keyframes = {kb::math::CurveKeyframe{.time = 0.0F, .value = 1.0F}}};
};
struct ParticleCollisionPlaneModule {
    kb::math::Vec3 normal{0.0F, 1.0F, 0.0F};
    float distance = 0.0F;
    float restitution = 0.5F;
    float friction = 0.0F;
    std::uint32_t maxEventsPerStep = 64U;
};
struct ParticleSubEmitterModule {
    ParticleStableId targetEmitterId = 0U;
    ParticleEventTrigger trigger = ParticleEventTrigger::Death;
    std::uint32_t count = 1U;
    std::uint32_t maxDepth = 1U;
};

using ParticleModulePayload =
    std::variant<ParticleInitialVelocityModule, ParticleGravityModule, ParticleWindModule, ParticleDragModule,
                 ParticleColorOverLifeModule, ParticleSizeOverLifeModule, ParticleAlphaOverLifeModule,
                 ParticleCollisionPlaneModule, ParticleSubEmitterModule>;

struct ParticleModuleAsset {
    ParticleStableId moduleId = 0U;
    ParticleModuleType type = ParticleModuleType::InitialVelocity;
    bool enabled = true;
    ParticleModulePayload payload = ParticleInitialVelocityModule{};
};

struct ParticleFlipbookAsset {
    std::uint32_t columns = 1U;
    std::uint32_t rows = 1U;
    float framesPerSecond = 0.0F;
    bool looping = true;
};
struct ParticleBillboardOutput {
    ParticleFlipbookAsset flipbook;
};
struct ParticleStretchedBillboardOutput {
    ParticleFlipbookAsset flipbook;
    float velocityScale = 1.0F;
    float minimumLength = 0.0F;
};
struct ParticlePointSpriteOutput {
    ParticleFlipbookAsset flipbook;
    float diameter = 1.0F;
};
struct ParticleMeshOutput {
    float lodBias = 0.0F;
    bool castsShadow = false;
    bool receivesShadow = true;
};
struct ParticleTrailOutput {
    float sampleIntervalSeconds = 1.0F / 60.0F;
    float minimumDistance = 0.0F;
    std::uint32_t maxSamplesPerParticle = 16U;
    float width = 1.0F;
};
struct ParticleRibbonOutput {
    std::uint32_t maxSegments = 256U;
    float width = 1.0F;
    bool breakOnDeath = true;
};
struct ParticleBeamOutput {
    kb::math::Vec3 localEnd{0.0F, 1.0F, 0.0F};
    std::uint32_t segments = 16U;
    float width = 1.0F;
    float noiseAmplitude = 0.0F;
    float noiseFrequency = 1.0F;
};
struct ParticleVolumetricOutput {
    float density = 1.0F;
    float radiusScale = 1.0F;
    std::uint32_t lowQualitySteps = 8U;
    std::uint32_t highQualitySteps = 24U;
};

using ParticleOutputPayload = std::variant<ParticleBillboardOutput, ParticleStretchedBillboardOutput,
                                           ParticlePointSpriteOutput, ParticleMeshOutput, ParticleTrailOutput,
                                           ParticleRibbonOutput, ParticleBeamOutput, ParticleVolumetricOutput>;

struct ParticleOutputAsset {
    ParticleOutputType type = ParticleOutputType::Billboard;
    ParticleAssetReference material;
    ParticleAssetReference mesh;
    ParticleAssetReference textureAtlas;
    ParticleBlendMode blend = ParticleBlendMode::Alpha;
    ParticleSortMode sort = ParticleSortMode::BackToFront;
    bool depthTest = true;
    bool depthWrite = false;
    bool softParticles = false;
    bool antiAliasing = false;
    ParticleAlignment alignment = ParticleAlignment::CameraFacing;
    ParticleOutputPayload payload = ParticleBillboardOutput{};
};

struct ParticleEmitterAsset {
    ParticleStableId emitterId = 0U;
    std::uint32_t authoringOrder = 0U;
    std::string name;
    bool enabled = true;
    kb::math::Vec3 localPosition{};
    kb::math::Quat localRotation{};
    kb::math::Vec3 localScale{1.0F, 1.0F, 1.0F};
    std::uint32_t maxParticles = 256U;
    ParticleSimulationSpace simulationSpace = ParticleSimulationSpace::World;
    ParticleSpawnAsset spawn;
    std::vector<ParticleModuleAsset> modules;
    ParticleOutputAsset output;
};

struct ParticleEventBindingAsset {
    ParticleStableId sourceEmitterId = 0U;
    ParticleEventTrigger trigger = ParticleEventTrigger::Death;
    ParticleStableId sourceModuleId = 0U;
    ParticleEventAction action = ParticleEventAction::EmitTargetEmitter;
    ParticleStableId targetEmitterId = 0U;
    ParticleAssetReference targetEffect;
    std::uint32_t count = 1U;
    std::uint32_t maxDepth = 1U;
    std::uint32_t perStepBudget = 64U;
};

[[nodiscard]] bool IsRepeatableParticleModule(ParticleModuleType type) noexcept;
[[nodiscard]] bool IsValidParticleEffectUtf8(std::string_view text) noexcept;
[[nodiscard]] bool IsValidParticleEffectString(std::string_view text) noexcept;
[[nodiscard]] ParticleModulePayload DefaultParticleModulePayload(ParticleModuleType type);
[[nodiscard]] ParticleOutputPayload DefaultParticleOutputPayload(ParticleOutputType type);

} // namespace kb::scene
