#pragma once

#include "engine/scene/ParticleEffectAsset.hpp"

#include <cstdint>
#include <vector>

namespace kb::particle_editor {

struct ParticleEmitterListRow {
    kb::scene::ParticleStableId emitterId = 0U;
    std::uint32_t authoringOrder = 0U;
    std::string name;
    bool enabled = false;
    bool selected = false;
};

class ParticleEmitterListModel final {
public:
    ParticleEmitterListModel() = delete;

    [[nodiscard]] static std::vector<ParticleEmitterListRow> Build(
        const kb::scene::ParticleEffectAsset& asset,
        kb::scene::ParticleStableId selectedEmitterId);
    [[nodiscard]] static const kb::scene::ParticleEmitterAsset* Find(
        const kb::scene::ParticleEffectAsset& asset,
        kb::scene::ParticleStableId emitterId) noexcept;
};

} // namespace kb::particle_editor
