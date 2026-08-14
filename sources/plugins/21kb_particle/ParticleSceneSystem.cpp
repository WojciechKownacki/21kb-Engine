#include "ParticleSceneSystem.hpp"

#include "engine/particles/ParticlePlayback.hpp"
#include "engine/ecs/World.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <stdexcept>

namespace kb::particle_plugin {
namespace {

[[noreturn]] void ThrowComponentCreateFailure(kb::particles::ParticleRuntimeStatus status) {
    switch (status) {
    case kb::particles::ParticleRuntimeStatus::InvalidAsset:
        throw std::logic_error("particle component effectAssetId does not resolve to an executable particle effect");
    case kb::particles::ParticleRuntimeStatus::UnsupportedOutput:
        throw std::logic_error("particle component effect is unsupported by the CPU verification backend");
    case kb::particles::ParticleRuntimeStatus::InvalidOwner:
        throw std::logic_error("particle component owner is invalid");
    case kb::particles::ParticleRuntimeStatus::InstanceLimitReached:
        throw std::logic_error("particle component runtime instance capacity was reached");
    default:
        throw std::logic_error("particle component runtime instance creation failed");
    }
}

} // namespace

void ParticleSceneSystem::OnCreate(kb::scene::SceneSystemContext& context) {
    backend_.Warmup();
    componentQuery_ = context.EcsWorld().CreateQuery<kb::scene::ParticleEffectComponent>();
    kb::ecs::QueryExecutionSettings querySettings{};
    querySettings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    if (!componentQuery_.IsValid() || !componentHotQuery_.Rebuild(componentQuery_, querySettings)) {
        throw std::logic_error("particle component query initialization failed");
    }
    const kb::particles::ParticleRuntimeResult result =
        kb::particles::ParticlePlayback::RegisterBackend(context.GetScene(), backend_);
    if (!result.Succeeded()) {
        throw std::logic_error("particle CPU backend registration conflicted with an existing scene provider");
    }
    registered_ = true;
}

void ParticleSceneSystem::OnDestroy(kb::scene::SceneSystemContext& context) {
    if (!registered_) return;
    for (std::size_t index = 0U; index < componentInstanceCount_; ++index) {
        if (componentInstances_[index].instanceId != 0U) {
            static_cast<void>(backend_.Release(context.GetScene(), componentInstances_[index].instanceId));
        }
    }
    componentInstanceCount_ = 0U;
    componentHotQuery_ = {};
    componentQuery_ = {};
    const kb::particles::ParticleRuntimeResult result =
        kb::particles::ParticlePlayback::UnregisterBackend(context.GetScene(), backend_);
    if (!result.Succeeded()) {
        throw std::logic_error("particle CPU backend ownership changed before scene detach");
    }
    registered_ = false;
}

void ParticleSceneSystem::OnFixedUpdate(kb::scene::SceneSystemContext& context) {
    backend_.ProcessOwnerLifecycle(context.GetScene());
    ReconcileComponents(context.GetScene());
    const kb::particles::ParticleRuntimeResult result = backend_.Step(context.GetScene(), context.DeltaSeconds());
    if (!result.Succeeded()) {
        throw std::logic_error("particle CPU backend rejected the authoritative fixed step");
    }
    std::size_t index = 0U;
    while (index < componentInstanceCount_) {
        const ComponentInstance& binding = componentInstances_[index];
        if (!context.GetScene().Entities().IsAlive(binding.owner) &&
            (binding.instanceId == 0U ||
             backend_.Query(context.GetScene(), binding.instanceId).status ==
                 kb::particles::ParticleRuntimeStatus::InvalidInstance)) {
            componentInstances_[index] = componentInstances_[--componentInstanceCount_];
            continue;
        }
        ++index;
    }
}

void ParticleSceneSystem::CreateComponentInstance(
    kb::scene::Scene& scene,
    ComponentInstance& binding) {
    if (!binding.component.enabled || !scene.Entities().IsActive(binding.owner)) return;
    const kb::particles::ParticleRuntimeResult created = backend_.Create(
        scene, binding.component.effectAssetId, binding.owner);
    if (!created.Succeeded()) ThrowComponentCreateFailure(created.status);
    binding.instanceId = created.instanceId;
    static_cast<void>(backend_.ConfigureOwnerDeathPolicy(
        binding.instanceId, binding.component.ownerDeathPolicy));
    const kb::scene::TransformComponent* transform = scene.Transforms().TryGet(binding.owner);
    if (transform == nullptr || !backend_.ConfigureComponent(binding.instanceId,
            binding.component.rateMultiplier, binding.component.maxParticlesOverride,
            binding.component.followTransform, transform->WorldPayload()).Succeeded()) {
        static_cast<void>(backend_.Release(scene, binding.instanceId));
        binding.instanceId = 0U;
        throw std::logic_error("particle component runtime configuration is invalid");
    }
    if (binding.component.deterministicSeed != 0U) {
        static_cast<void>(backend_.SetSeed(scene, binding.instanceId, binding.component.deterministicSeed));
    }
    if (binding.component.autoPlay) {
        static_cast<void>(backend_.Play(scene, binding.instanceId));
    }
}

void ParticleSceneSystem::ReconcileComponents(kb::scene::Scene& scene) {
    std::size_t liveBindingIndex = 0U;
    while (liveBindingIndex < componentInstanceCount_) {
        if (scene.Entities().IsAlive(componentInstances_[liveBindingIndex].owner)) {
            ++liveBindingIndex;
            continue;
        }
        componentInstances_[liveBindingIndex] = componentInstances_[--componentInstanceCount_];
    }
    for (std::size_t index = 0U; index < componentInstanceCount_; ++index) componentInstances_[index].seen = false;
    struct VisitContext {
        ParticleSceneSystem* system = nullptr;
        kb::scene::Scene* scene = nullptr;
    } context{this, &scene};
    if (componentHotQuery_.IsStale(componentQuery_) &&
        !componentHotQuery_.RebuildIfChanged(componentQuery_)) {
        throw std::logic_error("particle component query refresh failed");
    }
    if (componentHotQuery_.EntityCount() > kb::scene::kParticleEffectMaxInstancesPerScene) {
        throw std::length_error("particle component binding capacity exceeded");
    }
    const auto visitComponent = [&](kb::scene::SceneEntity owner,
                                    const kb::scene::ParticleEffectComponent& component) {
            auto& visit = context;
            ParticleSceneSystem& system = *visit.system;
            std::size_t index = 0U;
            while (index < system.componentInstanceCount_ && system.componentInstances_[index].owner != owner) ++index;
            if (index == system.componentInstanceCount_) {
                ComponentInstance candidate{
                    .owner = owner,
                    .component = component,
                    .ownerWasActive = visit.scene->Entities().IsActive(owner),
                    .seen = true,
                };
                system.CreateComponentInstance(*visit.scene, candidate);
                system.componentInstances_[index] = candidate;
                ++system.componentInstanceCount_;
                return;
            }
            ComponentInstance& binding = system.componentInstances_[index];
            binding.seen = true;
            const bool active = visit.scene->Entities().IsActive(owner);
            const bool assetChanged = binding.component.effectAssetId != component.effectAssetId;
            const bool becameEnabled = !binding.component.enabled && component.enabled;
            const bool autoPlayEnabled = !binding.component.autoPlay && component.autoPlay;
            const bool seedChanged = binding.component.deterministicSeed != component.deterministicSeed;
            if (assetChanged && component.enabled && active) {
                ComponentInstance replacement{
                    .owner = owner,
                    .component = component,
                    .ownerWasActive = active,
                    .seen = true,
                };
                system.CreateComponentInstance(*visit.scene, replacement);
                if (binding.instanceId != 0U) {
                    static_cast<void>(system.backend_.Release(*visit.scene, binding.instanceId));
                }
                binding = replacement;
                return;
            }
            if (assetChanged || !component.enabled) {
                if (binding.instanceId != 0U) static_cast<void>(system.backend_.Release(*visit.scene, binding.instanceId));
                binding.instanceId = 0U;
            }
            binding.component = component;
            if (binding.instanceId == 0U && component.enabled && active) {
                system.CreateComponentInstance(*visit.scene, binding);
            } else if (binding.instanceId != 0U) {
                static_cast<void>(system.backend_.ConfigureOwnerDeathPolicy(
                    binding.instanceId, component.ownerDeathPolicy));
                const kb::scene::TransformComponent* transform = visit.scene->Transforms().TryGet(owner);
                if (transform == nullptr || !system.backend_.ConfigureComponent(binding.instanceId,
                        component.rateMultiplier, component.maxParticlesOverride,
                        component.followTransform, transform->WorldPayload()).Succeeded()) {
                    throw std::logic_error("particle component runtime configuration is invalid");
                }
                if (seedChanged && component.deterministicSeed != 0U) {
                    static_cast<void>(system.backend_.SetSeed(
                        *visit.scene, binding.instanceId, component.deterministicSeed));
                }
                if (binding.ownerWasActive && !active) {
                    if (component.restartOnActivate) {
                        static_cast<void>(system.backend_.Release(*visit.scene, binding.instanceId));
                        binding.instanceId = 0U;
                    } else if (system.backend_.Query(*visit.scene, binding.instanceId).state) {
                        static_cast<void>(system.backend_.Pause(*visit.scene, binding.instanceId));
                    }
                } else if (!binding.ownerWasActive && active) {
                    if (component.restartOnActivate && binding.instanceId != 0U) {
                        static_cast<void>(system.backend_.Restart(*visit.scene, binding.instanceId));
                    } else if (!component.restartOnActivate && binding.instanceId != 0U) {
                        static_cast<void>(system.backend_.Play(*visit.scene, binding.instanceId));
                    }
                } else if ((becameEnabled || autoPlayEnabled) && active) {
                    static_cast<void>(system.backend_.Play(*visit.scene, binding.instanceId));
                }
            }
            binding.ownerWasActive = active;
        };
    static_cast<void>(componentHotQuery_.ForEachRange(
        kb::scene::kParticleEffectMaxInstancesPerScene,
        [&](const auto& chunk) {
            const kb::scene::ParticleEffectComponent* components = chunk.template Components<0>();
            for (std::size_t index = 0U; index < chunk.Count(); ++index) {
                visitComponent(chunk.EntityAt(index), components[index]);
            }
        }));

    std::size_t index = 0U;
    while (index < componentInstanceCount_) {
        ComponentInstance& binding = componentInstances_[index];
        if (!scene.Entities().IsAlive(binding.owner)) {
            ++index;
            continue;
        }
        if (!binding.seen) {
            if (binding.instanceId != 0U) static_cast<void>(backend_.Release(scene, binding.instanceId));
            binding = componentInstances_[--componentInstanceCount_];
            continue;
        }
        ++index;
    }
}

kb::scene::SceneFixedUpdatePhase ParticleSceneSystem::FixedUpdatePhase() const noexcept {
    return kb::scene::SceneFixedUpdatePhase::PostSimulation;
}

bool ParticleSceneSystem::RequiresFixedStep() const { return true; }

} // namespace kb::particle_plugin
