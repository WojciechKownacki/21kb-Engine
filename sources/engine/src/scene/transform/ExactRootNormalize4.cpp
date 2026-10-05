#include "scene/transform/ExactRootNormalize4.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#if defined(_MSC_VER) && defined(_M_X64)
#include <xmmintrin.h>
#include <emmintrin.h>
// This isolated new helper owns FP-environment reads; existing engine TUs and
// the frozen scalar oracle keep their current flags. No contraction/reassociation.
#pragma float_control(precise, on, push)
#pragma float_control(except, on)
#pragma fenv_access(on)
#pragma fp_contract(off)
#endif

namespace exact_local_batch {
namespace {
using Quat = kb::scene::Quat;
static_assert(sizeof(Quat) == 16U && std::is_trivially_copyable_v<Quat>);

#if defined(_MSC_VER) && defined(_M_X64)
__m128 Select(__m128 mask, __m128 yes, __m128 no) noexcept {
    return _mm_or_ps(_mm_and_ps(mask, yes), _mm_andnot_ps(mask, no));
}
bool SafeComponent(std::uint32_t bits) noexcept {
    const auto magnitude = bits & 0x7FFFFFFFU;
    if (magnitude == 0U) return true; // Keep either signed zero.
    const auto exponent = magnitude >> 23U;
    // 2^-62 <= abs(component) < 2^62. Every square, sum, reciprocal and
    // nonzero normalized result stays normal/finite in the active branch.
    return exponent >= 65U && exponent <= 188U;
}
bool Identity(const std::array<std::uint32_t, 4U>& bits) noexcept {
    return (bits[0] & 0x7FFFFFFFU) == 0U && (bits[1] & 0x7FFFFFFFU) == 0U &&
        (bits[2] & 0x7FFFFFFFU) == 0U && bits[3] == 0x3F800000U;
}
#endif
} // namespace

bool TryNormalizeRoot4Exact(const std::array<Quat, 4U>& input, std::array<Quat, 4U>& output) noexcept {
#if defined(_MSC_VER) && defined(_M_X64)
    // Exception masks + round-nearest + DAZ-off + FTZ-off. Preserve all six
    // existing sticky flags; do not alter the caller/worker's MXCSR at all.
    if ((_mm_getcsr() & 0x0000FFC0U) != 0x00001F80U) return false;
    std::array<std::array<std::uint32_t, 4U>, 4U> bits{};
    std::array<int, 4U> identityMask{};
    for (std::size_t lane = 0U; lane < 4U; ++lane) {
        bits[lane] = std::bit_cast<std::array<std::uint32_t, 4U>>(input[lane]);
        for (const auto component : bits[lane]) if (!SafeComponent(component)) return false;
        identityMask[lane] = Identity(bits[lane]) ? -1 : 0;
    }

    // memcpy states legal byte copies of complete Quat objects, avoiding a
    // float-array type pun and avoiding aligned loads or quaternion overreads.
    __m128 x{}, y{}, z{}, w{};
    std::memcpy(&x, &input[0], sizeof(Quat));
    std::memcpy(&y, &input[1], sizeof(Quat));
    std::memcpy(&z, &input[2], sizeof(Quat));
    std::memcpy(&w, &input[3], sizeof(Quat));
    _MM_TRANSPOSE4_PS(x, y, z, w);

    const auto xx = _mm_mul_ps(x, x);
    const auto yy = _mm_mul_ps(y, y);
    const auto zz = _mm_mul_ps(z, z);
    const auto ww = _mm_mul_ps(w, w);
    const auto sumXY = _mm_add_ps(xx, yy);
    const auto sumXYZ = _mm_add_ps(sumXY, zz);
    const auto lengthSquared = _mm_add_ps(sumXYZ, ww);
    const auto one = _mm_set1_ps(1.0F);
    const auto zero = _mm_setzero_ps();
    const auto active = _mm_cmpgt_ps(lengthSquared, _mm_set1_ps(0.000001F));
    // Epsilon/zero lanes must not execute divide-by-zero or tiny-input output
    // multiplies which scalar code skips. Neutral operations are exact.
    const auto safeLengthSquared = Select(active, lengthSquared, one);
    const auto inverse = _mm_div_ps(one, _mm_sqrt_ps(safeLengthSquared));
    auto normalizedX = _mm_mul_ps(Select(active, x, zero), inverse);
    auto normalizedY = _mm_mul_ps(Select(active, y, zero), inverse);
    auto normalizedZ = _mm_mul_ps(Select(active, z, zero), inverse);
    auto normalizedW = Select(active, _mm_mul_ps(Select(active, w, zero), inverse), one);
    const auto identity = _mm_castsi128_ps(_mm_set_epi32(identityMask[3], identityMask[2], identityMask[1], identityMask[0]));
    normalizedX = Select(identity, x, normalizedX);
    normalizedY = Select(identity, y, normalizedY);
    normalizedZ = Select(identity, z, normalizedZ);
    normalizedW = Select(identity, w, normalizedW);
    _MM_TRANSPOSE4_PS(normalizedX, normalizedY, normalizedZ, normalizedW);
    // Every input was loaded before any output write, including in-place use.
    std::memcpy(&output[0], &normalizedX, sizeof(Quat));
    std::memcpy(&output[1], &normalizedY, sizeof(Quat));
    std::memcpy(&output[2], &normalizedZ, sizeof(Quat));
    std::memcpy(&output[3], &normalizedW, sizeof(Quat));
    return true;
#else
    static_cast<void>(input);
    static_cast<void>(output);
    return false; // Existing scalar engine path on every other target.
#endif
}

bool TryNormalizeRoot4Exact(const std::array<kb::scene::LocalTransform, 4U>& input, std::array<Quat, 4U>& output) noexcept {
#if defined(_MSC_VER) && defined(_M_X64)
    // Exception masks + round-nearest + DAZ-off + FTZ-off. Preserve all six
    // existing sticky flags; do not alter the caller/worker's MXCSR at all.
    if ((_mm_getcsr() & 0x0000FFC0U) != 0x00001F80U) return false;
    std::array<std::array<std::uint32_t, 4U>, 4U> bits{};
    std::array<int, 4U> identityMask{};
    for (std::size_t lane = 0U; lane < 4U; ++lane) {
        bits[lane] = std::bit_cast<std::array<std::uint32_t, 4U>>(input[lane].rotation);
        for (const auto component : bits[lane]) if (!SafeComponent(component)) return false;
        identityMask[lane] = Identity(bits[lane]) ? -1 : 0;
    }

    // memcpy states legal byte copies of complete Quat objects, avoiding a
    // float-array type pun and avoiding aligned loads or quaternion overreads.
    __m128 x{}, y{}, z{}, w{};
    std::memcpy(&x, &input[0].rotation, sizeof(Quat));
    std::memcpy(&y, &input[1].rotation, sizeof(Quat));
    std::memcpy(&z, &input[2].rotation, sizeof(Quat));
    std::memcpy(&w, &input[3].rotation, sizeof(Quat));
    _MM_TRANSPOSE4_PS(x, y, z, w);

    const auto xx = _mm_mul_ps(x, x);
    const auto yy = _mm_mul_ps(y, y);
    const auto zz = _mm_mul_ps(z, z);
    const auto ww = _mm_mul_ps(w, w);
    const auto sumXY = _mm_add_ps(xx, yy);
    const auto sumXYZ = _mm_add_ps(sumXY, zz);
    const auto lengthSquared = _mm_add_ps(sumXYZ, ww);
    const auto one = _mm_set1_ps(1.0F);
    const auto zero = _mm_setzero_ps();
    const auto active = _mm_cmpgt_ps(lengthSquared, _mm_set1_ps(0.000001F));
    // Epsilon/zero lanes must not execute divide-by-zero or tiny-input output
    // multiplies which scalar code skips. Neutral operations are exact.
    const auto safeLengthSquared = Select(active, lengthSquared, one);
    const auto inverse = _mm_div_ps(one, _mm_sqrt_ps(safeLengthSquared));
    auto normalizedX = _mm_mul_ps(Select(active, x, zero), inverse);
    auto normalizedY = _mm_mul_ps(Select(active, y, zero), inverse);
    auto normalizedZ = _mm_mul_ps(Select(active, z, zero), inverse);
    auto normalizedW = Select(active, _mm_mul_ps(Select(active, w, zero), inverse), one);
    const auto identity = _mm_castsi128_ps(_mm_set_epi32(identityMask[3], identityMask[2], identityMask[1], identityMask[0]));
    normalizedX = Select(identity, x, normalizedX);
    normalizedY = Select(identity, y, normalizedY);
    normalizedZ = Select(identity, z, normalizedZ);
    normalizedW = Select(identity, w, normalizedW);
    _MM_TRANSPOSE4_PS(normalizedX, normalizedY, normalizedZ, normalizedW);
    // Every rotation was loaded before any normalized-output write.
    std::memcpy(&output[0], &normalizedX, sizeof(Quat));
    std::memcpy(&output[1], &normalizedY, sizeof(Quat));
    std::memcpy(&output[2], &normalizedZ, sizeof(Quat));
    std::memcpy(&output[3], &normalizedW, sizeof(Quat));
    return true;
#else
    static_cast<void>(input);
    static_cast<void>(output);
    return false; // Existing scalar engine path on every other target.
#endif
}
} // namespace exact_local_batch

#if defined(_MSC_VER) && defined(_M_X64)
#pragma fenv_access(off)
#pragma float_control(pop)
#endif
