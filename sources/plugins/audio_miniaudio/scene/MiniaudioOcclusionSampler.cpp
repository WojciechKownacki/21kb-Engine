#include "scene/MiniaudioOcclusionSampler.hpp"

#include "engine/library/EngineLibraryCollections.hpp"
#include "engine/scene/PhysicsBackend.hpp"

#include <array>
#include <cmath>

namespace kb::audio_miniaudio {
namespace {

// A ray that reaches within this distance of the source counts as arrived - keeps the
// source's own surface (when it has no collider to exclude) from registering as an
// occluder.
constexpr float kArrivalEpsilon = 0.05F;

} // namespace

void MiniaudioOcclusionSampler::BeginTick(const kb::scene::AudioOcclusionSettings& settings) noexcept {
    seenThisTick_.clear();
    budgetLeft_ = settings.maxRaycastsPerTick;
    // Rotating fairness: when last tick had more requests than budget, later ticks start
    // sampling further into the request order, so every source is refreshed eventually.
    if (requestsLastTick_ > settings.maxRaycastsPerTick && settings.maxRaycastsPerTick > 0U) {
        skipLeft_ = static_cast<std::uint32_t>((tickIndex_ * settings.maxRaycastsPerTick) % requestsLastTick_);
    } else {
        skipLeft_ = 0U;
    }
    requestsLastTick_ = requestsThisTick_;
    requestsThisTick_ = 0U;
    raycastsThisTick_ = 0U;
    occludedSamplesThisTick_ = 0U;
    ++tickIndex_;
}

void MiniaudioOcclusionSampler::EndTick() noexcept {
    for (auto iterator = lastScale_.begin(); iterator != lastScale_.end();) {
        if (seenThisTick_.contains(iterator->first)) {
            ++iterator;
        } else {
            iterator = lastScale_.erase(iterator);
        }
    }
}

float MiniaudioOcclusionSampler::Sample(
    kb::scene::Scene& scene,
    const kb::scene::AudioOcclusionSettings& settings,
    MiniaudioOcclusionKey key,
    const kb::scene::Vec3& listenerPosition,
    const kb::scene::Vec3& sourcePosition,
    std::uint64_t excludeEntityId) {
    ++requestsThisTick_;
    seenThisTick_.insert(key);

    const kb::scene::Vec3 toSource = sourcePosition - listenerPosition;
    const float distance = kb::math::Length(toSource);
    if (!std::isfinite(distance) || distance <= kArrivalEpsilon || distance > settings.maxDistance) {
        // Trivially clear (or out of occlusion range) - no ray spent, cache updated.
        lastScale_[key] = 1.0F;
        return 1.0F;
    }
    if (skipLeft_ > 0U) {
        --skipLeft_;
        return CachedOr(key, 1.0F);
    }
    if (budgetLeft_ == 0U) {
        return CachedOr(key, 1.0F);
    }
    --budgetLeft_;
    ++raycastsThisTick_;

    std::array<kb::scene::PhysicsCastResult, 8U> storage{};
    kb::library::ArrayNonAlloc<kb::scene::PhysicsCastResult> results{ std::span<kb::scene::PhysicsCastResult>{ storage } };
    kb::scene::RaycastAllNonAlloc(scene, listenerPosition, toSource * (1.0F / distance), distance - kArrivalEpsilon, settings.layerMask, results);
    bool occluded = false;
    for (std::size_t index = 0U; index < results.Count(); ++index) {
        if (storage[index].hit && storage[index].entity.Id() != excludeEntityId) {
            occluded = true;
            break;
        }
    }
    const float scale = occluded ? settings.occludedVolumeScale : 1.0F;
    if (occluded) {
        ++occludedSamplesThisTick_;
    }
    lastScale_[key] = scale;
    return scale;
}

kb::scene::AudioOcclusionRuntimeStats
MiniaudioOcclusionSampler::RuntimeStats() const noexcept {
    return kb::scene::AudioOcclusionRuntimeStats{
        .sampleRequests = requestsThisTick_,
        .raycasts = raycastsThisTick_,
        .occludedSamples = occludedSamplesThisTick_};
}

void MiniaudioOcclusionSampler::Clear() noexcept {
    lastScale_.clear();
    seenThisTick_.clear();
    budgetLeft_ = 0U;
    skipLeft_ = 0U;
    requestsThisTick_ = 0U;
    requestsLastTick_ = 0U;
    raycastsThisTick_ = 0U;
    occludedSamplesThisTick_ = 0U;
}

float MiniaudioOcclusionSampler::CachedOr(MiniaudioOcclusionKey key, float fallback) const noexcept {
    const auto iterator = lastScale_.find(key);
    return iterator == lastScale_.end() ? fallback : iterator->second;
}

} // namespace kb::audio_miniaudio
