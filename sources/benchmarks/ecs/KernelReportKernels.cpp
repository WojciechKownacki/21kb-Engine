#include "KernelReportKernels.hpp"

#include "engine/ecs/KernelVectorMath.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace kb::ecs::bench {
namespace {

template <typename BackendTag>
using FloatLanes = kb::ecs::KernelFloatLanes<BackendTag>;

template <typename BackendTag>
using Vec3Lanes = kb::ecs::KernelFloat3Lanes<BackendTag>;

template <typename BackendTag>
[[nodiscard]] FloatLanes<BackendTag> Abs(FloatLanes<BackendTag> value) noexcept {
    FloatLanes<BackendTag> result;
    for (std::size_t lane = 0; lane < FloatLanes<BackendTag>::LaneCount; ++lane) {
        result.SetLane(lane, std::fabs(value.Lane(lane)));
    }
    return result;
}

template <typename BackendTag>
[[nodiscard]] FloatLanes<BackendTag> Sqrt(FloatLanes<BackendTag> value) noexcept {
    FloatLanes<BackendTag> result;
    for (std::size_t lane = 0; lane < FloatLanes<BackendTag>::LaneCount; ++lane) {
        result.SetLane(lane, std::sqrt(value.Lane(lane)));
    }
    return result;
}

template <typename BackendTag>
[[nodiscard]] float SumVector(Vec3Lanes<BackendTag> value, std::size_t count) noexcept {
    float checksum = 0.0F;
    for (std::size_t lane = 0; lane < FloatLanes<BackendTag>::LaneCount && lane < count; ++lane) {
        checksum += value.template Component<0>().Lane(lane);
        checksum += value.template Component<1>().Lane(lane);
        checksum += value.template Component<2>().Lane(lane);
    }
    return checksum;
}

template <typename BackendTag>
[[nodiscard]] float SumScalar(FloatLanes<BackendTag> value, std::size_t count) noexcept {
    float checksum = 0.0F;
    for (std::size_t lane = 0; lane < FloatLanes<BackendTag>::LaneCount && lane < count; ++lane) {
        checksum += value.Lane(lane);
    }
    return checksum;
}

template <typename BackendTag>
[[nodiscard]] float MovementKernel(
    KernelReportPosition* positions,
    const KernelReportVelocity* velocities,
    std::size_t count,
    float deltaSeconds) noexcept {
    float checksum = 0.0F;
    for (std::size_t begin = 0; begin < count; begin += FloatLanes<BackendTag>::LaneCount) {
        const std::size_t batchCount = std::min(FloatLanes<BackendTag>::LaneCount, count - begin);
        Vec3Lanes<BackendTag> position = Vec3Lanes<BackendTag>::LoadMembersPartial(
            positions + begin,
            batchCount,
            0.0F,
            &KernelReportPosition::x,
            &KernelReportPosition::y,
            &KernelReportPosition::z);
        const Vec3Lanes<BackendTag> velocity = Vec3Lanes<BackendTag>::LoadMembersPartial(
            velocities + begin,
            batchCount,
            0.0F,
            &KernelReportVelocity::x,
            &KernelReportVelocity::y,
            &KernelReportVelocity::z);

        position = kb::ecs::KernelDeterministicMulAdd(velocity, FloatLanes<BackendTag>::Splat(deltaSeconds), position);
        position.StoreMembersPartial(
            positions + begin,
            batchCount,
            &KernelReportPosition::x,
            &KernelReportPosition::y,
            &KernelReportPosition::z);
        checksum += SumVector(position, batchCount);
    }
    return checksum;
}

template <typename BackendTag>
[[nodiscard]] float TransformKernel(
    const KernelReportLocalTransform* localTransforms,
    KernelReportWorldTransform* worldTransforms,
    std::size_t count) noexcept {
    float checksum = 0.0F;
    for (std::size_t begin = 0; begin < count; begin += FloatLanes<BackendTag>::LaneCount) {
        const std::size_t batchCount = std::min(FloatLanes<BackendTag>::LaneCount, count - begin);
        const FloatLanes<BackendTag> translationX = FloatLanes<BackendTag>::LoadMemberPartial(localTransforms + begin, batchCount, &KernelReportLocalTransform::translationX);
        const FloatLanes<BackendTag> translationY = FloatLanes<BackendTag>::LoadMemberPartial(localTransforms + begin, batchCount, &KernelReportLocalTransform::translationY);
        const FloatLanes<BackendTag> translationZ = FloatLanes<BackendTag>::LoadMemberPartial(localTransforms + begin, batchCount, &KernelReportLocalTransform::translationZ);
        const FloatLanes<BackendTag> rotationZ = FloatLanes<BackendTag>::LoadMemberPartial(localTransforms + begin, batchCount, &KernelReportLocalTransform::rotationZ);
        const FloatLanes<BackendTag> scaleX = FloatLanes<BackendTag>::LoadMemberPartial(localTransforms + begin, batchCount, &KernelReportLocalTransform::scaleX);
        const FloatLanes<BackendTag> scaleY = FloatLanes<BackendTag>::LoadMemberPartial(localTransforms + begin, batchCount, &KernelReportLocalTransform::scaleY);
        const FloatLanes<BackendTag> scaleZ = FloatLanes<BackendTag>::LoadMemberPartial(localTransforms + begin, batchCount, &KernelReportLocalTransform::scaleZ);

        for (std::size_t lane = 0; lane < batchCount; ++lane) {
            const float cosZ = std::cos(rotationZ.Lane(lane));
            const float sinZ = std::sin(rotationZ.Lane(lane));
            KernelReportWorldTransform& world = worldTransforms[begin + lane];

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

            checksum += world.matrix[0] + world.matrix[5] + world.matrix[10] + world.matrix[12] + world.matrix[13] + world.matrix[14];
        }
    }
    return checksum;
}

template <typename BackendTag>
[[nodiscard]] float BoundsKernel(
    const KernelReportLocalBounds* localBounds,
    const KernelReportLocalTransform* transforms,
    KernelReportWorldBounds* worldBounds,
    std::size_t count) noexcept {
    float checksum = 0.0F;
    for (std::size_t begin = 0; begin < count; begin += FloatLanes<BackendTag>::LaneCount) {
        const std::size_t batchCount = std::min(FloatLanes<BackendTag>::LaneCount, count - begin);
        const FloatLanes<BackendTag> centerX = FloatLanes<BackendTag>::LoadMemberPartial(localBounds + begin, batchCount, &KernelReportLocalBounds::centerX);
        const FloatLanes<BackendTag> centerY = FloatLanes<BackendTag>::LoadMemberPartial(localBounds + begin, batchCount, &KernelReportLocalBounds::centerY);
        const FloatLanes<BackendTag> centerZ = FloatLanes<BackendTag>::LoadMemberPartial(localBounds + begin, batchCount, &KernelReportLocalBounds::centerZ);
        const FloatLanes<BackendTag> extentX = FloatLanes<BackendTag>::LoadMemberPartial(localBounds + begin, batchCount, &KernelReportLocalBounds::extentX);
        const FloatLanes<BackendTag> extentY = FloatLanes<BackendTag>::LoadMemberPartial(localBounds + begin, batchCount, &KernelReportLocalBounds::extentY);
        const FloatLanes<BackendTag> extentZ = FloatLanes<BackendTag>::LoadMemberPartial(localBounds + begin, batchCount, &KernelReportLocalBounds::extentZ);
        const FloatLanes<BackendTag> translationX = FloatLanes<BackendTag>::LoadMemberPartial(transforms + begin, batchCount, &KernelReportLocalTransform::translationX);
        const FloatLanes<BackendTag> translationY = FloatLanes<BackendTag>::LoadMemberPartial(transforms + begin, batchCount, &KernelReportLocalTransform::translationY);
        const FloatLanes<BackendTag> translationZ = FloatLanes<BackendTag>::LoadMemberPartial(transforms + begin, batchCount, &KernelReportLocalTransform::translationZ);
        const FloatLanes<BackendTag> scaleX = Abs(FloatLanes<BackendTag>::LoadMemberPartial(transforms + begin, batchCount, &KernelReportLocalTransform::scaleX));
        const FloatLanes<BackendTag> scaleY = Abs(FloatLanes<BackendTag>::LoadMemberPartial(transforms + begin, batchCount, &KernelReportLocalTransform::scaleY));
        const FloatLanes<BackendTag> scaleZ = Abs(FloatLanes<BackendTag>::LoadMemberPartial(transforms + begin, batchCount, &KernelReportLocalTransform::scaleZ));

        const FloatLanes<BackendTag> worldCenterX = kb::ecs::KernelDeterministicMulAdd(centerX, scaleX, translationX);
        const FloatLanes<BackendTag> worldCenterY = kb::ecs::KernelDeterministicMulAdd(centerY, scaleY, translationY);
        const FloatLanes<BackendTag> worldCenterZ = kb::ecs::KernelDeterministicMulAdd(centerZ, scaleZ, translationZ);
        const FloatLanes<BackendTag> worldExtentX = extentX * scaleX;
        const FloatLanes<BackendTag> worldExtentY = extentY * scaleY;
        const FloatLanes<BackendTag> worldExtentZ = extentZ * scaleZ;

        for (std::size_t lane = 0; lane < batchCount; ++lane) {
            KernelReportWorldBounds& bounds = worldBounds[begin + lane];
            bounds.minX = worldCenterX.Lane(lane) - worldExtentX.Lane(lane);
            bounds.minY = worldCenterY.Lane(lane) - worldExtentY.Lane(lane);
            bounds.minZ = worldCenterZ.Lane(lane) - worldExtentZ.Lane(lane);
            bounds.maxX = worldCenterX.Lane(lane) + worldExtentX.Lane(lane);
            bounds.maxY = worldCenterY.Lane(lane) + worldExtentY.Lane(lane);
            bounds.maxZ = worldCenterZ.Lane(lane) + worldExtentZ.Lane(lane);
            checksum += bounds.minX + bounds.minY + bounds.minZ + bounds.maxX + bounds.maxY + bounds.maxZ;
        }
    }
    return checksum;
}

template <typename BackendTag>
[[nodiscard]] float PhysicsProxyKernel(
    KernelReportPhysicsBody* bodies,
    KernelReportPhysicsProxy* proxies,
    std::size_t count,
    float deltaSeconds,
    float gravityY) noexcept {
    float checksum = 0.0F;
    for (std::size_t begin = 0; begin < count; begin += FloatLanes<BackendTag>::LaneCount) {
        const std::size_t batchCount = std::min(FloatLanes<BackendTag>::LaneCount, count - begin);
        Vec3Lanes<BackendTag> position = Vec3Lanes<BackendTag>::LoadMembersPartial(
            bodies + begin,
            batchCount,
            0.0F,
            &KernelReportPhysicsBody::positionX,
            &KernelReportPhysicsBody::positionY,
            &KernelReportPhysicsBody::positionZ);
        Vec3Lanes<BackendTag> velocity = Vec3Lanes<BackendTag>::LoadMembersPartial(
            bodies + begin,
            batchCount,
            0.0F,
            &KernelReportPhysicsBody::velocityX,
            &KernelReportPhysicsBody::velocityY,
            &KernelReportPhysicsBody::velocityZ);
        const FloatLanes<BackendTag> inverseMass = FloatLanes<BackendTag>::LoadMemberPartial(bodies + begin, batchCount, &KernelReportPhysicsBody::inverseMass);
        const FloatLanes<BackendTag> gravityStep = FloatLanes<BackendTag>::Splat(gravityY * deltaSeconds) * inverseMass;

        velocity.template Component<1>() = velocity.template Component<1>() + gravityStep;
        position = kb::ecs::KernelDeterministicMulAdd(velocity, FloatLanes<BackendTag>::Splat(deltaSeconds), position);
        const FloatLanes<BackendTag> speed = Sqrt(kb::ecs::KernelDot(velocity, velocity));
        const FloatLanes<BackendTag> radius = FloatLanes<BackendTag>::LoadMemberPartial(bodies + begin, batchCount, &KernelReportPhysicsBody::radius);
        const FloatLanes<BackendTag> sweptRadius = radius + speed * FloatLanes<BackendTag>::Splat(deltaSeconds);

        velocity.StoreMembersPartial(
            bodies + begin,
            batchCount,
            &KernelReportPhysicsBody::velocityX,
            &KernelReportPhysicsBody::velocityY,
            &KernelReportPhysicsBody::velocityZ);
        position.StoreMembersPartial(
            bodies + begin,
            batchCount,
            &KernelReportPhysicsBody::positionX,
            &KernelReportPhysicsBody::positionY,
            &KernelReportPhysicsBody::positionZ);

        for (std::size_t lane = 0; lane < batchCount; ++lane) {
            KernelReportPhysicsProxy& proxy = proxies[begin + lane];
            proxy.predictedX = position.template Component<0>().Lane(lane);
            proxy.predictedY = position.template Component<1>().Lane(lane);
            proxy.predictedZ = position.template Component<2>().Lane(lane);
            proxy.sweptRadius = sweptRadius.Lane(lane);
        }
        checksum += SumVector(position, batchCount) + SumScalar(sweptRadius, batchCount);
    }
    return checksum;
}

} // namespace

float KbEcsKernelReportMovementScalar(KernelReportPosition* positions, const KernelReportVelocity* velocities, std::size_t count, float deltaSeconds) noexcept {
    return MovementKernel<kb::ecs::KernelScalarTag>(positions, velocities, count, deltaSeconds);
}

float KbEcsKernelReportMovementSse2(KernelReportPosition* positions, const KernelReportVelocity* velocities, std::size_t count, float deltaSeconds) noexcept {
    return MovementKernel<kb::ecs::KernelSse2Tag>(positions, velocities, count, deltaSeconds);
}

float KbEcsKernelReportMovementAvx2(KernelReportPosition* positions, const KernelReportVelocity* velocities, std::size_t count, float deltaSeconds) noexcept {
    return MovementKernel<kb::ecs::KernelAvx2Tag>(positions, velocities, count, deltaSeconds);
}

float KbEcsKernelReportMovementAvx512(KernelReportPosition* positions, const KernelReportVelocity* velocities, std::size_t count, float deltaSeconds) noexcept {
    return MovementKernel<kb::ecs::KernelAvx512Tag>(positions, velocities, count, deltaSeconds);
}

float KbEcsKernelReportTransformScalar(const KernelReportLocalTransform* localTransforms, KernelReportWorldTransform* worldTransforms, std::size_t count) noexcept {
    return TransformKernel<kb::ecs::KernelScalarTag>(localTransforms, worldTransforms, count);
}

float KbEcsKernelReportTransformSse2(const KernelReportLocalTransform* localTransforms, KernelReportWorldTransform* worldTransforms, std::size_t count) noexcept {
    return TransformKernel<kb::ecs::KernelSse2Tag>(localTransforms, worldTransforms, count);
}

float KbEcsKernelReportTransformAvx2(const KernelReportLocalTransform* localTransforms, KernelReportWorldTransform* worldTransforms, std::size_t count) noexcept {
    return TransformKernel<kb::ecs::KernelAvx2Tag>(localTransforms, worldTransforms, count);
}

float KbEcsKernelReportTransformAvx512(const KernelReportLocalTransform* localTransforms, KernelReportWorldTransform* worldTransforms, std::size_t count) noexcept {
    return TransformKernel<kb::ecs::KernelAvx512Tag>(localTransforms, worldTransforms, count);
}

float KbEcsKernelReportBoundsScalar(const KernelReportLocalBounds* localBounds, const KernelReportLocalTransform* transforms, KernelReportWorldBounds* worldBounds, std::size_t count) noexcept {
    return BoundsKernel<kb::ecs::KernelScalarTag>(localBounds, transforms, worldBounds, count);
}

float KbEcsKernelReportBoundsSse2(const KernelReportLocalBounds* localBounds, const KernelReportLocalTransform* transforms, KernelReportWorldBounds* worldBounds, std::size_t count) noexcept {
    return BoundsKernel<kb::ecs::KernelSse2Tag>(localBounds, transforms, worldBounds, count);
}

float KbEcsKernelReportBoundsAvx2(const KernelReportLocalBounds* localBounds, const KernelReportLocalTransform* transforms, KernelReportWorldBounds* worldBounds, std::size_t count) noexcept {
    return BoundsKernel<kb::ecs::KernelAvx2Tag>(localBounds, transforms, worldBounds, count);
}

float KbEcsKernelReportBoundsAvx512(const KernelReportLocalBounds* localBounds, const KernelReportLocalTransform* transforms, KernelReportWorldBounds* worldBounds, std::size_t count) noexcept {
    return BoundsKernel<kb::ecs::KernelAvx512Tag>(localBounds, transforms, worldBounds, count);
}

float KbEcsKernelReportPhysicsProxyScalar(KernelReportPhysicsBody* bodies, KernelReportPhysicsProxy* proxies, std::size_t count, float deltaSeconds, float gravityY) noexcept {
    return PhysicsProxyKernel<kb::ecs::KernelScalarTag>(bodies, proxies, count, deltaSeconds, gravityY);
}

float KbEcsKernelReportPhysicsProxySse2(KernelReportPhysicsBody* bodies, KernelReportPhysicsProxy* proxies, std::size_t count, float deltaSeconds, float gravityY) noexcept {
    return PhysicsProxyKernel<kb::ecs::KernelSse2Tag>(bodies, proxies, count, deltaSeconds, gravityY);
}

float KbEcsKernelReportPhysicsProxyAvx2(KernelReportPhysicsBody* bodies, KernelReportPhysicsProxy* proxies, std::size_t count, float deltaSeconds, float gravityY) noexcept {
    return PhysicsProxyKernel<kb::ecs::KernelAvx2Tag>(bodies, proxies, count, deltaSeconds, gravityY);
}

float KbEcsKernelReportPhysicsProxyAvx512(KernelReportPhysicsBody* bodies, KernelReportPhysicsProxy* proxies, std::size_t count, float deltaSeconds, float gravityY) noexcept {
    return PhysicsProxyKernel<kb::ecs::KernelAvx512Tag>(bodies, proxies, count, deltaSeconds, gravityY);
}

} // namespace kb::ecs::bench

