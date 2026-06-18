#include "EcsTestSuites.hpp"
#include "EcsTestTypes.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/Kernel.hpp"
#include "engine/ecs/KernelVectorMath.hpp"
#include "engine/ecs/SystemScheduler.hpp"
#include "engine/ecs/World.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace {

struct EcsMovementScaleAsset {
    float multiplier = 1.0F;
};

struct EcsMovementConstants {
    float deltaSeconds = 0.0F;
    float drag = 0.0F;
};

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

struct alignas(32) EcsKernelAlignedInput {
    float x = 0.0F;
};

struct alignas(32) EcsKernelAlignedOutput {
    float x = 0.0F;
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

using EcsMovementKernelContract = kb::ecs::KernelContract<
    kb::ecs::KernelInputComponents<EcsVelocity>,
    kb::ecs::KernelOutputComponents<EcsPosition>,
    kb::ecs::KernelReadOnlyAssets<EcsMovementScaleAsset>,
    EcsMovementConstants>;

struct EcsMovementKernel {
    void operator()(kb::ecs::KernelBatch<EcsMovementKernelContract>& batch) const {
        const EcsVelocity* velocities = batch.Inputs<0>();
        EcsPosition* positions = batch.Outputs<0>();
        const EcsMovementScaleAsset& scale = batch.Asset<0>();
        const EcsMovementConstants& constants = batch.Constants();

        for (std::size_t index = 0; index < batch.Count(); ++index) {
            batch.Prefetch(index + 4);
            positions[index].x += velocities[index].x * constants.deltaSeconds * scale.multiplier;
            positions[index].y += velocities[index].y * constants.deltaSeconds * (scale.multiplier - constants.drag);
        }
    }
};

using EcsSimdKernelContract = kb::ecs::KernelContract<
    kb::ecs::KernelInputComponents<EcsVelocity>,
    kb::ecs::KernelOutputComponents<EcsPosition>>;

using EcsAlignedKernelContract = kb::ecs::KernelContract<
    kb::ecs::KernelInputComponents<EcsKernelAlignedInput>,
    kb::ecs::KernelOutputComponents<EcsKernelAlignedOutput>>;

void AddProbeMovement(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch, float scale) {
    const EcsVelocity* velocities = batch.Inputs<0>();
    EcsPosition* positions = batch.Outputs<0>();
    for (std::size_t index = 0; index < batch.Count(); ++index) {
        positions[index].x += velocities[index].x * scale;
        positions[index].y += velocities[index].y * scale;
    }
}

struct EcsScalarOnlyProbeKernel {
    int* scalarBatches = nullptr;

    void operator()(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch) const {
        ++(*scalarBatches);
        AddProbeMovement(batch, 1.0F);
    }
};

struct EcsSimdProbeKernel {
    int* scalarBatches = nullptr;
    int* sse2Batches = nullptr;
    int* avx2Batches = nullptr;
    int* avx512Batches = nullptr;
    int* neonBatches = nullptr;

    void operator()(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch) const {
        ++(*scalarBatches);
        AddProbeMovement(batch, 1.0F);
    }

    void operator()(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch, kb::ecs::KernelSse2Tag) const {
        ++(*sse2Batches);
        AddProbeMovement(batch, 2.0F);
    }

    void operator()(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch, kb::ecs::KernelAvx2Tag) const {
        ++(*avx2Batches);
        AddProbeMovement(batch, 4.0F);
    }

    void operator()(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch, kb::ecs::KernelAvx512Tag) const {
        ++(*avx512Batches);
        AddProbeMovement(batch, 8.0F);
    }

    void operator()(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch, kb::ecs::KernelNeonTag) const {
        ++(*neonBatches);
        AddProbeMovement(batch, 3.0F);
    }
};

struct EcsAlignedKernel {
    void operator()(kb::ecs::KernelBatch<EcsAlignedKernelContract>& batch) const {
        const EcsKernelAlignedInput* inputs = batch.Inputs<0>();
        EcsKernelAlignedOutput* outputs = batch.Outputs<0>();
        for (std::size_t index = 0; index < batch.Count(); ++index) {
            outputs[index].x = inputs[index].x * 2.0F;
        }
    }
};

struct EcsEditorKernelContext {
    int batches = 0;
    float scale = 1.0F;
};

void EcsEditorHotReloadKernelV1(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch, void* context) {
    auto* typedContext = static_cast<EcsEditorKernelContext*>(context);
    ++typedContext->batches;
    AddProbeMovement(batch, typedContext->scale);
}

void EcsEditorHotReloadKernelV2(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch, void* context) {
    auto* typedContext = static_cast<EcsEditorKernelContext*>(context);
    ++typedContext->batches;
    AddProbeMovement(batch, typedContext->scale * 2.0F);
}

struct EcsEditorClearingKernelContext {
    kb::ecs::EditorKernelBinding<EcsSimdKernelContract>* binding = nullptr;
    int batches = 0;
};

void EcsEditorClearingKernel(kb::ecs::KernelBatch<EcsSimdKernelContract>& batch, void* context) {
    auto* typedContext = static_cast<EcsEditorClearingKernelContext*>(context);
    ++typedContext->batches;
    AddProbeMovement(batch, 1.0F);
    typedContext->binding->Clear();
}

[[nodiscard]] float SumPositions(kb::ecs::KernelQuery<EcsSimdKernelContract>& query) {
    float sum = 0.0F;
    query.ForEachBatchKernel([&sum](const kb::ecs::QueryBatch<EcsVelocity, EcsPosition>& batch) {
        const EcsPosition* positions = batch.Components<1>();
        for (std::size_t index = 0; index < batch.Count(); ++index) {
            sum += positions[index].x + positions[index].y;
        }
    });
    return sum;
}

void PopulateSimdProbeWorld(kb::ecs::World& world, int entityCount) {
    for (int index = 0; index < entityCount; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsVelocity{ .x = 1.0F, .y = 2.0F });
        world.Set(entity, EcsPosition{ .x = 0.0F, .y = 0.0F });
    }
}

[[nodiscard]] std::uint32_t FloatBits(float value) noexcept {
    return std::bit_cast<std::uint32_t>(value);
}

[[nodiscard]] float DeterministicMulAdd(float multiplicand, float multiplier, float addend) noexcept {
    const float product = multiplicand * multiplier;
    return product + addend;
}

template <std::size_t Count>
[[nodiscard]] std::array<std::uint32_t, Count * 2U> ReferenceMovementBits(
    std::array<EcsPosition, Count> positions,
    const std::array<EcsVelocity, Count>& velocities) noexcept {
    constexpr float deltaSeconds = 0.0166666675F;
    constexpr float maxSpeedSquared = 2.5F;
    constexpr float highBias = 0.125F;
    constexpr float lowBias = -0.25F;

    std::array<std::uint32_t, Count * 2U> bits{};
    for (std::size_t index = 0; index < Count; ++index) {
        const float speedSquared = velocities[index].x * velocities[index].x + velocities[index].y * velocities[index].y;
        const float bias = speedSquared > maxSpeedSquared ? highBias : lowBias;
        positions[index].x = DeterministicMulAdd(velocities[index].x, deltaSeconds, positions[index].x);
        positions[index].y = DeterministicMulAdd(velocities[index].y, deltaSeconds, positions[index].y + bias);
        bits[index * 2U] = FloatBits(positions[index].x);
        bits[index * 2U + 1U] = FloatBits(positions[index].y);
    }
    return bits;
}

template <typename BackendTag, std::size_t Count>
[[nodiscard]] std::array<std::uint32_t, Count * 2U> VectorMovementBits(
    std::array<EcsPosition, Count> positions,
    const std::array<EcsVelocity, Count>& velocities) noexcept {
    using FloatLanes = kb::ecs::KernelFloatLanes<BackendTag>;
    using Vec2Lanes = kb::ecs::KernelFloat2Lanes<BackendTag>;

    constexpr float deltaSeconds = 0.0166666675F;
    constexpr float maxSpeedSquared = 2.5F;
    constexpr float highBias = 0.125F;
    constexpr float lowBias = -0.25F;

    for (std::size_t begin = 0; begin < Count; begin += FloatLanes::LaneCount) {
        const std::size_t remaining = Count - begin;
        const std::size_t batchCount = remaining < FloatLanes::LaneCount ? remaining : FloatLanes::LaneCount;

        Vec2Lanes position = Vec2Lanes::LoadMembersPartial(positions.data() + begin, batchCount, 0.0F, &EcsPosition::x, &EcsPosition::y);
        const Vec2Lanes velocity = Vec2Lanes::LoadMembersPartial(velocities.data() + begin, batchCount, 0.0F, &EcsVelocity::x, &EcsVelocity::y);
        const FloatLanes speedSquared = kb::ecs::KernelDot(velocity, velocity);
        const FloatLanes bias = kb::ecs::KernelSelect(
            kb::ecs::KernelGreaterThan(speedSquared, FloatLanes::Splat(maxSpeedSquared)),
            FloatLanes::Splat(highBias),
            FloatLanes::Splat(lowBias));

        position.template Component<1>() = position.template Component<1>() + bias;
        position = kb::ecs::KernelDeterministicMulAdd(velocity, FloatLanes::Splat(deltaSeconds), position);
        position.StoreMembersPartial(positions.data() + begin, batchCount, &EcsPosition::x, &EcsPosition::y);
    }

    std::array<std::uint32_t, Count * 2U> bits{};
    for (std::size_t index = 0; index < Count; ++index) {
        bits[index * 2U] = FloatBits(positions[index].x);
        bits[index * 2U + 1U] = FloatBits(positions[index].y);
    }
    return bits;
}

struct EcsKernelLocalTransform {
    float translationX = 0.0F;
    float translationY = 0.0F;
    float translationZ = 0.0F;
    float rotationZ = 0.0F;
    float scaleX = 1.0F;
    float scaleY = 1.0F;
    float scaleZ = 1.0F;
};

struct EcsKernelWorldTransform {
    float matrix[16]{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
};

struct EcsKernelLocalBounds {
    float centerX = 0.0F;
    float centerY = 0.0F;
    float centerZ = 0.0F;
    float extentX = 0.5F;
    float extentY = 0.5F;
    float extentZ = 0.5F;
};

struct EcsKernelWorldBounds {
    float minX = 0.0F;
    float minY = 0.0F;
    float minZ = 0.0F;
    float maxX = 0.0F;
    float maxY = 0.0F;
    float maxZ = 0.0F;
};

struct EcsKernelPhysicsBody {
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
    float velocityX = 0.0F;
    float velocityY = 0.0F;
    float velocityZ = 0.0F;
    float radius = 0.5F;
    float inverseMass = 1.0F;
};

struct EcsKernelPhysicsProxy {
    float predictedX = 0.0F;
    float predictedY = 0.0F;
    float predictedZ = 0.0F;
    float sweptRadius = 0.0F;
};

template <typename BackendTag>
[[nodiscard]] kb::ecs::KernelFloatLanes<BackendTag> AbsLanes(kb::ecs::KernelFloatLanes<BackendTag> value) noexcept {
    kb::ecs::KernelFloatLanes<BackendTag> result;
    for (std::size_t lane = 0; lane < kb::ecs::KernelFloatLanes<BackendTag>::LaneCount; ++lane) {
        result.SetLane(lane, std::fabs(value.Lane(lane)));
    }
    return result;
}

template <typename BackendTag>
[[nodiscard]] kb::ecs::KernelFloatLanes<BackendTag> SqrtLanes(kb::ecs::KernelFloatLanes<BackendTag> value) noexcept {
    kb::ecs::KernelFloatLanes<BackendTag> result;
    for (std::size_t lane = 0; lane < kb::ecs::KernelFloatLanes<BackendTag>::LaneCount; ++lane) {
        result.SetLane(lane, std::sqrt(value.Lane(lane)));
    }
    return result;
}

template <typename BackendTag, std::size_t Count>
void RunCompatibilityMovementKernel(std::array<EcsPosition, Count>& positions, const std::array<EcsVelocity, Count>& velocities) noexcept {
    using FloatLanes = kb::ecs::KernelFloatLanes<BackendTag>;
    using Vec2Lanes = kb::ecs::KernelFloat2Lanes<BackendTag>;

    constexpr float deltaSeconds = 0.0111111114F;

    for (std::size_t begin = 0; begin < Count; begin += FloatLanes::LaneCount) {
        const std::size_t remaining = Count - begin;
        const std::size_t batchCount = remaining < FloatLanes::LaneCount ? remaining : FloatLanes::LaneCount;

        Vec2Lanes position = Vec2Lanes::LoadMembersPartial(positions.data() + begin, batchCount, 0.0F, &EcsPosition::x, &EcsPosition::y);
        const Vec2Lanes velocity = Vec2Lanes::LoadMembersPartial(velocities.data() + begin, batchCount, 0.0F, &EcsVelocity::x, &EcsVelocity::y);
        position = kb::ecs::KernelDeterministicMulAdd(velocity, FloatLanes::Splat(deltaSeconds), position);
        position.StoreMembersPartial(positions.data() + begin, batchCount, &EcsPosition::x, &EcsPosition::y);
    }
}

template <typename BackendTag, std::size_t Count>
void RunCompatibilityTransformKernel(
    const std::array<EcsKernelLocalTransform, Count>& localTransforms,
    std::array<EcsKernelWorldTransform, Count>& worldTransforms) noexcept {
    using FloatLanes = kb::ecs::KernelFloatLanes<BackendTag>;

    for (std::size_t begin = 0; begin < Count; begin += FloatLanes::LaneCount) {
        const std::size_t remaining = Count - begin;
        const std::size_t batchCount = remaining < FloatLanes::LaneCount ? remaining : FloatLanes::LaneCount;
        const FloatLanes translationX = FloatLanes::LoadMemberPartial(localTransforms.data() + begin, batchCount, &EcsKernelLocalTransform::translationX);
        const FloatLanes translationY = FloatLanes::LoadMemberPartial(localTransforms.data() + begin, batchCount, &EcsKernelLocalTransform::translationY);
        const FloatLanes translationZ = FloatLanes::LoadMemberPartial(localTransforms.data() + begin, batchCount, &EcsKernelLocalTransform::translationZ);
        const FloatLanes rotationZ = FloatLanes::LoadMemberPartial(localTransforms.data() + begin, batchCount, &EcsKernelLocalTransform::rotationZ);
        const FloatLanes scaleX = FloatLanes::LoadMemberPartial(localTransforms.data() + begin, batchCount, &EcsKernelLocalTransform::scaleX);
        const FloatLanes scaleY = FloatLanes::LoadMemberPartial(localTransforms.data() + begin, batchCount, &EcsKernelLocalTransform::scaleY);
        const FloatLanes scaleZ = FloatLanes::LoadMemberPartial(localTransforms.data() + begin, batchCount, &EcsKernelLocalTransform::scaleZ);

        for (std::size_t lane = 0; lane < batchCount; ++lane) {
            const float cosZ = std::cos(rotationZ.Lane(lane));
            const float sinZ = std::sin(rotationZ.Lane(lane));
            EcsKernelWorldTransform& world = worldTransforms[begin + lane];

            world.matrix[0] = cosZ * scaleX.Lane(lane);
            world.matrix[1] = sinZ * scaleX.Lane(lane);
            world.matrix[2] = 0.0F;
            world.matrix[3] = 0.0F;
            world.matrix[4] = -sinZ * scaleY.Lane(lane);
            world.matrix[5] = cosZ * scaleY.Lane(lane);
            world.matrix[6] = 0.0F;
            world.matrix[7] = 0.0F;
            world.matrix[8] = 0.0F;
            world.matrix[9] = 0.0F;
            world.matrix[10] = scaleZ.Lane(lane);
            world.matrix[11] = 0.0F;
            world.matrix[12] = translationX.Lane(lane);
            world.matrix[13] = translationY.Lane(lane);
            world.matrix[14] = translationZ.Lane(lane);
            world.matrix[15] = 1.0F;
        }
    }
}

template <typename BackendTag, std::size_t Count>
void RunCompatibilityBoundsKernel(
    const std::array<EcsKernelLocalBounds, Count>& localBounds,
    const std::array<EcsKernelLocalTransform, Count>& transforms,
    std::array<EcsKernelWorldBounds, Count>& worldBounds) noexcept {
    using FloatLanes = kb::ecs::KernelFloatLanes<BackendTag>;

    for (std::size_t begin = 0; begin < Count; begin += FloatLanes::LaneCount) {
        const std::size_t remaining = Count - begin;
        const std::size_t batchCount = remaining < FloatLanes::LaneCount ? remaining : FloatLanes::LaneCount;
        const FloatLanes centerX = FloatLanes::LoadMemberPartial(localBounds.data() + begin, batchCount, &EcsKernelLocalBounds::centerX);
        const FloatLanes centerY = FloatLanes::LoadMemberPartial(localBounds.data() + begin, batchCount, &EcsKernelLocalBounds::centerY);
        const FloatLanes centerZ = FloatLanes::LoadMemberPartial(localBounds.data() + begin, batchCount, &EcsKernelLocalBounds::centerZ);
        const FloatLanes extentX = FloatLanes::LoadMemberPartial(localBounds.data() + begin, batchCount, &EcsKernelLocalBounds::extentX);
        const FloatLanes extentY = FloatLanes::LoadMemberPartial(localBounds.data() + begin, batchCount, &EcsKernelLocalBounds::extentY);
        const FloatLanes extentZ = FloatLanes::LoadMemberPartial(localBounds.data() + begin, batchCount, &EcsKernelLocalBounds::extentZ);
        const FloatLanes translationX = FloatLanes::LoadMemberPartial(transforms.data() + begin, batchCount, &EcsKernelLocalTransform::translationX);
        const FloatLanes translationY = FloatLanes::LoadMemberPartial(transforms.data() + begin, batchCount, &EcsKernelLocalTransform::translationY);
        const FloatLanes translationZ = FloatLanes::LoadMemberPartial(transforms.data() + begin, batchCount, &EcsKernelLocalTransform::translationZ);
        const FloatLanes scaleX = AbsLanes(FloatLanes::LoadMemberPartial(transforms.data() + begin, batchCount, &EcsKernelLocalTransform::scaleX));
        const FloatLanes scaleY = AbsLanes(FloatLanes::LoadMemberPartial(transforms.data() + begin, batchCount, &EcsKernelLocalTransform::scaleY));
        const FloatLanes scaleZ = AbsLanes(FloatLanes::LoadMemberPartial(transforms.data() + begin, batchCount, &EcsKernelLocalTransform::scaleZ));

        const FloatLanes worldCenterX = kb::ecs::KernelDeterministicMulAdd(centerX, scaleX, translationX);
        const FloatLanes worldCenterY = kb::ecs::KernelDeterministicMulAdd(centerY, scaleY, translationY);
        const FloatLanes worldCenterZ = kb::ecs::KernelDeterministicMulAdd(centerZ, scaleZ, translationZ);
        const FloatLanes worldExtentX = extentX * scaleX;
        const FloatLanes worldExtentY = extentY * scaleY;
        const FloatLanes worldExtentZ = extentZ * scaleZ;

        for (std::size_t lane = 0; lane < batchCount; ++lane) {
            EcsKernelWorldBounds& bounds = worldBounds[begin + lane];
            bounds.minX = worldCenterX.Lane(lane) - worldExtentX.Lane(lane);
            bounds.minY = worldCenterY.Lane(lane) - worldExtentY.Lane(lane);
            bounds.minZ = worldCenterZ.Lane(lane) - worldExtentZ.Lane(lane);
            bounds.maxX = worldCenterX.Lane(lane) + worldExtentX.Lane(lane);
            bounds.maxY = worldCenterY.Lane(lane) + worldExtentY.Lane(lane);
            bounds.maxZ = worldCenterZ.Lane(lane) + worldExtentZ.Lane(lane);
        }
    }
}

template <typename BackendTag, std::size_t Count>
void RunCompatibilityPhysicsProxyKernel(
    std::array<EcsKernelPhysicsBody, Count>& bodies,
    std::array<EcsKernelPhysicsProxy, Count>& proxies) noexcept {
    using FloatLanes = kb::ecs::KernelFloatLanes<BackendTag>;
    using Vec3Lanes = kb::ecs::KernelFloat3Lanes<BackendTag>;

    constexpr float deltaSeconds = 0.0166666675F;
    constexpr float gravityY = -9.8125F;

    for (std::size_t begin = 0; begin < Count; begin += FloatLanes::LaneCount) {
        const std::size_t remaining = Count - begin;
        const std::size_t batchCount = remaining < FloatLanes::LaneCount ? remaining : FloatLanes::LaneCount;
        Vec3Lanes position = Vec3Lanes::LoadMembersPartial(
            bodies.data() + begin,
            batchCount,
            0.0F,
            &EcsKernelPhysicsBody::positionX,
            &EcsKernelPhysicsBody::positionY,
            &EcsKernelPhysicsBody::positionZ);
        Vec3Lanes velocity = Vec3Lanes::LoadMembersPartial(
            bodies.data() + begin,
            batchCount,
            0.0F,
            &EcsKernelPhysicsBody::velocityX,
            &EcsKernelPhysicsBody::velocityY,
            &EcsKernelPhysicsBody::velocityZ);
        const FloatLanes inverseMass = FloatLanes::LoadMemberPartial(bodies.data() + begin, batchCount, &EcsKernelPhysicsBody::inverseMass);
        const FloatLanes gravityStep = FloatLanes::Splat(gravityY * deltaSeconds) * inverseMass;

        velocity.template Component<1>() = velocity.template Component<1>() + gravityStep;
        position = kb::ecs::KernelDeterministicMulAdd(velocity, FloatLanes::Splat(deltaSeconds), position);
        const FloatLanes speed = SqrtLanes(kb::ecs::KernelDot(velocity, velocity));
        const FloatLanes radius = FloatLanes::LoadMemberPartial(bodies.data() + begin, batchCount, &EcsKernelPhysicsBody::radius);
        const FloatLanes sweptRadius = radius + speed * FloatLanes::Splat(deltaSeconds);

        velocity.StoreMembersPartial(
            bodies.data() + begin,
            batchCount,
            &EcsKernelPhysicsBody::velocityX,
            &EcsKernelPhysicsBody::velocityY,
            &EcsKernelPhysicsBody::velocityZ);
        position.StoreMembersPartial(
            bodies.data() + begin,
            batchCount,
            &EcsKernelPhysicsBody::positionX,
            &EcsKernelPhysicsBody::positionY,
            &EcsKernelPhysicsBody::positionZ);

        for (std::size_t lane = 0; lane < batchCount; ++lane) {
            EcsKernelPhysicsProxy& proxy = proxies[begin + lane];
            proxy.predictedX = position.template Component<0>().Lane(lane);
            proxy.predictedY = position.template Component<1>().Lane(lane);
            proxy.predictedZ = position.template Component<2>().Lane(lane);
            proxy.sweptRadius = sweptRadius.Lane(lane);
        }
    }
}

template <std::size_t Count>
[[nodiscard]] std::array<EcsPosition, Count> CreateCompatibilityPositions() noexcept {
    std::array<EcsPosition, Count> positions{};
    for (std::size_t index = 0; index < Count; ++index) {
        const float sample = static_cast<float>(index);
        positions[index] = EcsPosition{
            .x = sample * 0.1875F - 4.0F,
            .y = 2.5F - sample * 0.09375F,
        };
    }
    return positions;
}

template <std::size_t Count>
[[nodiscard]] std::array<EcsVelocity, Count> CreateCompatibilityVelocities() noexcept {
    std::array<EcsVelocity, Count> velocities{};
    for (std::size_t index = 0; index < Count; ++index) {
        const float sample = static_cast<float>(index);
        velocities[index] = EcsVelocity{
            .x = sample * 0.015625F - 0.375F,
            .y = 0.875F - sample * 0.0234375F,
        };
    }
    return velocities;
}

template <std::size_t Count>
[[nodiscard]] std::array<EcsKernelLocalTransform, Count> CreateCompatibilityTransforms() noexcept {
    std::array<EcsKernelLocalTransform, Count> transforms{};
    for (std::size_t index = 0; index < Count; ++index) {
        const float sample = static_cast<float>(index);
        transforms[index] = EcsKernelLocalTransform{
            .translationX = sample * 0.125F - 3.5F,
            .translationY = 1.75F - sample * 0.0625F,
            .translationZ = sample * 0.03125F - 0.75F,
            .rotationZ = sample * 0.02734375F - 0.3125F,
            .scaleX = 0.75F + static_cast<float>(index % 5U) * 0.125F,
            .scaleY = -1.25F + static_cast<float>(index % 7U) * 0.1875F,
            .scaleZ = 0.5F + static_cast<float>(index % 3U) * 0.25F,
        };
    }
    return transforms;
}

template <std::size_t Count>
[[nodiscard]] std::array<EcsKernelLocalBounds, Count> CreateCompatibilityBounds() noexcept {
    std::array<EcsKernelLocalBounds, Count> bounds{};
    for (std::size_t index = 0; index < Count; ++index) {
        const float sample = static_cast<float>(index);
        bounds[index] = EcsKernelLocalBounds{
            .centerX = sample * 0.03125F - 0.5F,
            .centerY = 0.25F - sample * 0.015625F,
            .centerZ = sample * 0.046875F - 0.875F,
            .extentX = 0.25F + static_cast<float>(index % 4U) * 0.0625F,
            .extentY = 0.375F + static_cast<float>(index % 5U) * 0.03125F,
            .extentZ = 0.5F + static_cast<float>(index % 6U) * 0.046875F,
        };
    }
    return bounds;
}

template <std::size_t Count>
[[nodiscard]] std::array<EcsKernelPhysicsBody, Count> CreateCompatibilityPhysicsBodies() noexcept {
    std::array<EcsKernelPhysicsBody, Count> bodies{};
    for (std::size_t index = 0; index < Count; ++index) {
        const float sample = static_cast<float>(index);
        bodies[index] = EcsKernelPhysicsBody{
            .positionX = sample * 0.09375F - 2.0F,
            .positionY = 3.0F - sample * 0.0546875F,
            .positionZ = sample * 0.0390625F - 0.25F,
            .velocityX = sample * 0.017578125F - 0.375F,
            .velocityY = 1.25F - sample * 0.025390625F,
            .velocityZ = sample * 0.01171875F + 0.125F,
            .radius = 0.25F + static_cast<float>(index % 5U) * 0.03125F,
            .inverseMass = 0.25F + static_cast<float>(index % 4U) * 0.125F,
        };
    }
    return bodies;
}

template <std::size_t Count>
void RequirePositionsBitEqual(const std::array<EcsPosition, Count>& expected, const std::array<EcsPosition, Count>& actual, const char* message) {
    for (std::size_t index = 0; index < Count; ++index) {
        kb::tests::Require(FloatBits(expected[index].x) == FloatBits(actual[index].x), message);
        kb::tests::Require(FloatBits(expected[index].y) == FloatBits(actual[index].y), message);
    }
}

template <std::size_t Count>
void RequireWorldTransformsBitEqual(
    const std::array<EcsKernelWorldTransform, Count>& expected,
    const std::array<EcsKernelWorldTransform, Count>& actual,
    const char* message) {
    for (std::size_t index = 0; index < Count; ++index) {
        for (std::size_t element = 0; element < 16U; ++element) {
            kb::tests::Require(FloatBits(expected[index].matrix[element]) == FloatBits(actual[index].matrix[element]), message);
        }
    }
}

template <std::size_t Count>
void RequireWorldBoundsBitEqual(
    const std::array<EcsKernelWorldBounds, Count>& expected,
    const std::array<EcsKernelWorldBounds, Count>& actual,
    const char* message) {
    for (std::size_t index = 0; index < Count; ++index) {
        kb::tests::Require(FloatBits(expected[index].minX) == FloatBits(actual[index].minX), message);
        kb::tests::Require(FloatBits(expected[index].minY) == FloatBits(actual[index].minY), message);
        kb::tests::Require(FloatBits(expected[index].minZ) == FloatBits(actual[index].minZ), message);
        kb::tests::Require(FloatBits(expected[index].maxX) == FloatBits(actual[index].maxX), message);
        kb::tests::Require(FloatBits(expected[index].maxY) == FloatBits(actual[index].maxY), message);
        kb::tests::Require(FloatBits(expected[index].maxZ) == FloatBits(actual[index].maxZ), message);
    }
}

template <std::size_t Count>
void RequirePhysicsBitEqual(
    const std::array<EcsKernelPhysicsBody, Count>& expectedBodies,
    const std::array<EcsKernelPhysicsBody, Count>& actualBodies,
    const std::array<EcsKernelPhysicsProxy, Count>& expectedProxies,
    const std::array<EcsKernelPhysicsProxy, Count>& actualProxies,
    const char* message) {
    for (std::size_t index = 0; index < Count; ++index) {
        kb::tests::Require(FloatBits(expectedBodies[index].positionX) == FloatBits(actualBodies[index].positionX), message);
        kb::tests::Require(FloatBits(expectedBodies[index].positionY) == FloatBits(actualBodies[index].positionY), message);
        kb::tests::Require(FloatBits(expectedBodies[index].positionZ) == FloatBits(actualBodies[index].positionZ), message);
        kb::tests::Require(FloatBits(expectedBodies[index].velocityX) == FloatBits(actualBodies[index].velocityX), message);
        kb::tests::Require(FloatBits(expectedBodies[index].velocityY) == FloatBits(actualBodies[index].velocityY), message);
        kb::tests::Require(FloatBits(expectedBodies[index].velocityZ) == FloatBits(actualBodies[index].velocityZ), message);
        kb::tests::Require(FloatBits(expectedProxies[index].predictedX) == FloatBits(actualProxies[index].predictedX), message);
        kb::tests::Require(FloatBits(expectedProxies[index].predictedY) == FloatBits(actualProxies[index].predictedY), message);
        kb::tests::Require(FloatBits(expectedProxies[index].predictedZ) == FloatBits(actualProxies[index].predictedZ), message);
        kb::tests::Require(FloatBits(expectedProxies[index].sweptRadius) == FloatBits(actualProxies[index].sweptRadius), message);
    }
}

template <typename BackendTag>
void RunKernelScalarSimdCompatibilityCase(const char* message) {
    constexpr std::size_t kCount = 29U;

    const std::array<EcsVelocity, kCount> velocities = CreateCompatibilityVelocities<kCount>();
    std::array<EcsPosition, kCount> expectedPositions = CreateCompatibilityPositions<kCount>();
    std::array<EcsPosition, kCount> actualPositions = expectedPositions;
    RunCompatibilityMovementKernel<kb::ecs::KernelScalarTag>(expectedPositions, velocities);
    RunCompatibilityMovementKernel<BackendTag>(actualPositions, velocities);
    RequirePositionsBitEqual(expectedPositions, actualPositions, message);

    const std::array<EcsKernelLocalTransform, kCount> transforms = CreateCompatibilityTransforms<kCount>();
    std::array<EcsKernelWorldTransform, kCount> expectedWorldTransforms{};
    std::array<EcsKernelWorldTransform, kCount> actualWorldTransforms{};
    RunCompatibilityTransformKernel<kb::ecs::KernelScalarTag>(transforms, expectedWorldTransforms);
    RunCompatibilityTransformKernel<BackendTag>(transforms, actualWorldTransforms);
    RequireWorldTransformsBitEqual(expectedWorldTransforms, actualWorldTransforms, message);

    const std::array<EcsKernelLocalBounds, kCount> localBounds = CreateCompatibilityBounds<kCount>();
    std::array<EcsKernelWorldBounds, kCount> expectedWorldBounds{};
    std::array<EcsKernelWorldBounds, kCount> actualWorldBounds{};
    RunCompatibilityBoundsKernel<kb::ecs::KernelScalarTag>(localBounds, transforms, expectedWorldBounds);
    RunCompatibilityBoundsKernel<BackendTag>(localBounds, transforms, actualWorldBounds);
    RequireWorldBoundsBitEqual(expectedWorldBounds, actualWorldBounds, message);

    std::array<EcsKernelPhysicsBody, kCount> expectedBodies = CreateCompatibilityPhysicsBodies<kCount>();
    std::array<EcsKernelPhysicsBody, kCount> actualBodies = expectedBodies;
    std::array<EcsKernelPhysicsProxy, kCount> expectedProxies{};
    std::array<EcsKernelPhysicsProxy, kCount> actualProxies{};
    RunCompatibilityPhysicsProxyKernel<kb::ecs::KernelScalarTag>(expectedBodies, expectedProxies);
    RunCompatibilityPhysicsProxyKernel<BackendTag>(actualBodies, actualProxies);
    RequirePhysicsBitEqual(expectedBodies, actualBodies, expectedProxies, actualProxies, message);
}

void RunEcsKernelContractScalarExecutionTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 8,
    });

    for (int index = 0; index < 17; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsVelocity{ .x = 2.0F, .y = 4.0F });
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 1.0F });
    }

    kb::ecs::SystemAccess access = kb::ecs::DeclareKernelAccess<EcsMovementKernelContract>(world);
    kb::tests::Require(access.ReadComponents().size() == 1U, "ECS kernel access contract did not declare input component reads");
    kb::tests::Require(access.WriteComponents().size() == 1U, "ECS kernel access contract did not declare output component writes");
    kb::tests::Require(access.ReadComponents().front() != access.WriteComponents().front(), "ECS kernel access contract overlapped input and output components");

    kb::ecs::KernelQuery<EcsMovementKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
    const EcsMovementScaleAsset asset{ .multiplier = 3.0F };
    const EcsMovementConstants constants{
        .deltaSeconds = 0.5F,
        .drag = 1.0F,
    };

    kb::ecs::ExecuteKernelScalar<EcsMovementKernelContract>(
        query,
        kb::ecs::QueryExecutionSettings{
            .maxBatchSize = 8,
            .prefetchDistance = 4,
        },
        EcsMovementKernel{},
        kb::ecs::BindKernelAssets(asset),
        constants);

    int visited = 0;
    float sumX = 0.0F;
    float sumY = 0.0F;
    query.ForEachBatchKernel([&visited, &sumX, &sumY](const kb::ecs::QueryBatch<EcsVelocity, EcsPosition>& batch) {
        const EcsPosition* positions = batch.Components<1>();
        for (std::size_t index = 0; index < batch.Count(); ++index) {
            ++visited;
            sumX += positions[index].x;
            sumY += positions[index].y;
        }
    });

    kb::tests::Require(visited == 17, "ECS scalar kernel did not visit all matching entities");
    kb::tests::Require(kb::tests::NearlyEqual(sumX, 187.0F), "ECS scalar kernel did not apply input components, output components, assets and constants to X");
    kb::tests::Require(kb::tests::NearlyEqual(sumY, 85.0F), "ECS scalar kernel did not apply input components, output components, assets and constants to Y");
}

void RunEcsKernelRequestedSimdFallsBackToScalarTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 5,
    });
    PopulateSimdProbeWorld(world, 13);

    int scalarBatches = 0;
    kb::ecs::KernelQuery<EcsSimdKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
    const kb::ecs::KernelNoConstants constants{};
    kb::ecs::QueryBatchExecutionScratch scratch;
    query.PrepareMutableBatchExecution(kb::ecs::QueryExecutionSettings{ .maxBatchSize = 5 }, scratch);
    kb::ecs::ExecuteKernelAvx2<EcsSimdKernelContract>(
        query,
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 5 },
        EcsScalarOnlyProbeKernel{ .scalarBatches = &scalarBatches },
        kb::ecs::BindKernelAssets(),
        constants,
        scratch);

    kb::tests::Require(scalarBatches == 3, "ECS kernel did not use scalar fallback for a requested SIMD backend without an overload");
    kb::tests::Require(kb::tests::NearlyEqual(SumPositions(query), 39.0F), "ECS scalar fallback did not process all kernel rows");
}

void RunEcsKernelSse2AndAvx2DispatchTest() {
    const kb::ecs::KernelNoConstants constants{};

    {
        kb::ecs::World world(kb::ecs::WorldConfig{
            .executionGrainSize = 4,
        });
        PopulateSimdProbeWorld(world, 8);

        int scalarBatches = 0;
        int sse2Batches = 0;
        int avx2Batches = 0;
        int avx512Batches = 0;
        int neonBatches = 0;
        kb::ecs::KernelQuery<EcsSimdKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
        auto compiled = kb::ecs::CompileKernelQuery<EcsSimdKernelContract>(
            std::move(query),
            kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 },
            kb::ecs::KernelBackendPreference::Sse2,
            EcsSimdProbeKernel{
                .scalarBatches = &scalarBatches,
                .sse2Batches = &sse2Batches,
                .avx2Batches = &avx2Batches,
                .avx512Batches = &avx512Batches,
                .neonBatches = &neonBatches,
            },
            kb::ecs::BindKernelAssets(),
            constants);

        compiled.Execute();
        kb::ecs::KernelQuery<EcsSimdKernelContract> verifyQuery = world.CreateQuery<EcsVelocity, EcsPosition>();
        if (kb::ecs::IsKernelBackendSupported(kb::ecs::KernelBackend::Sse2)) {
            kb::tests::Require(compiled.ResolvedBackend() == kb::ecs::KernelBackend::Sse2, "ECS kernel did not resolve the requested SSE2 backend");
            kb::tests::Require(sse2Batches == 2 && scalarBatches == 0 && avx2Batches == 0 && avx512Batches == 0 && neonBatches == 0, "ECS kernel did not dispatch through the SSE2 overload");
            kb::tests::Require(kb::tests::NearlyEqual(SumPositions(verifyQuery), 48.0F), "ECS SSE2 kernel path did not process all rows");
        } else {
            kb::tests::Require(compiled.ResolvedBackend() == kb::ecs::KernelBackend::Scalar, "ECS kernel did not resolve SSE2 to scalar on unsupported hardware");
            kb::tests::Require(scalarBatches == 2 && sse2Batches == 0 && avx2Batches == 0 && avx512Batches == 0 && neonBatches == 0, "ECS kernel did not fall back to scalar when SSE2 was unavailable");
            kb::tests::Require(kb::tests::NearlyEqual(SumPositions(verifyQuery), 24.0F), "ECS scalar fallback for SSE2 did not process all rows");
        }
    }

    {
        kb::ecs::World world(kb::ecs::WorldConfig{
            .executionGrainSize = 4,
        });
        PopulateSimdProbeWorld(world, 8);

        int scalarBatches = 0;
        int sse2Batches = 0;
        int avx2Batches = 0;
        int avx512Batches = 0;
        int neonBatches = 0;
        kb::ecs::KernelQuery<EcsSimdKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
        kb::ecs::ExecuteKernelAvx2<EcsSimdKernelContract>(
            query,
            kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 },
            EcsSimdProbeKernel{
                .scalarBatches = &scalarBatches,
                .sse2Batches = &sse2Batches,
                .avx2Batches = &avx2Batches,
                .avx512Batches = &avx512Batches,
                .neonBatches = &neonBatches,
            },
            kb::ecs::BindKernelAssets(),
            constants);

        if (kb::ecs::IsKernelBackendSupported(kb::ecs::KernelBackend::Avx2)) {
            kb::tests::Require(avx2Batches == 2 && scalarBatches == 0 && sse2Batches == 0 && avx512Batches == 0 && neonBatches == 0, "ECS kernel did not dispatch through the AVX2 overload");
            kb::tests::Require(kb::tests::NearlyEqual(SumPositions(query), 96.0F), "ECS AVX2 kernel path did not process all rows");
        } else {
            kb::tests::Require(scalarBatches == 2 && sse2Batches == 0 && avx2Batches == 0 && avx512Batches == 0 && neonBatches == 0, "ECS kernel did not fall back to scalar when AVX2 was unavailable");
            kb::tests::Require(kb::tests::NearlyEqual(SumPositions(query), 24.0F), "ECS scalar fallback for AVX2 did not process all rows");
        }
    }
}

void RunEcsKernelNeonDispatchTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 4,
    });
    PopulateSimdProbeWorld(world, 8);

    int scalarBatches = 0;
    int sse2Batches = 0;
    int avx2Batches = 0;
    int avx512Batches = 0;
    int neonBatches = 0;
    const kb::ecs::KernelNoConstants constants{};
    kb::ecs::KernelQuery<EcsSimdKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
    auto compiled = kb::ecs::CompileKernelQuery<EcsSimdKernelContract>(
        std::move(query),
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 },
        kb::ecs::KernelBackendPreference::Neon,
        EcsSimdProbeKernel{
            .scalarBatches = &scalarBatches,
            .sse2Batches = &sse2Batches,
            .avx2Batches = &avx2Batches,
            .avx512Batches = &avx512Batches,
            .neonBatches = &neonBatches,
        },
        kb::ecs::BindKernelAssets(),
        constants);

    compiled.Execute();
    kb::ecs::KernelQuery<EcsSimdKernelContract> verifyQuery = world.CreateQuery<EcsVelocity, EcsPosition>();
    if (kb::ecs::IsKernelBackendSupported(kb::ecs::KernelBackend::Neon)) {
        kb::tests::Require(compiled.ResolvedBackend() == kb::ecs::KernelBackend::Neon, "ECS kernel did not resolve the requested NEON backend");
        kb::tests::Require(neonBatches == 2 && scalarBatches == 0 && sse2Batches == 0 && avx2Batches == 0 && avx512Batches == 0, "ECS kernel did not dispatch through the NEON overload");
        kb::tests::Require(kb::tests::NearlyEqual(SumPositions(verifyQuery), 72.0F), "ECS NEON kernel path did not process all rows");
    } else {
        kb::tests::Require(compiled.ResolvedBackend() == kb::ecs::KernelBackend::Scalar, "ECS kernel did not resolve NEON to scalar on unsupported hardware");
        kb::tests::Require(scalarBatches == 2 && neonBatches == 0 && sse2Batches == 0 && avx2Batches == 0 && avx512Batches == 0, "ECS kernel did not fall back to scalar when NEON was unavailable");
        kb::tests::Require(kb::tests::NearlyEqual(SumPositions(verifyQuery), 24.0F), "ECS scalar fallback for NEON did not process all rows");
    }
}

void RunEcsKernelAvx512DispatchTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 4,
    });
    PopulateSimdProbeWorld(world, 8);

    int scalarBatches = 0;
    int sse2Batches = 0;
    int avx2Batches = 0;
    int avx512Batches = 0;
    int neonBatches = 0;
    const kb::ecs::KernelNoConstants constants{};
    kb::ecs::KernelQuery<EcsSimdKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
    auto compiled = kb::ecs::CompileKernelQuery<EcsSimdKernelContract>(
        std::move(query),
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 },
        kb::ecs::KernelBackendPreference::Avx512,
        EcsSimdProbeKernel{
            .scalarBatches = &scalarBatches,
            .sse2Batches = &sse2Batches,
            .avx2Batches = &avx2Batches,
            .avx512Batches = &avx512Batches,
            .neonBatches = &neonBatches,
        },
        kb::ecs::BindKernelAssets(),
        constants);

    compiled.Execute();
    kb::ecs::KernelQuery<EcsSimdKernelContract> verifyQuery = world.CreateQuery<EcsVelocity, EcsPosition>();
    if (kb::ecs::IsKernelBackendSupported(kb::ecs::KernelBackend::Avx512)) {
        kb::tests::Require(compiled.ResolvedBackend() == kb::ecs::KernelBackend::Avx512, "ECS kernel did not resolve the requested AVX-512 backend");
        kb::tests::Require(avx512Batches == 2 && scalarBatches == 0 && sse2Batches == 0 && avx2Batches == 0 && neonBatches == 0, "ECS kernel did not dispatch through the AVX-512 overload");
        kb::tests::Require(kb::tests::NearlyEqual(SumPositions(verifyQuery), 192.0F), "ECS AVX-512 kernel path did not process all rows");
    } else {
        kb::tests::Require(compiled.ResolvedBackend() == kb::ecs::KernelBackend::Scalar, "ECS kernel did not resolve AVX-512 to scalar on unsupported hardware");
        kb::tests::Require(scalarBatches == 2 && sse2Batches == 0 && avx2Batches == 0 && avx512Batches == 0 && neonBatches == 0, "ECS kernel did not fall back to scalar when AVX-512 was unavailable");
        kb::tests::Require(kb::tests::NearlyEqual(SumPositions(verifyQuery), 24.0F), "ECS scalar fallback for AVX-512 did not process all rows");
    }
}

void RunEcsCompiledKernelBackendPreferenceUpdateTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 4,
    });
    PopulateSimdProbeWorld(world, 8);

    int scalarBatches = 0;
    int sse2Batches = 0;
    int avx2Batches = 0;
    int avx512Batches = 0;
    int neonBatches = 0;
    const kb::ecs::KernelNoConstants constants{};
    kb::ecs::KernelQuery<EcsSimdKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
    auto compiled = kb::ecs::CompileKernelQuery<EcsSimdKernelContract>(
        std::move(query),
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 },
        kb::ecs::KernelBackendPreference::Scalar,
        EcsSimdProbeKernel{
            .scalarBatches = &scalarBatches,
            .sse2Batches = &sse2Batches,
            .avx2Batches = &avx2Batches,
            .avx512Batches = &avx512Batches,
            .neonBatches = &neonBatches,
        },
        kb::ecs::BindKernelAssets(),
        constants);

    kb::tests::Require(compiled.ResolvedBackend() == kb::ecs::KernelBackend::Scalar, "ECS compiled kernel did not cache the scalar backend");
    compiled.Execute();
    kb::tests::Require(scalarBatches == 2 && sse2Batches == 0 && avx2Batches == 0 && avx512Batches == 0 && neonBatches == 0, "ECS compiled kernel did not execute the cached scalar backend");

    compiled.SetBackendPreference(kb::ecs::KernelBackendPreference::Sse2);
    const kb::ecs::KernelBackend expectedBackend = kb::ecs::IsKernelBackendSupported(kb::ecs::KernelBackend::Sse2)
        ? kb::ecs::KernelBackend::Sse2
        : kb::ecs::KernelBackend::Scalar;
    kb::tests::Require(compiled.ResolvedBackend() == expectedBackend, "ECS compiled kernel did not refresh the cached backend after preference change");

    compiled.Execute();
    if (expectedBackend == kb::ecs::KernelBackend::Sse2) {
        kb::tests::Require(scalarBatches == 2 && sse2Batches == 2 && avx2Batches == 0 && avx512Batches == 0 && neonBatches == 0, "ECS compiled kernel did not execute the refreshed SSE2 backend");
    } else {
        kb::tests::Require(scalarBatches == 4 && sse2Batches == 0 && avx2Batches == 0 && avx512Batches == 0 && neonBatches == 0, "ECS compiled kernel did not execute the refreshed scalar fallback backend");
    }
}

void RunEcsCompiledKernelQueryExecutionTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 4,
    });

    for (int index = 0; index < 10; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsVelocity{ .x = 2.0F, .y = 4.0F });
        world.Set(entity, EcsPosition{ .x = static_cast<float>(index), .y = 1.0F });
    }

    const EcsMovementScaleAsset asset{ .multiplier = 4.0F };
    const EcsMovementConstants constants{
        .deltaSeconds = 0.25F,
        .drag = 0.5F,
    };

    auto kernelQuery = kb::ecs::CompileKernelQuery<EcsMovementKernelContract>(
        world,
        kb::ecs::QueryExecutionSettings{
            .maxBatchSize = 4,
            .prefetchDistance = 2,
        },
        EcsMovementKernel{},
        kb::ecs::BindKernelAssets(asset),
        constants);

    kb::tests::Require(kernelQuery.IsValid(), "Compiled ECS kernel query did not create a valid typed query");
    kb::tests::Require(kernelQuery.Settings().maxBatchSize == 4U, "Compiled ECS kernel query did not retain execution settings");

    kernelQuery.Execute();
    kernelQuery.Execute();

    kb::ecs::KernelQuery<EcsMovementKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
    int visited = 0;
    float sumX = 0.0F;
    float sumY = 0.0F;
    query.ForEachBatchKernel([&visited, &sumX, &sumY](const kb::ecs::QueryBatch<EcsVelocity, EcsPosition>& batch) {
        const EcsPosition* positions = batch.Components<1>();
        for (std::size_t index = 0; index < batch.Count(); ++index) {
            ++visited;
            sumX += positions[index].x;
            sumY += positions[index].y;
        }
    });

    kb::tests::Require(visited == 10, "Compiled ECS kernel query did not visit all matching entities");
    kb::tests::Require(kb::tests::NearlyEqual(sumX, 85.0F), "Compiled ECS kernel query did not persist output component writes to X");
    kb::tests::Require(kb::tests::NearlyEqual(sumY, 80.0F), "Compiled ECS kernel query did not persist output component writes to Y");
}

void RunEcsKernelAlignedComponentContractTest() {
    static_assert(alignof(EcsKernelAlignedInput) == 32U);
    static_assert(alignof(EcsKernelAlignedOutput) == 32U);

    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 4,
    });

    for (int index = 0; index < 9; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        world.Set(entity, EcsKernelAlignedInput{ .x = static_cast<float>(index) });
        world.Set(entity, EcsKernelAlignedOutput{});
    }

    auto compiled = kb::ecs::CompileKernelQuery<EcsAlignedKernelContract>(
        world,
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 4 },
        kb::ecs::KernelBackendPreference::Avx2,
        EcsAlignedKernel{},
        kb::ecs::BindKernelAssets(),
        kb::ecs::KernelNoConstants{});
    compiled.Execute();

    kb::ecs::KernelQuery<EcsAlignedKernelContract> query = world.CreateQuery<EcsKernelAlignedInput, EcsKernelAlignedOutput>();
    int visited = 0;
    float sum = 0.0F;
    query.ForEachBatchKernel([&visited, &sum](const kb::ecs::QueryBatch<EcsKernelAlignedInput, EcsKernelAlignedOutput>& batch) {
        const EcsKernelAlignedOutput* outputs = batch.Components<1>();
        for (std::size_t index = 0; index < batch.Count(); ++index) {
            ++visited;
            sum += outputs[index].x;
        }
    });

    kb::tests::Require(visited == 9, "ECS aligned kernel contract did not visit all entities");
    kb::tests::Require(kb::tests::NearlyEqual(sum, 72.0F), "ECS aligned kernel contract did not write expected output");
}

void RunEcsCompiledKernelMatchesQueryPathTest() {
    constexpr int kCount = 17;
    kb::ecs::World kernelWorld(kb::ecs::WorldConfig{ .executionGrainSize = 5 });
    kb::ecs::World queryWorld(kb::ecs::WorldConfig{ .executionGrainSize = 5 });

    std::vector<kb::ecs::Entity> kernelEntities;
    std::vector<kb::ecs::Entity> queryEntities;
    kernelEntities.reserve(kCount);
    queryEntities.reserve(kCount);
    for (int index = 0; index < kCount; ++index) {
        const EcsPosition position{
            .x = static_cast<float>(index) * 0.25F - 2.0F,
            .y = static_cast<float>(index) * -0.125F + 3.0F,
        };
        const EcsVelocity velocity{
            .x = static_cast<float>(index) * 0.5F + 1.0F,
            .y = static_cast<float>(index) * -0.25F + 0.75F,
        };

        const kb::ecs::Entity kernelEntity = kernelWorld.CreateEntity();
        kernelWorld.Set(kernelEntity, position);
        kernelWorld.Set(kernelEntity, velocity);
        kernelEntities.push_back(kernelEntity);

        const kb::ecs::Entity queryEntity = queryWorld.CreateEntity();
        queryWorld.Set(queryEntity, position);
        queryWorld.Set(queryEntity, velocity);
        queryEntities.push_back(queryEntity);
    }

    const EcsMovementScaleAsset asset{ .multiplier = 3.0F };
    const EcsMovementConstants constants{
        .deltaSeconds = 0.125F,
        .drag = 0.5F,
    };
    auto compiled = kb::ecs::CompileKernelQuery<EcsMovementKernelContract>(
        kernelWorld,
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 5 },
        kb::ecs::KernelBackendPreference::Scalar,
        EcsMovementKernel{},
        kb::ecs::BindKernelAssets(asset),
        constants);
    compiled.Execute();

    kb::ecs::KernelQuery<EcsMovementKernelContract> query = queryWorld.CreateQuery<EcsVelocity, EcsPosition>();
    query.ForEachMutableBatchKernel(kb::ecs::QueryExecutionSettings{ .maxBatchSize = 5 }, [&asset, &constants](kb::ecs::MutableQueryBatch<EcsVelocity, EcsPosition>& batch) {
        const EcsVelocity* velocities = batch.Components<0>();
        EcsPosition* positions = batch.Components<1>();
        for (std::size_t index = 0; index < batch.Count(); ++index) {
            positions[index].x += velocities[index].x * constants.deltaSeconds * asset.multiplier;
            positions[index].y += velocities[index].y * constants.deltaSeconds * (asset.multiplier - constants.drag);
        }
    });

    for (std::size_t index = 0; index < kernelEntities.size(); ++index) {
        const EcsPosition* kernelPosition = kernelWorld.TryGet<EcsPosition>(kernelEntities[index]);
        const EcsPosition* queryPosition = queryWorld.TryGet<EcsPosition>(queryEntities[index]);
        kb::tests::Require(kernelPosition != nullptr && queryPosition != nullptr, "ECS compiled/query path comparison lost a position component");
        kb::tests::Require(FloatBits(kernelPosition->x) == FloatBits(queryPosition->x), "ECS compiled kernel path did not match query path X bit pattern");
        kb::tests::Require(FloatBits(kernelPosition->y) == FloatBits(queryPosition->y), "ECS compiled kernel path did not match query path Y bit pattern");
    }
}

void RunEcsCompiledKernelSystemRegistrationTest() {
    kb::ecs::World world;
    PopulateSimdProbeWorld(world, 12);

    int scalarBatches = 0;
    kb::ecs::SystemScheduler scheduler{ kb::ecs::SystemSchedulerConfig{ .profilerEnabled = true } };
    scheduler.Add(
        std::make_unique<kb::ecs::CompiledKernelSystem<EcsSimdKernelContract, EcsScalarOnlyProbeKernel>>(
            kb::ecs::QueryExecutionSettings{ .maxBatchSize = 5 },
            EcsScalarOnlyProbeKernel{ .scalarBatches = &scalarBatches }),
        world);

    scheduler.Update(world, 0.0F);

    kb::ecs::KernelQuery<EcsSimdKernelContract> query = world.CreateQuery<EcsVelocity, EcsPosition>();
    kb::tests::Require(scalarBatches > 0, "Compiled ECS kernel system did not execute any kernel batches");
    kb::tests::Require(kb::tests::NearlyEqual(SumPositions(query), 36.0F), "Compiled ECS kernel system did not update component data");
    kb::tests::Require(scheduler.LastProfilerTrace().events.size() == 1U, "Compiled ECS kernel system trace omitted execution event");
    kb::tests::Require(scheduler.LastProfilerTrace().events[0].executionPath == "compiled_kernel", "Compiled ECS kernel system trace did not report compiled kernel execution path");

    scheduler.Shutdown(world);
}

void RunEcsEditorHotReloadKernelPathTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 3,
    });
    PopulateSimdProbeWorld(world, 6);

    EcsEditorKernelContext context{
        .scale = 1.0F,
    };
    auto binding = kb::ecs::MakeEditorKernelBinding<EcsSimdKernelContract>(&EcsEditorHotReloadKernelV1, &context);
    auto compiled = kb::ecs::CompileEditorKernelQuery<EcsSimdKernelContract>(
        world,
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 3 },
        binding,
        kb::ecs::BindKernelAssets(),
        kb::ecs::KernelNoConstants{});

    kb::tests::Require(compiled.IsValid(), "ECS editor kernel query did not create a valid hot-reload binding");
    kb::tests::Require(compiled.Execute(), "ECS editor kernel query did not execute the initial binding");
    kb::ecs::KernelQuery<EcsSimdKernelContract> firstVerifyQuery = world.CreateQuery<EcsVelocity, EcsPosition>();
    kb::tests::Require(context.batches == 2, "ECS editor kernel path did not batch through the initial binding");
    kb::tests::Require(kb::tests::NearlyEqual(SumPositions(firstVerifyQuery), 18.0F), "ECS editor kernel initial binding did not update all rows");

    binding->Bind(&EcsEditorHotReloadKernelV2, &context);
    kb::tests::Require(compiled.Execute(), "ECS editor kernel query did not execute the replaced binding");
    kb::ecs::KernelQuery<EcsSimdKernelContract> secondVerifyQuery = world.CreateQuery<EcsVelocity, EcsPosition>();
    kb::tests::Require(context.batches == 4, "ECS editor kernel path rebuilt or skipped batches after hot reload");
    kb::tests::Require(kb::tests::NearlyEqual(SumPositions(secondVerifyQuery), 54.0F), "ECS editor kernel replaced binding did not update all rows");
}

void RunEcsEditorKernelClearedDuringExecutionTest() {
    kb::ecs::World world(kb::ecs::WorldConfig{
        .executionGrainSize = 3,
    });
    PopulateSimdProbeWorld(world, 6);

    auto binding = kb::ecs::MakeEditorKernelBinding<EcsSimdKernelContract>(&EcsEditorClearingKernel, nullptr);
    EcsEditorClearingKernelContext context{
        .binding = binding.get(),
    };
    binding->Bind(&EcsEditorClearingKernel, &context);

    auto compiled = kb::ecs::CompileEditorKernelQuery<EcsSimdKernelContract>(
        world,
        kb::ecs::QueryExecutionSettings{ .maxBatchSize = 3 },
        binding,
        kb::ecs::BindKernelAssets(),
        kb::ecs::KernelNoConstants{});

    kb::tests::Require(!compiled.Execute(), "ECS editor kernel query did not report a binding cleared during execution");
    kb::tests::Require(context.batches == 1, "ECS editor kernel clearing test did not stop invoking cleared bindings");
}

void RunEcsKernelVectorMathBitDeterminismTest() {
    constexpr std::size_t kCount = 23U;
    std::array<EcsPosition, kCount> positions{};
    std::array<EcsVelocity, kCount> velocities{};

    for (std::size_t index = 0; index < kCount; ++index) {
        const float sample = static_cast<float>(index);
        positions[index] = EcsPosition{
            .x = sample * 0.125F - 1.5F,
            .y = 3.0F - sample * 0.0625F,
        };
        velocities[index] = EcsVelocity{
            .x = sample * 0.03125F - 0.5F,
            .y = 1.25F - sample * 0.046875F,
        };
    }

    const std::array<std::uint32_t, kCount * 2U> reference = ReferenceMovementBits(positions, velocities);
    kb::tests::Require(VectorMovementBits<kb::ecs::KernelScalarTag>(positions, velocities) == reference, "ECS scalar vector math was not bit-for-bit with reference movement");
    kb::tests::Require(VectorMovementBits<kb::ecs::KernelNeonTag>(positions, velocities) == reference, "ECS NEON-width vector math was not bit-for-bit deterministic");
    kb::tests::Require(VectorMovementBits<kb::ecs::KernelSse2Tag>(positions, velocities) == reference, "ECS SSE2-width vector math was not bit-for-bit deterministic");
    kb::tests::Require(VectorMovementBits<kb::ecs::KernelAvx2Tag>(positions, velocities) == reference, "ECS AVX2-width vector math was not bit-for-bit deterministic");
    kb::tests::Require(VectorMovementBits<kb::ecs::KernelAvx512Tag>(positions, velocities) == reference, "ECS AVX-512-width vector math was not bit-for-bit deterministic");
}

void RunEcsKernelScalarSimdCompatibilityTest() {
    RunKernelScalarSimdCompatibilityCase<kb::ecs::KernelNeonTag>("ECS NEON kernel compatibility output diverged from scalar");
    RunKernelScalarSimdCompatibilityCase<kb::ecs::KernelSse2Tag>("ECS SSE2 kernel compatibility output diverged from scalar");
    RunKernelScalarSimdCompatibilityCase<kb::ecs::KernelAvx2Tag>("ECS AVX2 kernel compatibility output diverged from scalar");
    RunKernelScalarSimdCompatibilityCase<kb::ecs::KernelAvx512Tag>("ECS AVX-512 kernel compatibility output diverged from scalar");
}

template <typename BackendTag>
void RunKernelVectorMathUnalignedFallbackCase() {
    using FloatLanes = kb::ecs::KernelFloatLanes<BackendTag>;

    alignas(FloatLanes::PreferredAlignment) std::array<float, FloatLanes::LaneCount> alignedValues{};
    for (std::size_t lane = 0; lane < FloatLanes::LaneCount; ++lane) {
        alignedValues[lane] = static_cast<float>(lane) * 0.5F - 2.0F;
    }
    kb::tests::Require(FloatLanes::IsAligned(alignedValues.data()), "ECS kernel aligned test buffer was not aligned to the backend lane width");
    const FloatLanes alignedLoaded = FloatLanes::LoadAligned(alignedValues.data());
    kb::tests::Require(FloatBits(alignedLoaded.Lane(FloatLanes::LaneCount - 1U)) == FloatBits(alignedValues.back()), "ECS kernel aligned load read invalid data");

    std::array<std::byte, sizeof(float) * FloatLanes::LaneCount + 1U> inputBytes{};
    std::array<std::byte, sizeof(float) * FloatLanes::LaneCount + 1U> outputBytes{};
    std::byte* input = inputBytes.data() + 1U;
    std::byte* output = outputBytes.data() + 1U;
    kb::tests::Require(!FloatLanes::IsAligned(input), "ECS kernel unaligned test input unexpectedly met backend alignment");
    kb::tests::Require(!FloatLanes::IsAligned(output), "ECS kernel unaligned test output unexpectedly met backend alignment");

    for (std::size_t lane = 0; lane < FloatLanes::LaneCount; ++lane) {
        const float value = static_cast<float>(lane) * 1.25F + 0.75F;
        std::memcpy(input + lane * sizeof(float), &value, sizeof(float));
    }

    const FloatLanes shifted = FloatLanes::Load(reinterpret_cast<const float*>(input)) + FloatLanes::Splat(3.5F);
    shifted.Store(reinterpret_cast<float*>(output));

    for (std::size_t lane = 0; lane < FloatLanes::LaneCount; ++lane) {
        const float expected = static_cast<float>(lane) * 1.25F + 4.25F;
        float actual = 0.0F;
        std::memcpy(&actual, output + lane * sizeof(float), sizeof(float));
        kb::tests::Require(FloatBits(actual) == FloatBits(expected), "ECS kernel vector math unaligned fallback did not preserve float bits");
    }
}

void RunEcsKernelVectorMathUnalignedFallbackTest() {
    RunKernelVectorMathUnalignedFallbackCase<kb::ecs::KernelScalarTag>();
    RunKernelVectorMathUnalignedFallbackCase<kb::ecs::KernelNeonTag>();
    RunKernelVectorMathUnalignedFallbackCase<kb::ecs::KernelSse2Tag>();
    RunKernelVectorMathUnalignedFallbackCase<kb::ecs::KernelAvx2Tag>();
    RunKernelVectorMathUnalignedFallbackCase<kb::ecs::KernelAvx512Tag>();
}

void RunEcsKernelBackendReportNamesTest() {
    kb::tests::Require(kb::ecs::KernelBackendName(kb::ecs::KernelBackend::Scalar) == "scalar", "ECS kernel backend report name changed for scalar");
    kb::tests::Require(kb::ecs::KernelBackendName(kb::ecs::KernelBackend::Neon) == "neon", "ECS kernel backend report name changed for NEON");
    kb::tests::Require(kb::ecs::KernelBackendName(kb::ecs::KernelBackend::Sse2) == "sse2", "ECS kernel backend report name changed for SSE2");
    kb::tests::Require(kb::ecs::KernelBackendName(kb::ecs::KernelBackend::Avx2) == "avx2", "ECS kernel backend report name changed for AVX2");
    kb::tests::Require(kb::ecs::KernelBackendName(kb::ecs::KernelBackend::Avx512) == "avx512", "ECS kernel backend report name changed for AVX-512");
    kb::tests::Require(kb::ecs::KernelBackendPreferenceName(kb::ecs::KernelBackendPreference::Auto) == "auto", "ECS kernel backend preference report name changed for auto");
    kb::tests::Require(kb::ecs::KernelBackendPreferenceName(kb::ecs::KernelBackendPreference::Neon) == "neon", "ECS kernel backend preference report name changed for NEON");
}

void RunEcsPreferredKernelBackendReportTest() {
    const kb::ecs::KernelBackend preferred = kb::ecs::PreferredKernelBackend();
    kb::tests::Require(kb::ecs::IsKernelBackendSupported(preferred), "ECS preferred kernel backend selected an unsupported path");
    kb::tests::Require(kb::ecs::KernelBackendFloatLaneCount(kb::ecs::KernelBackend::Scalar) == 1U, "ECS scalar lane count report changed");
    kb::tests::Require(kb::ecs::KernelBackendFloatLaneCount(kb::ecs::KernelBackend::Neon) == 4U, "ECS NEON lane count report changed");
    kb::tests::Require(kb::ecs::KernelBackendFloatLaneCount(kb::ecs::KernelBackend::Sse2) == 4U, "ECS SSE2 lane count report changed");
    kb::tests::Require(kb::ecs::KernelBackendFloatLaneCount(kb::ecs::KernelBackend::Avx2) == 8U, "ECS AVX2 lane count report changed");
    kb::tests::Require(kb::ecs::KernelBackendFloatLaneCount(kb::ecs::KernelBackend::Avx512) == 16U, "ECS AVX-512 lane count report changed");
    kb::tests::Require(kb::ecs::KernelBackendFloatLaneCount(preferred) >= 1U, "ECS preferred backend reported an invalid lane count");

    kb::ecs::KernelBackend expected = kb::ecs::KernelBackend::Scalar;
    if (kb::ecs::IsKernelBackendSupported(kb::ecs::KernelBackend::Neon)) {
        expected = kb::ecs::KernelBackend::Neon;
    } else if (kb::ecs::IsKernelBackendSupported(kb::ecs::KernelBackend::Avx512)) {
        expected = kb::ecs::KernelBackend::Avx512;
    } else if (kb::ecs::IsKernelBackendSupported(kb::ecs::KernelBackend::Avx2)) {
        expected = kb::ecs::KernelBackend::Avx2;
    } else if (kb::ecs::IsKernelBackendSupported(kb::ecs::KernelBackend::Sse2)) {
        expected = kb::ecs::KernelBackend::Sse2;
    }
    kb::tests::Require(preferred == expected, "ECS preferred kernel backend priority changed unexpectedly");
}

} // namespace

namespace kb::tests {

void RunEcsKernelTests() {
    RunEcsKernelContractScalarExecutionTest();
    RunEcsKernelRequestedSimdFallsBackToScalarTest();
    RunEcsKernelSse2AndAvx2DispatchTest();
    RunEcsKernelNeonDispatchTest();
    RunEcsKernelAvx512DispatchTest();
    RunEcsCompiledKernelBackendPreferenceUpdateTest();
    RunEcsCompiledKernelQueryExecutionTest();
    RunEcsKernelAlignedComponentContractTest();
    RunEcsCompiledKernelMatchesQueryPathTest();
    RunEcsCompiledKernelSystemRegistrationTest();
    RunEcsEditorHotReloadKernelPathTest();
    RunEcsEditorKernelClearedDuringExecutionTest();
    RunEcsKernelVectorMathBitDeterminismTest();
    RunEcsKernelScalarSimdCompatibilityTest();
    RunEcsKernelVectorMathUnalignedFallbackTest();
    RunEcsKernelBackendReportNamesTest();
    RunEcsPreferredKernelBackendReportTest();
}

} // namespace kb::tests
