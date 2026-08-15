#pragma once

#include "engine/scene/ParticleEffectAssetSchema.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <variant>

namespace kb::particles {

inline constexpr std::uint32_t kParticleCompiledEffectVersion = 1U;

enum class ParticleCompilePlatform : std::uint8_t {
    PlatformIndependent,
    WindowsDirectX12,
    WindowsVulkan,
};

struct ParticleCompiledCurveKey {
    float time = 0.0F;
    float value = 0.0F;
    kb::math::Easing easing = kb::math::Easing::Linear;
};

struct ParticleCompiledCurve {
    std::uint8_t keyCount = 0U;
    std::array<ParticleCompiledCurveKey, kb::scene::kParticleEffectMaxCurveKeys> keys{};
};

struct ParticleCompiledGradientStop {
    float time = 0.0F;
    kb::math::Color color{};
};

struct ParticleCompiledGradient {
    std::uint8_t stopCount = 0U;
    std::array<ParticleCompiledGradientStop, kb::scene::kParticleEffectMaxGradientStops> stops{};
};

struct ParticleCompiledEmitter {
    using ModulePayload = std::variant<kb::scene::ParticleInitialVelocityModule,
                                       kb::scene::ParticleGravityModule,
                                       kb::scene::ParticleWindModule,
                                       kb::scene::ParticleDragModule,
                                       kb::scene::ParticleCollisionPlaneModule,
                                       kb::scene::ParticleSubEmitterModule>;

    struct Module {
        kb::scene::ParticleStableId moduleId = 0U;
        kb::scene::ParticleModuleType type = kb::scene::ParticleModuleType::InitialVelocity;
        bool enabled = false;
        ModulePayload payload = kb::scene::ParticleInitialVelocityModule{};
    };

    kb::scene::ParticleStableId emitterId = 0U;
    kb::scene::ParticleOutputType outputType = kb::scene::ParticleOutputType::Billboard;
    std::uint64_t materialAssetId = 0U;
    std::uint64_t meshAssetId = 0U;
    std::uint64_t textureAtlasAssetId = 0U;
    float meshLodBias = 0.0F;
    bool meshCastsShadow = false;
    bool meshReceivesShadow = true;
    kb::scene::ParticleBlendMode blendMode = kb::scene::ParticleBlendMode::Alpha;
    kb::scene::ParticleSortMode sortMode = kb::scene::ParticleSortMode::BackToFront;
    kb::scene::ParticleAlignment alignment = kb::scene::ParticleAlignment::CameraFacing;
    bool depthTest = true;
    bool depthWrite = false;
    bool softParticles = false;
    bool antiAliasing = false;
    std::uint16_t flipbookColumns = 1U;
    std::uint16_t flipbookRows = 1U;
    std::uint32_t flipbookFrameCount = 1U;
    float flipbookFramesPerSecond = 0.0F;
    bool flipbookLooping = true;
    float stretchVelocityScale = 0.0F;
    float stretchMinimumLength = 0.0F;
    float pointSpriteDiameter = 1.0F;
    kb::scene::ParticleSimulationSpace simulationSpace = kb::scene::ParticleSimulationSpace::World;
    bool enabled = false;
    kb::scene::ParticleSpawnMode mode = kb::scene::ParticleSpawnMode::Continuous;
    std::uint32_t maxParticles = 0U;
    kb::math::Vec3 localPosition{};
    kb::math::Quat localRotation{};
    kb::scene::ParticleInitialVelocityModule initialVelocity{};
    float lifetimeMin = 1.0F;
    float lifetimeMax = 1.0F;
    float prewarmSeconds = 0.0F;
    std::uint8_t rateKeyCount = 0U;
    std::uint8_t burstCount = 0U;
    std::array<ParticleCompiledCurveKey, kb::scene::kParticleEffectMaxCurveKeys> rateKeys{};
    std::array<kb::scene::ParticleBurstAsset, kb::scene::kParticleEffectMaxBursts> bursts{};
    ParticleCompiledGradient colorOverLife{};
    ParticleCompiledCurve sizeOverLife{};
    ParticleCompiledCurve alphaOverLife{};
    std::uint8_t moduleCount = 0U;
    std::array<Module, kb::scene::kParticleEffectMaxModulesPerEmitter> modules{};
};

struct ParticleCompiledEffect {
    struct EventBinding {
        std::uint8_t sourceEmitterIndex = 0U;
        kb::scene::ParticleEventTrigger trigger = kb::scene::ParticleEventTrigger::Death;
        kb::scene::ParticleStableId sourceModuleId = 0U;
        std::uint8_t targetEmitterIndex = 0U;
        std::uint32_t count = 1U;
        std::uint8_t maxDepth = 1U;
        std::uint32_t perStepBudget = 1U;
    };

    std::uint64_t determinismSeed = 0U;
    float durationSeconds = 0.0F;
    bool looping = false;
    std::uint8_t emitterCount = 0U;
    std::uint8_t eventBindingCount = 0U;
    std::array<ParticleCompiledEmitter, kb::scene::kParticleEffectMaxEmitters> emitters{};
    std::array<EventBinding, kb::scene::kParticleEffectMaxEventBindings> eventBindings{};
};

using ParticleCompiledEffectHandle = std::shared_ptr<const ParticleCompiledEffect>;

[[nodiscard]] ParticleCompiledEffectHandle MakeParticleCompiledEffect(ParticleCompiledEffect effect);

} // namespace kb::particles
