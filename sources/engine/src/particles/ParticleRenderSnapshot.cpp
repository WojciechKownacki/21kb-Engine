#include "engine/particles/ParticleRenderSnapshot.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>

namespace kb::particles {
namespace {

[[nodiscard]] constexpr std::size_t AlignUp(std::size_t value, std::size_t alignment) noexcept {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

[[nodiscard]] bool IsFinite(const kb::math::Vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

template <typename Enum>
[[nodiscard]] constexpr bool EnumAtMost(Enum value, Enum maximum) noexcept {
    return static_cast<std::underlying_type_t<Enum>>(value) <=
        static_cast<std::underlying_type_t<Enum>>(maximum);
}

[[nodiscard]] bool ValidateEmitterRecords(
    std::span<const ParticleRenderEmitterRecord> emitters,
    std::size_t particleCount) noexcept {
    if (emitters.size() > kParticleRenderSnapshotMaxEmitterRecords) return false;
    std::size_t expectedFirst = 0U;
    for (const ParticleRenderEmitterRecord& emitter : emitters) {
        if (emitter.instanceId == 0U || emitter.effectAssetId == 0U || emitter.emitterId == 0U ||
            emitter.assetGeneration == 0U || emitter.materialAssetId == 0U ||
            emitter.liveParticleCount != emitter.particleCount || emitter.firstParticle != expectedFirst ||
            !EnumAtMost(emitter.output, ParticleRenderOutput::Volumetric) ||
            !EnumAtMost(emitter.blend, ParticleRenderBlendMode::Premultiplied) ||
            !EnumAtMost(emitter.depth, ParticleRenderDepthMode::ReadWrite) ||
            !EnumAtMost(emitter.sort, ParticleRenderSortMode::Age) ||
            !EnumAtMost(emitter.status, ParticleRenderEmitterStatus::Stopped) ||
            !EnumAtMost(emitter.droppedReason, ParticleRenderDropReason::SnapshotBudget) ||
            !EnumAtMost(emitter.alignment, ParticleRenderAlignment::Local) ||
            (static_cast<std::uint8_t>(emitter.flags) &
                ~static_cast<std::uint8_t>(ParticleRenderEmitterFlag::SoftParticles |
                    ParticleRenderEmitterFlag::AntiAliasing)) != 0U ||
            !std::isfinite(emitter.pointSpriteDiameter) || emitter.pointSpriteDiameter <= 0.0F ||
            ((emitter.droppedParticleCount == 0U) !=
                (emitter.droppedReason == ParticleRenderDropReason::None)) ||
            !IsFinite(emitter.boundsMinimum) || !IsFinite(emitter.boundsMaximum) ||
            emitter.boundsMinimum.x > emitter.boundsMaximum.x ||
            emitter.boundsMinimum.y > emitter.boundsMaximum.y ||
            emitter.boundsMinimum.z > emitter.boundsMaximum.z) {
            return false;
        }
        if (emitter.particleCount > particleCount - expectedFirst) return false;
        expectedFirst += emitter.particleCount;
    }
    return expectedFirst == particleCount;
}

[[nodiscard]] bool ValidateParticleRecords(std::span<const ParticleRenderRecord> particles) noexcept {
    for (const ParticleRenderRecord& particle : particles) {
        if (particle.particleId == 0U || !IsFinite(particle.position) || !IsFinite(particle.previousPosition) ||
            !IsFinite(particle.velocity) || !std::isfinite(particle.size) || particle.size < 0.0F ||
            !std::isfinite(particle.rotationRadians) || !std::isfinite(particle.stretch) ||
            particle.stretch < 0.0F) {
            return false;
        }
    }
    return true;
}

} // namespace

struct ParticleRenderSnapshot::Storage {
    ParticleRenderSnapshotHeader header{};
    std::unique_ptr<std::byte[]> payload;
    std::size_t emitterCount = 0U;
    std::size_t particleOffset = 0U;
    std::size_t particleCount = 0U;
};

struct ParticleRenderSnapshotChannel::Impl {
    enum class LatestKind : std::uint8_t { None, Payload, Terminal };

    mutable std::mutex mutex;
    std::array<std::shared_ptr<ParticleRenderSnapshot>, kParticleRenderSnapshotSlotCount> payloadSlots;
    std::array<std::shared_ptr<ParticleRenderSnapshot>, kParticleRenderSnapshotSlotCount> terminalSlots;
    LatestKind latestKind = LatestKind::None;
    std::size_t latestSlot = 0U;
    std::uint64_t sceneId = 0U;
    bool warmedUp = false;
};

ParticleRenderSnapshot::ParticleRenderSnapshot()
    : storage_(std::make_unique<Storage>()) {}

ParticleRenderSnapshot::~ParticleRenderSnapshot() = default;

const ParticleRenderSnapshotHeader& ParticleRenderSnapshot::Header() const noexcept { return storage_->header; }
std::uint64_t ParticleRenderSnapshot::Revision() const noexcept { return storage_->header.revision; }
std::uint64_t ParticleRenderSnapshot::SceneId() const noexcept { return storage_->header.sceneId; }
std::uint64_t ParticleRenderSnapshot::BackendEpoch() const noexcept { return storage_->header.backendEpoch; }
std::uint64_t ParticleRenderSnapshot::FixedStepIndex() const noexcept { return storage_->header.fixedStepIndex; }
bool ParticleRenderSnapshot::IsTombstone() const noexcept { return storage_->header.tombstone; }
std::span<const ParticleRenderEmitterRecord> ParticleRenderSnapshot::Emitters() const noexcept {
    return {reinterpret_cast<const ParticleRenderEmitterRecord*>(storage_->payload.get()), storage_->emitterCount};
}
std::span<const ParticleRenderRecord> ParticleRenderSnapshot::Particles() const noexcept {
    if (!storage_->payload) return {};
    return {reinterpret_cast<const ParticleRenderRecord*>(storage_->payload.get() + storage_->particleOffset),
        storage_->particleCount};
}

ParticleRenderSnapshotChannel::ParticleRenderSnapshotChannel()
    : impl_(std::make_unique<Impl>()) {}

ParticleRenderSnapshotChannel::~ParticleRenderSnapshotChannel() = default;

ParticleRenderSnapshotResult ParticleRenderSnapshotChannel::Warmup(std::uint64_t sceneId) noexcept {
    if (sceneId == 0U) return {ParticleRenderSnapshotStatus::InvalidSnapshot};
    std::lock_guard lock{impl_->mutex};
    if (impl_->warmedUp) {
        return {impl_->sceneId == sceneId
            ? ParticleRenderSnapshotStatus::Success
            : ParticleRenderSnapshotStatus::InvalidSnapshot};
    }
    try {
        std::array<std::shared_ptr<ParticleRenderSnapshot>, kParticleRenderSnapshotSlotCount> payloadSlots;
        std::array<std::shared_ptr<ParticleRenderSnapshot>, kParticleRenderSnapshotSlotCount> terminalSlots;
        for (std::shared_ptr<ParticleRenderSnapshot>& slot : payloadSlots) {
            slot = std::make_shared<ParticleRenderSnapshot>();
            slot->storage_->payload = std::make_unique_for_overwrite<std::byte[]>(kParticleRenderSnapshotBytesPerSlot);
        }
        for (std::shared_ptr<ParticleRenderSnapshot>& slot : terminalSlots) {
            slot = std::make_shared<ParticleRenderSnapshot>();
        }
        impl_->payloadSlots = std::move(payloadSlots);
        impl_->terminalSlots = std::move(terminalSlots);
        impl_->sceneId = sceneId;
        impl_->warmedUp = true;
        return {ParticleRenderSnapshotStatus::Success};
    } catch (const std::bad_alloc&) {
        return {ParticleRenderSnapshotStatus::AllocationFailed};
    }
}

bool ParticleRenderSnapshotChannel::IsWarmedUp() const noexcept {
    std::lock_guard lock{impl_->mutex};
    return impl_->warmedUp;
}

ParticleRenderSnapshotResult ParticleRenderSnapshotChannel::Publish(
    std::uint64_t backendEpoch,
    const ParticleRenderSnapshotPublishDesc& desc) noexcept {
    std::lock_guard lock{impl_->mutex};
    if (!impl_->warmedUp) return {ParticleRenderSnapshotStatus::NotWarmed};
    if (backendEpoch == 0U || desc.revision == 0U ||
        (desc.tombstone && (!desc.emitters.empty() || !desc.particles.empty()))) {
        return {ParticleRenderSnapshotStatus::InvalidSnapshot};
    }

    const std::size_t emitterBytes = desc.emitters.size_bytes();
    const std::size_t particleOffset = AlignUp(emitterBytes, alignof(ParticleRenderRecord));
    if (particleOffset > kParticleRenderSnapshotBytesPerSlot ||
        desc.particles.size_bytes() > kParticleRenderSnapshotBytesPerSlot - particleOffset) {
        return {ParticleRenderSnapshotStatus::SnapshotTooLarge};
    }
    if ((!desc.tombstone && !ValidateEmitterRecords(desc.emitters, desc.particles.size())) ||
        !ValidateParticleRecords(desc.particles)) {
        return {ParticleRenderSnapshotStatus::InvalidSnapshot};
    }
    const auto latestSnapshot = [&]() -> const std::shared_ptr<ParticleRenderSnapshot>* {
        if (impl_->latestKind == Impl::LatestKind::Payload) return &impl_->payloadSlots[impl_->latestSlot];
        if (impl_->latestKind == Impl::LatestKind::Terminal) return &impl_->terminalSlots[impl_->latestSlot];
        return nullptr;
    }();
    if (latestSnapshot != nullptr) {
        const ParticleRenderSnapshotHeader& latest = (*latestSnapshot)->storage_->header;
        if (desc.revision <= latest.revision || desc.fixedStepIndex < latest.fixedStepIndex) {
            return {ParticleRenderSnapshotStatus::StaleRevision};
        }
    }

    auto& candidateSlots = desc.tombstone ? impl_->terminalSlots : impl_->payloadSlots;
    std::size_t freeSlot = candidateSlots.size();
    for (std::size_t index = 0U; index < candidateSlots.size(); ++index) {
        if (candidateSlots[index].use_count() == 1L) {
            freeSlot = index;
            break;
        }
    }
    if (freeSlot == candidateSlots.size()) return {ParticleRenderSnapshotStatus::SnapshotBackpressure};

    ParticleRenderSnapshot::Storage& destination = *candidateSlots[freeSlot]->storage_;
    if (emitterBytes != 0U) std::memcpy(destination.payload.get(), desc.emitters.data(), emitterBytes);
    if (!desc.particles.empty()) {
        std::memcpy(destination.payload.get() + particleOffset, desc.particles.data(), desc.particles.size_bytes());
    }
    destination.emitterCount = desc.emitters.size();
    destination.particleOffset = particleOffset;
    destination.particleCount = desc.particles.size();
    destination.header = {
        .revision = desc.revision,
        .sceneId = impl_->sceneId,
        .backendEpoch = backendEpoch,
        .fixedStepIndex = desc.fixedStepIndex,
        .tombstone = desc.tombstone,
    };
    impl_->latestKind = desc.tombstone ? Impl::LatestKind::Terminal : Impl::LatestKind::Payload;
    impl_->latestSlot = freeSlot;
    return {ParticleRenderSnapshotStatus::Success};
}

std::shared_ptr<const ParticleRenderSnapshot> ParticleRenderSnapshotChannel::Read() const noexcept {
    std::lock_guard lock{impl_->mutex};
    if (impl_->latestKind == Impl::LatestKind::Payload) return impl_->payloadSlots[impl_->latestSlot];
    if (impl_->latestKind == Impl::LatestKind::Terminal) return impl_->terminalSlots[impl_->latestSlot];
    return {};
}

} // namespace kb::particles
