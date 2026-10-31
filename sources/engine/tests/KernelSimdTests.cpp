#include "engine/ecs/HotKernel.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#if defined(_MSC_VER)
#define KB_TEST_NOINLINE __declspec(noinline)
#else
#define KB_TEST_NOINLINE __attribute__((noinline))
#endif

// Keep one production dispatch call identifiable in native instruction reports.
extern "C" KB_TEST_NOINLINE void KbTestApplyVelocity(float* x, float* y, const float* vx,
    const float* vy, std::size_t count, float scale, kb::ecs::KernelBackend backend) {
    kb::ecs::ApplyDenseFloat2VelocityColumns(x, y, vx, vy, count, scale, backend);
}

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

bool Same(float expected, float actual) {
    if (std::isnan(expected)) return std::isnan(actual);
    if (expected == actual) return true;
    if (!std::isfinite(expected) || !std::isfinite(actual)) return false;
    return std::fabs(expected - actual) <= 4.0F * std::numeric_limits<float>::epsilon() *
        std::max({1.0F, std::fabs(expected), std::fabs(actual)});
}

std::size_t Check(kb::ecs::KernelBackend backend) {
    constexpr std::array values{0.0F, -0.0F, 1.0F, -1.0F, 0.1F, -0.25F,
        std::numeric_limits<float>::min(), std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max(), std::numeric_limits<float>::denorm_min(),
        std::numeric_limits<float>::epsilon(), std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(),
        1.00000011920928955078125F, -1.0000002384185791015625F};
    constexpr std::array scales{0.0F, -0.0F, 1.0F, -0.25F, 0.1F, 1.00000011920928955078125F};
    std::size_t cases = 0U;
    for (const float scale : scales) for (std::size_t count = 0U; count <= 33U; ++count) {
        for (std::size_t offset = 0U; offset < 8U; ++offset) {
            alignas(64) std::array<float, 80U> x{}, y{}, vx{}, vy{};
            x.fill(123.0F); y.fill(-123.0F);
            const std::size_t begin = 8U + offset;
            for (std::size_t index = 0U; index < count; ++index) {
                x[begin + index] = values[index % values.size()];
                y[begin + index] = values[(index + 3U) % values.size()];
                vx[begin + index] = values[(index + 7U) % values.size()];
                vy[begin + index] = values[(index + 11U) % values.size()];
            }
            const auto originalVx = vx, originalVy = vy;
            auto expectedX = x, expectedY = y;
            for (std::size_t index = begin; index < begin + count; ++index) {
                // Round the product independently of the add, including on FMA-capable CPUs.
                volatile float productX = vx[index] * scale;
                volatile float productY = vy[index] * scale;
                expectedX[index] += productX;
                expectedY[index] += productY;
            }
            KbTestApplyVelocity(x.data() + begin, y.data() + begin, vx.data() + begin,
                vy.data() + begin, count, scale, backend);
            for (std::size_t index = 0U; index < x.size(); ++index) {
                const bool guard = index < begin || index >= begin + count;
                const bool equal = guard ?
                    std::bit_cast<std::uint32_t>(expectedX[index]) == std::bit_cast<std::uint32_t>(x[index]) &&
                        std::bit_cast<std::uint32_t>(expectedY[index]) == std::bit_cast<std::uint32_t>(y[index]) :
                    Same(expectedX[index], x[index]) && Same(expectedY[index], y[index]);
                if (!equal) {
                    std::fprintf(stderr, "backend=%s count=%zu offset=%zu index=%zu scale=%a\n",
                        kb::ecs::KernelBackendName(backend).data(), count, offset, index, static_cast<double>(scale));
                    Require(false, "Velocity kernel exceeds arithmetic tolerance or overwrites a guard");
                }
            }
            Require(std::memcmp(vx.data(), originalVx.data(), sizeof(vx)) == 0 &&
                std::memcmp(vy.data(), originalVy.data(), sizeof(vy)) == 0, "Velocity kernel modifies its inputs");
            ++cases;
        }
    }
    return cases;
}

} // namespace

int main() {
    constexpr std::array backends{kb::ecs::KernelBackend::Scalar, kb::ecs::KernelBackend::Sse2,
        kb::ecs::KernelBackend::Avx2, kb::ecs::KernelBackend::Avx512, kb::ecs::KernelBackend::Neon};
    std::printf("{\"preferred\":\"%s\",\"operation\":\"dense-float2-velocity\",\"finiteTolerance\":\"4*epsilon*max(1,abs(expected),abs(actual))\",\"backends\":[",
        kb::ecs::KernelBackendName(kb::ecs::PreferredKernelBackend()).data());
    for (std::size_t index = 0U; index < backends.size(); ++index) {
        const auto report = kb::ecs::MakeKernelBackendReport(backends[index]);
        const auto cases = Check(backends[index]);
        std::printf("%s{\"requested\":\"%s\",\"hardware\":%s,\"compiled\":%s,\"auto\":%s,\"scalarFallback\":%s,\"cases\":%zu}",
            index == 0U ? "" : ",", report.name.data(), report.hardwareSupported ? "true" : "false",
            report.nativelyCompiled ? "true" : "false", report.autoSelectable ? "true" : "false",
            report.hardwareSupported ? "false" : "true", cases);
    }
    std::puts("],\"result\":\"PASS\"}");
}
