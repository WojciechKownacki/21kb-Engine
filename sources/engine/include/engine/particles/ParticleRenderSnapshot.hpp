#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/scene/ParticleEffectAssetSchema.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <span>
#include <type_traits>

namespace kb::particles {

enum class ParticleRenderOutput : std::uint8_t {
    Billboard,
    StretchedBillboard,
    PointSprite,
    Mesh,
    Trail,
    Ribbon,
    Beam,
    Volumetric,
};

enum class ParticleRenderBlendMode : std::uint8_t {
    Opaque,
    Alpha,
    Add,
    Multiply,
    Subtract,
    Premultiplied,
};

enum class ParticleRenderDepthMode : std::uint8_t {
    Disabled,
    ReadOnly,
    WriteOnly,
    ReadWrite,
};

enum class ParticleRenderSortMode : std::uint8_t {
    None,
    BackToFront,
    FrontToBack,
    Distance,
    Age,
};

enum class ParticleRenderAlignment : std::uint8_t {
    CameraFacing,
    Velocity,
    WorldUp,
    Local,
};

enum class ParticleRenderEmitterFlag : std::uint8_t {
    None = 0U,
    SoftParticles = 1U << 0U,
    AntiAliasing = 1U << 1U,
};

[[nodiscard]] constexpr ParticleRenderEmitterFlag operator|(
    ParticleRenderEmitterFlag lhs, ParticleRenderEmitterFlag rhs) noexcept {
    return static_cast<ParticleRenderEmitterFlag>(
        static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

[[nodiscard]] constexpr bool HasParticleRenderEmitterFlag(
    ParticleRenderEmitterFlag value, ParticleRenderEmitterFlag flag) noexcept {
    return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(flag)) != 0U;
}

enum class ParticleRenderEmitterStatus : std::uint8_t {
    Playing,
    Paused,
    Draining,
    Stopped,
};

enum class ParticleRenderDropReason : std::uint8_t {
    None,
    Capacity,
    SpawnBudget,
    EventBudget,
    SnapshotBudget,
};

struct ParticleRenderRecord {
    kb::math::Vec3 position{};
    float size = 1.0F;
    kb::math::Vec3 previousPosition{};
    float rotationRadians = 0.0F;
    kb::math::Vec3 velocity{};
    float stretch = 0.0F;
    std::uint64_t particleId = 0U;
    std::uint32_t packedColor = 0xFFFFFFFFU;
    std::uint16_t frame = 0U;
    std::uint16_t normalizedAgeUnorm = 0U;
};

static_assert(sizeof(ParticleRenderRecord) >= 48U && sizeof(ParticleRenderRecord) <= 64U);
static_assert(sizeof(ParticleRenderRecord) == 64U);
static_assert(std::is_trivially_copyable_v<ParticleRenderRecord>);

struct ParticleRenderEmitterRecord {
    std::uint64_t instanceId = 0U;
    std::uint64_t effectAssetId = 0U;
    std::uint64_t emitterId = 0U;
    std::uint64_t assetGeneration = 0U;
    std::uint64_t materialAssetId = 0U;
    std::uint64_t meshAssetId = 0U;
    std::uint64_t textureAtlasAssetId = 0U;
    std::uint32_t firstParticle = 0U;
    std::uint32_t particleCount = 0U;
    std::uint32_t liveParticleCount = 0U;
    std::uint32_t rejectedByCapacity = 0U;
    std::uint32_t rejectedBySpawnBudget = 0U;
    std::uint32_t rejectedByEventBudget = 0U;
    std::uint32_t droppedParticleCount = 0U;
    ParticleRenderOutput output = ParticleRenderOutput::Billboard;
    ParticleRenderBlendMode blend = ParticleRenderBlendMode::Alpha;
    ParticleRenderDepthMode depth = ParticleRenderDepthMode::ReadOnly;
    ParticleRenderSortMode sort = ParticleRenderSortMode::BackToFront;
    ParticleRenderEmitterStatus status = ParticleRenderEmitterStatus::Stopped;
    ParticleRenderDropReason droppedReason = ParticleRenderDropReason::None;
    ParticleRenderAlignment alignment = ParticleRenderAlignment::CameraFacing;
    ParticleRenderEmitterFlag flags = ParticleRenderEmitterFlag::None;
    // A zero encoded axis represents 256, preserving the authored 1..256 grid
    // without widening the renderer ABI for the common 1x1 case.
    std::uint8_t flipbookColumnsEncoded = 1U;
    std::uint8_t flipbookRowsEncoded = 1U;
    std::array<std::int16_t, 4U> localBasisQuaternionSnorm{};
    float pointSpriteDiameter = 1.0F;
    kb::math::Vec3 boundsMinimum{};
    kb::math::Vec3 boundsMaximum{};

    [[nodiscard]] constexpr std::uint16_t FlipbookColumns() const noexcept {
        return flipbookColumnsEncoded == 0U ? 256U : flipbookColumnsEncoded;
    }

    [[nodiscard]] constexpr std::uint16_t FlipbookRows() const noexcept {
        return flipbookRowsEncoded == 0U ? 256U : flipbookRowsEncoded;
    }
};

static_assert(std::is_trivially_copyable_v<ParticleRenderEmitterRecord>);
static_assert(sizeof(ParticleRenderEmitterRecord) <= 144U);

struct ParticleRenderSnapshotHeader {
    std::uint64_t revision = 0U;
    std::uint64_t sceneId = 0U;
    std::uint64_t backendEpoch = 0U;
    std::uint64_t fixedStepIndex = 0U;
    bool tombstone = false;
    std::uint8_t reserved[7]{};
};

static_assert(std::is_trivially_copyable_v<ParticleRenderSnapshotHeader>);

inline constexpr std::size_t kParticleRenderSnapshotSlotCount = 4U;
inline constexpr std::size_t kParticleRenderSnapshotRetainedPayloadBytes = 64U * 1024U * 1024U;
inline constexpr std::size_t kParticleRenderSnapshotBytesPerSlot =
    kParticleRenderSnapshotRetainedPayloadBytes / kParticleRenderSnapshotSlotCount;
inline constexpr std::size_t kParticleRenderSnapshotRecordsPerSlot =
    kParticleRenderSnapshotBytesPerSlot / sizeof(ParticleRenderRecord);
inline constexpr std::size_t kParticleRenderSnapshotMaxEmitterRecords =
    kb::scene::kParticleEffectMaxInstancesPerScene * kb::scene::kParticleEffectMaxEmitters;

enum class ParticleRenderSnapshotStatus : std::uint8_t {
    Success,
    NotWarmed,
    InvalidSnapshot,
    StaleRevision,
    SnapshotBackpressure,
    SnapshotTooLarge,
    AllocationFailed,
    BackendMismatch,
};

struct ParticleRenderSnapshotResult {
    ParticleRenderSnapshotStatus status = ParticleRenderSnapshotStatus::NotWarmed;

    [[nodiscard]] constexpr bool Succeeded() const noexcept {
        return status == ParticleRenderSnapshotStatus::Success;
    }
};

struct ParticleRenderSnapshotPublishDesc {
    std::uint64_t revision = 0U;
    std::uint64_t fixedStepIndex = 0U;
    bool tombstone = false;
    std::span<const ParticleRenderEmitterRecord> emitters;
    std::span<const ParticleRenderRecord> particles;
};

class ParticleRenderSnapshot final {
public:
    ParticleRenderSnapshot();
    ~ParticleRenderSnapshot();

    ParticleRenderSnapshot(const ParticleRenderSnapshot&) = delete;
    ParticleRenderSnapshot& operator=(const ParticleRenderSnapshot&) = delete;
    ParticleRenderSnapshot(ParticleRenderSnapshot&&) = delete;
    ParticleRenderSnapshot& operator=(ParticleRenderSnapshot&&) = delete;

    [[nodiscard]] const ParticleRenderSnapshotHeader& Header() const noexcept;
    [[nodiscard]] std::uint64_t Revision() const noexcept;
    [[nodiscard]] std::uint64_t SceneId() const noexcept;
    [[nodiscard]] std::uint64_t BackendEpoch() const noexcept;
    [[nodiscard]] std::uint64_t FixedStepIndex() const noexcept;
    [[nodiscard]] bool IsTombstone() const noexcept;
    [[nodiscard]] std::span<const ParticleRenderEmitterRecord> Emitters() const noexcept;
    [[nodiscard]] std::span<const ParticleRenderRecord> Particles() const noexcept;

private:
    friend class ParticleRenderSnapshotChannel;

    struct Storage;
    std::unique_ptr<Storage> storage_;
};

class ParticleRenderSnapshotChannel final {
public:
    ParticleRenderSnapshotChannel();
    ~ParticleRenderSnapshotChannel();

    ParticleRenderSnapshotChannel(const ParticleRenderSnapshotChannel&) = delete;
    ParticleRenderSnapshotChannel& operator=(const ParticleRenderSnapshotChannel&) = delete;
    ParticleRenderSnapshotChannel(ParticleRenderSnapshotChannel&&) = delete;
    ParticleRenderSnapshotChannel& operator=(ParticleRenderSnapshotChannel&&) = delete;

    [[nodiscard]] ParticleRenderSnapshotResult Warmup(std::uint64_t sceneId) noexcept;
    [[nodiscard]] bool IsWarmedUp() const noexcept;
    [[nodiscard]] ParticleRenderSnapshotResult Publish(
        std::uint64_t backendEpoch,
        const ParticleRenderSnapshotPublishDesc& desc) noexcept;
    [[nodiscard]] std::shared_ptr<const ParticleRenderSnapshot> Read() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace kb::particles
