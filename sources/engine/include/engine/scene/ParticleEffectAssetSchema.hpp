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
inline constexpr std::size_t kParticleEffectMaxEmitters = 16U;
inline constexpr std::size_t kParticleEffectMaxModulesPerEmitter = 16U;
inline constexpr std::size_t kParticleEffectMaxEventBindings = 32U;
inline constexpr std::size_t kParticleEffectMaxCurveKeys = 64U;
inline constexpr std::size_t kParticleEffectMaxGradientStops = 64U;
inline constexpr std::size_t kParticleEffectMaxBursts = 64U;
inline constexpr std::size_t kParticleEffectMaxCurveRecords = 1U + 3U * kParticleEffectMaxCurveKeys;
inline constexpr std::size_t kParticleEffectMaxGradientRecords = 1U + 3U * kParticleEffectMaxGradientStops;
inline constexpr std::size_t kParticleEffectMaxBurstRecords = 1U + 2U * kParticleEffectMaxBursts;
inline constexpr std::size_t kParticleEffectMaxModuleRecords = 5U + kParticleEffectMaxGradientRecords;
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

// Effects that prefer the GPU may keep far more particles per emitter and effect than the CPU
// pipeline allows: eligible emitters are simulated on the GPU, the others fall back to the CPU caps
// at run time.
[[nodiscard]] constexpr std::uint32_t ParticleEmitterCapacityLimit(ParticleBackendPolicy policy) noexcept {
    return policy == ParticleBackendPolicy::CpuDeterministic ? kParticleEffectMaxCpuParticlesPerEmitter
                                                               : kParticleEffectMaxGpuParticlesPerScene;
}

[[nodiscard]] constexpr std::uint32_t ParticleEffectCapacityLimit(ParticleBackendPolicy policy) noexcept {
    return policy == ParticleBackendPolicy::CpuDeterministic ? kParticleEffectMaxCpuParticlesPerScene
                                                               : kParticleEffectMaxGpuParticlesPerScene;
}
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
    Noise,
    AxisRotation,
    Attraction,
    TorusSwirl,
};
// WhileAlive: every live particle of the source keeps emitting the target at the binding's rate
// (smoke behind debris; Niagara's spawn per particle).
enum class ParticleEventTrigger : std::uint8_t { Birth, Death, Collision, WhileAlive };
// Where a particle is born (Unity's Shape module, Niagara's shape location), around the emitter's
// position and turned by its rotation.
enum class ParticleSpawnShape : std::uint8_t { Point, Sphere, Box, Cone, Circle, Disc, Line, Spiral };
// Start velocity: Cone around the direction (speed range, spread), Radial along the shape's normal at
// the birth point, Spiral2D along one of spiralArms directions in the XY plane turning with time.
enum class ParticleVelocityMode : std::uint8_t { Cone, Radial, Spiral2D };
enum class ParticleEventAction : std::uint8_t { EmitTargetEmitter, EmitEffectAsset };
enum class ParticleOutputType : std::uint8_t {
    Billboard,
    StretchedBillboard,
    PointSprite,
    Mesh,
    Trail,
    Ribbon,
    Beam,
    Volumetric,
    Decal
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

struct ParticleSpawnShapeAsset {
    ParticleSpawnShape type = ParticleSpawnShape::Point;
    float radius = 0.0F;                 // Sphere, Cone, Circle, Disc, Spiral
    kb::math::Vec3 boxSize{};            // Box: size around the emitter
    float coneAngleDegrees = 0.0F;       // Cone: the normal leans outward up to this at the rim
    bool surfaceOnly = false;            // Sphere: on its surface
    bool upperHalfOnly = false;          // Sphere: the upper half only
    kb::math::Vec3 lineStart{};          // Line
    kb::math::Vec3 lineEnd{};
    float spiralArms = 1.0F;             // Spiral: galaxy arms
    float spiralTwist = 0.0F;            // radians per metre from the centre
    float spiralThickness = 0.0F;        // metres
};

// A beam per birth (lightning): the emitter's every birth is beamSegments + 1 particles from
// beamStart to beamEnd (emitter space), pushed sideways up to beamNoise metres (most at the middle),
// all in one ribbon group. Ends: fixed; both moved by a point of the spawn shape (in XZ); from a point
// of the spawn shape to the emitter.
enum class ParticleBeamEnds : std::uint8_t { Fixed, ShapeOffset, ShapeToEmitter };
struct ParticleBeamSpawnAsset {
    bool enabled = false;
    ParticleBeamEnds ends = ParticleBeamEnds::Fixed;
    std::uint32_t segments = 8U;
    kb::math::Vec3 start{};
    kb::math::Vec3 end{};
    float noise = 0.0F;
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
    kb::math::Color startColor{1.0F, 1.0F, 1.0F, 1.0F};
    float startSize = 1.0F;
    // Every particle draws a random initial angle and angular velocity per axis from these ranges at birth and
    // turns by angle + velocity x age (degrees, degrees per second). Z is the spin of a billboard about the view
    // axis; a mesh particle is turned by the three angles as Euler angles (Z after Y after X, in the emitter's
    // frame), so a tumbling rock needs X and Y as well.
    kb::math::Vec3 initialRotationMinDegrees{};
    kb::math::Vec3 initialRotationMaxDegrees{};
    kb::math::Vec3 angularVelocityMinDegrees{};
    kb::math::Vec3 angularVelocityMaxDegrees{};
    // Each particle's size at birth is startSize x (1 - sizeRandom x r), r random in [0, 1].
    float sizeRandom = 0.0F;
    // The emitter's own clock (Unity's per-system Start Delay, Duration and Looping): nothing before
    // startDelaySeconds; with cycleSeconds > 0 the rate runs and the bursts repeat every cycle while
    // cycleLooping, or run one cycle only; 0 = the effect's duration and looping.
    float startDelaySeconds = 0.0F;
    float cycleSeconds = 0.0F;
    bool cycleLooping = true;
    ParticleSpawnShapeAsset shape;
    ParticleVelocityMode velocityMode = ParticleVelocityMode::Cone;
    float spiralArms = 1.0F;                  // Spiral2D
    float spiralTurnDegreesPerSecond = 0.0F;  // Spiral2D
    kb::math::Vec3 addedVelocity{};           // added after the emitter rotation
    ParticleBeamSpawnAsset beam;
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
    bool killOnContact = false;  // the particle dies where it struck, after its collision event
};
// Curl noise (divergence-free swirl): acceleration strength x curl of gradient noise at position x frequency.
struct ParticleNoiseModule {
    float strength = 1.0F;
    float frequency = 1.0F;
};
// Positions and velocities turn about an axis through the emitter (Niagara's rotate around point).
struct ParticleAxisRotationModule {
    kb::math::Vec3 axis{0.0F, 1.0F, 0.0F};
    float degreesPerSecond = 90.0F;
};
// Pull toward a point of the emitter's space; radius 0 = the same pull at any distance, else fading
// to zero at the radius.
struct ParticleAttractionModule {
    kb::math::Vec3 point{};
    float strength = 5.0F;
    float radius = 0.0F;
};
// Every particle rolls around the nearest point of a horizontal ring (the cap of a mushroom cloud).
struct ParticleTorusSwirlModule {
    kb::math::Vec3 center{};
    float radius = 2.0F;
    float degreesPerSecond = 180.0F;
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
                 ParticleCollisionPlaneModule, ParticleSubEmitterModule, ParticleNoiseModule,
                 ParticleAxisRotationModule, ParticleAttractionModule, ParticleTorusSwirlModule>;

struct ParticleModuleAsset {
    ParticleStableId moduleId = 0U;
    std::uint32_t authoringOrder = 0U;
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
// Every particle projects the texture atlas onto the scene below it: a box size wide and deep,
// `depth` metres tall, turned by the particle's rotation about the vertical.
struct ParticleDecalOutput {
    float depth = 1.0F;
};

using ParticleOutputPayload = std::variant<ParticleBillboardOutput, ParticleStretchedBillboardOutput,
                                           ParticlePointSpriteOutput, ParticleMeshOutput, ParticleTrailOutput,
                                           ParticleRibbonOutput, ParticleBeamOutput, ParticleVolumetricOutput,
                                           ParticleDecalOutput>;

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
    // HDR multiplier of the particle colour (Niagara's emissive colour above 1): the gradient stays
    // in [0, 1], the drawn colour is gradient x colorIntensity.
    float colorIntensity = 1.0F;
    // Particle lights (Niagara's light renderer): the first lightMaxCount particles of the emitter
    // light the scene as point lights of their colour x alpha x lightIntensity, lightRadius metres.
    bool lightEnabled = false;
    float lightRadius = 4.0F;
    float lightIntensity = 1.0F;
    std::uint32_t lightMaxCount = 16U;
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
    // World space: localPosition is a point of the world; the emitter stays there while its owner moves
    // (the crater at the end of a meteor's flight).
    bool absolutePosition = false;
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
    float ratePerSecond = 0.0F;  // WhileAlive: births a second at every live source particle
};

[[nodiscard]] bool IsRepeatableParticleModule(ParticleModuleType type) noexcept;
[[nodiscard]] bool IsValidParticleEffectUtf8(std::string_view text) noexcept;
[[nodiscard]] bool IsValidParticleEffectString(std::string_view text) noexcept;
[[nodiscard]] ParticleModulePayload DefaultParticleModulePayload(ParticleModuleType type);
[[nodiscard]] ParticleOutputPayload DefaultParticleOutputPayload(ParticleOutputType type);

} // namespace kb::scene
