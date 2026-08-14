#include "engine/particles/ParticleCompiledEffect.hpp"

#include <memory>
#include <utility>

namespace kb::particles {

ParticleCompiledEffectHandle MakeParticleCompiledEffect(ParticleCompiledEffect effect) {
    return std::make_shared<const ParticleCompiledEffect>(std::move(effect));
}

} // namespace kb::particles
