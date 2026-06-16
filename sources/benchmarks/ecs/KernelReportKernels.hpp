#pragma once

#include <cstddef>

#if defined(_MSC_VER)
#define KB_ECS_KERNEL_REPORT_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define KB_ECS_KERNEL_REPORT_NOINLINE __attribute__((noinline))
#else
#define KB_ECS_KERNEL_REPORT_NOINLINE
#endif

namespace kb::ecs::bench {

struct KernelReportPosition {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct KernelReportVelocity {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct KernelReportLocalTransform {
    float translationX = 0.0F;
    float translationY = 0.0F;
    float translationZ = 0.0F;
    float rotationZ = 0.0F;
    float scaleX = 1.0F;
    float scaleY = 1.0F;
    float scaleZ = 1.0F;
};

struct KernelReportWorldTransform {
    float matrix[16]{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
};

struct KernelReportLocalBounds {
    float centerX = 0.0F;
    float centerY = 0.0F;
    float centerZ = 0.0F;
    float extentX = 0.5F;
    float extentY = 0.5F;
    float extentZ = 0.5F;
};

struct KernelReportWorldBounds {
    float minX = 0.0F;
    float minY = 0.0F;
    float minZ = 0.0F;
    float maxX = 0.0F;
    float maxY = 0.0F;
    float maxZ = 0.0F;
};

struct KernelReportPhysicsBody {
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
    float velocityX = 0.0F;
    float velocityY = 0.0F;
    float velocityZ = 0.0F;
    float radius = 0.5F;
    float inverseMass = 1.0F;
};

struct KernelReportPhysicsProxy {
    float predictedX = 0.0F;
    float predictedY = 0.0F;
    float predictedZ = 0.0F;
    float sweptRadius = 0.0F;
};

extern "C" {
KB_ECS_KERNEL_REPORT_NOINLINE float KbEcsKernelReportMovementScalar(KernelReportPosition* positions, const KernelReportVelocity* velocities, std::size_t count, float deltaSeconds) noexcept;
KB_ECS_KERNEL_REPORT_NOINLINE float KbEcsKernelReportMovementSse2(KernelReportPosition* positions, const KernelReportVelocity* velocities, std::size_t count, float deltaSeconds) noexcept;
KB_ECS_KERNEL_REPORT_NOINLINE float KbEcsKernelReportMovementAvx2(KernelReportPosition* positions, const KernelReportVelocity* velocities, std::size_t count, float deltaSeconds) noexcept;
KB_ECS_KERNEL_REPORT_NOINLINE float KbEcsKernelReportMovementAvx512(KernelReportPosition* positions, const KernelReportVelocity* velocities, std::size_t count, float deltaSeconds) noexcept;

KB_ECS_KERNEL_REPORT_NOINLINE float KbEcsKernelReportTransformScalar(const KernelReportLocalTransform* localTransforms, KernelReportWorldTransform* worldTransforms, std::size_t count) noexcept;
KB_ECS_KERNEL_REPORT_NOINLINE float KbEcsKernelReportTransformSse2(const KernelReportLocalTransform* localTransforms, KernelReportWorldTransform* worldTransforms, std::size_t count) noexcept;
KB_ECS_KERNEL_REPORT_NOINLINE float KbEcsKernelReportTransformAvx2(const KernelReportLocalTransform* localTransforms, KernelReportWorldTransform* worldTransforms, std::size_t count) noexcept;
KB_ECS_KERNEL_REPORT_NOINLINE float KbEcsKernelReportTransformAvx512(const KernelReportLocalTransform* localTransforms, KernelReportWorldTransform* worldTransforms, std::size_t count) noexcept;

KB_ECS_KERNEL_REPORT_NOINLINE float KbEcsKernelReportBoundsScalar(const KernelReportLocalBounds* localBounds, const KernelReportLocalTransform* transforms, KernelReportWorldBounds* worldBounds, std::size_t count) noexcept;
KB_ECS_KERNEL_REPORT_NOINLINE float KbEcsKernelReportBoundsSse2(const KernelReportLocalBounds* localBounds, const KernelReportLocalTransform* transforms, KernelReportWorldBounds* worldBounds, std::size_t count) noexcept;
KB_ECS_KERNEL_REPORT_NOINLINE float KbEcsKernelReportBoundsAvx2(const KernelReportLocalBounds* localBounds, const KernelReportLocalTransform* transforms, KernelReportWorldBounds* worldBounds, std::size_t count) noexcept;
KB_ECS_KERNEL_REPORT_NOINLINE float KbEcsKernelReportBoundsAvx512(const KernelReportLocalBounds* localBounds, const KernelReportLocalTransform* transforms, KernelReportWorldBounds* worldBounds, std::size_t count) noexcept;

KB_ECS_KERNEL_REPORT_NOINLINE float KbEcsKernelReportPhysicsProxyScalar(KernelReportPhysicsBody* bodies, KernelReportPhysicsProxy* proxies, std::size_t count, float deltaSeconds, float gravityY) noexcept;
KB_ECS_KERNEL_REPORT_NOINLINE float KbEcsKernelReportPhysicsProxySse2(KernelReportPhysicsBody* bodies, KernelReportPhysicsProxy* proxies, std::size_t count, float deltaSeconds, float gravityY) noexcept;
KB_ECS_KERNEL_REPORT_NOINLINE float KbEcsKernelReportPhysicsProxyAvx2(KernelReportPhysicsBody* bodies, KernelReportPhysicsProxy* proxies, std::size_t count, float deltaSeconds, float gravityY) noexcept;
KB_ECS_KERNEL_REPORT_NOINLINE float KbEcsKernelReportPhysicsProxyAvx512(KernelReportPhysicsBody* bodies, KernelReportPhysicsProxy* proxies, std::size_t count, float deltaSeconds, float gravityY) noexcept;
}

} // namespace kb::ecs::bench

