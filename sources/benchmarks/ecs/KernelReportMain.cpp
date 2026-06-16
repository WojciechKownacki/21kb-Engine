#include "KernelReportKernels.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifndef KB_ECS_KERNEL_REPORT_GIT_COMMIT
#define KB_ECS_KERNEL_REPORT_GIT_COMMIT "unknown"
#endif

#ifndef KB_ECS_KERNEL_REPORT_GIT_BRANCH
#define KB_ECS_KERNEL_REPORT_GIT_BRANCH "unknown"
#endif

#ifndef KB_ECS_KERNEL_REPORT_BUILD_CONFIG
#define KB_ECS_KERNEL_REPORT_BUILD_CONFIG "unknown"
#endif

#ifndef KB_ECS_KERNEL_REPORT_COMPILER_ID
#define KB_ECS_KERNEL_REPORT_COMPILER_ID "unknown"
#endif

#ifndef KB_ECS_KERNEL_REPORT_COMPILER_VERSION
#define KB_ECS_KERNEL_REPORT_COMPILER_VERSION "unknown"
#endif

namespace {

using kb::ecs::bench::KernelReportLocalBounds;
using kb::ecs::bench::KernelReportLocalTransform;
using kb::ecs::bench::KernelReportPhysicsBody;
using kb::ecs::bench::KernelReportPhysicsProxy;
using kb::ecs::bench::KernelReportPosition;
using kb::ecs::bench::KernelReportVelocity;
using kb::ecs::bench::KernelReportWorldBounds;
using kb::ecs::bench::KernelReportWorldTransform;

struct KernelReportOptions {
    std::filesystem::path outputPath = "ecs_kernel_report.json";
    std::filesystem::path assemblyDir;
    std::size_t elements = 65'536;
    std::size_t iterations = 16;
    std::size_t samples = 8;
    std::size_t warmupSamples = 2;
};

struct AssemblyArtifact {
    std::filesystem::path path;
    std::uintmax_t bytes = 0;
};

struct KernelPerfStats {
    double minMs = 0.0;
    double avgMs = 0.0;
    double p95Ms = 0.0;
};

struct KernelReportEntry {
    std::string kernel;
    std::string backend;
    std::string symbol;
    KernelPerfStats time;
    double throughputElementsPerSecond = 0.0;
    double checksum = 0.0;
};

struct MovementState {
    std::vector<KernelReportPosition> positions;
};

struct TransformState {
    std::vector<KernelReportWorldTransform> worldTransforms;
};

struct BoundsState {
    std::vector<KernelReportWorldBounds> worldBounds;
};

struct PhysicsState {
    std::vector<KernelReportPhysicsBody> bodies;
    std::vector<KernelReportPhysicsProxy> proxies;
};

using MovementFunction = float (*)(KernelReportPosition*, const KernelReportVelocity*, std::size_t, float) noexcept;
using TransformFunction = float (*)(const KernelReportLocalTransform*, KernelReportWorldTransform*, std::size_t) noexcept;
using BoundsFunction = float (*)(const KernelReportLocalBounds*, const KernelReportLocalTransform*, KernelReportWorldBounds*, std::size_t) noexcept;
using PhysicsFunction = float (*)(KernelReportPhysicsBody*, KernelReportPhysicsProxy*, std::size_t, float, float) noexcept;

[[nodiscard]] std::string JsonEscape(std::string_view value) {
    std::string output;
    output.reserve(value.size() + 8U);
    for (const char character : value) {
        switch (character) {
        case '"':
            output += "\\\"";
            break;
        case '\\':
            output += "\\\\";
            break;
        case '\b':
            output += "\\b";
            break;
        case '\f':
            output += "\\f";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U) {
                output += '?';
            } else {
                output += character;
            }
            break;
        }
    }
    return output;
}

[[nodiscard]] std::size_t ParseSize(std::string_view value, std::string_view optionName) {
    std::uint64_t parsed = 0;
    const char* begin = value.data();
    const char* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (value.empty() || result.ec != std::errc{} || result.ptr != end || parsed == 0U || parsed > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument(std::string{ optionName } + " expects a positive integer");
    }
    return static_cast<std::size_t>(parsed);
}

[[nodiscard]] KernelReportOptions ParseOptions(int argc, char** argv) {
    KernelReportOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{ argv[index] };
        const auto readValue = [&](std::string_view optionName) -> std::string_view {
            if (index + 1 >= argc) {
                throw std::invalid_argument(std::string{ optionName } + " expects a value");
            }
            ++index;
            return std::string_view{ argv[index] };
        };

        if (argument == "--output") {
            options.outputPath = std::filesystem::path{ std::string{ readValue(argument) } };
        } else if (argument == "--assembly-dir") {
            options.assemblyDir = std::filesystem::path{ std::string{ readValue(argument) } };
        } else if (argument == "--elements") {
            options.elements = ParseSize(readValue(argument), argument);
        } else if (argument == "--iterations") {
            options.iterations = ParseSize(readValue(argument), argument);
        } else if (argument == "--samples") {
            options.samples = ParseSize(readValue(argument), argument);
        } else if (argument == "--warmup") {
            options.warmupSamples = ParseSize(readValue(argument), argument);
        } else if (argument == "--help") {
            std::cout << "Usage: kb_ecs_kernel_report [--output path] [--assembly-dir path] [--elements N] [--iterations N] [--samples N] [--warmup N]\n";
            std::exit(0);
        } else {
            throw std::invalid_argument("Unknown argument: " + std::string{ argument });
        }
    }
    return options;
}

[[nodiscard]] KernelPerfStats ComputeStats(std::vector<double> sampleTimesMs) {
    if (sampleTimesMs.empty()) {
        throw std::runtime_error("Kernel report has no timing samples");
    }

    std::sort(sampleTimesMs.begin(), sampleTimesMs.end());
    const double total = std::accumulate(sampleTimesMs.begin(), sampleTimesMs.end(), 0.0);
    const std::size_t p95Index = (sampleTimesMs.size() * 95U + 99U) / 100U - 1U;
    return KernelPerfStats{
        .minMs = sampleTimesMs.front(),
        .avgMs = total / static_cast<double>(sampleTimesMs.size()),
        .p95Ms = sampleTimesMs[std::min(p95Index, sampleTimesMs.size() - 1U)],
    };
}

template <typename StateFactory, typename Invocation>
[[nodiscard]] KernelReportEntry MeasureKernel(
    std::string kernel,
    std::string backend,
    std::string symbol,
    const KernelReportOptions& options,
    StateFactory&& makeState,
    Invocation&& invoke) {
    using Clock = std::chrono::steady_clock;

    double warmupChecksum = 0.0;
    for (std::size_t sample = 0; sample < options.warmupSamples; ++sample) {
        auto state = makeState();
        for (std::size_t iteration = 0; iteration < options.iterations; ++iteration) {
            warmupChecksum += static_cast<double>(invoke(state));
        }
    }

    std::vector<double> sampleTimesMs;
    sampleTimesMs.reserve(options.samples);
    double checksum = 0.0;
    for (std::size_t sample = 0; sample < options.samples; ++sample) {
        auto state = makeState();
        const auto start = Clock::now();
        for (std::size_t iteration = 0; iteration < options.iterations; ++iteration) {
            checksum += static_cast<double>(invoke(state));
        }
        const auto end = Clock::now();
        const std::chrono::duration<double, std::milli> elapsed = end - start;
        sampleTimesMs.push_back(elapsed.count());
    }

    if (!std::isfinite(warmupChecksum) || !std::isfinite(checksum) || checksum == 0.0) {
        throw std::runtime_error("Kernel report produced an invalid checksum for " + kernel + "/" + backend);
    }

    const KernelPerfStats stats = ComputeStats(std::move(sampleTimesMs));
    const double secondsPerSample = stats.avgMs / 1000.0;
    const double processedElements = static_cast<double>(options.elements) * static_cast<double>(options.iterations);
    return KernelReportEntry{
        .kernel = std::move(kernel),
        .backend = std::move(backend),
        .symbol = std::move(symbol),
        .time = stats,
        .throughputElementsPerSecond = secondsPerSample == 0.0 ? 0.0 : processedElements / secondsPerSample,
        .checksum = checksum,
    };
}

[[nodiscard]] std::vector<KernelReportPosition> CreatePositions(std::size_t count) {
    std::vector<KernelReportPosition> positions(count);
    for (std::size_t index = 0; index < count; ++index) {
        const float value = static_cast<float>(index % 997U);
        positions[index] = KernelReportPosition{
            .x = value * 0.03125F,
            .y = value * 0.015625F + 1.0F,
            .z = value * -0.0078125F,
        };
    }
    return positions;
}

[[nodiscard]] std::vector<KernelReportVelocity> CreateVelocities(std::size_t count) {
    std::vector<KernelReportVelocity> velocities(count);
    for (std::size_t index = 0; index < count; ++index) {
        const float value = static_cast<float>((index * 17U) % 251U);
        velocities[index] = KernelReportVelocity{
            .x = value * 0.00390625F - 0.25F,
            .y = value * 0.001953125F + 0.125F,
            .z = value * -0.0029296875F,
        };
    }
    return velocities;
}

[[nodiscard]] std::vector<KernelReportLocalTransform> CreateTransforms(std::size_t count) {
    std::vector<KernelReportLocalTransform> transforms(count);
    for (std::size_t index = 0; index < count; ++index) {
        const float value = static_cast<float>(index % 1024U);
        transforms[index] = KernelReportLocalTransform{
            .translationX = value * 0.125F,
            .translationY = value * -0.0625F,
            .translationZ = value * 0.03125F,
            .rotationZ = static_cast<float>(index % 360U) * 0.017453292519943295F,
            .scaleX = 0.75F + static_cast<float>(index % 5U) * 0.125F,
            .scaleY = 0.875F + static_cast<float>(index % 7U) * 0.0625F,
            .scaleZ = 1.0F + static_cast<float>(index % 3U) * 0.25F,
        };
    }
    return transforms;
}

[[nodiscard]] std::vector<KernelReportLocalBounds> CreateBounds(std::size_t count) {
    std::vector<KernelReportLocalBounds> bounds(count);
    for (std::size_t index = 0; index < count; ++index) {
        const float value = static_cast<float>((index * 13U) % 511U);
        bounds[index] = KernelReportLocalBounds{
            .centerX = value * 0.0078125F,
            .centerY = value * -0.00390625F,
            .centerZ = value * 0.001953125F,
            .extentX = 0.25F + static_cast<float>(index % 11U) * 0.03125F,
            .extentY = 0.5F + static_cast<float>(index % 13U) * 0.015625F,
            .extentZ = 0.75F + static_cast<float>(index % 17U) * 0.0078125F,
        };
    }
    return bounds;
}

[[nodiscard]] std::vector<KernelReportPhysicsBody> CreatePhysicsBodies(std::size_t count) {
    std::vector<KernelReportPhysicsBody> bodies(count);
    for (std::size_t index = 0; index < count; ++index) {
        const float value = static_cast<float>((index * 29U) % 4093U);
        bodies[index] = KernelReportPhysicsBody{
            .positionX = value * 0.015625F,
            .positionY = value * 0.0078125F + 2.0F,
            .positionZ = value * -0.00390625F,
            .velocityX = value * 0.0009765625F - 1.0F,
            .velocityY = value * 0.00048828125F,
            .velocityZ = value * -0.000244140625F + 0.25F,
            .radius = 0.25F + static_cast<float>(index % 9U) * 0.03125F,
            .inverseMass = 0.5F + static_cast<float>(index % 4U) * 0.25F,
        };
    }
    return bodies;
}

[[nodiscard]] KernelReportEntry RunMovementReport(
    MovementFunction function,
    std::string backend,
    std::string symbol,
    const KernelReportOptions& options,
    const std::vector<KernelReportPosition>& positions,
    const std::vector<KernelReportVelocity>& velocities) {
    constexpr float kDeltaSeconds = 1.0F / 60.0F;
    return MeasureKernel(
        "movement",
        std::move(backend),
        std::move(symbol),
        options,
        [&positions]() {
            return MovementState{ .positions = positions };
        },
        [&](MovementState& state) {
            return function(state.positions.data(), velocities.data(), state.positions.size(), kDeltaSeconds);
        });
}

[[nodiscard]] KernelReportEntry RunTransformReport(
    TransformFunction function,
    std::string backend,
    std::string symbol,
    const KernelReportOptions& options,
    const std::vector<KernelReportLocalTransform>& transforms) {
    return MeasureKernel(
        "transform_local_to_world",
        std::move(backend),
        std::move(symbol),
        options,
        [&options]() {
            return TransformState{ .worldTransforms = std::vector<KernelReportWorldTransform>(options.elements) };
        },
        [&](TransformState& state) {
            return function(transforms.data(), state.worldTransforms.data(), transforms.size());
        });
}

[[nodiscard]] KernelReportEntry RunBoundsReport(
    BoundsFunction function,
    std::string backend,
    std::string symbol,
    const KernelReportOptions& options,
    const std::vector<KernelReportLocalBounds>& bounds,
    const std::vector<KernelReportLocalTransform>& transforms) {
    return MeasureKernel(
        "bounds_world_aabb",
        std::move(backend),
        std::move(symbol),
        options,
        [&options]() {
            return BoundsState{ .worldBounds = std::vector<KernelReportWorldBounds>(options.elements) };
        },
        [&](BoundsState& state) {
            return function(bounds.data(), transforms.data(), state.worldBounds.data(), bounds.size());
        });
}

[[nodiscard]] KernelReportEntry RunPhysicsReport(
    PhysicsFunction function,
    std::string backend,
    std::string symbol,
    const KernelReportOptions& options,
    const std::vector<KernelReportPhysicsBody>& bodies) {
    constexpr float kDeltaSeconds = 1.0F / 60.0F;
    constexpr float kGravityY = -9.80665F;
    return MeasureKernel(
        "physics_proxy",
        std::move(backend),
        std::move(symbol),
        options,
        [&bodies, &options]() {
            return PhysicsState{
                .bodies = bodies,
                .proxies = std::vector<KernelReportPhysicsProxy>(options.elements),
            };
        },
        [&](PhysicsState& state) {
            return function(state.bodies.data(), state.proxies.data(), state.bodies.size(), kDeltaSeconds, kGravityY);
        });
}

[[nodiscard]] std::vector<KernelReportEntry> RunKernelReports(const KernelReportOptions& options) {
    const std::vector<KernelReportPosition> positions = CreatePositions(options.elements);
    const std::vector<KernelReportVelocity> velocities = CreateVelocities(options.elements);
    const std::vector<KernelReportLocalTransform> transforms = CreateTransforms(options.elements);
    const std::vector<KernelReportLocalBounds> bounds = CreateBounds(options.elements);
    const std::vector<KernelReportPhysicsBody> bodies = CreatePhysicsBodies(options.elements);

    std::vector<KernelReportEntry> entries;
    entries.reserve(16U);
    entries.push_back(RunMovementReport(kb::ecs::bench::KbEcsKernelReportMovementScalar, "scalar", "KbEcsKernelReportMovementScalar", options, positions, velocities));
    entries.push_back(RunMovementReport(kb::ecs::bench::KbEcsKernelReportMovementSse2, "sse2", "KbEcsKernelReportMovementSse2", options, positions, velocities));
    entries.push_back(RunMovementReport(kb::ecs::bench::KbEcsKernelReportMovementAvx2, "avx2", "KbEcsKernelReportMovementAvx2", options, positions, velocities));
    entries.push_back(RunMovementReport(kb::ecs::bench::KbEcsKernelReportMovementAvx512, "avx512", "KbEcsKernelReportMovementAvx512", options, positions, velocities));

    entries.push_back(RunTransformReport(kb::ecs::bench::KbEcsKernelReportTransformScalar, "scalar", "KbEcsKernelReportTransformScalar", options, transforms));
    entries.push_back(RunTransformReport(kb::ecs::bench::KbEcsKernelReportTransformSse2, "sse2", "KbEcsKernelReportTransformSse2", options, transforms));
    entries.push_back(RunTransformReport(kb::ecs::bench::KbEcsKernelReportTransformAvx2, "avx2", "KbEcsKernelReportTransformAvx2", options, transforms));
    entries.push_back(RunTransformReport(kb::ecs::bench::KbEcsKernelReportTransformAvx512, "avx512", "KbEcsKernelReportTransformAvx512", options, transforms));

    entries.push_back(RunBoundsReport(kb::ecs::bench::KbEcsKernelReportBoundsScalar, "scalar", "KbEcsKernelReportBoundsScalar", options, bounds, transforms));
    entries.push_back(RunBoundsReport(kb::ecs::bench::KbEcsKernelReportBoundsSse2, "sse2", "KbEcsKernelReportBoundsSse2", options, bounds, transforms));
    entries.push_back(RunBoundsReport(kb::ecs::bench::KbEcsKernelReportBoundsAvx2, "avx2", "KbEcsKernelReportBoundsAvx2", options, bounds, transforms));
    entries.push_back(RunBoundsReport(kb::ecs::bench::KbEcsKernelReportBoundsAvx512, "avx512", "KbEcsKernelReportBoundsAvx512", options, bounds, transforms));

    entries.push_back(RunPhysicsReport(kb::ecs::bench::KbEcsKernelReportPhysicsProxyScalar, "scalar", "KbEcsKernelReportPhysicsProxyScalar", options, bodies));
    entries.push_back(RunPhysicsReport(kb::ecs::bench::KbEcsKernelReportPhysicsProxySse2, "sse2", "KbEcsKernelReportPhysicsProxySse2", options, bodies));
    entries.push_back(RunPhysicsReport(kb::ecs::bench::KbEcsKernelReportPhysicsProxyAvx2, "avx2", "KbEcsKernelReportPhysicsProxyAvx2", options, bodies));
    entries.push_back(RunPhysicsReport(kb::ecs::bench::KbEcsKernelReportPhysicsProxyAvx512, "avx512", "KbEcsKernelReportPhysicsProxyAvx512", options, bodies));
    return entries;
}

[[nodiscard]] AssemblyArtifact FindAssemblyArtifact(const std::filesystem::path& assemblyDir) {
    if (assemblyDir.empty()) {
        return AssemblyArtifact{};
    }
    if (!std::filesystem::is_directory(assemblyDir)) {
        throw std::runtime_error("Kernel report assembly directory does not exist: " + assemblyDir.string());
    }

    const std::filesystem::path asmPath = assemblyDir / "KernelReportKernels.asm";
    const std::filesystem::path sPath = assemblyDir / "KernelReportKernels.s";
    for (const std::filesystem::path& path : { asmPath, sPath }) {
        if (!std::filesystem::is_regular_file(path)) {
            continue;
        }
        const std::uintmax_t size = std::filesystem::file_size(path);
        if (size == 0U) {
            throw std::runtime_error("Kernel report assembly file is empty: " + path.string());
        }
        return AssemblyArtifact{
            .path = path,
            .bytes = size,
        };
    }

    throw std::runtime_error("Kernel report assembly file was not generated in: " + assemblyDir.string());
}

void WriteReportJson(
    const std::filesystem::path& outputPath,
    const KernelReportOptions& options,
    const AssemblyArtifact& assembly,
    const std::vector<KernelReportEntry>& entries) {
    const std::filesystem::path parent = outputPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    std::ofstream output{ outputPath, std::ios::trunc };
    if (!output) {
        throw std::runtime_error("Failed to open ECS kernel report output JSON: " + outputPath.string());
    }

    output << std::fixed << std::setprecision(6);
    output << "{\n";
    output << "  \"commit\": \"" << JsonEscape(KB_ECS_KERNEL_REPORT_GIT_COMMIT) << "\",\n";
    output << "  \"branch\": \"" << JsonEscape(KB_ECS_KERNEL_REPORT_GIT_BRANCH) << "\",\n";
    output << "  \"build_config\": \"" << JsonEscape(KB_ECS_KERNEL_REPORT_BUILD_CONFIG) << "\",\n";
    output << "  \"compiler_id\": \"" << JsonEscape(KB_ECS_KERNEL_REPORT_COMPILER_ID) << "\",\n";
    output << "  \"compiler_version\": \"" << JsonEscape(KB_ECS_KERNEL_REPORT_COMPILER_VERSION) << "\",\n";
    output << "  \"elements\": " << options.elements << ",\n";
    output << "  \"iterations\": " << options.iterations << ",\n";
    output << "  \"samples\": " << options.samples << ",\n";
    output << "  \"warmup_samples\": " << options.warmupSamples << ",\n";
    output << "  \"assembly\": {\n";
    output << "    \"available\": " << (assembly.path.empty() ? "false" : "true") << ",\n";
    output << "    \"path\": \"" << JsonEscape(assembly.path.generic_string()) << "\",\n";
    output << "    \"bytes\": " << assembly.bytes << "\n";
    output << "  },\n";
    output << "  \"kernels\": [\n";
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const KernelReportEntry& entry = entries[index];
        output << "    {\n";
        output << "      \"kernel\": \"" << JsonEscape(entry.kernel) << "\",\n";
        output << "      \"backend\": \"" << JsonEscape(entry.backend) << "\",\n";
        output << "      \"symbol\": \"" << JsonEscape(entry.symbol) << "\",\n";
        output << "      \"time_ms_min\": " << entry.time.minMs << ",\n";
        output << "      \"time_ms_avg\": " << entry.time.avgMs << ",\n";
        output << "      \"time_ms_p95\": " << entry.time.p95Ms << ",\n";
        output << "      \"throughput_elements_per_second\": " << entry.throughputElementsPerSecond << ",\n";
        output << "      \"checksum\": " << entry.checksum << "\n";
        output << "    }" << (index + 1U == entries.size() ? "\n" : ",\n");
    }
    output << "  ]\n";
    output << "}\n";
}

void PrintReportSummary(const std::filesystem::path& outputPath, const std::vector<KernelReportEntry>& entries) {
    for (const KernelReportEntry& entry : entries) {
        std::cout << entry.kernel << "/" << entry.backend << ": avg " << entry.time.avgMs << " ms, p95 " << entry.time.p95Ms
                  << " ms, throughput " << entry.throughputElementsPerSecond << " elements/s\n";
    }
    std::cout << "Wrote " << outputPath.string() << '\n';
}

} // namespace

int main(int argc, char** argv) {
    try {
        const KernelReportOptions options = ParseOptions(argc, argv);
        const AssemblyArtifact assembly = FindAssemblyArtifact(options.assemblyDir);
        const std::vector<KernelReportEntry> entries = RunKernelReports(options);
        WriteReportJson(options.outputPath, options, assembly, entries);
        PrintReportSummary(options.outputPath, entries);
    } catch (const std::exception& exception) {
        std::cerr << "kb_ecs_kernel_report: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
