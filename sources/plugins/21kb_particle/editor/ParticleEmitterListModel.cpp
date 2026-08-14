#include "ParticleEmitterListModel.hpp"

#include <algorithm>

namespace kb::particle_editor {

std::vector<ParticleEmitterListRow> ParticleEmitterListModel::Build(
    const kb::scene::ParticleEffectAsset& asset,
    kb::scene::ParticleStableId selectedEmitterId) {
    std::vector<ParticleEmitterListRow> rows;
    rows.reserve(asset.emitters.size());
    for (const kb::scene::ParticleEmitterAsset& emitter : asset.emitters) {
        rows.push_back({.emitterId = emitter.emitterId,
                        .authoringOrder = emitter.authoringOrder,
                        .name = emitter.name,
                        .enabled = emitter.enabled,
                        .selected = emitter.emitterId == selectedEmitterId});
    }
    std::sort(rows.begin(), rows.end(), [](const ParticleEmitterListRow& left,
                                           const ParticleEmitterListRow& right) {
        return left.authoringOrder < right.authoringOrder;
    });
    return rows;
}

const kb::scene::ParticleEmitterAsset* ParticleEmitterListModel::Find(
    const kb::scene::ParticleEffectAsset& asset,
    kb::scene::ParticleStableId emitterId) noexcept {
    const auto found = std::find_if(asset.emitters.begin(), asset.emitters.end(),
        [emitterId](const kb::scene::ParticleEmitterAsset& emitter) {
            return emitter.emitterId == emitterId;
        });
    return found == asset.emitters.end() ? nullptr : &*found;
}

} // namespace kb::particle_editor
