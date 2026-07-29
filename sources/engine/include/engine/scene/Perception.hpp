#pragma once

#include "engine/library/EngineLibraryCollections.hpp"
#include "engine/scene/PhysicsBackend.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace kb::scene {

// Perception intentionally consumes caller-owned snapshots and output storage.
// A scene/gameplay system chooses how teams are persisted and creates those
// snapshots; this header keeps a perception tick deterministic, bounded and
// allocation-free even when it is called for many agents.
using PerceptionTeamId = std::uint32_t;
inline constexpr PerceptionTeamId kNoPerceptionTeam = 0U;

enum class PerceptionSense : std::uint8_t {
    Sight,
    Hearing,
    Proximity,
};

struct PerceptionFilter {
    PerceptionTeamId observerTeam = kNoPerceptionTeam;
    PerceptionTeamId requiredTargetTeam = kNoPerceptionTeam;
    std::uint32_t layerMask = kPhysicsAllLayers;
    std::uint32_t maxResults = 16U;
    float range = 20.0F;
    bool requireLineOfSight = false;
    bool includeSameTeam = false;

    [[nodiscard]] bool AcceptsTeam(PerceptionTeamId targetTeam) const noexcept {
        if (requiredTargetTeam != kNoPerceptionTeam && targetTeam != requiredTargetTeam) return false;
        return includeSameTeam || observerTeam == kNoPerceptionTeam || targetTeam != observerTeam;
    }

    [[nodiscard]] bool IsValid() const noexcept {
        return maxResults != 0U && range > 0.0F && std::isfinite(range) && layerMask != 0U;
    }
};

struct PerceptionObserver {
    SceneEntity entity{};
    Vec3 position{};
    // Only the horizontal XZ projection is used for sight. A zero vector is
    // invalid for sight but still permits hearing and proximity.
    Vec3 forward{ 0.0F, 0.0F, 1.0F };
    float sightHalfAngleDegrees = 60.0F;
    PerceptionFilter filter{};
};

struct PerceptionTarget {
    SceneEntity entity{};
    Vec3 position{};
    PerceptionTeamId team = kNoPerceptionTeam;
    std::uint32_t layerMask = kPhysicsAllLayers;
};

// A hearing producer emits this once (for example on a weapon/fire/impact).
// It is data, not an allocating event-bus message: the owning gameplay system
// can retain a fixed ring buffer for the current simulation tick.
struct PerceptionStimulus {
    SceneEntity source{};
    Vec3 position{};
    PerceptionTeamId team = kNoPerceptionTeam;
    std::uint32_t layerMask = kPhysicsAllLayers;
    float radius = 10.0F;
    float strength = 1.0F;
};

struct PerceptionEvent {
    PerceptionSense sense = PerceptionSense::Sight;
    SceneEntity observer{};
    SceneEntity subject{};
    Vec3 position{};
    float distance = 0.0F;
    // Strength is the producer strength for hearing and 1.0 for sight/proximity.
    float strength = 1.0F;
};

struct PerceptionEvaluationResult {
    std::uint32_t emitted = 0U;
    // True means that the configured output limit was reached; callers that
    // need every candidate must increase maxResults and their buffer capacity.
    bool limitReached = false;
};

// Optional sight callback. It is invoked only after inexpensive team, layer,
// distance and field-of-view tests pass. Returning false blocks sight.
using PerceptionVisibilityTest = bool (*)(void* context, const PerceptionObserver& observer, const PerceptionTarget& target) noexcept;

struct PerceptionVisibilityQuery {
    void* context = nullptr;
    PerceptionVisibilityTest test = nullptr;
};

namespace detail {

[[nodiscard]] inline bool IsFinite(Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] inline float DistanceSquared(Vec3 first, Vec3 second) noexcept {
    const float x = first.x - second.x;
    const float y = first.y - second.y;
    const float z = first.z - second.z;
    return x * x + y * y + z * z;
}

[[nodiscard]] inline bool SharesLayer(std::uint32_t first, std::uint32_t second) noexcept {
    return (first & second) != 0U;
}

[[nodiscard]] inline bool IsVisibleInFieldOfView(const PerceptionObserver& observer, Vec3 targetPosition) noexcept {
    if (!std::isfinite(observer.sightHalfAngleDegrees) || observer.sightHalfAngleDegrees < 0.0F || observer.sightHalfAngleDegrees > 180.0F) return false;
    if (observer.sightHalfAngleDegrees >= 180.0F) return true;

    const float forwardLengthSquared = observer.forward.x * observer.forward.x + observer.forward.z * observer.forward.z;
    const float deltaX = targetPosition.x - observer.position.x;
    const float deltaZ = targetPosition.z - observer.position.z;
    const float targetLengthSquared = deltaX * deltaX + deltaZ * deltaZ;
    if (forwardLengthSquared <= std::numeric_limits<float>::epsilon() || targetLengthSquared <= std::numeric_limits<float>::epsilon()) return false;

    const float cosine = (observer.forward.x * deltaX + observer.forward.z * deltaZ) /
        std::sqrt(forwardLengthSquared * targetLengthSquared);
    constexpr float kPi = 3.14159265358979323846F;
    return cosine >= std::cos(observer.sightHalfAngleDegrees * (kPi / 180.0F));
}

[[nodiscard]] inline bool AcceptsTarget(const PerceptionObserver& observer, const PerceptionTarget& target, float& distance) noexcept {
    if (!target.entity.IsValid() || target.entity == observer.entity || !IsFinite(target.position) ||
        !observer.filter.AcceptsTeam(target.team) || !SharesLayer(observer.filter.layerMask, target.layerMask)) return false;
    const float distanceSquared = DistanceSquared(observer.position, target.position);
    const float rangeSquared = observer.filter.range * observer.filter.range;
    if (!std::isfinite(distanceSquared) || distanceSquared > rangeSquared) return false;
    distance = std::sqrt(distanceSquared);
    return true;
}

} // namespace detail

// Evaluates all three senses. Output is cleared first and then emitted in a
// stable order (subject entity id, then sight/hearing/proximity). The bounded
// selection intentionally trades O(candidateCount * maxResults) comparisons
// for zero allocations and no dependence on input iteration order.
inline PerceptionEvaluationResult EvaluatePerception(
    const PerceptionObserver& observer,
    std::span<const PerceptionTarget> targets,
    std::span<const PerceptionStimulus> stimuli,
    PerceptionVisibilityQuery visibility,
    kb::library::ArrayNonAlloc<PerceptionEvent>& output) noexcept {
    output.Clear();
    if (!observer.entity.IsValid() || !detail::IsFinite(observer.position) || !observer.filter.IsValid()) return {};

    const std::uint32_t limit = static_cast<std::uint32_t>(std::min<std::size_t>(observer.filter.maxResults, output.Capacity()));
    PerceptionEvaluationResult result{};
    if (limit == 0U) return result;

    std::uint64_t previousSubjectId = 0U;
    std::uint8_t previousSense = 0U;
    bool hasPrevious = false;
    while (result.emitted < limit) {
        PerceptionEvent selected{};
        std::uint64_t selectedSubjectId = std::numeric_limits<std::uint64_t>::max();
        std::uint8_t selectedSense = std::numeric_limits<std::uint8_t>::max();
        bool found = false;
        const auto consider = [&](PerceptionEvent candidate) {
            const std::uint64_t subjectId = candidate.subject.Id();
            const std::uint8_t sense = static_cast<std::uint8_t>(candidate.sense);
            if (hasPrevious && (subjectId < previousSubjectId || (subjectId == previousSubjectId && sense <= previousSense))) return;
            if (!found || subjectId < selectedSubjectId || (subjectId == selectedSubjectId && sense < selectedSense)) {
                selected = candidate;
                selectedSubjectId = subjectId;
                selectedSense = sense;
                found = true;
            }
        };

        for (const PerceptionTarget& target : targets) {
            float distance = 0.0F;
            if (!detail::AcceptsTarget(observer, target, distance)) continue;
            if (detail::IsVisibleInFieldOfView(observer, target.position) &&
                (!observer.filter.requireLineOfSight || (visibility.test != nullptr && visibility.test(visibility.context, observer, target)))) {
                consider(PerceptionEvent{ .sense = PerceptionSense::Sight, .observer = observer.entity, .subject = target.entity,
                    .position = target.position, .distance = distance });
            }
            consider(PerceptionEvent{ .sense = PerceptionSense::Proximity, .observer = observer.entity, .subject = target.entity,
                .position = target.position, .distance = distance });
        }
        for (const PerceptionStimulus& stimulus : stimuli) {
            if (!stimulus.source.IsValid() || stimulus.source == observer.entity || !detail::IsFinite(stimulus.position) ||
                !std::isfinite(stimulus.radius) || !std::isfinite(stimulus.strength) || stimulus.radius <= 0.0F || stimulus.strength < 0.0F ||
                !observer.filter.AcceptsTeam(stimulus.team) || !detail::SharesLayer(observer.filter.layerMask, stimulus.layerMask)) continue;
            const float distanceSquared = detail::DistanceSquared(observer.position, stimulus.position);
            const float hearingRange = std::min(observer.filter.range, stimulus.radius);
            if (!std::isfinite(distanceSquared) || distanceSquared > hearingRange * hearingRange) continue;
            consider(PerceptionEvent{ .sense = PerceptionSense::Hearing, .observer = observer.entity, .subject = stimulus.source,
                .position = stimulus.position, .distance = std::sqrt(distanceSquared), .strength = stimulus.strength });
        }

        if (!found || !output.PushBack(selected)) break;
        ++result.emitted;
        previousSubjectId = selectedSubjectId;
        previousSense = selectedSense;
        hasPrevious = true;
    }
    result.limitReached = result.emitted == limit;
    return result;
}

// Physics-backed candidate collection for proximity. The engine backend writes
// directly into caller storage; resolve returned entities to PerceptionTarget
// snapshots (including team) before calling EvaluatePerception.
inline void QueryPhysicsProximity(
    Scene& scene,
    Vec3 center,
    const PerceptionFilter& filter,
    kb::library::ArrayNonAlloc<PhysicsOverlapResult>& results) noexcept {
    results.Clear();
    if (!filter.IsValid() || !detail::IsFinite(center)) return;
    PhysicsBackend::OverlapShapeAll(scene, PhysicsShapeDesc{ .kind = PhysicsShapeKind::Sphere, .radius = filter.range }, center, filter.layerMask, results);
}

// Ready-to-use visibility callback for PerceptionVisibilityQuery. The supplied
// hit buffer is caller-owned and reset for each ray; the first non-observer
// collider blocks sight unless it belongs to the target.
struct PhysicsPerceptionVisibilityContext {
    Scene* scene = nullptr;
    kb::library::ArrayNonAlloc<PhysicsCastResult>* hits = nullptr;
};

inline bool PhysicsPerceptionVisibilityTest(void* context, const PerceptionObserver& observer, const PerceptionTarget& target) noexcept {
    auto* physics = static_cast<PhysicsPerceptionVisibilityContext*>(context);
    if (physics == nullptr || physics->scene == nullptr || physics->hits == nullptr) return false;
    const float distanceSquared = detail::DistanceSquared(observer.position, target.position);
    if (distanceSquared <= std::numeric_limits<float>::epsilon() || !std::isfinite(distanceSquared)) return true;
    const float distance = std::sqrt(distanceSquared);
    const Vec3 direction{
        (target.position.x - observer.position.x) / distance,
        (target.position.y - observer.position.y) / distance,
        (target.position.z - observer.position.z) / distance,
    };
    physics->hits->Clear();
    RaycastAllNonAlloc(*physics->scene, observer.position, direction, distance, observer.filter.layerMask, *physics->hits);
    for (const PhysicsCastResult& hit : *physics->hits) {
        if (!hit.hit || hit.entity == observer.entity) continue;
        return hit.entity == target.entity;
    }
    // A target need not have a collider; no intervening collider is clear LOS.
    return true;
}

} // namespace kb::scene
