#pragma once

#include "engine/ecs/KernelVectorMath.hpp"

#include <cassert>
#include <cstddef>
#include <cmath>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#include <emmintrin.h>
#endif

namespace kb::ecs {

struct HotKernelRange {
    std::size_t begin = 0U;
    std::size_t count = 0U;

    [[nodiscard]] constexpr std::size_t End() const noexcept {
        return begin + count;
    }

    [[nodiscard]] constexpr bool Empty() const noexcept {
        return count == 0U;
    }
};

struct HotKernelMemoryTraffic {
    std::size_t bytesRead = 0U;
    std::size_t bytesWritten = 0U;

    [[nodiscard]] constexpr std::size_t TotalBytes() const noexcept {
        return bytesRead + bytesWritten;
    }
};

struct DenseTransformVersionedApplyStats {
    std::size_t visited = 0U;
    std::size_t updated = 0U;
    std::size_t skipped = 0U;
    double checksum = 0.0;
};

struct DenseTransformVersionRangeSummary {
    HotKernelRange range;
    std::size_t dirtyCount = 0U;
};

enum class TransformTrigPolicy {
    Exact,
    FastApprox,
};

struct TransformSinCos {
    float sin = 0.0F;
    float cos = 1.0F;
};

namespace detail {

inline constexpr float kHotKernelPi = 3.14159265358979323846F;
inline constexpr float kHotKernelTwoPi = 6.28318530717958647692F;
inline constexpr float kHotKernelHalfPi = 1.57079632679489661923F;

[[nodiscard]] inline TransformSinCos FastApproxSinCos(float radians) noexcept {
    float reduced = radians - (kHotKernelTwoPi * std::nearbyint(radians / kHotKernelTwoPi));
    bool flipCos = false;
    if (reduced > kHotKernelHalfPi) {
        reduced = kHotKernelPi - reduced;
        flipCos = true;
    } else if (reduced < -kHotKernelHalfPi) {
        reduced = -kHotKernelPi - reduced;
        flipCos = true;
    }

    const float x2 = reduced * reduced;
    const float sin = reduced * (1.0F + x2 * (-0.16666666666666666F + x2 * (0.00833333333333333F + x2 * (-0.0001984126984126984F + x2 * 0.0000027557319223985893F))));
    float cos = 1.0F + x2 * (-0.5F + x2 * (0.041666666666666664F + x2 * (-0.001388888888888889F + x2 * (0.0000248015873015873F + x2 * -0.0000002755731922398589F))));
    if (flipCos) {
        cos = -cos;
    }
    return TransformSinCos{ .sin = sin, .cos = cos };
}

[[nodiscard]] inline TransformSinCos ResolveSinCos(float radians, TransformTrigPolicy policy) noexcept {
    if (radians == 0.0F) {
        return TransformSinCos{};
    }
    if (policy == TransformTrigPolicy::FastApprox) {
        return FastApproxSinCos(radians);
    }
    return TransformSinCos{ .sin = std::sin(radians), .cos = std::cos(radians) };
}

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
[[nodiscard]] inline __m128 SelectSse(__m128 mask, __m128 trueValue, __m128 falseValue) noexcept {
    return _mm_or_ps(_mm_and_ps(mask, trueValue), _mm_andnot_ps(mask, falseValue));
}

inline void FastApproxSinCosSse(__m128 radians, __m128& outSin, __m128& outCos) noexcept {
    const __m128 twoPi = _mm_set1_ps(kHotKernelTwoPi);
    const __m128 invTwoPi = _mm_set1_ps(1.0F / kHotKernelTwoPi);
    const __m128 pi = _mm_set1_ps(kHotKernelPi);
    const __m128 negativePi = _mm_set1_ps(-kHotKernelPi);
    const __m128 halfPi = _mm_set1_ps(kHotKernelHalfPi);
    const __m128 negativeHalfPi = _mm_set1_ps(-kHotKernelHalfPi);

    const __m128 nearestPeriod = _mm_cvtepi32_ps(_mm_cvtps_epi32(_mm_mul_ps(radians, invTwoPi)));
    __m128 reduced = _mm_sub_ps(radians, _mm_mul_ps(nearestPeriod, twoPi));

    const __m128 highMask = _mm_cmpgt_ps(reduced, halfPi);
    const __m128 lowMask = _mm_cmplt_ps(reduced, negativeHalfPi);
    reduced = SelectSse(highMask, _mm_sub_ps(pi, reduced), reduced);
    reduced = SelectSse(lowMask, _mm_sub_ps(negativePi, reduced), reduced);
    const __m128 flipCosMask = _mm_or_ps(highMask, lowMask);

    const __m128 x2 = _mm_mul_ps(reduced, reduced);
    __m128 sinPolynomial = _mm_set1_ps(0.0000027557319223985893F);
    sinPolynomial = _mm_add_ps(_mm_mul_ps(sinPolynomial, x2), _mm_set1_ps(-0.0001984126984126984F));
    sinPolynomial = _mm_add_ps(_mm_mul_ps(sinPolynomial, x2), _mm_set1_ps(0.00833333333333333F));
    sinPolynomial = _mm_add_ps(_mm_mul_ps(sinPolynomial, x2), _mm_set1_ps(-0.16666666666666666F));
    sinPolynomial = _mm_add_ps(_mm_mul_ps(sinPolynomial, x2), _mm_set1_ps(1.0F));
    outSin = _mm_mul_ps(reduced, sinPolynomial);

    __m128 cosPolynomial = _mm_set1_ps(-0.0000002755731922398589F);
    cosPolynomial = _mm_add_ps(_mm_mul_ps(cosPolynomial, x2), _mm_set1_ps(0.0000248015873015873F));
    cosPolynomial = _mm_add_ps(_mm_mul_ps(cosPolynomial, x2), _mm_set1_ps(-0.001388888888888889F));
    cosPolynomial = _mm_add_ps(_mm_mul_ps(cosPolynomial, x2), _mm_set1_ps(0.041666666666666664F));
    cosPolynomial = _mm_add_ps(_mm_mul_ps(cosPolynomial, x2), _mm_set1_ps(-0.5F));
    cosPolynomial = _mm_add_ps(_mm_mul_ps(cosPolynomial, x2), _mm_set1_ps(1.0F));
    outCos = SelectSse(flipCosMask, _mm_sub_ps(_mm_setzero_ps(), cosPolynomial), cosPolynomial);
}
#endif

} // namespace detail

[[nodiscard]] constexpr HotKernelMemoryTraffic EstimateDenseFloat2VelocityUpdateTraffic(
    std::size_t count,
    std::size_t positionComponentBytes,
    std::size_t velocityComponentBytes) noexcept {
    return HotKernelMemoryTraffic{
        .bytesRead = count * (positionComponentBytes + velocityComponentBytes),
        .bytesWritten = count * positionComponentBytes,
    };
}

template <typename Position, typename Velocity>
[[nodiscard]] constexpr HotKernelMemoryTraffic EstimateDenseFloat2VelocityUpdateTraffic(std::size_t count) noexcept {
    return EstimateDenseFloat2VelocityUpdateTraffic(count, sizeof(Position), sizeof(Velocity));
}

[[nodiscard]] constexpr HotKernelMemoryTraffic EstimateDenseFloat2VelocityColumnUpdateTraffic(std::size_t count) noexcept {
    return HotKernelMemoryTraffic{
        .bytesRead = count * sizeof(float) * 4U,
        .bytesWritten = count * sizeof(float) * 2U,
    };
}

[[nodiscard]] constexpr HotKernelMemoryTraffic EstimateDenseTransformZToMatrix4x4Traffic(
    std::size_t count,
    std::size_t localTransformBytes,
    std::size_t worldTransformBytes) noexcept {
    return HotKernelMemoryTraffic{
        .bytesRead = count * localTransformBytes,
        .bytesWritten = count * worldTransformBytes,
    };
}

template <typename LocalTransform, typename WorldTransform>
[[nodiscard]] constexpr HotKernelMemoryTraffic EstimateDenseTransformZToMatrix4x4Traffic(std::size_t count) noexcept {
    return EstimateDenseTransformZToMatrix4x4Traffic(count, sizeof(LocalTransform), sizeof(WorldTransform));
}

[[nodiscard]] constexpr HotKernelMemoryTraffic EstimateDenseTransformZToAffine3x4Traffic(
    std::size_t count,
    std::size_t localTransformBytes,
    std::size_t worldTransformBytes) noexcept {
    return HotKernelMemoryTraffic{
        .bytesRead = count * localTransformBytes,
        .bytesWritten = count * worldTransformBytes,
    };
}

template <typename LocalTransform, typename WorldTransform>
[[nodiscard]] constexpr HotKernelMemoryTraffic EstimateDenseTransformZToAffine3x4Traffic(std::size_t count) noexcept {
    return EstimateDenseTransformZToAffine3x4Traffic(count, sizeof(LocalTransform), sizeof(WorldTransform));
}

[[nodiscard]] constexpr HotKernelMemoryTraffic EstimateDenseTransformTranslationToAffine3x4Traffic(
    std::size_t count,
    std::size_t localTransformBytes,
    std::size_t worldTransformBytes) noexcept {
    return HotKernelMemoryTraffic{
        .bytesRead = count * localTransformBytes,
        .bytesWritten = count * worldTransformBytes,
    };
}

template <typename LocalTransform, typename WorldTransform>
[[nodiscard]] constexpr HotKernelMemoryTraffic EstimateDenseTransformTranslationToAffine3x4Traffic(std::size_t count) noexcept {
    return EstimateDenseTransformTranslationToAffine3x4Traffic(count, sizeof(LocalTransform), sizeof(WorldTransform));
}

[[nodiscard]] constexpr HotKernelMemoryTraffic EstimateDenseTransformVersionedCleanTraffic(
    std::size_t count,
    std::size_t versionComponentBytes) noexcept {
    return HotKernelMemoryTraffic{
        .bytesRead = count * versionComponentBytes,
    };
}

template <typename VersionState>
[[nodiscard]] constexpr HotKernelMemoryTraffic EstimateDenseTransformVersionedCleanTraffic(std::size_t count) noexcept {
    return EstimateDenseTransformVersionedCleanTraffic(count, sizeof(VersionState));
}

[[nodiscard]] constexpr std::size_t DenseTransformVersionRangeSummaryCount(std::size_t count, std::size_t rangeSize) noexcept {
    return count == 0U ? 0U : ((count - 1U) / (rangeSize == 0U ? count : rangeSize)) + 1U;
}

template <
    typename VersionState,
    auto LocalVersion,
    auto AppliedLocalVersion>
[[nodiscard]] std::size_t BuildDenseTransformVersionRangeSummaries(
    const VersionState* versions,
    std::size_t count,
    std::size_t rangeSize,
    std::span<DenseTransformVersionRangeSummary> summaries) noexcept {
    assert((count == 0U || versions != nullptr) && "ECS transform summary builder received a null version column");
    const std::size_t resolvedRangeSize = rangeSize == 0U ? count : rangeSize;
    const std::size_t summaryCount = DenseTransformVersionRangeSummaryCount(count, resolvedRangeSize);
    assert(summaries.size() >= summaryCount && "ECS transform summary output span is too small");

    for (std::size_t summaryIndex = 0U; summaryIndex < summaryCount; ++summaryIndex) {
        const std::size_t begin = summaryIndex * resolvedRangeSize;
        const std::size_t itemCount = count - begin < resolvedRangeSize ? count - begin : resolvedRangeSize;
        std::size_t dirtyCount = 0U;
        for (std::size_t offset = 0U; offset < itemCount; ++offset) {
            const VersionState& version = versions[begin + offset];
            dirtyCount += version.*AppliedLocalVersion == version.*LocalVersion ? 0U : 1U;
        }
        summaries[summaryIndex] = DenseTransformVersionRangeSummary{
            .range = HotKernelRange{ .begin = begin, .count = itemCount },
            .dirtyCount = dirtyCount,
        };
    }
    return summaryCount;
}

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float LocalTransform::*RotationZ,
    float LocalTransform::*ScaleX,
    float LocalTransform::*ScaleY,
    float LocalTransform::*ScaleZ,
    float (WorldTransform::*WorldMatrix)[16]>
void ApplyDenseTransformZToMatrix4x4Scalar(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    std::size_t count,
    TransformTrigPolicy trigPolicy = TransformTrigPolicy::Exact) noexcept {
    assert((count == 0U || localTransforms != nullptr) && "ECS transform hot kernel received null input column");
    assert((count == 0U || worldTransforms != nullptr) && "ECS transform hot kernel received null output column");

    for (std::size_t index = 0U; index < count; ++index) {
        const LocalTransform& local = localTransforms[index];
        float (&matrix)[16] = worldTransforms[index].*WorldMatrix;
        const TransformSinCos sinCos = detail::ResolveSinCos(local.*RotationZ, trigPolicy);
        matrix[0] = sinCos.cos * (local.*ScaleX);
        matrix[1] = sinCos.sin * (local.*ScaleX);
        matrix[2] = 0.0F;
        matrix[3] = 0.0F;
        matrix[4] = -sinCos.sin * (local.*ScaleY);
        matrix[5] = sinCos.cos * (local.*ScaleY);
        matrix[6] = 0.0F;
        matrix[7] = 0.0F;
        matrix[8] = 0.0F;
        matrix[9] = 0.0F;
        matrix[10] = local.*ScaleZ;
        matrix[11] = 0.0F;
        matrix[12] = local.*TranslationX;
        matrix[13] = local.*TranslationY;
        matrix[14] = local.*TranslationZ;
        matrix[15] = 1.0F;
    }
}

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float LocalTransform::*RotationZ,
    float LocalTransform::*ScaleX,
    float LocalTransform::*ScaleY,
    float LocalTransform::*ScaleZ,
    float (WorldTransform::*WorldMatrix)[16]>
[[nodiscard]] double ApplyDenseTransformZToMatrix4x4ScalarAndReduce(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    std::size_t count,
    TransformTrigPolicy trigPolicy = TransformTrigPolicy::Exact) noexcept {
    assert((count == 0U || localTransforms != nullptr) && "ECS transform hot kernel received null input column");
    assert((count == 0U || worldTransforms != nullptr) && "ECS transform hot kernel received null output column");

    double checksum = 0.0;
    for (std::size_t index = 0U; index < count; ++index) {
        const LocalTransform& local = localTransforms[index];
        float (&matrix)[16] = worldTransforms[index].*WorldMatrix;
        const TransformSinCos sinCos = detail::ResolveSinCos(local.*RotationZ, trigPolicy);
        const float m0 = sinCos.cos * (local.*ScaleX);
        const float m5 = sinCos.cos * (local.*ScaleY);
        const float m10 = local.*ScaleZ;
        const float m12 = local.*TranslationX;

        matrix[0] = m0;
        matrix[1] = sinCos.sin * (local.*ScaleX);
        matrix[2] = 0.0F;
        matrix[3] = 0.0F;
        matrix[4] = -sinCos.sin * (local.*ScaleY);
        matrix[5] = m5;
        matrix[6] = 0.0F;
        matrix[7] = 0.0F;
        matrix[8] = 0.0F;
        matrix[9] = 0.0F;
        matrix[10] = m10;
        matrix[11] = 0.0F;
        matrix[12] = m12;
        matrix[13] = local.*TranslationY;
        matrix[14] = local.*TranslationZ;
        matrix[15] = 1.0F;

        checksum += static_cast<double>(m0) + static_cast<double>(m5) + static_cast<double>(m10) + static_cast<double>(m12);
    }
    return checksum;
}

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float LocalTransform::*RotationZ,
    float LocalTransform::*ScaleX,
    float LocalTransform::*ScaleY,
    float LocalTransform::*ScaleZ,
    float (WorldTransform::*WorldAffine)[12]>
void ApplyDenseTransformZToAffine3x4Sse2FastApprox(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    std::size_t count) noexcept;

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float LocalTransform::*RotationZ,
    float LocalTransform::*ScaleX,
    float LocalTransform::*ScaleY,
    float LocalTransform::*ScaleZ,
    float (WorldTransform::*WorldAffine)[12]>
[[nodiscard]] double ApplyDenseTransformZToAffine3x4Sse2FastApproxAndReduce(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    std::size_t count) noexcept;
#endif

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float LocalTransform::*RotationZ,
    float LocalTransform::*ScaleX,
    float LocalTransform::*ScaleY,
    float LocalTransform::*ScaleZ,
    float (WorldTransform::*WorldAffine)[12]>
void ApplyDenseTransformZToAffine3x4Scalar(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    std::size_t count,
    TransformTrigPolicy trigPolicy = TransformTrigPolicy::Exact) noexcept {
    assert((count == 0U || localTransforms != nullptr) && "ECS affine transform hot kernel received null input column");
    assert((count == 0U || worldTransforms != nullptr) && "ECS affine transform hot kernel received null output column");

    for (std::size_t index = 0U; index < count; ++index) {
        const LocalTransform& local = localTransforms[index];
        float (&affine)[12] = worldTransforms[index].*WorldAffine;
        const TransformSinCos sinCos = detail::ResolveSinCos(local.*RotationZ, trigPolicy);
        affine[0] = sinCos.cos * (local.*ScaleX);
        affine[1] = sinCos.sin * (local.*ScaleX);
        affine[2] = 0.0F;
        affine[3] = -sinCos.sin * (local.*ScaleY);
        affine[4] = sinCos.cos * (local.*ScaleY);
        affine[5] = 0.0F;
        affine[6] = 0.0F;
        affine[7] = 0.0F;
        affine[8] = local.*ScaleZ;
        affine[9] = local.*TranslationX;
        affine[10] = local.*TranslationY;
        affine[11] = local.*TranslationZ;
    }
}

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float LocalTransform::*RotationZ,
    float LocalTransform::*ScaleX,
    float LocalTransform::*ScaleY,
    float LocalTransform::*ScaleZ,
    float (WorldTransform::*WorldAffine)[12]>
[[nodiscard]] double ApplyDenseTransformZToAffine3x4ScalarAndReduce(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    std::size_t count,
    TransformTrigPolicy trigPolicy = TransformTrigPolicy::Exact) noexcept {
    assert((count == 0U || localTransforms != nullptr) && "ECS affine transform hot kernel received null input column");
    assert((count == 0U || worldTransforms != nullptr) && "ECS affine transform hot kernel received null output column");

    double checksum = 0.0;
    for (std::size_t index = 0U; index < count; ++index) {
        const LocalTransform& local = localTransforms[index];
        float (&affine)[12] = worldTransforms[index].*WorldAffine;
        const TransformSinCos sinCos = detail::ResolveSinCos(local.*RotationZ, trigPolicy);
        const float m0 = sinCos.cos * (local.*ScaleX);
        const float m4 = sinCos.cos * (local.*ScaleY);
        const float m8 = local.*ScaleZ;
        const float m9 = local.*TranslationX;

        affine[0] = m0;
        affine[1] = sinCos.sin * (local.*ScaleX);
        affine[2] = 0.0F;
        affine[3] = -sinCos.sin * (local.*ScaleY);
        affine[4] = m4;
        affine[5] = 0.0F;
        affine[6] = 0.0F;
        affine[7] = 0.0F;
        affine[8] = m8;
        affine[9] = m9;
        affine[10] = local.*TranslationY;
        affine[11] = local.*TranslationZ;

        checksum += static_cast<double>(m0) + static_cast<double>(m4) + static_cast<double>(m8) + static_cast<double>(m9);
    }
    return checksum;
}

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float LocalTransform::*RotationZ,
    float LocalTransform::*ScaleX,
    float LocalTransform::*ScaleY,
    float LocalTransform::*ScaleZ,
    float (WorldTransform::*WorldAffine)[12]>
void ApplyDenseTransformZToAffine3x4(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    HotKernelRange range,
    TransformTrigPolicy trigPolicy = TransformTrigPolicy::Exact,
    KernelBackend backend = PreferredKernelBackend()) noexcept {
    assert((range.count == 0U || localTransforms != nullptr) && "ECS affine transform hot kernel received null input column");
    assert((range.count == 0U || worldTransforms != nullptr) && "ECS affine transform hot kernel received null output column");
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
    if (trigPolicy == TransformTrigPolicy::FastApprox
        && IsKernelBackendSupported(KernelBackend::Sse2)
        && (backend == KernelBackend::Sse2 || backend == KernelBackend::Avx2 || backend == KernelBackend::Avx512)) {
        ApplyDenseTransformZToAffine3x4Sse2FastApprox<
            LocalTransform,
            WorldTransform,
            TranslationX,
            TranslationY,
            TranslationZ,
            RotationZ,
            ScaleX,
            ScaleY,
            ScaleZ,
            WorldAffine>(
            localTransforms + range.begin,
            worldTransforms + range.begin,
            range.count);
        return;
    }
#else
    static_cast<void>(backend);
#endif
    ApplyDenseTransformZToAffine3x4Scalar<
        LocalTransform,
        WorldTransform,
        TranslationX,
        TranslationY,
        TranslationZ,
        RotationZ,
        ScaleX,
        ScaleY,
        ScaleZ,
        WorldAffine>(
        localTransforms + range.begin,
        worldTransforms + range.begin,
        range.count,
        trigPolicy);
}

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float LocalTransform::*RotationZ,
    float LocalTransform::*ScaleX,
    float LocalTransform::*ScaleY,
    float LocalTransform::*ScaleZ,
    float (WorldTransform::*WorldAffine)[12]>
[[nodiscard]] double ApplyDenseTransformZToAffine3x4AndReduce(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    HotKernelRange range,
    TransformTrigPolicy trigPolicy = TransformTrigPolicy::Exact,
    KernelBackend backend = PreferredKernelBackend()) noexcept {
    assert((range.count == 0U || localTransforms != nullptr) && "ECS affine transform hot kernel received null input column");
    assert((range.count == 0U || worldTransforms != nullptr) && "ECS affine transform hot kernel received null output column");
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
    if (trigPolicy == TransformTrigPolicy::FastApprox
        && IsKernelBackendSupported(KernelBackend::Sse2)
        && (backend == KernelBackend::Sse2 || backend == KernelBackend::Avx2 || backend == KernelBackend::Avx512)) {
        return ApplyDenseTransformZToAffine3x4Sse2FastApproxAndReduce<
            LocalTransform,
            WorldTransform,
            TranslationX,
            TranslationY,
            TranslationZ,
            RotationZ,
            ScaleX,
            ScaleY,
            ScaleZ,
            WorldAffine>(
            localTransforms + range.begin,
            worldTransforms + range.begin,
            range.count);
    }
#else
    static_cast<void>(backend);
#endif
    return ApplyDenseTransformZToAffine3x4ScalarAndReduce<
        LocalTransform,
        WorldTransform,
        TranslationX,
        TranslationY,
        TranslationZ,
        RotationZ,
        ScaleX,
        ScaleY,
        ScaleZ,
        WorldAffine>(
        localTransforms + range.begin,
        worldTransforms + range.begin,
        range.count,
        trigPolicy);
}

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float (WorldTransform::*WorldMatrix)[16]>
void ApplyDenseTransformTranslationToMatrix4x4Scalar(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    std::size_t count) noexcept {
    assert((count == 0U || localTransforms != nullptr) && "ECS translation transform hot kernel received null input column");
    assert((count == 0U || worldTransforms != nullptr) && "ECS translation transform hot kernel received null output column");

    for (std::size_t index = 0U; index < count; ++index) {
        const LocalTransform& local = localTransforms[index];
        float (&matrix)[16] = worldTransforms[index].*WorldMatrix;
        matrix[0] = 1.0F;
        matrix[1] = 0.0F;
        matrix[2] = 0.0F;
        matrix[3] = 0.0F;
        matrix[4] = 0.0F;
        matrix[5] = 1.0F;
        matrix[6] = 0.0F;
        matrix[7] = 0.0F;
        matrix[8] = 0.0F;
        matrix[9] = 0.0F;
        matrix[10] = 1.0F;
        matrix[11] = 0.0F;
        matrix[12] = local.*TranslationX;
        matrix[13] = local.*TranslationY;
        matrix[14] = local.*TranslationZ;
        matrix[15] = 1.0F;
    }
}

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float (WorldTransform::*WorldMatrix)[16]>
[[nodiscard]] double ApplyDenseTransformTranslationToMatrix4x4ScalarAndReduce(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    std::size_t count) noexcept {
    assert((count == 0U || localTransforms != nullptr) && "ECS translation transform hot kernel received null input column");
    assert((count == 0U || worldTransforms != nullptr) && "ECS translation transform hot kernel received null output column");

    double checksum = 0.0;
    for (std::size_t index = 0U; index < count; ++index) {
        const LocalTransform& local = localTransforms[index];
        float (&matrix)[16] = worldTransforms[index].*WorldMatrix;
        const float m12 = local.*TranslationX;

        matrix[0] = 1.0F;
        matrix[1] = 0.0F;
        matrix[2] = 0.0F;
        matrix[3] = 0.0F;
        matrix[4] = 0.0F;
        matrix[5] = 1.0F;
        matrix[6] = 0.0F;
        matrix[7] = 0.0F;
        matrix[8] = 0.0F;
        matrix[9] = 0.0F;
        matrix[10] = 1.0F;
        matrix[11] = 0.0F;
        matrix[12] = m12;
        matrix[13] = local.*TranslationY;
        matrix[14] = local.*TranslationZ;
        matrix[15] = 1.0F;

        checksum += static_cast<double>(3.0F + m12);
    }
    return checksum;
}

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float (WorldTransform::*WorldAffine)[12]>
void ApplyDenseTransformTranslationToAffine3x4Scalar(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    std::size_t count) noexcept {
    assert((count == 0U || localTransforms != nullptr) && "ECS translation affine hot kernel received null input column");
    assert((count == 0U || worldTransforms != nullptr) && "ECS translation affine hot kernel received null output column");

    for (std::size_t index = 0U; index < count; ++index) {
        const LocalTransform& local = localTransforms[index];
        float (&affine)[12] = worldTransforms[index].*WorldAffine;
        affine[0] = 1.0F;
        affine[1] = 0.0F;
        affine[2] = 0.0F;
        affine[3] = 0.0F;
        affine[4] = 1.0F;
        affine[5] = 0.0F;
        affine[6] = 0.0F;
        affine[7] = 0.0F;
        affine[8] = 1.0F;
        affine[9] = local.*TranslationX;
        affine[10] = local.*TranslationY;
        affine[11] = local.*TranslationZ;
    }
}

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float (WorldTransform::*WorldAffine)[12]>
[[nodiscard]] double ApplyDenseTransformTranslationToAffine3x4ScalarAndReduce(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    std::size_t count) noexcept {
    assert((count == 0U || localTransforms != nullptr) && "ECS translation affine hot kernel received null input column");
    assert((count == 0U || worldTransforms != nullptr) && "ECS translation affine hot kernel received null output column");

    double checksum = 0.0;
    for (std::size_t index = 0U; index < count; ++index) {
        const LocalTransform& local = localTransforms[index];
        float (&affine)[12] = worldTransforms[index].*WorldAffine;
        const float translationX = local.*TranslationX;
        affine[0] = 1.0F;
        affine[1] = 0.0F;
        affine[2] = 0.0F;
        affine[3] = 0.0F;
        affine[4] = 1.0F;
        affine[5] = 0.0F;
        affine[6] = 0.0F;
        affine[7] = 0.0F;
        affine[8] = 1.0F;
        affine[9] = translationX;
        affine[10] = local.*TranslationY;
        affine[11] = local.*TranslationZ;
        checksum += static_cast<double>(3.0F + translationX);
    }
    return checksum;
}

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
[[nodiscard]] inline bool ShouldUseSse2TransformFastApprox(KernelBackend backend) noexcept {
    return IsKernelBackendSupported(KernelBackend::Sse2)
        && (backend == KernelBackend::Sse2 || backend == KernelBackend::Avx2 || backend == KernelBackend::Avx512);
}

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float (WorldTransform::*WorldMatrix)[16]>
void ApplyDenseTransformTranslationToMatrix4x4Sse2(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    std::size_t count) noexcept {
    assert((count == 0U || localTransforms != nullptr) && "ECS translation transform hot kernel received null input column");
    assert((count == 0U || worldTransforms != nullptr) && "ECS translation transform hot kernel received null output column");

    const __m128 row0 = _mm_set_ps(0.0F, 0.0F, 0.0F, 1.0F);
    const __m128 row1 = _mm_set_ps(0.0F, 0.0F, 1.0F, 0.0F);
    const __m128 row2 = _mm_set_ps(0.0F, 1.0F, 0.0F, 0.0F);
    for (std::size_t index = 0U; index < count; ++index) {
        const LocalTransform& local = localTransforms[index];
        float (&matrix)[16] = worldTransforms[index].*WorldMatrix;
        const __m128 row3 = _mm_set_ps(1.0F, local.*TranslationZ, local.*TranslationY, local.*TranslationX);
        _mm_storeu_ps(matrix + 0U, row0);
        _mm_storeu_ps(matrix + 4U, row1);
        _mm_storeu_ps(matrix + 8U, row2);
        _mm_storeu_ps(matrix + 12U, row3);
    }
}

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float (WorldTransform::*WorldMatrix)[16]>
[[nodiscard]] double ApplyDenseTransformTranslationToMatrix4x4Sse2AndReduce(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    std::size_t count) noexcept {
    assert((count == 0U || localTransforms != nullptr) && "ECS translation transform hot kernel received null input column");
    assert((count == 0U || worldTransforms != nullptr) && "ECS translation transform hot kernel received null output column");

    const __m128 row0 = _mm_set_ps(0.0F, 0.0F, 0.0F, 1.0F);
    const __m128 row1 = _mm_set_ps(0.0F, 0.0F, 1.0F, 0.0F);
    const __m128 row2 = _mm_set_ps(0.0F, 1.0F, 0.0F, 0.0F);
    double checksum = 0.0;
    for (std::size_t index = 0U; index < count; ++index) {
        const LocalTransform& local = localTransforms[index];
        float (&matrix)[16] = worldTransforms[index].*WorldMatrix;
        const float translationX = local.*TranslationX;
        const __m128 row3 = _mm_set_ps(1.0F, local.*TranslationZ, local.*TranslationY, translationX);
        _mm_storeu_ps(matrix + 0U, row0);
        _mm_storeu_ps(matrix + 4U, row1);
        _mm_storeu_ps(matrix + 8U, row2);
        _mm_storeu_ps(matrix + 12U, row3);
        checksum += static_cast<double>(3.0F + translationX);
    }
    return checksum;
}

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float (WorldTransform::*WorldAffine)[12]>
void ApplyDenseTransformTranslationToAffine3x4Sse2(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    std::size_t count) noexcept {
    assert((count == 0U || localTransforms != nullptr) && "ECS translation affine hot kernel received null input column");
    assert((count == 0U || worldTransforms != nullptr) && "ECS translation affine hot kernel received null output column");

    const __m128 row0 = _mm_set_ps(0.0F, 0.0F, 0.0F, 1.0F);
    const __m128 row1 = _mm_set_ps(0.0F, 0.0F, 0.0F, 1.0F);
    for (std::size_t index = 0U; index < count; ++index) {
        const LocalTransform& local = localTransforms[index];
        float (&affine)[12] = worldTransforms[index].*WorldAffine;
        const __m128 row2AndTranslation = _mm_set_ps(local.*TranslationZ, local.*TranslationY, local.*TranslationX, 1.0F);
        _mm_storeu_ps(affine + 0U, row0);
        _mm_storeu_ps(affine + 4U, row1);
        _mm_storeu_ps(affine + 8U, row2AndTranslation);
    }
}

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float (WorldTransform::*WorldAffine)[12]>
[[nodiscard]] double ApplyDenseTransformTranslationToAffine3x4Sse2AndReduce(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    std::size_t count) noexcept {
    assert((count == 0U || localTransforms != nullptr) && "ECS translation affine hot kernel received null input column");
    assert((count == 0U || worldTransforms != nullptr) && "ECS translation affine hot kernel received null output column");

    const __m128 row0 = _mm_set_ps(0.0F, 0.0F, 0.0F, 1.0F);
    const __m128 row1 = _mm_set_ps(0.0F, 0.0F, 0.0F, 1.0F);
    double checksum = 0.0;
    for (std::size_t index = 0U; index < count; ++index) {
        const LocalTransform& local = localTransforms[index];
        float (&affine)[12] = worldTransforms[index].*WorldAffine;
        const float translationX = local.*TranslationX;
        const __m128 row2AndTranslation = _mm_set_ps(local.*TranslationZ, local.*TranslationY, translationX, 1.0F);
        _mm_storeu_ps(affine + 0U, row0);
        _mm_storeu_ps(affine + 4U, row1);
        _mm_storeu_ps(affine + 8U, row2AndTranslation);
        checksum += static_cast<double>(3.0F + translationX);
    }
    return checksum;
}
#endif

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float (WorldTransform::*WorldMatrix)[16]>
void ApplyDenseTransformTranslationToMatrix4x4(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    HotKernelRange range) noexcept {
    assert((range.count == 0U || localTransforms != nullptr) && "ECS translation transform hot kernel received null input column");
    assert((range.count == 0U || worldTransforms != nullptr) && "ECS translation transform hot kernel received null output column");
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
    if (IsKernelBackendSupported(KernelBackend::Sse2)) {
        ApplyDenseTransformTranslationToMatrix4x4Sse2<
            LocalTransform,
            WorldTransform,
            TranslationX,
            TranslationY,
            TranslationZ,
            WorldMatrix>(
            localTransforms + range.begin,
            worldTransforms + range.begin,
            range.count);
        return;
    }
#endif
    ApplyDenseTransformTranslationToMatrix4x4Scalar<
        LocalTransform,
        WorldTransform,
        TranslationX,
        TranslationY,
        TranslationZ,
        WorldMatrix>(
        localTransforms + range.begin,
        worldTransforms + range.begin,
        range.count);
}

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float (WorldTransform::*WorldAffine)[12]>
void ApplyDenseTransformTranslationToAffine3x4(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    HotKernelRange range) noexcept {
    assert((range.count == 0U || localTransforms != nullptr) && "ECS translation affine hot kernel received null input column");
    assert((range.count == 0U || worldTransforms != nullptr) && "ECS translation affine hot kernel received null output column");
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
    if (IsKernelBackendSupported(KernelBackend::Sse2)) {
        ApplyDenseTransformTranslationToAffine3x4Sse2<
            LocalTransform,
            WorldTransform,
            TranslationX,
            TranslationY,
            TranslationZ,
            WorldAffine>(
            localTransforms + range.begin,
            worldTransforms + range.begin,
            range.count);
        return;
    }
#endif
    ApplyDenseTransformTranslationToAffine3x4Scalar<
        LocalTransform,
        WorldTransform,
        TranslationX,
        TranslationY,
        TranslationZ,
        WorldAffine>(
        localTransforms + range.begin,
        worldTransforms + range.begin,
        range.count);
}

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float (WorldTransform::*WorldMatrix)[16]>
[[nodiscard]] double ApplyDenseTransformTranslationToMatrix4x4AndReduce(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    HotKernelRange range) noexcept {
    assert((range.count == 0U || localTransforms != nullptr) && "ECS translation transform hot kernel received null input column");
    assert((range.count == 0U || worldTransforms != nullptr) && "ECS translation transform hot kernel received null output column");
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
    if (IsKernelBackendSupported(KernelBackend::Sse2)) {
        return ApplyDenseTransformTranslationToMatrix4x4Sse2AndReduce<
            LocalTransform,
            WorldTransform,
            TranslationX,
            TranslationY,
            TranslationZ,
            WorldMatrix>(
            localTransforms + range.begin,
            worldTransforms + range.begin,
            range.count);
    }
#endif
    return ApplyDenseTransformTranslationToMatrix4x4ScalarAndReduce<
        LocalTransform,
        WorldTransform,
        TranslationX,
        TranslationY,
        TranslationZ,
        WorldMatrix>(
        localTransforms + range.begin,
        worldTransforms + range.begin,
        range.count);
}

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float (WorldTransform::*WorldAffine)[12]>
[[nodiscard]] double ApplyDenseTransformTranslationToAffine3x4AndReduce(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    HotKernelRange range) noexcept {
    assert((range.count == 0U || localTransforms != nullptr) && "ECS translation affine hot kernel received null input column");
    assert((range.count == 0U || worldTransforms != nullptr) && "ECS translation affine hot kernel received null output column");
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
    if (IsKernelBackendSupported(KernelBackend::Sse2)) {
        return ApplyDenseTransformTranslationToAffine3x4Sse2AndReduce<
            LocalTransform,
            WorldTransform,
            TranslationX,
            TranslationY,
            TranslationZ,
            WorldAffine>(
            localTransforms + range.begin,
            worldTransforms + range.begin,
            range.count);
    }
#endif
    return ApplyDenseTransformTranslationToAffine3x4ScalarAndReduce<
        LocalTransform,
        WorldTransform,
        TranslationX,
        TranslationY,
        TranslationZ,
        WorldAffine>(
        localTransforms + range.begin,
        worldTransforms + range.begin,
        range.count);
}

template <
    typename LocalTransform,
    typename WorldTransform,
    typename VersionState,
    auto LocalVersion,
    auto AppliedLocalVersion,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float (WorldTransform::*WorldMatrix)[16]>
[[nodiscard]] DenseTransformVersionedApplyStats ApplyDenseTransformTranslationToMatrix4x4VersionedAndReduce(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    VersionState* versions,
    HotKernelRange range) noexcept {
    assert((range.count == 0U || localTransforms != nullptr) && "ECS translation transform hot kernel received null input column");
    assert((range.count == 0U || worldTransforms != nullptr) && "ECS translation transform hot kernel received null output column");
    assert((range.count == 0U || versions != nullptr) && "ECS translation transform hot kernel received null version column");

    DenseTransformVersionedApplyStats stats{
        .visited = range.count,
    };
    const std::size_t rangeEnd = range.End();
    std::size_t index = range.begin;
    while (index < rangeEnd) {
        if (versions[index].*AppliedLocalVersion == versions[index].*LocalVersion) {
            ++stats.skipped;
            ++index;
            continue;
        }

        const std::size_t dirtyBegin = index;
        do {
            ++index;
        } while (index < rangeEnd && versions[index].*AppliedLocalVersion != versions[index].*LocalVersion);
        const std::size_t dirtyCount = index - dirtyBegin;
        stats.checksum += ApplyDenseTransformTranslationToMatrix4x4AndReduce<
            LocalTransform,
            WorldTransform,
            TranslationX,
            TranslationY,
            TranslationZ,
            WorldMatrix>(
            localTransforms,
            worldTransforms,
            HotKernelRange{ .begin = dirtyBegin, .count = dirtyCount });
        for (std::size_t dirtyIndex = dirtyBegin; dirtyIndex < index; ++dirtyIndex) {
            versions[dirtyIndex].*AppliedLocalVersion = versions[dirtyIndex].*LocalVersion;
        }
        stats.updated += dirtyCount;
    }
    return stats;
}

template <
    typename LocalTransform,
    typename WorldTransform,
    typename VersionState,
    auto LocalVersion,
    auto AppliedLocalVersion,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float (WorldTransform::*WorldMatrix)[16]>
[[nodiscard]] DenseTransformVersionedApplyStats ApplyDenseTransformTranslationToMatrix4x4VersionedSummariesAndReduce(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    VersionState* versions,
    std::span<DenseTransformVersionRangeSummary> summaries) noexcept {
    DenseTransformVersionedApplyStats totals;
    for (DenseTransformVersionRangeSummary& summary : summaries) {
        totals.visited += summary.range.count;
        if (summary.dirtyCount == 0U) {
            totals.skipped += summary.range.count;
            continue;
        }

        const DenseTransformVersionedApplyStats stats =
            ApplyDenseTransformTranslationToMatrix4x4VersionedAndReduce<
                LocalTransform,
                WorldTransform,
                VersionState,
                LocalVersion,
                AppliedLocalVersion,
                TranslationX,
                TranslationY,
                TranslationZ,
                WorldMatrix>(
                localTransforms,
                worldTransforms,
                versions,
                summary.range);
        totals.updated += stats.updated;
        totals.skipped += stats.skipped;
        totals.checksum += stats.checksum;
        summary.dirtyCount = 0U;
    }
    return totals;
}

template <
    typename MutableBatch,
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float (WorldTransform::*WorldMatrix)[16],
    std::size_t LocalIndex = 0U,
    std::size_t WorldIndex = 1U>
void ApplyDenseTransformTranslationToMatrix4x4Batch(MutableBatch& batch) noexcept {
    ApplyDenseTransformTranslationToMatrix4x4<
        LocalTransform,
        WorldTransform,
        TranslationX,
        TranslationY,
        TranslationZ,
        WorldMatrix>(
        batch.template Components<LocalIndex>(),
        batch.template Components<WorldIndex>(),
        HotKernelRange{ .begin = 0U, .count = batch.Count() });
}

template <
    typename MutableBatch,
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float (WorldTransform::*WorldMatrix)[16],
    std::size_t LocalIndex = 0U,
    std::size_t WorldIndex = 1U>
[[nodiscard]] double ApplyDenseTransformTranslationToMatrix4x4BatchAndReduce(MutableBatch& batch) noexcept {
    return ApplyDenseTransformTranslationToMatrix4x4AndReduce<
        LocalTransform,
        WorldTransform,
        TranslationX,
        TranslationY,
        TranslationZ,
        WorldMatrix>(
        batch.template Components<LocalIndex>(),
        batch.template Components<WorldIndex>(),
        HotKernelRange{ .begin = 0U, .count = batch.Count() });
}

template <
    typename MutableBatch,
    typename LocalTransform,
    typename WorldTransform,
    typename VersionState,
    auto LocalVersion,
    auto AppliedLocalVersion,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float (WorldTransform::*WorldMatrix)[16],
    std::size_t LocalIndex = 0U,
    std::size_t WorldIndex = 1U,
    std::size_t VersionIndex = 2U>
[[nodiscard]] DenseTransformVersionedApplyStats ApplyDenseTransformTranslationToMatrix4x4VersionedBatchAndReduce(MutableBatch& batch) noexcept {
    return ApplyDenseTransformTranslationToMatrix4x4VersionedAndReduce<
        LocalTransform,
        WorldTransform,
        VersionState,
        LocalVersion,
        AppliedLocalVersion,
        TranslationX,
        TranslationY,
        TranslationZ,
        WorldMatrix>(
        batch.template Components<LocalIndex>(),
        batch.template Components<WorldIndex>(),
        batch.template Components<VersionIndex>(),
        HotKernelRange{ .begin = 0U, .count = batch.Count() });
}

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float LocalTransform::*RotationZ,
    float LocalTransform::*ScaleX,
    float LocalTransform::*ScaleY,
    float LocalTransform::*ScaleZ,
    float (WorldTransform::*WorldMatrix)[16]>
void ApplyDenseTransformZToMatrix4x4Sse2FastApprox(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    std::size_t count) noexcept {
    assert((count == 0U || localTransforms != nullptr) && "ECS transform hot kernel received null input column");
    assert((count == 0U || worldTransforms != nullptr) && "ECS transform hot kernel received null output column");

    alignas(16) float sinValues[4]{};
    alignas(16) float cosValues[4]{};
    std::size_t index = 0U;
    for (; index + 4U <= count; index += 4U) {
        const __m128 rotations = _mm_set_ps(
            localTransforms[index + 3U].*RotationZ,
            localTransforms[index + 2U].*RotationZ,
            localTransforms[index + 1U].*RotationZ,
            localTransforms[index].*RotationZ);
        __m128 sinLanes = _mm_setzero_ps();
        __m128 cosLanes = _mm_setzero_ps();
        detail::FastApproxSinCosSse(rotations, sinLanes, cosLanes);
        _mm_store_ps(sinValues, sinLanes);
        _mm_store_ps(cosValues, cosLanes);

        for (std::size_t lane = 0U; lane < 4U; ++lane) {
            const LocalTransform& local = localTransforms[index + lane];
            float (&matrix)[16] = worldTransforms[index + lane].*WorldMatrix;
            const float sinZ = sinValues[lane];
            const float cosZ = cosValues[lane];
            const float scaleX = local.*ScaleX;
            const float scaleY = local.*ScaleY;
            _mm_storeu_ps(matrix + 0U, _mm_set_ps(0.0F, 0.0F, sinZ * scaleX, cosZ * scaleX));
            _mm_storeu_ps(matrix + 4U, _mm_set_ps(0.0F, 0.0F, cosZ * scaleY, -sinZ * scaleY));
            _mm_storeu_ps(matrix + 8U, _mm_set_ps(0.0F, local.*ScaleZ, 0.0F, 0.0F));
            _mm_storeu_ps(matrix + 12U, _mm_set_ps(1.0F, local.*TranslationZ, local.*TranslationY, local.*TranslationX));
        }
    }

    ApplyDenseTransformZToMatrix4x4Scalar<
        LocalTransform,
        WorldTransform,
        TranslationX,
        TranslationY,
        TranslationZ,
        RotationZ,
        ScaleX,
        ScaleY,
        ScaleZ,
        WorldMatrix>(
        localTransforms + index,
        worldTransforms + index,
        count - index,
        TransformTrigPolicy::FastApprox);
}

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float LocalTransform::*RotationZ,
    float LocalTransform::*ScaleX,
    float LocalTransform::*ScaleY,
    float LocalTransform::*ScaleZ,
    float (WorldTransform::*WorldMatrix)[16]>
[[nodiscard]] double ApplyDenseTransformZToMatrix4x4Sse2FastApproxAndReduce(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    std::size_t count) noexcept {
    assert((count == 0U || localTransforms != nullptr) && "ECS transform hot kernel received null input column");
    assert((count == 0U || worldTransforms != nullptr) && "ECS transform hot kernel received null output column");

    alignas(16) float sinValues[4]{};
    alignas(16) float cosValues[4]{};
    double checksum = 0.0;
    std::size_t index = 0U;
    for (; index + 4U <= count; index += 4U) {
        const __m128 rotations = _mm_set_ps(
            localTransforms[index + 3U].*RotationZ,
            localTransforms[index + 2U].*RotationZ,
            localTransforms[index + 1U].*RotationZ,
            localTransforms[index].*RotationZ);
        __m128 sinLanes = _mm_setzero_ps();
        __m128 cosLanes = _mm_setzero_ps();
        detail::FastApproxSinCosSse(rotations, sinLanes, cosLanes);
        _mm_store_ps(sinValues, sinLanes);
        _mm_store_ps(cosValues, cosLanes);

        for (std::size_t lane = 0U; lane < 4U; ++lane) {
            const LocalTransform& local = localTransforms[index + lane];
            float (&matrix)[16] = worldTransforms[index + lane].*WorldMatrix;
            const float sinZ = sinValues[lane];
            const float cosZ = cosValues[lane];
            const float m0 = cosZ * (local.*ScaleX);
            const float m1 = sinZ * (local.*ScaleX);
            const float m4 = -sinZ * (local.*ScaleY);
            const float m5 = cosZ * (local.*ScaleY);
            const float m10 = local.*ScaleZ;
            const float m12 = local.*TranslationX;

            _mm_storeu_ps(matrix + 0U, _mm_set_ps(0.0F, 0.0F, m1, m0));
            _mm_storeu_ps(matrix + 4U, _mm_set_ps(0.0F, 0.0F, m5, m4));
            _mm_storeu_ps(matrix + 8U, _mm_set_ps(0.0F, m10, 0.0F, 0.0F));
            _mm_storeu_ps(matrix + 12U, _mm_set_ps(1.0F, local.*TranslationZ, local.*TranslationY, m12));

            checksum += static_cast<double>(m0) + static_cast<double>(m5) + static_cast<double>(m10) + static_cast<double>(m12);
        }
    }

    return checksum + ApplyDenseTransformZToMatrix4x4ScalarAndReduce<
        LocalTransform,
        WorldTransform,
        TranslationX,
        TranslationY,
        TranslationZ,
        RotationZ,
        ScaleX,
        ScaleY,
        ScaleZ,
        WorldMatrix>(
        localTransforms + index,
        worldTransforms + index,
        count - index,
        TransformTrigPolicy::FastApprox);
}

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float LocalTransform::*RotationZ,
    float LocalTransform::*ScaleX,
    float LocalTransform::*ScaleY,
    float LocalTransform::*ScaleZ,
    float (WorldTransform::*WorldAffine)[12]>
void ApplyDenseTransformZToAffine3x4Sse2FastApprox(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    std::size_t count) noexcept {
    assert((count == 0U || localTransforms != nullptr) && "ECS affine transform hot kernel received null input column");
    assert((count == 0U || worldTransforms != nullptr) && "ECS affine transform hot kernel received null output column");

    alignas(16) float sinValues[4]{};
    alignas(16) float cosValues[4]{};
    std::size_t index = 0U;
    for (; index + 4U <= count; index += 4U) {
        const __m128 rotations = _mm_set_ps(
            localTransforms[index + 3U].*RotationZ,
            localTransforms[index + 2U].*RotationZ,
            localTransforms[index + 1U].*RotationZ,
            localTransforms[index].*RotationZ);
        __m128 sinLanes = _mm_setzero_ps();
        __m128 cosLanes = _mm_setzero_ps();
        detail::FastApproxSinCosSse(rotations, sinLanes, cosLanes);
        _mm_store_ps(sinValues, sinLanes);
        _mm_store_ps(cosValues, cosLanes);

        for (std::size_t lane = 0U; lane < 4U; ++lane) {
            const LocalTransform& local = localTransforms[index + lane];
            float (&affine)[12] = worldTransforms[index + lane].*WorldAffine;
            const float sinZ = sinValues[lane];
            const float cosZ = cosValues[lane];
            const float scaleX = local.*ScaleX;
            const float scaleY = local.*ScaleY;
            _mm_storeu_ps(affine + 0U, _mm_set_ps(-sinZ * scaleY, 0.0F, sinZ * scaleX, cosZ * scaleX));
            _mm_storeu_ps(affine + 4U, _mm_set_ps(0.0F, 0.0F, 0.0F, cosZ * scaleY));
            _mm_storeu_ps(affine + 8U, _mm_set_ps(local.*TranslationZ, local.*TranslationY, local.*TranslationX, local.*ScaleZ));
        }
    }

    ApplyDenseTransformZToAffine3x4Scalar<
        LocalTransform,
        WorldTransform,
        TranslationX,
        TranslationY,
        TranslationZ,
        RotationZ,
        ScaleX,
        ScaleY,
        ScaleZ,
        WorldAffine>(
        localTransforms + index,
        worldTransforms + index,
        count - index,
        TransformTrigPolicy::FastApprox);
}

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float LocalTransform::*RotationZ,
    float LocalTransform::*ScaleX,
    float LocalTransform::*ScaleY,
    float LocalTransform::*ScaleZ,
    float (WorldTransform::*WorldAffine)[12]>
[[nodiscard]] double ApplyDenseTransformZToAffine3x4Sse2FastApproxAndReduce(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    std::size_t count) noexcept {
    assert((count == 0U || localTransforms != nullptr) && "ECS affine transform hot kernel received null input column");
    assert((count == 0U || worldTransforms != nullptr) && "ECS affine transform hot kernel received null output column");

    alignas(16) float sinValues[4]{};
    alignas(16) float cosValues[4]{};
    double checksum = 0.0;
    std::size_t index = 0U;
    for (; index + 4U <= count; index += 4U) {
        const __m128 rotations = _mm_set_ps(
            localTransforms[index + 3U].*RotationZ,
            localTransforms[index + 2U].*RotationZ,
            localTransforms[index + 1U].*RotationZ,
            localTransforms[index].*RotationZ);
        __m128 sinLanes = _mm_setzero_ps();
        __m128 cosLanes = _mm_setzero_ps();
        detail::FastApproxSinCosSse(rotations, sinLanes, cosLanes);
        _mm_store_ps(sinValues, sinLanes);
        _mm_store_ps(cosValues, cosLanes);

        for (std::size_t lane = 0U; lane < 4U; ++lane) {
            const LocalTransform& local = localTransforms[index + lane];
            float (&affine)[12] = worldTransforms[index + lane].*WorldAffine;
            const float sinZ = sinValues[lane];
            const float cosZ = cosValues[lane];
            const float m0 = cosZ * (local.*ScaleX);
            const float m1 = sinZ * (local.*ScaleX);
            const float m3 = -sinZ * (local.*ScaleY);
            const float m4 = cosZ * (local.*ScaleY);
            const float m8 = local.*ScaleZ;
            const float m9 = local.*TranslationX;

            _mm_storeu_ps(affine + 0U, _mm_set_ps(m3, 0.0F, m1, m0));
            _mm_storeu_ps(affine + 4U, _mm_set_ps(0.0F, 0.0F, 0.0F, m4));
            _mm_storeu_ps(affine + 8U, _mm_set_ps(local.*TranslationZ, local.*TranslationY, m9, m8));

            checksum += static_cast<double>(m0) + static_cast<double>(m4) + static_cast<double>(m8) + static_cast<double>(m9);
        }
    }

    return checksum + ApplyDenseTransformZToAffine3x4ScalarAndReduce<
        LocalTransform,
        WorldTransform,
        TranslationX,
        TranslationY,
        TranslationZ,
        RotationZ,
        ScaleX,
        ScaleY,
        ScaleZ,
        WorldAffine>(
        localTransforms + index,
        worldTransforms + index,
        count - index,
        TransformTrigPolicy::FastApprox);
}
#endif

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float LocalTransform::*RotationZ,
    float LocalTransform::*ScaleX,
    float LocalTransform::*ScaleY,
    float LocalTransform::*ScaleZ,
    float (WorldTransform::*WorldMatrix)[16]>
void ApplyDenseTransformZToMatrix4x4(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    HotKernelRange range,
    TransformTrigPolicy trigPolicy = TransformTrigPolicy::Exact,
    KernelBackend backend = PreferredKernelBackend()) noexcept {
    assert((range.count == 0U || localTransforms != nullptr) && "ECS transform hot kernel received null input column");
    assert((range.count == 0U || worldTransforms != nullptr) && "ECS transform hot kernel received null output column");
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
    if (trigPolicy == TransformTrigPolicy::FastApprox && ShouldUseSse2TransformFastApprox(backend)) {
        ApplyDenseTransformZToMatrix4x4Sse2FastApprox<
            LocalTransform,
            WorldTransform,
            TranslationX,
            TranslationY,
            TranslationZ,
            RotationZ,
            ScaleX,
            ScaleY,
            ScaleZ,
            WorldMatrix>(
            localTransforms + range.begin,
            worldTransforms + range.begin,
            range.count);
        return;
    }
#else
    static_cast<void>(backend);
#endif
    ApplyDenseTransformZToMatrix4x4Scalar<
        LocalTransform,
        WorldTransform,
        TranslationX,
        TranslationY,
        TranslationZ,
        RotationZ,
        ScaleX,
        ScaleY,
        ScaleZ,
        WorldMatrix>(
        localTransforms + range.begin,
        worldTransforms + range.begin,
        range.count,
        trigPolicy);
}

template <
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float LocalTransform::*RotationZ,
    float LocalTransform::*ScaleX,
    float LocalTransform::*ScaleY,
    float LocalTransform::*ScaleZ,
    float (WorldTransform::*WorldMatrix)[16]>
[[nodiscard]] double ApplyDenseTransformZToMatrix4x4AndReduce(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    HotKernelRange range,
    TransformTrigPolicy trigPolicy = TransformTrigPolicy::Exact,
    KernelBackend backend = PreferredKernelBackend()) noexcept {
    assert((range.count == 0U || localTransforms != nullptr) && "ECS transform hot kernel received null input column");
    assert((range.count == 0U || worldTransforms != nullptr) && "ECS transform hot kernel received null output column");
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
    if (trigPolicy == TransformTrigPolicy::FastApprox && ShouldUseSse2TransformFastApprox(backend)) {
        return ApplyDenseTransformZToMatrix4x4Sse2FastApproxAndReduce<
            LocalTransform,
            WorldTransform,
            TranslationX,
            TranslationY,
            TranslationZ,
            RotationZ,
            ScaleX,
            ScaleY,
            ScaleZ,
            WorldMatrix>(
            localTransforms + range.begin,
            worldTransforms + range.begin,
            range.count);
    }
#else
    static_cast<void>(backend);
#endif
    return ApplyDenseTransformZToMatrix4x4ScalarAndReduce<
        LocalTransform,
        WorldTransform,
        TranslationX,
        TranslationY,
        TranslationZ,
        RotationZ,
        ScaleX,
        ScaleY,
        ScaleZ,
        WorldMatrix>(
        localTransforms + range.begin,
        worldTransforms + range.begin,
        range.count,
        trigPolicy);
}

template <
    typename LocalTransform,
    typename WorldTransform,
    typename VersionState,
    auto LocalVersion,
    auto AppliedLocalVersion,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float LocalTransform::*RotationZ,
    float LocalTransform::*ScaleX,
    float LocalTransform::*ScaleY,
    float LocalTransform::*ScaleZ,
    float (WorldTransform::*WorldMatrix)[16]>
[[nodiscard]] DenseTransformVersionedApplyStats ApplyDenseTransformZToMatrix4x4VersionedAndReduce(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    VersionState* versions,
    HotKernelRange range,
    TransformTrigPolicy trigPolicy = TransformTrigPolicy::Exact,
    KernelBackend backend = PreferredKernelBackend()) noexcept {
    assert((range.count == 0U || localTransforms != nullptr) && "ECS transform hot kernel received null input column");
    assert((range.count == 0U || worldTransforms != nullptr) && "ECS transform hot kernel received null output column");
    assert((range.count == 0U || versions != nullptr) && "ECS transform hot kernel received null version column");

    DenseTransformVersionedApplyStats stats{
        .visited = range.count,
    };
    const std::size_t rangeEnd = range.End();
    std::size_t index = range.begin;
    while (index < rangeEnd) {
        if (versions[index].*AppliedLocalVersion == versions[index].*LocalVersion) {
            ++stats.skipped;
            ++index;
            continue;
        }

        const std::size_t dirtyBegin = index;
        do {
            ++index;
        } while (index < rangeEnd && versions[index].*AppliedLocalVersion != versions[index].*LocalVersion);
        const std::size_t dirtyCount = index - dirtyBegin;
        stats.checksum += ApplyDenseTransformZToMatrix4x4AndReduce<
            LocalTransform,
            WorldTransform,
            TranslationX,
            TranslationY,
            TranslationZ,
            RotationZ,
            ScaleX,
            ScaleY,
            ScaleZ,
            WorldMatrix>(
            localTransforms,
            worldTransforms,
            HotKernelRange{ .begin = dirtyBegin, .count = dirtyCount },
            trigPolicy,
            backend);
        for (std::size_t dirtyIndex = dirtyBegin; dirtyIndex < index; ++dirtyIndex) {
            versions[dirtyIndex].*AppliedLocalVersion = versions[dirtyIndex].*LocalVersion;
        }
        stats.updated += dirtyCount;
    }
    return stats;
}

template <
    typename LocalTransform,
    typename WorldTransform,
    typename VersionState,
    auto LocalVersion,
    auto AppliedLocalVersion,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float LocalTransform::*RotationZ,
    float LocalTransform::*ScaleX,
    float LocalTransform::*ScaleY,
    float LocalTransform::*ScaleZ,
    float (WorldTransform::*WorldMatrix)[16]>
[[nodiscard]] DenseTransformVersionedApplyStats ApplyDenseTransformZToMatrix4x4VersionedSummariesAndReduce(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    VersionState* versions,
    std::span<DenseTransformVersionRangeSummary> summaries,
    TransformTrigPolicy trigPolicy = TransformTrigPolicy::Exact,
    KernelBackend backend = PreferredKernelBackend()) noexcept {
    DenseTransformVersionedApplyStats totals;
    for (DenseTransformVersionRangeSummary& summary : summaries) {
        totals.visited += summary.range.count;
        if (summary.dirtyCount == 0U) {
            totals.skipped += summary.range.count;
            continue;
        }

        const DenseTransformVersionedApplyStats stats =
            ApplyDenseTransformZToMatrix4x4VersionedAndReduce<
                LocalTransform,
                WorldTransform,
                VersionState,
                LocalVersion,
                AppliedLocalVersion,
                TranslationX,
                TranslationY,
                TranslationZ,
                RotationZ,
                ScaleX,
                ScaleY,
                ScaleZ,
                WorldMatrix>(
                localTransforms,
                worldTransforms,
                versions,
                summary.range,
                trigPolicy,
                backend);
        totals.updated += stats.updated;
        totals.skipped += stats.skipped;
        totals.checksum += stats.checksum;
        summary.dirtyCount = 0U;
    }
    return totals;
}

template <
    typename MutableBatch,
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float LocalTransform::*RotationZ,
    float LocalTransform::*ScaleX,
    float LocalTransform::*ScaleY,
    float LocalTransform::*ScaleZ,
    float (WorldTransform::*WorldMatrix)[16],
    std::size_t LocalIndex = 0U,
    std::size_t WorldIndex = 1U>
void ApplyDenseTransformZToMatrix4x4Batch(
    MutableBatch& batch,
    TransformTrigPolicy trigPolicy = TransformTrigPolicy::Exact,
    KernelBackend backend = PreferredKernelBackend()) noexcept {
    ApplyDenseTransformZToMatrix4x4<
        LocalTransform,
        WorldTransform,
        TranslationX,
        TranslationY,
        TranslationZ,
        RotationZ,
        ScaleX,
        ScaleY,
        ScaleZ,
        WorldMatrix>(
        batch.template Components<LocalIndex>(),
        batch.template Components<WorldIndex>(),
        HotKernelRange{ .begin = 0U, .count = batch.Count() },
        trigPolicy,
        backend);
}

template <
    typename MutableBatch,
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float LocalTransform::*RotationZ,
    float LocalTransform::*ScaleX,
    float LocalTransform::*ScaleY,
    float LocalTransform::*ScaleZ,
    float (WorldTransform::*WorldMatrix)[16],
    std::size_t LocalIndex = 0U,
    std::size_t WorldIndex = 1U>
[[nodiscard]] double ApplyDenseTransformZToMatrix4x4BatchAndReduce(
    MutableBatch& batch,
    TransformTrigPolicy trigPolicy = TransformTrigPolicy::Exact,
    KernelBackend backend = PreferredKernelBackend()) noexcept {
    return ApplyDenseTransformZToMatrix4x4AndReduce<
        LocalTransform,
        WorldTransform,
        TranslationX,
        TranslationY,
        TranslationZ,
        RotationZ,
        ScaleX,
        ScaleY,
        ScaleZ,
        WorldMatrix>(
        batch.template Components<LocalIndex>(),
        batch.template Components<WorldIndex>(),
        HotKernelRange{ .begin = 0U, .count = batch.Count() },
        trigPolicy,
        backend);
}

template <
    typename MutableBatch,
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float LocalTransform::*RotationZ,
    float LocalTransform::*ScaleX,
    float LocalTransform::*ScaleY,
    float LocalTransform::*ScaleZ,
    float (WorldTransform::*WorldAffine)[12],
    std::size_t LocalIndex = 0U,
    std::size_t WorldIndex = 1U>
void ApplyDenseTransformZToAffine3x4Batch(
    MutableBatch& batch,
    TransformTrigPolicy trigPolicy = TransformTrigPolicy::Exact,
    KernelBackend backend = PreferredKernelBackend()) noexcept {
    ApplyDenseTransformZToAffine3x4<
        LocalTransform,
        WorldTransform,
        TranslationX,
        TranslationY,
        TranslationZ,
        RotationZ,
        ScaleX,
        ScaleY,
        ScaleZ,
        WorldAffine>(
        batch.template Components<LocalIndex>(),
        batch.template Components<WorldIndex>(),
        HotKernelRange{ .begin = 0U, .count = batch.Count() },
        trigPolicy,
        backend);
}

template <
    typename MutableBatch,
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float LocalTransform::*RotationZ,
    float LocalTransform::*ScaleX,
    float LocalTransform::*ScaleY,
    float LocalTransform::*ScaleZ,
    float (WorldTransform::*WorldAffine)[12],
    std::size_t LocalIndex = 0U,
    std::size_t WorldIndex = 1U>
[[nodiscard]] double ApplyDenseTransformZToAffine3x4BatchAndReduce(
    MutableBatch& batch,
    TransformTrigPolicy trigPolicy = TransformTrigPolicy::Exact,
    KernelBackend backend = PreferredKernelBackend()) noexcept {
    return ApplyDenseTransformZToAffine3x4AndReduce<
        LocalTransform,
        WorldTransform,
        TranslationX,
        TranslationY,
        TranslationZ,
        RotationZ,
        ScaleX,
        ScaleY,
        ScaleZ,
        WorldAffine>(
        batch.template Components<LocalIndex>(),
        batch.template Components<WorldIndex>(),
        HotKernelRange{ .begin = 0U, .count = batch.Count() },
        trigPolicy,
        backend);
}

template <
    typename MutableBatch,
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float (WorldTransform::*WorldAffine)[12],
    std::size_t LocalIndex = 0U,
    std::size_t WorldIndex = 1U>
void ApplyDenseTransformTranslationToAffine3x4Batch(MutableBatch& batch) noexcept {
    ApplyDenseTransformTranslationToAffine3x4<
        LocalTransform,
        WorldTransform,
        TranslationX,
        TranslationY,
        TranslationZ,
        WorldAffine>(
        batch.template Components<LocalIndex>(),
        batch.template Components<WorldIndex>(),
        HotKernelRange{ .begin = 0U, .count = batch.Count() });
}

template <
    typename MutableBatch,
    typename LocalTransform,
    typename WorldTransform,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float (WorldTransform::*WorldAffine)[12],
    std::size_t LocalIndex = 0U,
    std::size_t WorldIndex = 1U>
[[nodiscard]] double ApplyDenseTransformTranslationToAffine3x4BatchAndReduce(MutableBatch& batch) noexcept {
    return ApplyDenseTransformTranslationToAffine3x4AndReduce<
        LocalTransform,
        WorldTransform,
        TranslationX,
        TranslationY,
        TranslationZ,
        WorldAffine>(
        batch.template Components<LocalIndex>(),
        batch.template Components<WorldIndex>(),
        HotKernelRange{ .begin = 0U, .count = batch.Count() });
}

inline void ApplyDenseFloat2VelocityColumnsScalar(
    float* positionX,
    float* positionY,
    const float* velocityX,
    const float* velocityY,
    std::size_t count,
    float scale) noexcept {
    assert((count == 0U || positionX != nullptr) && "ECS hot kernel received null X output column");
    assert((count == 0U || positionY != nullptr) && "ECS hot kernel received null Y output column");
    assert((count == 0U || velocityX != nullptr) && "ECS hot kernel received null X input column");
    assert((count == 0U || velocityY != nullptr) && "ECS hot kernel received null Y input column");

    for (std::size_t index = 0U; index < count; ++index) {
        positionX[index] += velocityX[index] * scale;
        positionY[index] += velocityY[index] * scale;
    }
}

template <typename BackendTag>
void ApplyDenseFloat2VelocityColumnsVector(
    float* positionX,
    float* positionY,
    const float* velocityX,
    const float* velocityY,
    std::size_t count,
    float scale) noexcept {
    assert((count == 0U || positionX != nullptr) && "ECS hot kernel received null X output column");
    assert((count == 0U || positionY != nullptr) && "ECS hot kernel received null Y output column");
    assert((count == 0U || velocityX != nullptr) && "ECS hot kernel received null X input column");
    assert((count == 0U || velocityY != nullptr) && "ECS hot kernel received null Y input column");

    using FloatLanes = KernelFloatLanes<BackendTag>;

    const FloatLanes scaleLanes = FloatLanes::Splat(scale);
    for (std::size_t begin = 0U; begin < count; begin += FloatLanes::LaneCount) {
        const std::size_t remaining = count - begin;
        const std::size_t batchCount = remaining < FloatLanes::LaneCount ? remaining : FloatLanes::LaneCount;

        FloatLanes x = FloatLanes::LoadPartial(positionX + begin, batchCount);
        FloatLanes y = FloatLanes::LoadPartial(positionY + begin, batchCount);
        const FloatLanes vx = FloatLanes::LoadPartial(velocityX + begin, batchCount);
        const FloatLanes vy = FloatLanes::LoadPartial(velocityY + begin, batchCount);
        x = KernelDeterministicMulAdd(vx, scaleLanes, x);
        y = KernelDeterministicMulAdd(vy, scaleLanes, y);
        x.StorePartial(positionX + begin, batchCount);
        y.StorePartial(positionY + begin, batchCount);
    }
}

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
template <>
inline void ApplyDenseFloat2VelocityColumnsVector<KernelSse2Tag>(
    float* positionX,
    float* positionY,
    const float* velocityX,
    const float* velocityY,
    std::size_t count,
    float scale) noexcept {
    assert((count == 0U || positionX != nullptr) && "ECS hot kernel received null X output column");
    assert((count == 0U || positionY != nullptr) && "ECS hot kernel received null Y output column");
    assert((count == 0U || velocityX != nullptr) && "ECS hot kernel received null X input column");
    assert((count == 0U || velocityY != nullptr) && "ECS hot kernel received null Y input column");

    const __m128 scaleLanes = _mm_set1_ps(scale);
    std::size_t index = 0U;
    for (; index + 4U <= count; index += 4U) {
        const __m128 x = _mm_loadu_ps(positionX + index);
        const __m128 y = _mm_loadu_ps(positionY + index);
        const __m128 vx = _mm_loadu_ps(velocityX + index);
        const __m128 vy = _mm_loadu_ps(velocityY + index);
        _mm_storeu_ps(positionX + index, _mm_add_ps(x, _mm_mul_ps(vx, scaleLanes)));
        _mm_storeu_ps(positionY + index, _mm_add_ps(y, _mm_mul_ps(vy, scaleLanes)));
    }
    ApplyDenseFloat2VelocityColumnsScalar(positionX + index, positionY + index, velocityX + index, velocityY + index, count - index, scale);
}
#endif

#if defined(__AVX2__)
template <>
inline void ApplyDenseFloat2VelocityColumnsVector<KernelAvx2Tag>(
    float* positionX,
    float* positionY,
    const float* velocityX,
    const float* velocityY,
    std::size_t count,
    float scale) noexcept {
    assert((count == 0U || positionX != nullptr) && "ECS hot kernel received null X output column");
    assert((count == 0U || positionY != nullptr) && "ECS hot kernel received null Y output column");
    assert((count == 0U || velocityX != nullptr) && "ECS hot kernel received null X input column");
    assert((count == 0U || velocityY != nullptr) && "ECS hot kernel received null Y input column");

    const __m256 scaleLanes = _mm256_set1_ps(scale);
    std::size_t index = 0U;
    for (; index + 8U <= count; index += 8U) {
        const __m256 x = _mm256_loadu_ps(positionX + index);
        const __m256 y = _mm256_loadu_ps(positionY + index);
        const __m256 vx = _mm256_loadu_ps(velocityX + index);
        const __m256 vy = _mm256_loadu_ps(velocityY + index);
        _mm256_storeu_ps(positionX + index, _mm256_add_ps(x, _mm256_mul_ps(vx, scaleLanes)));
        _mm256_storeu_ps(positionY + index, _mm256_add_ps(y, _mm256_mul_ps(vy, scaleLanes)));
    }
    ApplyDenseFloat2VelocityColumnsScalar(positionX + index, positionY + index, velocityX + index, velocityY + index, count - index, scale);
}
#endif

inline void ApplyDenseFloat2VelocityColumns(
    float* positionX,
    float* positionY,
    const float* velocityX,
    const float* velocityY,
    std::size_t count,
    float scale,
    KernelBackend backend = PreferredKernelBackend()) noexcept {
    switch (backend) {
    case KernelBackend::Neon:
        if (IsKernelBackendSupported(KernelBackend::Neon)) {
            ApplyDenseFloat2VelocityColumnsVector<KernelNeonTag>(positionX, positionY, velocityX, velocityY, count, scale);
            return;
        }
        break;
    case KernelBackend::Avx512:
        if (IsKernelBackendSupported(KernelBackend::Avx512)) {
            ApplyDenseFloat2VelocityColumnsVector<KernelAvx512Tag>(positionX, positionY, velocityX, velocityY, count, scale);
            return;
        }
        break;
    case KernelBackend::Avx2:
        if (IsKernelBackendSupported(KernelBackend::Avx2)) {
            ApplyDenseFloat2VelocityColumnsVector<KernelAvx2Tag>(positionX, positionY, velocityX, velocityY, count, scale);
            return;
        }
        break;
    case KernelBackend::Sse2:
        if (IsKernelBackendSupported(KernelBackend::Sse2)) {
            ApplyDenseFloat2VelocityColumnsVector<KernelSse2Tag>(positionX, positionY, velocityX, velocityY, count, scale);
            return;
        }
        break;
    case KernelBackend::Scalar:
        break;
    }

    ApplyDenseFloat2VelocityColumnsScalar(positionX, positionY, velocityX, velocityY, count, scale);
}

namespace detail {

template <typename Component>
[[nodiscard]] bool IsPackedFloat2Layout(float Component::*x, float Component::*y) noexcept {
    if constexpr (std::is_standard_layout_v<Component> && std::is_trivially_copyable_v<Component> && std::is_default_constructible_v<Component>) {
        Component probe{};
        const auto* base = reinterpret_cast<const std::byte*>(std::addressof(probe));
        const auto* xAddress = reinterpret_cast<const std::byte*>(std::addressof(probe.*x));
        const auto* yAddress = reinterpret_cast<const std::byte*>(std::addressof(probe.*y));
        const std::size_t xOffset = static_cast<std::size_t>(xAddress - base);
        const std::size_t yOffset = static_cast<std::size_t>(yAddress - base);
        return sizeof(Component) == sizeof(float) * 2U &&
            alignof(Component) >= alignof(float) &&
            xOffset == 0U &&
            yOffset == sizeof(float);
    } else {
        static_cast<void>(x);
        static_cast<void>(y);
        return false;
    }
}

inline void ApplyDensePackedFloat2VelocityScalar(float* positions, const float* velocities, std::size_t count, float scale) noexcept {
    assert((count == 0U || positions != nullptr) && "ECS packed hot kernel received null output column");
    assert((count == 0U || velocities != nullptr) && "ECS packed hot kernel received null input column");

    const std::size_t laneCount = count * 2U;
    for (std::size_t index = 0U; index < laneCount; ++index) {
        positions[index] += velocities[index] * scale;
    }
}

[[nodiscard]] inline double ApplyDensePackedFloat2VelocityScalarAndReduceX(float* positions, const float* velocities, std::size_t count, float scale) noexcept {
    assert((count == 0U || positions != nullptr) && "ECS packed hot kernel received null output column");
    assert((count == 0U || velocities != nullptr) && "ECS packed hot kernel received null input column");

    double checksum = 0.0;
    for (std::size_t index = 0U; index < count; ++index) {
        const std::size_t lane = index * 2U;
        const float x = positions[lane] + velocities[lane] * scale;
        const float y = positions[lane + 1U] + velocities[lane + 1U] * scale;
        positions[lane] = x;
        positions[lane + 1U] = y;
        checksum += static_cast<double>(x);
    }
    return checksum;
}

template <typename BackendTag>
void ApplyDensePackedFloat2VelocityVector(float* positions, const float* velocities, std::size_t count, float scale) noexcept {
    assert((count == 0U || positions != nullptr) && "ECS packed hot kernel received null output column");
    assert((count == 0U || velocities != nullptr) && "ECS packed hot kernel received null input column");

    using FloatLanes = KernelFloatLanes<BackendTag>;

    const FloatLanes scaleLanes = FloatLanes::Splat(scale);
    const std::size_t laneCount = count * 2U;
    for (std::size_t begin = 0U; begin < laneCount; begin += FloatLanes::LaneCount) {
        const std::size_t remaining = laneCount - begin;
        const std::size_t batchCount = remaining < FloatLanes::LaneCount ? remaining : FloatLanes::LaneCount;

        FloatLanes position = FloatLanes::LoadPartial(positions + begin, batchCount);
        const FloatLanes velocity = FloatLanes::LoadPartial(velocities + begin, batchCount);
        position = KernelDeterministicMulAdd(velocity, scaleLanes, position);
        position.StorePartial(positions + begin, batchCount);
    }
}

template <typename BackendTag>
[[nodiscard]] double ApplyDensePackedFloat2VelocityVectorAndReduceX(float* positions, const float* velocities, std::size_t count, float scale) noexcept {
    ApplyDensePackedFloat2VelocityVector<BackendTag>(positions, velocities, count, scale);
    double checksum = 0.0;
    for (std::size_t index = 0U; index < count; ++index) {
        checksum += static_cast<double>(positions[index * 2U]);
    }
    return checksum;
}

template <typename BackendTag>
inline constexpr bool HasNativePackedFloat2VelocityVector = false;

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
template <>
inline constexpr bool HasNativePackedFloat2VelocityVector<KernelSse2Tag> = true;

template <>
inline void ApplyDensePackedFloat2VelocityVector<KernelSse2Tag>(float* positions, const float* velocities, std::size_t count, float scale) noexcept {
    assert((count == 0U || positions != nullptr) && "ECS packed hot kernel received null output column");
    assert((count == 0U || velocities != nullptr) && "ECS packed hot kernel received null input column");

    const __m128 scaleLanes = _mm_set1_ps(scale);
    const std::size_t laneCount = count * 2U;
    std::size_t index = 0U;
    for (; index + 4U <= laneCount; index += 4U) {
        const __m128 position = _mm_loadu_ps(positions + index);
        const __m128 velocity = _mm_loadu_ps(velocities + index);
        _mm_storeu_ps(positions + index, _mm_add_ps(position, _mm_mul_ps(velocity, scaleLanes)));
    }
    ApplyDensePackedFloat2VelocityScalar(positions + index, velocities + index, (laneCount - index) / 2U, scale);
    if ((laneCount - index) % 2U != 0U) {
        positions[laneCount - 1U] += velocities[laneCount - 1U] * scale;
    }
}

template <>
[[nodiscard]] inline double ApplyDensePackedFloat2VelocityVectorAndReduceX<KernelSse2Tag>(float* positions, const float* velocities, std::size_t count, float scale) noexcept {
    assert((count == 0U || positions != nullptr) && "ECS packed hot kernel received null output column");
    assert((count == 0U || velocities != nullptr) && "ECS packed hot kernel received null input column");

    const __m128 scaleLanes = _mm_set1_ps(scale);
    double checksum = 0.0;
    std::size_t laneIndex = 0U;
    const std::size_t laneCount = count * 2U;
    for (; laneIndex + 4U <= laneCount; laneIndex += 4U) {
        const __m128 position = _mm_loadu_ps(positions + laneIndex);
        const __m128 velocity = _mm_loadu_ps(velocities + laneIndex);
        const __m128 updated = _mm_add_ps(position, _mm_mul_ps(velocity, scaleLanes));
        _mm_storeu_ps(positions + laneIndex, updated);
        alignas(16) float lanes[4];
        _mm_store_ps(lanes, updated);
        checksum += static_cast<double>(lanes[0]) + static_cast<double>(lanes[2]);
    }
    const std::size_t entityIndex = laneIndex / 2U;
    return checksum + ApplyDensePackedFloat2VelocityScalarAndReduceX(positions + laneIndex, velocities + laneIndex, count - entityIndex, scale);
}
#endif

#if defined(__AVX2__)
template <>
inline constexpr bool HasNativePackedFloat2VelocityVector<KernelAvx2Tag> = true;

template <>
inline void ApplyDensePackedFloat2VelocityVector<KernelAvx2Tag>(float* positions, const float* velocities, std::size_t count, float scale) noexcept {
    assert((count == 0U || positions != nullptr) && "ECS packed hot kernel received null output column");
    assert((count == 0U || velocities != nullptr) && "ECS packed hot kernel received null input column");

    const __m256 scaleLanes = _mm256_set1_ps(scale);
    const std::size_t laneCount = count * 2U;
    std::size_t index = 0U;
    for (; index + 8U <= laneCount; index += 8U) {
        const __m256 position = _mm256_loadu_ps(positions + index);
        const __m256 velocity = _mm256_loadu_ps(velocities + index);
        _mm256_storeu_ps(positions + index, _mm256_add_ps(position, _mm256_mul_ps(velocity, scaleLanes)));
    }
    ApplyDensePackedFloat2VelocityScalar(positions + index, velocities + index, (laneCount - index) / 2U, scale);
    if ((laneCount - index) % 2U != 0U) {
        positions[laneCount - 1U] += velocities[laneCount - 1U] * scale;
    }
}

template <>
[[nodiscard]] inline double ApplyDensePackedFloat2VelocityVectorAndReduceX<KernelAvx2Tag>(float* positions, const float* velocities, std::size_t count, float scale) noexcept {
    assert((count == 0U || positions != nullptr) && "ECS packed hot kernel received null output column");
    assert((count == 0U || velocities != nullptr) && "ECS packed hot kernel received null input column");

    const __m256 scaleLanes = _mm256_set1_ps(scale);
    const __m256 xLaneMask = _mm256_set_ps(0.0F, 1.0F, 0.0F, 1.0F, 0.0F, 1.0F, 0.0F, 1.0F);
    __m256 checksumLanes = _mm256_setzero_ps();
    double checksum = 0.0;
    const std::size_t laneCount = count * 2U;
    std::size_t laneIndex = 0U;
    for (; laneIndex + 8U <= laneCount; laneIndex += 8U) {
        const __m256 position = _mm256_loadu_ps(positions + laneIndex);
        const __m256 velocity = _mm256_loadu_ps(velocities + laneIndex);
        const __m256 updated = _mm256_add_ps(position, _mm256_mul_ps(velocity, scaleLanes));
        _mm256_storeu_ps(positions + laneIndex, updated);
        checksumLanes = _mm256_add_ps(checksumLanes, _mm256_mul_ps(updated, xLaneMask));
    }
    alignas(32) float lanes[8];
    _mm256_store_ps(lanes, checksumLanes);
    checksum += static_cast<double>(lanes[0])
        + static_cast<double>(lanes[2])
        + static_cast<double>(lanes[4])
        + static_cast<double>(lanes[6]);
    const std::size_t entityIndex = laneIndex / 2U;
    return checksum + ApplyDensePackedFloat2VelocityScalarAndReduceX(positions + laneIndex, velocities + laneIndex, count - entityIndex, scale);
}
#endif

template <typename Position, typename Velocity>
[[nodiscard]] bool CanUseDensePackedFloat2Velocity(
    float Position::*positionX,
    float Position::*positionY,
    float Velocity::*velocityX,
    float Velocity::*velocityY) noexcept {
    return IsPackedFloat2Layout(positionX, positionY) && IsPackedFloat2Layout(velocityX, velocityY);
}

template <typename BackendTag, typename Position, typename Velocity>
[[nodiscard]] bool TryApplyDensePackedFloat2Velocity(
    Position* positions,
    const Velocity* velocities,
    std::size_t count,
    float scale,
    float Position::*positionX,
    float Position::*positionY,
    float Velocity::*velocityX,
    float Velocity::*velocityY) noexcept {
    if constexpr (!HasNativePackedFloat2VelocityVector<BackendTag>) {
        static_cast<void>(positions);
        static_cast<void>(velocities);
        static_cast<void>(count);
        static_cast<void>(scale);
        static_cast<void>(positionX);
        static_cast<void>(positionY);
        static_cast<void>(velocityX);
        static_cast<void>(velocityY);
        return false;
    } else {
        if (!CanUseDensePackedFloat2Velocity(positionX, positionY, velocityX, velocityY)) {
            return false;
        }
        ApplyDensePackedFloat2VelocityVector<BackendTag>(
            reinterpret_cast<float*>(positions),
            reinterpret_cast<const float*>(velocities),
            count,
            scale);
        return true;
    }
}

template <typename BackendTag, typename Position, typename Velocity>
[[nodiscard]] bool TryApplyDensePackedFloat2VelocityAndReduceX(
    Position* positions,
    const Velocity* velocities,
    std::size_t count,
    float scale,
    float Position::*positionX,
    float Position::*positionY,
    float Velocity::*velocityX,
    float Velocity::*velocityY,
    double& checksum) noexcept {
    if constexpr (!HasNativePackedFloat2VelocityVector<BackendTag>) {
        static_cast<void>(positions);
        static_cast<void>(velocities);
        static_cast<void>(count);
        static_cast<void>(scale);
        static_cast<void>(positionX);
        static_cast<void>(positionY);
        static_cast<void>(velocityX);
        static_cast<void>(velocityY);
        static_cast<void>(checksum);
        return false;
    } else {
        if (!CanUseDensePackedFloat2Velocity(positionX, positionY, velocityX, velocityY)) {
            return false;
        }
        checksum = ApplyDensePackedFloat2VelocityVectorAndReduceX<BackendTag>(
            reinterpret_cast<float*>(positions),
            reinterpret_cast<const float*>(velocities),
            count,
            scale);
        return true;
    }
}

} // namespace detail

template <typename Position, typename Velocity>
[[nodiscard]] bool CanUseDensePackedFloat2Velocity(
    float Position::*positionX,
    float Position::*positionY,
    float Velocity::*velocityX,
    float Velocity::*velocityY) noexcept {
    return detail::CanUseDensePackedFloat2Velocity(positionX, positionY, velocityX, velocityY);
}

template <typename Position, typename Velocity>
void ApplyDenseFloat2VelocityScalar(
    Position* positions,
    const Velocity* velocities,
    std::size_t count,
    float scale,
    float Position::*positionX,
    float Position::*positionY,
    float Velocity::*velocityX,
    float Velocity::*velocityY) noexcept {
    assert((count == 0U || positions != nullptr) && "ECS hot kernel received null output column");
    assert((count == 0U || velocities != nullptr) && "ECS hot kernel received null input column");
    assert(positionX != nullptr && positionY != nullptr && velocityX != nullptr && velocityY != nullptr);

    for (std::size_t index = 0U; index < count; ++index) {
        positions[index].*positionX += velocities[index].*velocityX * scale;
        positions[index].*positionY += velocities[index].*velocityY * scale;
    }
}

template <typename Position, typename Velocity>
[[nodiscard]] double ApplyDenseFloat2VelocityScalarAndReduceX(
    Position* positions,
    const Velocity* velocities,
    std::size_t count,
    float scale,
    float Position::*positionX,
    float Position::*positionY,
    float Velocity::*velocityX,
    float Velocity::*velocityY) noexcept {
    assert((count == 0U || positions != nullptr) && "ECS hot kernel received null output column");
    assert((count == 0U || velocities != nullptr) && "ECS hot kernel received null input column");
    assert(positionX != nullptr && positionY != nullptr && velocityX != nullptr && velocityY != nullptr);

    double checksum = 0.0;
    for (std::size_t index = 0U; index < count; ++index) {
        const float x = positions[index].*positionX + velocities[index].*velocityX * scale;
        const float y = positions[index].*positionY + velocities[index].*velocityY * scale;
        positions[index].*positionX = x;
        positions[index].*positionY = y;
        checksum += static_cast<double>(x);
    }
    return checksum;
}

template <typename BackendTag, typename Position, typename Velocity>
void ApplyDenseFloat2VelocityVector(
    Position* positions,
    const Velocity* velocities,
    std::size_t count,
    float scale,
    float Position::*positionX,
    float Position::*positionY,
    float Velocity::*velocityX,
    float Velocity::*velocityY) noexcept {
    assert((count == 0U || positions != nullptr) && "ECS hot kernel received null output column");
    assert((count == 0U || velocities != nullptr) && "ECS hot kernel received null input column");
    assert(positionX != nullptr && positionY != nullptr && velocityX != nullptr && velocityY != nullptr);

    using FloatLanes = KernelFloatLanes<BackendTag>;
    using Float2Lanes = KernelFloat2Lanes<BackendTag>;

    const FloatLanes scaleLanes = FloatLanes::Splat(scale);
    for (std::size_t begin = 0U; begin < count; begin += FloatLanes::LaneCount) {
        const std::size_t remaining = count - begin;
        const std::size_t batchCount = remaining < FloatLanes::LaneCount ? remaining : FloatLanes::LaneCount;

        Float2Lanes position = Float2Lanes::LoadMembersPartial(positions + begin, batchCount, 0.0F, positionX, positionY);
        const Float2Lanes velocity = Float2Lanes::LoadMembersPartial(velocities + begin, batchCount, 0.0F, velocityX, velocityY);
        position = KernelDeterministicMulAdd(velocity, scaleLanes, position);
        position.StoreMembersPartial(positions + begin, batchCount, positionX, positionY);
    }
}

template <typename Position, typename Velocity>
[[nodiscard]] double ApplyDenseFloat2VelocityAndReduceX(
    Position* positions,
    const Velocity* velocities,
    std::size_t count,
    float scale,
    float Position::*positionX,
    float Position::*positionY,
    float Velocity::*velocityX,
    float Velocity::*velocityY,
    KernelBackend backend = PreferredKernelBackend()) noexcept {
    double checksum = 0.0;
    switch (backend) {
    case KernelBackend::Neon:
        if (IsKernelBackendSupported(KernelBackend::Neon)
            && detail::TryApplyDensePackedFloat2VelocityAndReduceX<KernelNeonTag>(positions, velocities, count, scale, positionX, positionY, velocityX, velocityY, checksum)) {
            return checksum;
        }
        break;
    case KernelBackend::Avx512:
        if (IsKernelBackendSupported(KernelBackend::Avx512)
            && detail::TryApplyDensePackedFloat2VelocityAndReduceX<KernelAvx512Tag>(positions, velocities, count, scale, positionX, positionY, velocityX, velocityY, checksum)) {
            return checksum;
        }
        break;
    case KernelBackend::Avx2:
        if (IsKernelBackendSupported(KernelBackend::Avx2)
            && detail::TryApplyDensePackedFloat2VelocityAndReduceX<KernelAvx2Tag>(positions, velocities, count, scale, positionX, positionY, velocityX, velocityY, checksum)) {
            return checksum;
        }
        break;
    case KernelBackend::Sse2:
        if (IsKernelBackendSupported(KernelBackend::Sse2)
            && detail::TryApplyDensePackedFloat2VelocityAndReduceX<KernelSse2Tag>(positions, velocities, count, scale, positionX, positionY, velocityX, velocityY, checksum)) {
            return checksum;
        }
        break;
    case KernelBackend::Scalar:
        break;
    }

    return ApplyDenseFloat2VelocityScalarAndReduceX(positions, velocities, count, scale, positionX, positionY, velocityX, velocityY);
}

template <typename Position, typename Velocity>
void ApplyDenseFloat2Velocity(
    Position* positions,
    const Velocity* velocities,
    std::size_t count,
    float scale,
    float Position::*positionX,
    float Position::*positionY,
    float Velocity::*velocityX,
    float Velocity::*velocityY,
    KernelBackend backend = PreferredKernelBackend()) noexcept {
    switch (backend) {
    case KernelBackend::Neon:
        if (IsKernelBackendSupported(KernelBackend::Neon)) {
            if (detail::TryApplyDensePackedFloat2Velocity<KernelNeonTag>(positions, velocities, count, scale, positionX, positionY, velocityX, velocityY)) {
                return;
            }
            ApplyDenseFloat2VelocityVector<KernelNeonTag>(positions, velocities, count, scale, positionX, positionY, velocityX, velocityY);
            return;
        }
        break;
    case KernelBackend::Avx512:
        if (IsKernelBackendSupported(KernelBackend::Avx512)) {
            if (detail::TryApplyDensePackedFloat2Velocity<KernelAvx512Tag>(positions, velocities, count, scale, positionX, positionY, velocityX, velocityY)) {
                return;
            }
            ApplyDenseFloat2VelocityVector<KernelAvx512Tag>(positions, velocities, count, scale, positionX, positionY, velocityX, velocityY);
            return;
        }
        break;
    case KernelBackend::Avx2:
        if (IsKernelBackendSupported(KernelBackend::Avx2)) {
            if (detail::TryApplyDensePackedFloat2Velocity<KernelAvx2Tag>(positions, velocities, count, scale, positionX, positionY, velocityX, velocityY)) {
                return;
            }
            ApplyDenseFloat2VelocityVector<KernelAvx2Tag>(positions, velocities, count, scale, positionX, positionY, velocityX, velocityY);
            return;
        }
        break;
    case KernelBackend::Sse2:
        if (IsKernelBackendSupported(KernelBackend::Sse2)) {
            if (detail::TryApplyDensePackedFloat2Velocity<KernelSse2Tag>(positions, velocities, count, scale, positionX, positionY, velocityX, velocityY)) {
                return;
            }
            ApplyDenseFloat2VelocityVector<KernelSse2Tag>(positions, velocities, count, scale, positionX, positionY, velocityX, velocityY);
            return;
        }
        break;
    case KernelBackend::Scalar:
        break;
    }

    ApplyDenseFloat2VelocityScalar(positions, velocities, count, scale, positionX, positionY, velocityX, velocityY);
}

template <typename Position, typename Velocity>
void ApplyDenseFloat2Velocity(
    Position* positions,
    const Velocity* velocities,
    HotKernelRange range,
    float scale,
    float Position::*positionX,
    float Position::*positionY,
    float Velocity::*velocityX,
    float Velocity::*velocityY,
    KernelBackend backend = PreferredKernelBackend()) noexcept {
    assert((range.count == 0U || positions != nullptr) && "ECS hot kernel received null output column");
    assert((range.count == 0U || velocities != nullptr) && "ECS hot kernel received null input column");
    ApplyDenseFloat2Velocity(
        positions + range.begin,
        velocities + range.begin,
        range.count,
        scale,
        positionX,
        positionY,
        velocityX,
        velocityY,
        backend);
}

template <std::size_t OutputIndex = 0U, std::size_t InputIndex = 0U, typename Contract, typename Position, typename Velocity>
void ApplyDenseFloat2VelocityBatch(
    KernelBatch<Contract>& batch,
    float scale,
    float Position::*positionX,
    float Position::*positionY,
    float Velocity::*velocityX,
    float Velocity::*velocityY,
    KernelBackend backend = PreferredKernelBackend()) noexcept {
    ApplyDenseFloat2Velocity(
        batch.template Outputs<OutputIndex>(),
        batch.template Inputs<InputIndex>(),
        batch.Count(),
        scale,
        positionX,
        positionY,
        velocityX,
        velocityY,
        backend);
}

template <
    typename LocalTransform,
    typename WorldTransform,
    typename HierarchyNode,
    auto ParentIndex,
    auto LocalVersion,
    auto AppliedLocalVersion,
    auto ObservedParentWorldVersion,
    auto WorldVersion,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float LocalTransform::*RotationZ,
    float LocalTransform::*ScaleX,
    float LocalTransform::*ScaleY,
    float LocalTransform::*ScaleZ,
    float (WorldTransform::*WorldMatrix)[16]>
[[nodiscard]] DenseTransformVersionedApplyStats ApplyDenseTransformHierarchyZToMatrix4x4RangeAndReduce(
    const LocalTransform* localTransforms,
    WorldTransform* worldTransforms,
    HierarchyNode* nodes,
    HotKernelRange range,
    std::remove_cv_t<std::remove_reference_t<decltype(std::declval<HierarchyNode&>().*ParentIndex)>> invalidParent,
    TransformTrigPolicy trigPolicy = TransformTrigPolicy::FastApprox) noexcept {
    assert((range.count == 0U || localTransforms != nullptr) && "ECS hierarchy transform hot kernel received null local column");
    assert((range.count == 0U || worldTransforms != nullptr) && "ECS hierarchy transform hot kernel received null world column");
    assert((range.count == 0U || nodes != nullptr) && "ECS hierarchy transform hot kernel received null hierarchy column");

    DenseTransformVersionedApplyStats stats{
        .visited = range.count,
    };
    const std::size_t rangeEnd = range.End();
    for (std::size_t index = range.begin; index < rangeEnd; ++index) {
        const LocalTransform& local = localTransforms[index];
        HierarchyNode& node = nodes[index];
        WorldTransform& world = worldTransforms[index];
        const auto parentIndex = node.*ParentIndex;

        std::remove_cv_t<std::remove_reference_t<decltype(node.*WorldVersion)>> parentWorldVersion = 0;
        const float* parentMatrix = nullptr;
        if (parentIndex != invalidParent) {
            parentWorldVersion = nodes[parentIndex].*WorldVersion;
            parentMatrix = (worldTransforms[parentIndex].*WorldMatrix);
        }

        const bool localDirty = node.*LocalVersion != node.*AppliedLocalVersion;
        const bool parentDirty = node.*ObservedParentWorldVersion != parentWorldVersion;
        if (!localDirty && !parentDirty) {
            ++stats.skipped;
            continue;
        }

        float (&matrix)[16] = world.*WorldMatrix;
        const TransformSinCos sinCos = detail::ResolveSinCos(local.*RotationZ, trigPolicy);
        const float localMatrix[16]{
            sinCos.cos * (local.*ScaleX),
            sinCos.sin * (local.*ScaleX),
            0.0F,
            0.0F,
            -sinCos.sin * (local.*ScaleY),
            sinCos.cos * (local.*ScaleY),
            0.0F,
            0.0F,
            0.0F,
            0.0F,
            local.*ScaleZ,
            0.0F,
            local.*TranslationX,
            local.*TranslationY,
            local.*TranslationZ,
            1.0F,
        };

        if (parentMatrix == nullptr) {
            for (std::size_t element = 0U; element < 16U; ++element) {
                matrix[element] = localMatrix[element];
            }
        } else {
            matrix[0] = parentMatrix[0] * localMatrix[0] + parentMatrix[4] * localMatrix[1];
            matrix[1] = parentMatrix[1] * localMatrix[0] + parentMatrix[5] * localMatrix[1];
            matrix[2] = 0.0F;
            matrix[3] = 0.0F;
            matrix[4] = parentMatrix[0] * localMatrix[4] + parentMatrix[4] * localMatrix[5];
            matrix[5] = parentMatrix[1] * localMatrix[4] + parentMatrix[5] * localMatrix[5];
            matrix[6] = 0.0F;
            matrix[7] = 0.0F;
            matrix[8] = 0.0F;
            matrix[9] = 0.0F;
            matrix[10] = parentMatrix[10] * localMatrix[10];
            matrix[11] = 0.0F;
            matrix[12] = parentMatrix[0] * localMatrix[12] + parentMatrix[4] * localMatrix[13] + parentMatrix[12];
            matrix[13] = parentMatrix[1] * localMatrix[12] + parentMatrix[5] * localMatrix[13] + parentMatrix[13];
            matrix[14] = parentMatrix[10] * localMatrix[14] + parentMatrix[14];
            matrix[15] = 1.0F;
        }

        node.*AppliedLocalVersion = node.*LocalVersion;
        node.*ObservedParentWorldVersion = parentWorldVersion;
        ++(node.*WorldVersion);

        stats.checksum += static_cast<double>(matrix[0]) + static_cast<double>(matrix[5])
            + static_cast<double>(matrix[10]) + static_cast<double>(matrix[12]);
        ++stats.updated;
    }
    return stats;
}

template <
    typename LocalTransform,
    typename WorldTransform,
    typename HierarchyNode,
    auto ParentIndex,
    auto LocalVersion,
    auto AppliedLocalVersion,
    auto ObservedParentWorldVersion,
    auto WorldVersion,
    float LocalTransform::*TranslationX,
    float LocalTransform::*TranslationY,
    float LocalTransform::*TranslationZ,
    float LocalTransform::*RotationZ,
    float LocalTransform::*ScaleX,
    float LocalTransform::*ScaleY,
    float LocalTransform::*ScaleZ,
    float (WorldTransform::*WorldMatrix)[16]>
[[nodiscard]] DenseTransformVersionedApplyStats ApplyDenseTransformHierarchyZToMatrix4x4PointerRangeAndReduce(
    const LocalTransform* const* localTransforms,
    WorldTransform* const* worldTransforms,
    HierarchyNode* const* nodes,
    HotKernelRange range,
    std::remove_cv_t<std::remove_reference_t<decltype(std::declval<HierarchyNode&>().*ParentIndex)>> invalidParent,
    TransformTrigPolicy trigPolicy = TransformTrigPolicy::FastApprox) noexcept {
    assert((range.count == 0U || localTransforms != nullptr) && "ECS hierarchy transform hot kernel received null local pointer column");
    assert((range.count == 0U || worldTransforms != nullptr) && "ECS hierarchy transform hot kernel received null world pointer column");
    assert((range.count == 0U || nodes != nullptr) && "ECS hierarchy transform hot kernel received null hierarchy pointer column");

    DenseTransformVersionedApplyStats stats{
        .visited = range.count,
    };
    const std::size_t rangeEnd = range.End();
    for (std::size_t index = range.begin; index < rangeEnd; ++index) {
        const LocalTransform& local = *localTransforms[index];
        HierarchyNode& node = *nodes[index];
        WorldTransform& world = *worldTransforms[index];
        const auto parentIndex = node.*ParentIndex;

        std::remove_cv_t<std::remove_reference_t<decltype(node.*WorldVersion)>> parentWorldVersion = 0;
        const float* parentMatrix = nullptr;
        if (parentIndex != invalidParent) {
            parentWorldVersion = (*nodes[parentIndex]).*WorldVersion;
            parentMatrix = ((*worldTransforms[parentIndex]).*WorldMatrix);
        }

        const bool localDirty = node.*LocalVersion != node.*AppliedLocalVersion;
        const bool parentDirty = node.*ObservedParentWorldVersion != parentWorldVersion;
        if (!localDirty && !parentDirty) {
            ++stats.skipped;
            continue;
        }

        float (&matrix)[16] = world.*WorldMatrix;
        const TransformSinCos sinCos = detail::ResolveSinCos(local.*RotationZ, trigPolicy);
        const float localMatrix[16]{
            sinCos.cos * (local.*ScaleX),
            sinCos.sin * (local.*ScaleX),
            0.0F,
            0.0F,
            -sinCos.sin * (local.*ScaleY),
            sinCos.cos * (local.*ScaleY),
            0.0F,
            0.0F,
            0.0F,
            0.0F,
            local.*ScaleZ,
            0.0F,
            local.*TranslationX,
            local.*TranslationY,
            local.*TranslationZ,
            1.0F,
        };

        if (parentMatrix == nullptr) {
            for (std::size_t element = 0U; element < 16U; ++element) {
                matrix[element] = localMatrix[element];
            }
        } else {
            matrix[0] = parentMatrix[0] * localMatrix[0] + parentMatrix[4] * localMatrix[1];
            matrix[1] = parentMatrix[1] * localMatrix[0] + parentMatrix[5] * localMatrix[1];
            matrix[2] = 0.0F;
            matrix[3] = 0.0F;
            matrix[4] = parentMatrix[0] * localMatrix[4] + parentMatrix[4] * localMatrix[5];
            matrix[5] = parentMatrix[1] * localMatrix[4] + parentMatrix[5] * localMatrix[5];
            matrix[6] = 0.0F;
            matrix[7] = 0.0F;
            matrix[8] = 0.0F;
            matrix[9] = 0.0F;
            matrix[10] = parentMatrix[10] * localMatrix[10];
            matrix[11] = 0.0F;
            matrix[12] = parentMatrix[0] * localMatrix[12] + parentMatrix[4] * localMatrix[13] + parentMatrix[12];
            matrix[13] = parentMatrix[1] * localMatrix[12] + parentMatrix[5] * localMatrix[13] + parentMatrix[13];
            matrix[14] = parentMatrix[10] * localMatrix[14] + parentMatrix[14];
            matrix[15] = 1.0F;
        }

        node.*AppliedLocalVersion = node.*LocalVersion;
        node.*ObservedParentWorldVersion = parentWorldVersion;
        ++(node.*WorldVersion);

        stats.checksum += static_cast<double>(matrix[0]) + static_cast<double>(matrix[5])
            + static_cast<double>(matrix[10]) + static_cast<double>(matrix[12]);
        ++stats.updated;
    }
    return stats;
}

} // namespace kb::ecs
