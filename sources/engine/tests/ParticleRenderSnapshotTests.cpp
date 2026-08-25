#include "engine/particles/ParticlePlayback.hpp"
#include "engine/scene/Scene.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_countAllocations{false};
std::atomic<std::size_t> g_allocationCount{0U};

void Require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

class TestBackend final : public kb::particles::IParticleSimulationBackend {
public:
    explicit TestBackend(std::atomic<std::uint32_t>* destructionCount = nullptr) noexcept
        : destructionCount_(destructionCount) {}
    ~TestBackend() override {
        if (destructionCount_ != nullptr) destructionCount_->fetch_add(1U, std::memory_order_relaxed);
    }

    kb::particles::ParticleRuntimeResult Create(
        kb::scene::Scene&, std::uint64_t, kb::scene::SceneEntity) override { return Success(); }
    kb::particles::ParticleRuntimeResult Release(kb::scene::Scene&, std::uint64_t) noexcept override { return Success(); }
    kb::particles::ParticleRuntimeResult Play(kb::scene::Scene&, std::uint64_t) noexcept override { return Success(); }
    kb::particles::ParticleRuntimeResult Pause(kb::scene::Scene&, std::uint64_t) noexcept override { return Success(); }
    kb::particles::ParticleRuntimeResult Stop(kb::scene::Scene&, std::uint64_t) noexcept override { return Success(); }
    kb::particles::ParticleRuntimeResult Restart(kb::scene::Scene&, std::uint64_t) noexcept override { return Success(); }
    kb::particles::ParticleRuntimeResult SetSeed(kb::scene::Scene&, std::uint64_t, std::uint64_t) noexcept override {
        return Success();
    }
    kb::particles::ParticleRuntimeResult SetParameterScalar(
        kb::scene::Scene&, std::uint64_t, std::string_view, float) noexcept override { return Success(); }
    kb::particles::ParticleRuntimeResult ClearParameter(
        kb::scene::Scene&, std::uint64_t, std::string_view) noexcept override { return Success(); }
    kb::particles::ParticleRuntimeResult Emit(kb::scene::Scene&, std::uint64_t, std::uint32_t) override {
        return Success();
    }
    kb::particles::ParticleRuntimeQueryResult Query(
        const kb::scene::Scene&, std::uint64_t) const noexcept override {
        return {.status = kb::particles::ParticleRuntimeStatus::Success};
    }
    std::size_t CopyLiveInstanceIds(
        const kb::scene::Scene&, std::span<std::uint64_t>) const noexcept override { return 0U; }
    std::size_t CopyLiveParticleStates(
        const kb::scene::Scene&,
        std::uint64_t,
        std::span<kb::particles::ParticleRuntimeState>) const noexcept override { return 0U; }

private:
    [[nodiscard]] static kb::particles::ParticleRuntimeResult Success() noexcept {
        return {.status = kb::particles::ParticleRuntimeStatus::Success};
    }

    std::atomic<std::uint32_t>* destructionCount_ = nullptr;
};

[[nodiscard]] kb::particles::ParticleRenderRecord Record(std::uint64_t identity) noexcept {
    return {
        .position = {static_cast<float>(identity), 2.0F, 3.0F},
        .size = 4.0F,
        .previousPosition = {static_cast<float>(identity) - 0.25F, 1.75F, 2.75F},
        .rotationRadians = 0.5F,
        .velocity = {5.0F, 6.0F, 7.0F},
        .stretch = 1.5F,
        .particleId = identity,
        .spawnOrdinal = identity + 100U,
        .packedColor = 0xA1B2C3D4U,
        .ribbonGroup = 23U,
        .frame = 17U,
        .normalizedAgeUnorm = 49'152U,
    };
}

[[nodiscard]] kb::particles::ParticleRenderEmitterRecord Emitter(
    std::uint64_t identity,
    std::uint32_t first,
    std::uint32_t count) noexcept {
    return {
        .instanceId = identity,
        .effectAssetId = 1000U + identity,
        .emitterId = 2000U + identity,
        .assetGeneration = 9U,
        .materialAssetId = 3000U + identity,
        .meshAssetId = 4000U + identity,
        .textureAtlasAssetId = 5000U + identity,
        .firstParticle = first,
        .particleCount = count,
        .liveParticleCount = count,
        .rejectedByCapacity = 2U,
        .rejectedBySpawnBudget = 3U,
        .rejectedByEventBudget = 4U,
        .droppedParticleCount = 9U,
        .output = kb::particles::ParticleRenderOutput::Mesh,
        .blend = kb::particles::ParticleRenderBlendMode::Add,
        .depth = kb::particles::ParticleRenderDepthMode::ReadWrite,
        .sort = kb::particles::ParticleRenderSortMode::Age,
        .status = kb::particles::ParticleRenderEmitterStatus::Playing,
        .droppedReason = kb::particles::ParticleRenderDropReason::EventBudget,
        .alignment = kb::particles::ParticleRenderAlignment::Local,
        .flags = kb::particles::ParticleRenderEmitterFlag::SoftParticles |
            kb::particles::ParticleRenderEmitterFlag::AntiAliasing |
            kb::particles::ParticleRenderEmitterFlag::CastsShadow |
            kb::particles::ParticleRenderEmitterFlag::ReceivesShadow,
        .backendPolicy = kb::scene::ParticleBackendPolicy::GpuVisualRequired,
        .flipbookColumnsEncoded = 8U,
        .flipbookRowsEncoded = 4U,
        .localBasisQuaternionSnorm = {0, 16'384, 0, 28'377},
        .pointSpriteDiameter = 0.75F,
        .meshLodLevel = -3,
        .trailSampleIntervalSeconds = 0.05F,
        .trailMinimumDistance = 0.25F,
        .trailMaxSamplesPerParticle = 31U,
        .trailWidth = 0.5F,
        .ribbonMaxSegments = 63U,
        .ribbonWidth = 0.75F,
        .ribbonBreakOnDeath = false,
        .beamLocalEnd = {1.0F, 2.0F, 3.0F},
        .beamSegments = 15U,
        .beamWidth = 0.8F,
        .beamNoiseAmplitude = 0.3F,
        .beamNoiseFrequency = 2.0F,
        .volumetricDensity = 0.6F,
        .volumetricRadiusScale = 1.25F,
        .volumetricLowQualitySteps = 8U,
        .volumetricHighQualitySteps = 32U,
        .outputOrigin = {4.0F, 5.0F, 6.0F},
        .beamEnd = {7.0F, 8.0F, 9.0F},
        .boundsMinimum = {-10.0F, -20.0F, -30.0F},
        .boundsMaximum = {10.0F, 20.0F, 30.0F},
    };
}

void TestCompleteContractAndMalformedRanges() {
    static_assert(sizeof(kb::particles::ParticleRenderRecord) == 80U);
    static_assert(kb::particles::kParticleRenderSnapshotSlotCount == 4U);
    static_assert(kb::particles::kParticleRenderSnapshotBytesPerSlot == 16U * 1024U * 1024U);
    static_assert(kb::particles::kParticleRenderSnapshotBytesPerSlot *
            kb::particles::kParticleRenderSnapshotSlotCount ==
        kb::particles::kParticleRenderSnapshotRetainedPayloadBytes);
    static_assert(kb::particles::kParticleRenderSnapshotMaxEmitterRecords ==
        kb::scene::kParticleEffectMaxInstancesPerScene * kb::scene::kParticleEffectMaxEmitters);

    kb::particles::ParticleRenderSnapshotChannel channel;
    Require(channel.Warmup(41U).Succeeded(), "snapshot contract fixture warmup failed");
    const std::array particles{Record(11U), Record(12U)};
    const std::array emitters{Emitter(7U, 0U, 1U), Emitter(8U, 1U, 1U)};
    Require(channel.Publish(3U, {
                .revision = 1U, .fixedStepIndex = 17U, .emitters = emitters, .particles = particles}).Succeeded(),
        "complete render snapshot contract was rejected");
    const auto snapshot = channel.Read();
    Require(snapshot && snapshot->Header().revision == 1U && snapshot->Header().sceneId == 41U &&
            snapshot->Header().backendEpoch == 3U && snapshot->Header().fixedStepIndex == 17U &&
            !snapshot->Header().tombstone && snapshot->Emitters().size() == 2U &&
            snapshot->Particles().size() == 2U,
        "snapshot header or batch ranges were not retained");
    const auto& emitter = snapshot->Emitters()[1];
    Require(emitter.instanceId == 8U && emitter.effectAssetId == 1008U && emitter.emitterId == 2008U &&
            emitter.assetGeneration == 9U && emitter.materialAssetId == 3008U &&
            emitter.meshAssetId == 4008U && emitter.textureAtlasAssetId == 5008U &&
            emitter.firstParticle == 1U && emitter.particleCount == 1U && emitter.liveParticleCount == 1U &&
            emitter.rejectedByCapacity == 2U && emitter.rejectedBySpawnBudget == 3U &&
            emitter.rejectedByEventBudget == 4U && emitter.droppedParticleCount == 9U &&
            emitter.output == kb::particles::ParticleRenderOutput::Mesh &&
            emitter.blend == kb::particles::ParticleRenderBlendMode::Add &&
            emitter.depth == kb::particles::ParticleRenderDepthMode::ReadWrite &&
            emitter.sort == kb::particles::ParticleRenderSortMode::Age &&
            emitter.status == kb::particles::ParticleRenderEmitterStatus::Playing &&
            emitter.droppedReason == kb::particles::ParticleRenderDropReason::EventBudget &&
            emitter.alignment == kb::particles::ParticleRenderAlignment::Local &&
            emitter.backendPolicy == kb::scene::ParticleBackendPolicy::GpuVisualRequired &&
            kb::particles::HasParticleRenderEmitterFlag(
                emitter.flags, kb::particles::ParticleRenderEmitterFlag::SoftParticles) &&
            kb::particles::HasParticleRenderEmitterFlag(
                emitter.flags, kb::particles::ParticleRenderEmitterFlag::AntiAliasing) &&
            kb::particles::HasParticleRenderEmitterFlag(
                emitter.flags, kb::particles::ParticleRenderEmitterFlag::CastsShadow) &&
            kb::particles::HasParticleRenderEmitterFlag(
                emitter.flags, kb::particles::ParticleRenderEmitterFlag::ReceivesShadow) &&
            emitter.FlipbookColumns() == 8U && emitter.FlipbookRows() == 4U &&
            emitter.localBasisQuaternionSnorm[1] == 16'384 && emitter.pointSpriteDiameter == 0.75F &&
            emitter.meshLodLevel == -3 &&
            emitter.trailSampleIntervalSeconds == 0.05F && emitter.trailMinimumDistance == 0.25F &&
            emitter.trailMaxSamplesPerParticle == 31U && emitter.trailWidth == 0.5F &&
            emitter.ribbonMaxSegments == 63U && emitter.ribbonWidth == 0.75F && !emitter.ribbonBreakOnDeath &&
            emitter.beamLocalEnd.z == 3.0F && emitter.beamSegments == 15U && emitter.beamWidth == 0.8F &&
            emitter.beamNoiseAmplitude == 0.3F && emitter.beamNoiseFrequency == 2.0F &&
            emitter.volumetricDensity == 0.6F && emitter.volumetricRadiusScale == 1.25F &&
            emitter.volumetricLowQualitySteps == 8U && emitter.volumetricHighQualitySteps == 32U &&
            emitter.outputOrigin.y == 5.0F && emitter.beamEnd.z == 9.0F &&
            emitter.boundsMinimum.x == -10.0F && emitter.boundsMaximum.z == 30.0F,
        "per-emitter render metadata was incomplete or changed");
    const auto& particle = snapshot->Particles()[0];
    Require(particle.particleId == 11U && particle.spawnOrdinal == 111U && particle.ribbonGroup == 23U &&
            particle.position.x == 11.0F &&
            particle.previousPosition.x == 10.75F && particle.velocity.z == 7.0F &&
            particle.size == 4.0F && particle.rotationRadians == 0.5F && particle.stretch == 1.5F &&
            particle.frame == 17U && particle.normalizedAgeUnorm == 49'152U &&
            particle.packedColor == 0xA1B2C3D4U,
        "compact stable particle payload was incomplete or changed");

    const auto invalid = [&](std::span<const kb::particles::ParticleRenderEmitterRecord> candidateEmitters,
                             std::span<const kb::particles::ParticleRenderRecord> candidateParticles) {
        return channel.Publish(3U, {
            .revision = 2U, .fixedStepIndex = 18U,
            .emitters = candidateEmitters, .particles = candidateParticles}).status;
    };
    Require(invalid({}, particles) == kb::particles::ParticleRenderSnapshotStatus::InvalidSnapshot,
        "unbatched particles were accepted");
    auto malformed = emitters;
    malformed[1].firstParticle = 0U;
    Require(invalid(malformed, particles) == kb::particles::ParticleRenderSnapshotStatus::InvalidSnapshot,
        "overlapping batch ranges were accepted");
    malformed = emitters;
    malformed[0].particleCount = 2U;
    malformed[0].liveParticleCount = 2U;
    Require(invalid(malformed, particles) == kb::particles::ParticleRenderSnapshotStatus::InvalidSnapshot,
        "out-of-range batch records were accepted");
    malformed = emitters;
    malformed[0].liveParticleCount = 0U;
    Require(invalid(malformed, particles) == kb::particles::ParticleRenderSnapshotStatus::InvalidSnapshot,
        "inconsistent batch live count was accepted");
    malformed = emitters;
    malformed[0].boundsMaximum.x = std::numeric_limits<float>::quiet_NaN();
    Require(invalid(malformed, particles) == kb::particles::ParticleRenderSnapshotStatus::InvalidSnapshot,
        "non-finite batch bounds were accepted");
    malformed = emitters;
    malformed[0].output = static_cast<kb::particles::ParticleRenderOutput>(255U);
    Require(invalid(malformed, particles) == kb::particles::ParticleRenderSnapshotStatus::InvalidSnapshot,
        "unknown output enum was accepted");
    malformed = emitters;
    malformed[0].alignment = static_cast<kb::particles::ParticleRenderAlignment>(255U);
    Require(invalid(malformed, particles) == kb::particles::ParticleRenderSnapshotStatus::InvalidSnapshot,
        "unknown alignment enum was accepted");
    malformed = emitters;
    malformed[0].flags = static_cast<kb::particles::ParticleRenderEmitterFlag>(1U << 7U);
    Require(invalid(malformed, particles) == kb::particles::ParticleRenderSnapshotStatus::InvalidSnapshot,
        "unknown emitter flag bit was accepted");
    malformed = emitters;
    malformed[0].pointSpriteDiameter = 0.0F;
    Require(invalid(malformed, particles) == kb::particles::ParticleRenderSnapshotStatus::InvalidSnapshot,
        "non-positive point-sprite diameter was accepted");
    malformed = emitters;
    malformed[0].droppedReason = kb::particles::ParticleRenderDropReason::None;
    Require(invalid(malformed, particles) == kb::particles::ParticleRenderSnapshotStatus::InvalidSnapshot,
        "overflow counters without a dropped reason were accepted");
    malformed = emitters;
    malformed[0].output = kb::particles::ParticleRenderOutput::Volumetric;
    malformed[0].volumetricHighQualitySteps = 257U;
    Require(invalid(malformed, particles) == kb::particles::ParticleRenderSnapshotStatus::InvalidSnapshot,
        "out-of-range volumetric raymarch quality was accepted");

    std::vector<kb::particles::ParticleRenderRecord> fullParticleArena(
        kb::particles::kParticleRenderSnapshotRecordsPerSlot);
    const std::array fullEmitter{Emitter(1U, 0U, static_cast<std::uint32_t>(fullParticleArena.size()))};
    Require(channel.Publish(3U, {
                .revision = 2U, .fixedStepIndex = 18U,
                .emitters = fullEmitter, .particles = fullParticleArena}).status ==
            kb::particles::ParticleRenderSnapshotStatus::SnapshotTooLarge,
        "combined emitter and particle payload exceeded the exact 16 MiB slot without rejection");
}

void TestRetentionBackpressureAllocationAndEpoch() {
    kb::scene::Scene scene;
    TestBackend backend;
    TestBackend otherBackend;
    Require(kb::particles::ParticlePlayback::RegisterBackend(scene, backend).Succeeded() &&
            kb::particles::ParticlePlayback::BackendEpoch(scene) == 1U,
        "snapshot fixture did not establish its first backend epoch");
    const std::array particles{Record(1U), Record(2U)};
    const std::array emitters{Emitter(1U, 0U, 2U)};
    const kb::particles::ParticleRenderSnapshotPublishDesc beforeWarm{
        .revision = 1U, .fixedStepIndex = 10U, .emitters = emitters, .particles = particles};
    Require(kb::particles::ParticlePlayback::PublishRenderSnapshot(scene, backend, beforeWarm).status ==
            kb::particles::ParticleRenderSnapshotStatus::NotWarmed,
        "snapshot publish before explicit warmup was accepted");
    Require(kb::particles::ParticlePlayback::WarmupRenderSnapshots(scene).Succeeded(),
        "snapshot channel warmup failed");
    Require(kb::particles::ParticlePlayback::PublishRenderSnapshot(scene, otherBackend, beforeWarm).status ==
            kb::particles::ParticleRenderSnapshotStatus::BackendMismatch,
        "non-owner backend published into a scene snapshot channel");
    Require(kb::particles::ParticlePlayback::PublishRenderSnapshot(scene, backend, beforeWarm).Succeeded(),
        "first complete particle render snapshot publish failed");
    const auto retained = kb::particles::ParticlePlayback::ReadRenderSnapshot(scene);
    Require(retained && retained->Revision() == 1U && retained->Emitters()[0].instanceId == 1U &&
            retained->Particles()[0].particleId == 1U,
        "first snapshot payload was incomplete");

    const std::array nextParticles{Record(20U)};
    const std::array nextEmitters{Emitter(20U, 0U, 1U)};
    Require(kb::particles::ParticlePlayback::PublishRenderSnapshot(scene, backend, {
                .revision = 2U, .fixedStepIndex = 11U,
                .emitters = nextEmitters, .particles = nextParticles}).Succeeded(),
        "second snapshot publish failed");
    Require(retained->Revision() == 1U && retained->Emitters()[0].effectAssetId == 1001U &&
            retained->Particles()[0].particleId == 1U,
        "older retained batch metadata or particles changed after publication");
    Require(kb::particles::ParticlePlayback::PublishRenderSnapshot(scene, backend, {
                .revision = 2U, .fixedStepIndex = 12U,
                .emitters = nextEmitters, .particles = nextParticles}).status ==
            kb::particles::ParticleRenderSnapshotStatus::StaleRevision &&
            kb::particles::ParticlePlayback::PublishRenderSnapshot(scene, backend, {
                .revision = 3U, .fixedStepIndex = 9U,
                .emitters = nextEmitters, .particles = nextParticles}).status ==
            kb::particles::ParticleRenderSnapshotStatus::StaleRevision,
        "equal revision or decreasing fixed-step publication was accepted");

    std::vector<std::shared_ptr<const kb::particles::ParticleRenderSnapshot>> held;
    held.reserve(kb::particles::kParticleRenderSnapshotSlotCount);
    held.push_back(retained);
    held.push_back(kb::particles::ParticlePlayback::ReadRenderSnapshot(scene));
    for (std::uint64_t revision = 3U; revision <= 4U; ++revision) {
        Require(kb::particles::ParticlePlayback::PublishRenderSnapshot(scene, backend, {
                    .revision = revision, .fixedStepIndex = 10U + revision,
                    .emitters = nextEmitters, .particles = nextParticles}).Succeeded(),
            "retained-slot setup publish failed");
        held.push_back(kb::particles::ParticlePlayback::ReadRenderSnapshot(scene));
    }
    Require(kb::particles::ParticlePlayback::PublishRenderSnapshot(scene, backend, {
                .revision = 5U, .fixedStepIndex = 15U,
                .emitters = nextEmitters, .particles = nextParticles}).status ==
            kb::particles::ParticleRenderSnapshotStatus::SnapshotBackpressure,
        "four retained snapshots did not produce typed backpressure");
    const auto latestAfterBackpressure = kb::particles::ParticlePlayback::ReadRenderSnapshot(scene);
    Require(latestAfterBackpressure->Revision() == 4U &&
            latestAfterBackpressure->Emitters()[0].instanceId == 20U &&
            latestAfterBackpressure->Particles()[0].particleId == 20U,
        "backpressure replaced or damaged the latest complete snapshot");
    held.clear();

    g_allocationCount.store(0U, std::memory_order_relaxed);
    g_countAllocations.store(true, std::memory_order_release);
    const auto allocationResult = kb::particles::ParticlePlayback::PublishRenderSnapshot(scene, backend, {
        .revision = 5U, .fixedStepIndex = 15U, .emitters = nextEmitters, .particles = nextParticles});
    g_countAllocations.store(false, std::memory_order_release);
    Require(allocationResult.Succeeded() && g_allocationCount.load(std::memory_order_relaxed) == 0U,
        "snapshot publish allocated after explicit channel warmup");

    Require(kb::particles::ParticlePlayback::PublishRenderSnapshot(scene, backend, {
                .revision = 6U, .fixedStepIndex = 16U, .tombstone = true}).Succeeded(),
        "empty tombstone snapshot publish failed");
    const auto tombstone = kb::particles::ParticlePlayback::ReadRenderSnapshot(scene);
    Require(tombstone->IsTombstone() && tombstone->Emitters().empty() && tombstone->Particles().empty(),
        "tombstone snapshot retained render payload");
    Require(kb::particles::ParticlePlayback::UnregisterBackend(scene, backend).Succeeded() &&
            kb::particles::ParticlePlayback::BackendEpoch(scene) == 2U &&
            kb::particles::ParticlePlayback::RegisterBackend(scene, backend).Succeeded() &&
            kb::particles::ParticlePlayback::BackendEpoch(scene) == 3U,
        "snapshot backend epoch did not advance across detach and reattach");
    Require(kb::particles::ParticlePlayback::PublishRenderSnapshot(scene, backend, {
                .revision = 7U, .fixedStepIndex = 17U,
                .emitters = nextEmitters, .particles = nextParticles}).Succeeded() &&
            kb::particles::ParticlePlayback::ReadRenderSnapshot(scene)->BackendEpoch() == 3U,
        "reattached backend did not stamp its new epoch into the snapshot");
    Require(kb::particles::ParticlePlayback::UnregisterBackend(scene, backend).Succeeded(),
        "snapshot fixture backend cleanup failed");
}

void TestConcurrentReadsAreCompleteAndMonotonic() {
    kb::scene::Scene scene;
    TestBackend backend;
    Require(kb::particles::ParticlePlayback::RegisterBackend(scene, backend).Succeeded() &&
            kb::particles::ParticlePlayback::WarmupRenderSnapshots(scene).Succeeded(),
        "concurrent snapshot fixture setup failed");
    std::atomic<bool> writerFinished{false};
    std::atomic<bool> invalidRead{false};
    std::thread reader{[&] {
        std::uint64_t observedRevision = 0U;
        while (!writerFinished.load(std::memory_order_acquire)) {
            const auto snapshot = kb::particles::ParticlePlayback::ReadRenderSnapshot(scene);
            if (!snapshot) continue;
            if (snapshot->Revision() < observedRevision || snapshot->FixedStepIndex() != snapshot->Revision() ||
                snapshot->Emitters().size() != 1U || snapshot->Particles().size() != 1U ||
                snapshot->Emitters()[0].instanceId != snapshot->Revision() ||
                snapshot->Particles()[0].particleId != snapshot->Revision()) {
                invalidRead.store(true, std::memory_order_release);
                return;
            }
            observedRevision = snapshot->Revision();
        }
    }};
    for (std::uint64_t revision = 1U; revision <= 4'096U; ++revision) {
        const std::array record{Record(revision)};
        const std::array emitter{Emitter(revision, 0U, 1U)};
        Require(kb::particles::ParticlePlayback::PublishRenderSnapshot(scene, backend, {
                    .revision = revision, .fixedStepIndex = revision,
                    .emitters = emitter, .particles = record}).Succeeded(),
            "monotonic concurrent snapshot publication failed");
    }
    writerFinished.store(true, std::memory_order_release);
    reader.join();
    const auto final = kb::particles::ParticlePlayback::ReadRenderSnapshot(scene);
    Require(!invalidRead.load(std::memory_order_acquire) && final && final->Revision() == 4'096U &&
            final->Emitters()[0].instanceId == 4'096U && final->Particles()[0].particleId == 4'096U,
        "concurrent reader observed a torn or decreasing particle snapshot");
    Require(kb::particles::ParticlePlayback::UnregisterBackend(scene, backend).Succeeded(),
        "concurrent snapshot fixture backend cleanup failed");
}

void TestTwoViewportConsumersRetainTheSameImmutableRevision() {
    kb::particles::ParticleRenderSnapshotChannel channel;
    Require(channel.Warmup(808U).Succeeded(),
        "two-viewport snapshot channel warmup failed");
    const std::array firstParticles{Record(31U), Record(32U)};
    const std::array firstEmitters{Emitter(31U, 0U, 2U)};
    Require(channel.Publish(7U, {
                .revision = 11U,
                .fixedStepIndex = 101U,
                .emitters = firstEmitters,
                .particles = firstParticles,
            }).Succeeded(),
        "two-viewport source snapshot publication failed");

    const std::shared_ptr<const kb::particles::ParticleRenderSnapshot> viewportA = channel.Read();
    const std::shared_ptr<const kb::particles::ParticleRenderSnapshot> viewportB = channel.Read();
    Require(viewportA && viewportB && viewportA.get() == viewportB.get() &&
            viewportA->Revision() == viewportB->Revision() &&
            viewportA->BackendEpoch() == viewportB->BackendEpoch() &&
            viewportA->FixedStepIndex() == viewportB->FixedStepIndex() &&
            viewportA->Revision() == 11U && viewportA->BackendEpoch() == 7U &&
            viewportA->FixedStepIndex() == 101U && viewportA->Emitters().size() == 1U &&
            viewportA->Particles().size() == 2U && viewportA->Emitters()[0].effectAssetId == 1031U &&
            viewportA->Particles()[0].particleId == 31U && viewportA->Particles()[1].particleId == 32U,
        "two viewport consumers did not retain one identical complete scene snapshot");

    const std::array nextParticles{Record(90U)};
    const std::array nextEmitters{Emitter(90U, 0U, 1U)};
    Require(channel.Publish(7U, {
                .revision = 12U,
                .fixedStepIndex = 102U,
                .emitters = nextEmitters,
                .particles = nextParticles,
            }).Succeeded(),
        "next scene snapshot publication failed while two viewports retained the previous revision");
    const std::shared_ptr<const kb::particles::ParticleRenderSnapshot> newest = channel.Read();
    Require(newest && newest->Revision() == 12U && newest->FixedStepIndex() == 102U &&
            newest->Particles()[0].particleId == 90U &&
            viewportA->Revision() == 11U && viewportB->Revision() == 11U &&
            viewportA->FixedStepIndex() == 101U && viewportB->FixedStepIndex() == 101U &&
            viewportA->Particles().size() == 2U && viewportB->Particles().size() == 2U &&
            viewportA->Particles()[0].particleId == 31U && viewportB->Particles()[1].particleId == 32U,
        "next publication mutated a snapshot retained independently by two viewport consumers");
}

void TestRetainedSnapshotOutlivesBackendAndScene() {
    std::atomic<std::uint32_t> backendDestructions{0U};
    std::shared_ptr<const kb::particles::ParticleRenderSnapshot> retained;
    {
        kb::scene::Scene scene;
        auto backend = std::make_unique<TestBackend>(&backendDestructions);
        Require(kb::particles::ParticlePlayback::RegisterBackend(scene, *backend).Succeeded() &&
                kb::particles::ParticlePlayback::WarmupRenderSnapshots(scene).Succeeded(),
            "retained lifetime fixture setup failed");
        const std::array particle{Record(77U)};
        const std::array emitter{Emitter(77U, 0U, 1U)};
        Require(kb::particles::ParticlePlayback::PublishRenderSnapshot(scene, *backend, {
                    .revision = 1U, .fixedStepIndex = 1U,
                    .emitters = emitter, .particles = particle}).Succeeded(),
            "retained lifetime snapshot publish failed");
        retained = kb::particles::ParticlePlayback::ReadRenderSnapshot(scene);
        Require(kb::particles::ParticlePlayback::UnregisterBackend(scene, *backend).Succeeded(),
            "retained lifetime backend unregister failed");
        backend.reset();
        Require(backendDestructions.load(std::memory_order_relaxed) == 1U,
            "test backend did not unload before retained snapshot verification");
    }
    Require(retained && retained->Emitters()[0].effectAssetId == 1077U &&
            retained->Particles()[0].particleId == 77U,
        "engine-owned retained snapshot did not survive backend unload and scene destruction");
    retained.reset();
    Require(backendDestructions.load(std::memory_order_relaxed) == 1U,
        "snapshot destruction invoked provider-owned lifetime code");
}

void TestRendererCapabilityOwnershipAndProgress() {
    kb::scene::Scene scene;
    constexpr std::uint64_t rendererA = 101U;
    constexpr std::uint64_t rendererB = 202U;
    const kb::particles::ParticleRenderCapabilities capabilities{
        .capabilityEpoch = 7U,
        .lastConsumedFixedStep = 0U,
        .outputs = kb::particles::ParticleRenderOutputCapability::Billboard |
            kb::particles::ParticleRenderOutputCapability::StretchedBillboard |
            kb::particles::ParticleRenderOutputCapability::PointSprite,
        .gpuDrawing = true,
        .instancing = true,
        .softParticles = true,
        .subtractiveBlend = true,
        .gpuVisualAvailability = kb::particles::ParticleGpuVisualAvailability::Ready,
        .maxGpuVisualParticles = 123U,
        .maxGpuResourceBytes = 456U,
    };
    Require(kb::particles::ParticlePlayback::PublishRenderCapabilities(scene, rendererA, capabilities).Succeeded(),
        "renderer capability publication failed");
    Require(kb::particles::ParticlePlayback::PublishRenderCapabilities(scene, rendererB, capabilities).status ==
            kb::particles::ParticleRenderCapabilityStatus::ConsumerConflict,
        "a second renderer silently replaced the active capability owner");
    Require(kb::particles::ParticlePlayback::AcknowledgeRenderedFixedStep(scene, rendererA, 19U).Succeeded(),
        "renderer fixed-step acknowledgement failed");
    const auto observed = kb::particles::ParticlePlayback::RenderCapabilities(scene);
    Require(observed.capabilityEpoch == 7U && observed.lastConsumedFixedStep == 19U &&
            observed.gpuDrawing && observed.instancing && observed.softParticles &&
            observed.subtractiveBlend &&
            observed.gpuVisualAvailability == kb::particles::ParticleGpuVisualAvailability::Ready &&
            observed.maxGpuVisualParticles == 123U && observed.maxGpuResourceBytes == 456U &&
            kb::particles::HasParticleRenderOutputCapability(
                observed.outputs, kb::particles::ParticleRenderOutputCapability::PointSprite),
        "renderer capabilities or consumed fixed-step progress were not retained");
    Require(kb::particles::ParticlePlayback::ClearRenderCapabilities(scene, rendererB).status ==
            kb::particles::ParticleRenderCapabilityStatus::ConsumerConflict &&
            kb::particles::ParticlePlayback::ClearRenderCapabilities(scene, rendererA).Succeeded() &&
            !kb::particles::ParticlePlayback::RenderCapabilities(scene).gpuDrawing,
        "renderer capability ownership was not enforced during teardown");
}

void TestGpuVisualStepJournalAcknowledgesExactlyOnceAndOverflows() {
    kb::scene::Scene scene;
    TestBackend backend;
    constexpr std::uint64_t renderer = 303U;
    Require(kb::particles::ParticlePlayback::RegisterBackend(scene, backend).Succeeded() &&
            kb::particles::ParticlePlayback::WarmupRenderSnapshots(scene).Succeeded(),
        "GPU visual step journal fixture could not initialize the core channel");
    Require(kb::particles::ParticlePlayback::PublishRenderCapabilities(scene, renderer, {
                .capabilityEpoch = 1U,
                .gpuVisualAvailability = kb::particles::ParticleGpuVisualAvailability::Ready,
            }).Succeeded(),
        "GPU visual step journal fixture could not register its renderer consumer");

    const auto publish = [&](std::uint64_t step) {
        return kb::particles::ParticlePlayback::PublishRenderSnapshot(scene, backend, {
            .revision = step,
            .fixedStepIndex = step,
        });
    };
    Require(publish(1U).Succeeded() && publish(2U).Succeeded() &&
            kb::particles::ParticlePlayback::PendingGpuVisualSteps(scene, renderer).size() == 2U &&
            kb::particles::ParticlePlayback::AcknowledgeRenderedFixedStep(scene, renderer, 2U).Succeeded() &&
            kb::particles::ParticlePlayback::RenderCapabilities(scene).lastConsumedFixedStep == 2U &&
            kb::particles::ParticlePlayback::AcknowledgeRenderedFixedStep(scene, renderer, 2U).status ==
                kb::particles::ParticleRenderCapabilityStatus::StaleFixedStep,
        "GPU visual step journal did not consume each fixed step exactly once");

    for (std::uint64_t step = 3U;
         step < 3U + kb::scene::kParticleEffectRetainedGpuSteps;
         ++step) {
        Require(publish(step).Succeeded(), "GPU visual step journal could not retain a bounded fixed step");
    }
    Require(publish(3U + kb::scene::kParticleEffectRetainedGpuSteps).Succeeded() &&
            kb::particles::ParticlePlayback::RenderCapabilities(scene).gpuVisualAvailability ==
                kb::particles::ParticleGpuVisualAvailability::GpuCatchupOverflow &&
            kb::particles::ParticlePlayback::AcknowledgeRenderedFixedStep(
                scene, renderer, 3U + kb::scene::kParticleEffectRetainedGpuSteps).status ==
                kb::particles::ParticleRenderCapabilityStatus::GpuCatchupOverflow,
        "GPU visual step journal did not report its bounded catch-up overflow");
    Require(kb::particles::ParticlePlayback::ClearRenderCapabilities(scene, renderer).Succeeded() &&
            kb::particles::ParticlePlayback::UnregisterBackend(scene, backend).Succeeded(),
        "GPU visual step journal fixture did not release its core ownership");
}

} // namespace

void* operator new(std::size_t size) {
    if (g_countAllocations.load(std::memory_order_relaxed)) {
        g_allocationCount.fetch_add(1U, std::memory_order_relaxed);
    }
    if (void* memory = std::malloc(size == 0U ? 1U : size)) return memory;
    throw std::bad_alloc{};
}

void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

int main() {
    try {
        TestCompleteContractAndMalformedRanges();
        TestRetentionBackpressureAllocationAndEpoch();
        TestConcurrentReadsAreCompleteAndMonotonic();
        TestTwoViewportConsumersRetainTheSameImmutableRevision();
        TestRetainedSnapshotOutlivesBackendAndScene();
        TestRendererCapabilityOwnershipAndProgress();
        TestGpuVisualStepJournalAcknowledgesExactlyOnceAndOverflows();
        std::cout << "21kb Particle System render snapshot tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        g_countAllocations.store(false, std::memory_order_release);
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
