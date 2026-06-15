#include "engine/ecs/QueryBatch.hpp"
#include "engine/ecs/SystemScheduler.hpp"
#include "engine/ecs/World.hpp"
#include "engine/ecs/WorldConfigPresets.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabs.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
#include <cpuid.h>
#endif

namespace {

struct BenchPosition {
    float x = 0.0F;
    float y = 0.0F;
};

struct BenchVelocity {
    float x = 0.0F;
    float y = 0.0F;
};

struct BenchStructuralMarker {
    std::uint32_t value = 0;
    std::uint32_t frame = 0;
};

struct BenchLocalTransform {
    float translationX = 0.0F;
    float translationY = 0.0F;
    float translationZ = 0.0F;
    float rotationZ = 0.0F;
    float scaleX = 1.0F;
    float scaleY = 1.0F;
    float scaleZ = 1.0F;
};

struct BenchWorldTransform {
    float matrix[16]{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
};

inline constexpr std::uint32_t kInvalidHierarchyParent = std::numeric_limits<std::uint32_t>::max();

struct BenchHierarchyNode {
    std::uint32_t parentIndex = kInvalidHierarchyParent;
    std::uint32_t localVersion = 0;
    std::uint32_t appliedLocalVersion = 0;
    std::uint32_t observedParentWorldVersion = 0;
    std::uint32_t worldVersion = 0;
};

template <std::size_t Index>
struct BenchFanoutComponent {
    float value = 0.0F;
};

template <std::size_t Index>
struct BenchFanoutColdComponent {
    std::uint32_t value = 0;
};

struct BenchSparseMatch {
    float value = 0.0F;
};

struct BenchSparsePayload {
    float value = 0.0F;
};

template <std::size_t Index>
struct BenchSparseArchetypeComponent {
    std::uint32_t value = 0;
};

struct BenchSystemChainState {
    static constexpr std::size_t kMaxSlots = 256;

    float values[kMaxSlots]{};
    std::uint32_t writes[kMaxSlots]{};
};

enum class BenchmarkValidationSelection {
    Off,
    Debug,
    Both,
};

struct BenchmarkOptions {
    std::size_t entityCount = 1'000'000;
    std::size_t hierarchyEntityCount = 250'000;
    std::size_t structuralChangesPerFrame = 100'000;
    std::size_t sparseArchetypeCount = 512;
    std::size_t sparseMatchingArchetypeCount = 8;
    std::size_t sparseEntitiesPerArchetype = 16;
    std::vector<std::size_t> systemChainCounts{ 16, 64, 256 };
    std::vector<std::size_t> prefabSpawnInstanceCounts{ 10'000, 50'000, 100'000 };
    std::vector<std::size_t> prefabHierarchyNodeCounts{ 1, 4, 16 };
    std::size_t frames = 64;
    std::size_t warmupFrames = 4;
    std::size_t executionGrainSize = kb::ecs::kDefaultQueryExecutionGrainSize;
    float deltaSeconds = 1.0F / 60.0F;
    std::filesystem::path outputPath = "ecs_benchmark_results.json";
    std::filesystem::path saveBaselinePath;
    std::filesystem::path compareBaselinePath;
    std::filesystem::path compareBeforePath;
    std::filesystem::path compareAfterPath;
    std::filesystem::path comparisonOutputPath = "ecs_benchmark_comparison.json";
    double regressionThresholdPercent = 0.0;
    bool failOnRegression = false;
    BenchmarkValidationSelection validationSelection = BenchmarkValidationSelection::Off;
    bool debugValidationEnabled = false;
};

struct FrameStats {
    double minMs = 0.0;
    double avgMs = 0.0;
    double p95Ms = 0.0;
};

struct BenchmarkResult {
    std::string name;
    std::string dataset;
    std::string validationMode;
    std::size_t entities = 0;
    std::size_t frames = 0;
    std::size_t warmupFrames = 0;
    std::size_t executionGrainSize = 0;
    FrameStats frameTime;
    double throughputEntitiesPerSecond = 0.0;
    double checksum = 0.0;
};

struct BenchmarkRunData {
    std::string commit;
    std::string branch;
    std::string buildConfig;
    std::string cpu;
    unsigned int threadCount = 0;
    std::vector<BenchmarkResult> results;
};

struct BenchmarkComparisonEntry {
    BenchmarkResult before;
    BenchmarkResult after;
    double avgTimeRatio = 0.0;
    double p95TimeRatio = 0.0;
    double throughputRatio = 0.0;
    bool sameProfile = false;
};

struct BenchmarkRegressionFailure {
    std::string benchmarkName;
    std::string dataset;
    std::string metric;
    double before = 0.0;
    double after = 0.0;
    double ratio = 0.0;
    double limitRatio = 0.0;
};

struct BenchmarkComparison {
    std::string beforeLabel;
    std::string afterLabel;
    double regressionThresholdPercent = 0.0;
    bool regressionGateEnabled = false;
    std::vector<BenchmarkComparisonEntry> entries;
    std::vector<BenchmarkRegressionFailure> regressions;
    std::vector<std::string> missingAfter;
    std::vector<std::string> missingBefore;
};

struct BatchReadContext {
    std::uint64_t visited = 0;
    double checksum = 0.0;
};

struct MutableUpdateContext {
    kb::ecs::World* world = nullptr;
    float deltaSeconds = 0.0F;
    std::uint64_t visited = 0;
    double checksum = 0.0;
};

struct TransformUpdateContext {
    kb::ecs::World* world = nullptr;
    std::uint64_t visited = 0;
    double checksum = 0.0;
};

struct HierarchyBenchmarkData {
    kb::ecs::World world;
    std::vector<kb::ecs::Entity> entities;
    std::size_t maxDepth = 0;
};

struct StructuralBenchmarkData {
    kb::ecs::World world;
    std::vector<kb::ecs::Entity> transientEntities;
    std::vector<kb::ecs::Entity> markerEntities;
    std::size_t operationCount = 0;
};

struct PrefabSpawnBenchmarkData {
    std::unique_ptr<kb::scene::Scene> scene;
    kb::scene::ScenePrefabHandle prefab;
    std::size_t instanceCount = 0;
    std::size_t nodeCount = 0;
};

struct QueryFanoutBenchmarkData {
    kb::ecs::World hotWorld;
    kb::ecs::World coldWorld;
    std::size_t entityCount = 0;
};

struct QueryFanoutContext {
    std::uint64_t visited = 0;
    double checksum = 0.0;
};

struct SparseQueryBenchmarkData {
    kb::ecs::World world;
    std::size_t archetypeCount = 0;
    std::size_t matchingArchetypeCount = 0;
    std::size_t matchingEntityCount = 0;
};

struct SparseQueryContext {
    std::uint64_t visited = 0;
    double checksum = 0.0;
};

struct SystemChainAccess {
    std::size_t readSlot = 0;
    std::size_t writeSlot = 0;
};

struct SystemChainBenchmarkData {
    kb::ecs::World world;
    kb::ecs::SystemScheduler scheduler;
    kb::ecs::Entity stateEntity;
    std::vector<SystemChainAccess> accessChain;
    std::size_t systemCount = 0;
};

struct HierarchyFrameStats {
    std::uint64_t updated = 0;
    double checksum = 0.0;
};

class StructuralCommandBuffer {
    enum class StructuralCommandKind : std::uint8_t {
        Create,
        Destroy,
        AddMarker,
        RemoveMarker,
    };

    struct StructuralCommand {
        StructuralCommandKind kind = StructuralCommandKind::Create;
        kb::ecs::Entity entity;
        BenchPosition position;
        BenchVelocity velocity;
        BenchStructuralMarker marker;
    };

public:
    explicit StructuralCommandBuffer(std::size_t reserveCount) {
        commands_.reserve(reserveCount);
    }

    void Clear() noexcept {
        commands_.clear();
    }

    void Create(BenchPosition position, BenchVelocity velocity) {
        commands_.push_back(StructuralCommand{
            .kind = StructuralCommandKind::Create,
            .position = position,
            .velocity = velocity,
        });
    }

    void Destroy(kb::ecs::Entity entity) {
        commands_.push_back(StructuralCommand{
            .kind = StructuralCommandKind::Destroy,
            .entity = entity,
        });
    }

    void AddMarker(kb::ecs::Entity entity, BenchStructuralMarker marker) {
        commands_.push_back(StructuralCommand{
            .kind = StructuralCommandKind::AddMarker,
            .entity = entity,
            .marker = marker,
        });
    }

    void RemoveMarker(kb::ecs::Entity entity) {
        commands_.push_back(StructuralCommand{
            .kind = StructuralCommandKind::RemoveMarker,
            .entity = entity,
        });
    }

    void Playback(kb::ecs::World& world, std::vector<kb::ecs::Entity>& createdEntities) const {
        createdEntities.clear();
        for (const StructuralCommand& command : commands_) {
            switch (command.kind) {
            case StructuralCommandKind::Create: {
                const kb::ecs::Entity entity = world.CreateEntity();
                world.Set(entity, command.position);
                world.Set(entity, command.velocity);
                createdEntities.push_back(entity);
                break;
            }
            case StructuralCommandKind::Destroy:
                world.DestroyEntity(command.entity);
                break;
            case StructuralCommandKind::AddMarker:
                world.Set(command.entity, command.marker);
                break;
            case StructuralCommandKind::RemoveMarker:
                world.Remove<BenchStructuralMarker>(command.entity);
                break;
            }
        }
    }

private:
    std::vector<StructuralCommand> commands_;
};

[[nodiscard]] std::string JsonEscape(std::string_view value) {
    std::string output;
    output.reserve(value.size() + 8);
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
                std::ostringstream escaped;
                escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(character);
                output += escaped.str();
            } else {
                output += character;
            }
            break;
        }
    }
    return output;
}

class BenchmarkJsonReader {
public:
    explicit BenchmarkJsonReader(std::string source)
        : source_(std::move(source)) {}

    [[nodiscard]] BenchmarkRunData Parse() {
        BenchmarkRunData run;
        Expect('{');
        if (TryConsume('}')) {
            throw std::runtime_error("Benchmark JSON root object is empty");
        }

        while (true) {
            const std::string key = ReadString();
            Expect(':');
            if (key == "commit") {
                run.commit = ReadString();
            } else if (key == "branch") {
                run.branch = ReadString();
            } else if (key == "build_config") {
                run.buildConfig = ReadString();
            } else if (key == "cpu") {
                run.cpu = ReadString();
            } else if (key == "thread_count") {
                run.threadCount = static_cast<unsigned int>(ReadUnsigned());
            } else if (key == "results") {
                run.results = ReadResults();
            } else {
                SkipValue();
            }

            if (TryConsume('}')) {
                break;
            }
            Expect(',');
        }

        SkipWhitespace();
        if (cursor_ != source_.size()) {
            throw std::runtime_error("Benchmark JSON has trailing content");
        }
        return run;
    }

private:
    void SkipWhitespace() noexcept {
        while (cursor_ < source_.size()) {
            const char character = source_[cursor_];
            if (character != ' ' && character != '\n' && character != '\r' && character != '\t') {
                return;
            }
            ++cursor_;
        }
    }

    [[nodiscard]] bool TryConsume(char expected) noexcept {
        SkipWhitespace();
        if (cursor_ < source_.size() && source_[cursor_] == expected) {
            ++cursor_;
            return true;
        }
        return false;
    }

    void Expect(char expected) {
        if (!TryConsume(expected)) {
            throw std::runtime_error(std::string{ "Benchmark JSON expected '" } + expected + "'");
        }
    }

    [[nodiscard]] std::string ReadString() {
        SkipWhitespace();
        if (cursor_ >= source_.size() || source_[cursor_] != '"') {
            throw std::runtime_error("Benchmark JSON expected a string");
        }
        ++cursor_;

        std::string value;
        while (cursor_ < source_.size()) {
            const char character = source_[cursor_++];
            if (character == '"') {
                return value;
            }
            if (character != '\\') {
                value += character;
                continue;
            }
            if (cursor_ >= source_.size()) {
                throw std::runtime_error("Benchmark JSON string escape is incomplete");
            }

            const char escaped = source_[cursor_++];
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                value += escaped;
                break;
            case 'b':
                value += '\b';
                break;
            case 'f':
                value += '\f';
                break;
            case 'n':
                value += '\n';
                break;
            case 'r':
                value += '\r';
                break;
            case 't':
                value += '\t';
                break;
            case 'u':
                if (cursor_ + 4U > source_.size()) {
                    throw std::runtime_error("Benchmark JSON unicode escape is incomplete");
                }
                cursor_ += 4U;
                value += '?';
                break;
            default:
                throw std::runtime_error("Benchmark JSON string escape is invalid");
            }
        }

        throw std::runtime_error("Benchmark JSON string is unterminated");
    }

    [[nodiscard]] double ReadNumber() {
        SkipWhitespace();
        const char* begin = source_.c_str() + cursor_;
        char* end = nullptr;
        const double value = std::strtod(begin, &end);
        if (end == begin) {
            throw std::runtime_error("Benchmark JSON expected a number");
        }
        cursor_ += static_cast<std::size_t>(end - begin);
        return value;
    }

    [[nodiscard]] std::uint64_t ReadUnsigned() {
        const double value = ReadNumber();
        if (value < 0.0 || value > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
            throw std::runtime_error("Benchmark JSON unsigned number is out of range");
        }
        return static_cast<std::uint64_t>(value);
    }

    void SkipValue() {
        SkipWhitespace();
        if (cursor_ >= source_.size()) {
            throw std::runtime_error("Benchmark JSON value is missing");
        }

        const char character = source_[cursor_];
        if (character == '"') {
            static_cast<void>(ReadString());
        } else if (character == '{') {
            SkipObject();
        } else if (character == '[') {
            SkipArray();
        } else if ((character >= '0' && character <= '9') || character == '-') {
            static_cast<void>(ReadNumber());
        } else if (source_.compare(cursor_, 4U, "true") == 0) {
            cursor_ += 4U;
        } else if (source_.compare(cursor_, 5U, "false") == 0) {
            cursor_ += 5U;
        } else if (source_.compare(cursor_, 4U, "null") == 0) {
            cursor_ += 4U;
        } else {
            throw std::runtime_error("Benchmark JSON value is invalid");
        }
    }

    void SkipObject() {
        Expect('{');
        if (TryConsume('}')) {
            return;
        }
        while (true) {
            static_cast<void>(ReadString());
            Expect(':');
            SkipValue();
            if (TryConsume('}')) {
                return;
            }
            Expect(',');
        }
    }

    void SkipArray() {
        Expect('[');
        if (TryConsume(']')) {
            return;
        }
        while (true) {
            SkipValue();
            if (TryConsume(']')) {
                return;
            }
            Expect(',');
        }
    }

    [[nodiscard]] std::vector<BenchmarkResult> ReadResults() {
        std::vector<BenchmarkResult> results;
        Expect('[');
        if (TryConsume(']')) {
            return results;
        }
        while (true) {
            results.push_back(ReadResult());
            if (TryConsume(']')) {
                return results;
            }
            Expect(',');
        }
    }

    [[nodiscard]] BenchmarkResult ReadResult() {
        BenchmarkResult result;
        Expect('{');
        if (TryConsume('}')) {
            throw std::runtime_error("Benchmark JSON result object is empty");
        }

        while (true) {
            const std::string key = ReadString();
            Expect(':');
            if (key == "name") {
                result.name = ReadString();
            } else if (key == "dataset") {
                result.dataset = ReadString();
            } else if (key == "validation_mode") {
                result.validationMode = ReadString();
            } else if (key == "entities") {
                result.entities = static_cast<std::size_t>(ReadUnsigned());
            } else if (key == "frames") {
                result.frames = static_cast<std::size_t>(ReadUnsigned());
            } else if (key == "warmup_frames") {
                result.warmupFrames = static_cast<std::size_t>(ReadUnsigned());
            } else if (key == "execution_grain_size") {
                result.executionGrainSize = static_cast<std::size_t>(ReadUnsigned());
            } else if (key == "time_ms_min") {
                result.frameTime.minMs = ReadNumber();
            } else if (key == "time_ms_avg") {
                result.frameTime.avgMs = ReadNumber();
            } else if (key == "time_ms_p95") {
                result.frameTime.p95Ms = ReadNumber();
            } else if (key == "throughput_entities_per_second") {
                result.throughputEntitiesPerSecond = ReadNumber();
            } else if (key == "checksum") {
                result.checksum = ReadNumber();
            } else {
                SkipValue();
            }

            if (TryConsume('}')) {
                break;
            }
            Expect(',');
        }

        if (result.name.empty()) {
            throw std::runtime_error("Benchmark JSON result is missing name");
        }
        if (result.validationMode.empty()) {
            result.validationMode = "off";
        }
        return result;
    }

    std::string source_;
    std::size_t cursor_ = 0;
};

[[nodiscard]] std::string GetEnvironmentValue(const char* name) {
    if (name == nullptr) {
        return {};
    }

#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return {};
    }
    std::string result{ value, length == 0 ? 0 : length - 1 };
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string{ value };
#endif
}

[[nodiscard]] std::string CpuBrandString() {
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    int cpuInfo[4]{};
    __cpuid(cpuInfo, 0x80000000);
    const unsigned int maxExtendedId = static_cast<unsigned int>(cpuInfo[0]);
    if (maxExtendedId >= 0x80000004U) {
        char brand[49]{};
        __cpuid(cpuInfo, 0x80000002);
        std::memcpy(brand, cpuInfo, sizeof(cpuInfo));
        __cpuid(cpuInfo, 0x80000003);
        std::memcpy(brand + 16, cpuInfo, sizeof(cpuInfo));
        __cpuid(cpuInfo, 0x80000004);
        std::memcpy(brand + 32, cpuInfo, sizeof(cpuInfo));
        return std::string{ brand };
    }
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
    unsigned int eax = 0;
    unsigned int ebx = 0;
    unsigned int ecx = 0;
    unsigned int edx = 0;
    if (__get_cpuid_max(0x80000000, nullptr) >= 0x80000004U) {
        char brand[49]{};
        __cpuid(0x80000002, eax, ebx, ecx, edx);
        std::copy_n(reinterpret_cast<const char*>(&eax), sizeof(eax), brand);
        std::copy_n(reinterpret_cast<const char*>(&ebx), sizeof(ebx), brand + 4);
        std::copy_n(reinterpret_cast<const char*>(&ecx), sizeof(ecx), brand + 8);
        std::copy_n(reinterpret_cast<const char*>(&edx), sizeof(edx), brand + 12);
        __cpuid(0x80000003, eax, ebx, ecx, edx);
        std::copy_n(reinterpret_cast<const char*>(&eax), sizeof(eax), brand + 16);
        std::copy_n(reinterpret_cast<const char*>(&ebx), sizeof(ebx), brand + 20);
        std::copy_n(reinterpret_cast<const char*>(&ecx), sizeof(ecx), brand + 24);
        std::copy_n(reinterpret_cast<const char*>(&edx), sizeof(edx), brand + 28);
        __cpuid(0x80000004, eax, ebx, ecx, edx);
        std::copy_n(reinterpret_cast<const char*>(&eax), sizeof(eax), brand + 32);
        std::copy_n(reinterpret_cast<const char*>(&ebx), sizeof(ebx), brand + 36);
        std::copy_n(reinterpret_cast<const char*>(&ecx), sizeof(ecx), brand + 40);
        std::copy_n(reinterpret_cast<const char*>(&edx), sizeof(edx), brand + 44);
        return std::string{ brand };
    }
#endif

    std::string cpu = GetEnvironmentValue("PROCESSOR_IDENTIFIER");
    return cpu.empty() ? "unknown" : cpu;
}

[[nodiscard]] std::size_t ParseSize(std::string_view value, std::string_view optionName) {
    std::size_t parsed = 0;
    for (const char character : value) {
        if (character < '0' || character > '9') {
            throw std::invalid_argument(std::string{ optionName } + " expects an unsigned integer");
        }
        const std::size_t digit = static_cast<std::size_t>(character - '0');
        if (parsed > (std::numeric_limits<std::size_t>::max() - digit) / 10) {
            throw std::invalid_argument(std::string{ optionName } + " is too large");
        }
        parsed = parsed * 10 + digit;
    }
    return parsed;
}

[[nodiscard]] double ParsePercent(std::string_view value, std::string_view optionName) {
    std::string owned{ value };
    char* end = nullptr;
    const double parsed = std::strtod(owned.c_str(), &end);
    if (end == owned.c_str() || *end != '\0' || !std::isfinite(parsed) || parsed < 0.0 || parsed >= 100.0) {
        throw std::invalid_argument(std::string{ optionName } + " expects a percent in range 0..<100");
    }
    return parsed;
}

[[nodiscard]] BenchmarkValidationSelection ParseValidationSelection(std::string_view value, std::string_view optionName) {
    if (value == "off" || value == "none" || value == "disabled") {
        return BenchmarkValidationSelection::Off;
    }
    if (value == "debug" || value == "on" || value == "enabled") {
        return BenchmarkValidationSelection::Debug;
    }
    if (value == "both") {
        return BenchmarkValidationSelection::Both;
    }
    throw std::invalid_argument(std::string{ optionName } + " expects off, debug, or both");
}

[[nodiscard]] std::string_view ValidationModeName(bool debugValidationEnabled) noexcept {
    return debugValidationEnabled ? "debug" : "off";
}

[[nodiscard]] BenchmarkOptions ParseOptions(int argc, char** argv) {
    BenchmarkOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{ argv[index] };
        const auto readValue = [&](std::string_view optionName) -> std::string_view {
            if (index + 1 >= argc) {
                throw std::invalid_argument(std::string{ optionName } + " expects a value");
            }
            ++index;
            return std::string_view{ argv[index] };
        };

        if (argument == "--entities") {
            options.entityCount = ParseSize(readValue(argument), argument);
        } else if (argument == "--hierarchy-entities") {
            options.hierarchyEntityCount = ParseSize(readValue(argument), argument);
        } else if (argument == "--structural-changes") {
            options.structuralChangesPerFrame = ParseSize(readValue(argument), argument);
        } else if (argument == "--frames") {
            options.frames = ParseSize(readValue(argument), argument);
        } else if (argument == "--warmup") {
            options.warmupFrames = ParseSize(readValue(argument), argument);
        } else if (argument == "--grain") {
            options.executionGrainSize = ParseSize(readValue(argument), argument);
        } else if (argument == "--output") {
            options.outputPath = std::filesystem::path{ readValue(argument) };
        } else if (argument == "--save-baseline") {
            options.saveBaselinePath = std::filesystem::path{ readValue(argument) };
        } else if (argument == "--compare-baseline") {
            options.compareBaselinePath = std::filesystem::path{ readValue(argument) };
        } else if (argument == "--compare-before") {
            options.compareBeforePath = std::filesystem::path{ readValue(argument) };
        } else if (argument == "--compare-after") {
            options.compareAfterPath = std::filesystem::path{ readValue(argument) };
        } else if (argument == "--compare-output") {
            options.comparisonOutputPath = std::filesystem::path{ readValue(argument) };
        } else if (argument == "--fail-on-regression") {
            options.regressionThresholdPercent = ParsePercent(readValue(argument), argument);
            options.failOnRegression = true;
        } else if (argument == "--validation") {
            options.validationSelection = ParseValidationSelection(readValue(argument), argument);
        } else if (argument == "--smoke") {
            options.entityCount = 10'000;
            options.hierarchyEntityCount = 4'096;
            options.structuralChangesPerFrame = 4'000;
            options.sparseArchetypeCount = 128;
            options.sparseMatchingArchetypeCount = 4;
            options.sparseEntitiesPerArchetype = 4;
            options.systemChainCounts = { 16, 32, 64 };
            options.prefabSpawnInstanceCounts = { 64, 128, 256 };
            options.prefabHierarchyNodeCounts = { 1, 4, 8 };
            options.frames = 3;
            options.warmupFrames = 1;
            options.outputPath = "ecs_benchmark_smoke.json";
        } else if (argument == "--help") {
            std::cout << "Usage: kb_ecs_benchmarks [--entities N] [--hierarchy-entities N] [--structural-changes N] [--frames N] [--warmup N] [--grain N] [--output path] [--save-baseline path] [--compare-baseline path] [--compare-before path --compare-after path] [--compare-output path] [--fail-on-regression percent] [--validation off|debug|both] [--smoke]\n";
            std::exit(0);
        } else {
            throw std::invalid_argument("Unknown argument: " + std::string{ argument });
        }
    }

    if (options.entityCount == 0 || options.hierarchyEntityCount == 0 || options.structuralChangesPerFrame == 0 || options.frames == 0 || options.executionGrainSize == 0) {
        throw std::invalid_argument("entities, hierarchy-entities, structural-changes, frames, and grain must be greater than zero");
    }
    if (options.structuralChangesPerFrame % 4U != 0U) {
        throw std::invalid_argument("structural-changes must be divisible by four");
    }
    if (options.sparseArchetypeCount == 0 || options.sparseMatchingArchetypeCount == 0 || options.sparseEntitiesPerArchetype == 0) {
        throw std::invalid_argument("sparse query benchmark dimensions must be greater than zero");
    }
    if (options.sparseMatchingArchetypeCount > options.sparseArchetypeCount) {
        throw std::invalid_argument("sparse matching archetype count must not exceed sparse archetype count");
    }
    if (options.systemChainCounts.empty()) {
        throw std::invalid_argument("system chain benchmark requires at least one system count");
    }
    for (const std::size_t systemCount : options.systemChainCounts) {
        if (systemCount == 0 || systemCount > BenchSystemChainState::kMaxSlots) {
            throw std::invalid_argument("system chain counts must be in range 1..256");
        }
    }
    const bool hasCompareBefore = !options.compareBeforePath.empty();
    const bool hasCompareAfter = !options.compareAfterPath.empty();
    if (hasCompareBefore != hasCompareAfter) {
        throw std::invalid_argument("compare-before and compare-after must be provided together");
    }
    if (hasCompareBefore && (!options.compareBaselinePath.empty() || !options.saveBaselinePath.empty())) {
        throw std::invalid_argument("compare-before/compare-after mode cannot be combined with save-baseline or compare-baseline");
    }
    if (options.failOnRegression && !hasCompareBefore && options.compareBaselinePath.empty()) {
        throw std::invalid_argument("fail-on-regression requires compare-baseline or compare-before/compare-after");
    }
    return options;
}

template <typename... Components>
void CountValidationBatch(const kb::ecs::QueryBatch<Components...>& batch, void* context) {
    auto* visited = static_cast<std::uint64_t*>(context);
    *visited += batch.Count();
}

template <typename... Components>
void ValidateQueryCount(kb::ecs::World& world, const BenchmarkOptions& options, std::size_t expectedCount, std::string_view label) {
    kb::ecs::Query<Components...> query = world.CreateQuery<Components...>();
    if (!query.IsValid()) {
        throw std::runtime_error("Debug validation failed to create query for " + std::string{ label });
    }

    std::uint64_t visited = 0;
    query.ForEachBatch(kb::ecs::QueryExecutionSettings{ .maxBatchSize = options.executionGrainSize }, &CountValidationBatch<Components...>, &visited);
    if (visited != expectedCount) {
        throw std::runtime_error(
            "Debug validation count mismatch for " + std::string{ label } + ": expected " + std::to_string(expectedCount) + ", visited "
            + std::to_string(visited));
    }
}

void ValidateDenseBenchmarkWorld(kb::ecs::World& world, const BenchmarkOptions& options) {
    if (!options.debugValidationEnabled) {
        return;
    }
    ValidateQueryCount<BenchPosition, BenchVelocity>(world, options, options.entityCount, "Position+Velocity");
    ValidateQueryCount<BenchLocalTransform, BenchWorldTransform>(world, options, options.entityCount, "LocalTransform+WorldTransform");
}

[[nodiscard]] std::string BenchmarkNameForMode(std::string name, const BenchmarkOptions& options) {
    if (options.debugValidationEnabled) {
        name += "_debug_validation";
    }
    return name;
}

[[nodiscard]] std::string BenchmarkDatasetForMode(std::string dataset, const BenchmarkOptions& options) {
    static_cast<void>(options);
    return dataset;
}

void PopulateWorld(kb::ecs::World& world, std::size_t entityCount) {
    const kb::ecs::ComponentId positionComponent = world.RegisterComponent<BenchPosition>("BenchPosition");
    const kb::ecs::ComponentId velocityComponent = world.RegisterComponent<BenchVelocity>("BenchVelocity");
    const kb::ecs::ComponentId localTransformComponent = world.RegisterComponent<BenchLocalTransform>("BenchLocalTransform");
    const kb::ecs::ComponentId worldTransformComponent = world.RegisterComponent<BenchWorldTransform>("BenchWorldTransform");
    if (positionComponent == 0 || velocityComponent == 0 || localTransformComponent == 0 || worldTransformComponent == 0) {
        throw std::runtime_error("Failed to register benchmark components");
    }

    for (std::size_t index = 0; index < entityCount; ++index) {
        const kb::ecs::Entity entity = world.CreateEntity();
        const float value = static_cast<float>(index % 1024U);
        world.Set(entity, BenchPosition{ .x = value, .y = value * 0.5F });
        world.Set(entity, BenchVelocity{ .x = 1.0F + static_cast<float>(index % 7U), .y = -0.25F });
        world.Set(
            entity,
            BenchLocalTransform{
                .translationX = value,
                .translationY = static_cast<float>((index / 3U) % 1024U) * 0.25F,
                .translationZ = static_cast<float>((index / 7U) % 512U) * 0.125F,
                .rotationZ = static_cast<float>(index % 6283U) * 0.001F,
                .scaleX = 1.0F + static_cast<float>(index % 5U) * 0.05F,
                .scaleY = 1.0F + static_cast<float>(index % 7U) * 0.03F,
                .scaleZ = 1.0F + static_cast<float>(index % 3U) * 0.02F,
            });
        world.Set(entity, BenchWorldTransform{});
    }
}

void VisitBatchRead(const kb::ecs::QueryBatch<BenchPosition, BenchVelocity>& batch, void* context) {
    auto* readContext = static_cast<BatchReadContext*>(context);
    const BenchPosition* positions = batch.Components<0>();
    const BenchVelocity* velocities = batch.Components<1>();
    for (std::size_t index = 0; index < batch.Count(); ++index) {
        readContext->checksum += static_cast<double>(positions[index].x) + static_cast<double>(velocities[index].x);
    }
    readContext->visited += batch.Count();
}

void VisitMutableUpdate(kb::ecs::Entity entity, BenchPosition& position, void* context) {
    auto* updateContext = static_cast<MutableUpdateContext*>(context);
    const BenchVelocity* velocity = updateContext->world->TryGet<BenchVelocity>(entity);
    if (velocity == nullptr) {
        return;
    }

    position.x += velocity->x * updateContext->deltaSeconds;
    position.y += velocity->y * updateContext->deltaSeconds;
    updateContext->checksum += static_cast<double>(position.x);
    ++updateContext->visited;
}

void WriteLocalToWorld(const BenchLocalTransform& local, BenchWorldTransform& world) noexcept {
    const float cosZ = static_cast<float>(std::cos(static_cast<double>(local.rotationZ)));
    const float sinZ = static_cast<float>(std::sin(static_cast<double>(local.rotationZ)));

    world.matrix[0] = cosZ * local.scaleX;
    world.matrix[1] = sinZ * local.scaleX;
    world.matrix[2] = 0.0F;
    world.matrix[3] = 0.0F;

    world.matrix[4] = -sinZ * local.scaleY;
    world.matrix[5] = cosZ * local.scaleY;
    world.matrix[6] = 0.0F;
    world.matrix[7] = 0.0F;

    world.matrix[8] = 0.0F;
    world.matrix[9] = 0.0F;
    world.matrix[10] = local.scaleZ;
    world.matrix[11] = 0.0F;

    world.matrix[12] = local.translationX;
    world.matrix[13] = local.translationY;
    world.matrix[14] = local.translationZ;
    world.matrix[15] = 1.0F;
}

void MultiplyTransformMatrices(const BenchWorldTransform& parent, const BenchWorldTransform& local, BenchWorldTransform& world) noexcept {
    BenchWorldTransform result;
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            result.matrix[column * 4 + row] =
                parent.matrix[row] * local.matrix[column * 4]
                + parent.matrix[4 + row] * local.matrix[column * 4 + 1]
                + parent.matrix[8 + row] * local.matrix[column * 4 + 2]
                + parent.matrix[12 + row] * local.matrix[column * 4 + 3];
        }
    }
    world = result;
}

void VisitTransformUpdate(kb::ecs::Entity entity, BenchWorldTransform& worldTransform, void* context) {
    auto* updateContext = static_cast<TransformUpdateContext*>(context);
    const BenchLocalTransform* localTransform = updateContext->world->TryGet<BenchLocalTransform>(entity);
    if (localTransform == nullptr) {
        throw std::runtime_error("Transform benchmark found an entity without a local transform");
    }

    WriteLocalToWorld(*localTransform, worldTransform);
    updateContext->checksum += static_cast<double>(worldTransform.matrix[0]) + static_cast<double>(worldTransform.matrix[5])
        + static_cast<double>(worldTransform.matrix[10]) + static_cast<double>(worldTransform.matrix[12]);
    ++updateContext->visited;
}

void RegisterHierarchyComponents(kb::ecs::World& world) {
    const kb::ecs::ComponentId localTransformComponent = world.RegisterComponent<BenchLocalTransform>("BenchHierarchyLocalTransform");
    const kb::ecs::ComponentId worldTransformComponent = world.RegisterComponent<BenchWorldTransform>("BenchHierarchyWorldTransform");
    const kb::ecs::ComponentId hierarchyNodeComponent = world.RegisterComponent<BenchHierarchyNode>("BenchHierarchyNode");
    if (localTransformComponent == 0 || worldTransformComponent == 0 || hierarchyNodeComponent == 0) {
        throw std::runtime_error("Failed to register hierarchy benchmark components");
    }
}

[[nodiscard]] HierarchyBenchmarkData CreateHierarchyBenchmarkData(const BenchmarkOptions& options, std::size_t maxDepth) {
    kb::ecs::WorldConfig config = kb::ecs::WorldConfigPresets::BenchmarkDefault();
    config.reserveEntities = options.hierarchyEntityCount;
    config.executionGrainSize = options.executionGrainSize;

    HierarchyBenchmarkData data{
        .world = kb::ecs::World{ config },
        .entities = {},
        .maxDepth = maxDepth,
    };
    data.entities.reserve(options.hierarchyEntityCount);
    RegisterHierarchyComponents(data.world);

    for (std::size_t index = 0; index < options.hierarchyEntityCount; ++index) {
        const kb::ecs::Entity entity = data.world.CreateEntity();
        const std::size_t depthInTree = index % maxDepth;
        const std::uint32_t parentIndex = depthInTree == 0 ? kInvalidHierarchyParent : static_cast<std::uint32_t>(index - 1U);
        const float value = static_cast<float>(index % 1024U);

        data.world.Set(
            entity,
            BenchLocalTransform{
                .translationX = 0.01F * static_cast<float>(depthInTree),
                .translationY = value * 0.001F,
                .translationZ = static_cast<float>((index / 11U) % 256U) * 0.002F,
                .rotationZ = static_cast<float>((index + depthInTree) % 2048U) * 0.0005F,
                .scaleX = 1.0F + static_cast<float>(depthInTree % 5U) * 0.01F,
                .scaleY = 1.0F + static_cast<float>(depthInTree % 7U) * 0.01F,
                .scaleZ = 1.0F,
            });
        data.world.Set(entity, BenchWorldTransform{});
        data.world.Set(entity, BenchHierarchyNode{ .parentIndex = parentIndex });

        if (parentIndex != kInvalidHierarchyParent) {
            data.world.SetParent(entity, data.entities[parentIndex]);
        }

        data.entities.push_back(entity);
    }

    return data;
}

void MarkHierarchyRootsDirty(HierarchyBenchmarkData& data, std::size_t frame) {
    for (std::size_t rootIndex = 0; rootIndex < data.entities.size(); rootIndex += data.maxDepth) {
        const kb::ecs::Entity entity = data.entities[rootIndex];
        auto* local = data.world.TryGetMutable<BenchLocalTransform>(entity);
        auto* node = data.world.TryGetMutable<BenchHierarchyNode>(entity);
        if (local == nullptr || node == nullptr) {
            throw std::runtime_error("Hierarchy benchmark root is missing required components");
        }

        local->rotationZ += 0.00025F * static_cast<float>((frame % 17U) + 1U);
        ++node->localVersion;
        data.world.MarkModified<BenchLocalTransform>(entity);
        data.world.MarkModified<BenchHierarchyNode>(entity);
    }
}

[[nodiscard]] HierarchyFrameStats PropagateHierarchyTransforms(HierarchyBenchmarkData& data, std::size_t frame) {
    MarkHierarchyRootsDirty(data, frame);

    HierarchyFrameStats stats;
    for (std::size_t index = 0; index < data.entities.size(); ++index) {
        const kb::ecs::Entity entity = data.entities[index];
        const auto* local = data.world.TryGet<BenchLocalTransform>(entity);
        auto* node = data.world.TryGetMutable<BenchHierarchyNode>(entity);
        auto* worldTransform = data.world.TryGetMutable<BenchWorldTransform>(entity);
        if (local == nullptr || node == nullptr || worldTransform == nullptr) {
            throw std::runtime_error("Hierarchy benchmark entity is missing required components");
        }

        std::uint32_t parentWorldVersion = 0;
        const BenchWorldTransform* parentWorldTransform = nullptr;
        if (node->parentIndex != kInvalidHierarchyParent) {
            if (node->parentIndex >= data.entities.size()) {
                throw std::runtime_error("Hierarchy benchmark has an invalid parent index");
            }

            const kb::ecs::Entity parentEntity = data.entities[node->parentIndex];
            const auto* parentNode = data.world.TryGet<BenchHierarchyNode>(parentEntity);
            parentWorldTransform = data.world.TryGet<BenchWorldTransform>(parentEntity);
            if (parentNode == nullptr || parentWorldTransform == nullptr) {
                throw std::runtime_error("Hierarchy benchmark parent is missing required components");
            }
            parentWorldVersion = parentNode->worldVersion;
        }

        const bool localDirty = node->localVersion != node->appliedLocalVersion;
        const bool parentDirty = node->observedParentWorldVersion != parentWorldVersion;
        if (!localDirty && !parentDirty) {
            continue;
        }

        BenchWorldTransform localMatrix;
        WriteLocalToWorld(*local, localMatrix);
        if (parentWorldTransform == nullptr) {
            *worldTransform = localMatrix;
        } else {
            MultiplyTransformMatrices(*parentWorldTransform, localMatrix, *worldTransform);
        }

        node->appliedLocalVersion = node->localVersion;
        node->observedParentWorldVersion = parentWorldVersion;
        ++node->worldVersion;
        data.world.MarkModified<BenchWorldTransform>(entity);
        data.world.MarkModified<BenchHierarchyNode>(entity);

        stats.checksum += static_cast<double>(worldTransform->matrix[0]) + static_cast<double>(worldTransform->matrix[5])
            + static_cast<double>(worldTransform->matrix[10]) + static_cast<double>(worldTransform->matrix[12]);
        ++stats.updated;
    }

    return stats;
}

void RegisterStructuralComponents(kb::ecs::World& world) {
    const kb::ecs::ComponentId positionComponent = world.RegisterComponent<BenchPosition>("BenchStructuralPosition");
    const kb::ecs::ComponentId velocityComponent = world.RegisterComponent<BenchVelocity>("BenchStructuralVelocity");
    const kb::ecs::ComponentId markerComponent = world.RegisterComponent<BenchStructuralMarker>("BenchStructuralMarker");
    if (positionComponent == 0 || velocityComponent == 0 || markerComponent == 0) {
        throw std::runtime_error("Failed to register structural benchmark components");
    }
}

[[nodiscard]] BenchPosition StructuralPositionValue(std::size_t index, std::size_t frame) noexcept {
    const float seed = static_cast<float>((index + frame * 131U) % 4096U);
    return BenchPosition{
        .x = seed * 0.25F,
        .y = seed * -0.125F,
    };
}

[[nodiscard]] BenchVelocity StructuralVelocityValue(std::size_t index, std::size_t frame) noexcept {
    return BenchVelocity{
        .x = 0.5F + static_cast<float>((index + frame) % 11U) * 0.1F,
        .y = -0.25F + static_cast<float>((index + frame * 3U) % 7U) * 0.05F,
    };
}

[[nodiscard]] BenchStructuralMarker StructuralMarkerValue(std::size_t index, std::size_t frame) noexcept {
    return BenchStructuralMarker{
        .value = static_cast<std::uint32_t>((index * 1664525U + 1013904223U) & 0xFFFFFFFFU),
        .frame = static_cast<std::uint32_t>(frame),
    };
}

[[nodiscard]] StructuralBenchmarkData CreateStructuralBenchmarkData(const BenchmarkOptions& options) {
    const std::size_t operationCount = options.structuralChangesPerFrame / 4U;

    kb::ecs::WorldConfig config = kb::ecs::WorldConfigPresets::BenchmarkDefault();
    config.reserveEntities = std::max(config.reserveEntities, operationCount * 3U);
    config.executionGrainSize = options.executionGrainSize;

    StructuralBenchmarkData data{
        .world = kb::ecs::World{ config },
        .transientEntities = {},
        .markerEntities = {},
        .operationCount = operationCount,
    };
    data.transientEntities.reserve(operationCount);
    data.markerEntities.reserve(operationCount * 2U);
    RegisterStructuralComponents(data.world);

    for (std::size_t index = 0; index < operationCount; ++index) {
        const kb::ecs::Entity entity = data.world.CreateEntity();
        data.world.Set(entity, StructuralPositionValue(index, 0));
        data.world.Set(entity, StructuralVelocityValue(index, 0));
        data.transientEntities.push_back(entity);
    }

    for (std::size_t index = 0; index < operationCount * 2U; ++index) {
        const kb::ecs::Entity entity = data.world.CreateEntity();
        data.world.Set(entity, StructuralPositionValue(index + operationCount, 0));
        data.world.Set(entity, StructuralVelocityValue(index + operationCount, 0));
        if (index >= operationCount) {
            data.world.Set(entity, StructuralMarkerValue(index, 0));
        }
        data.markerEntities.push_back(entity);
    }

    return data;
}

[[nodiscard]] double RunStructuralChangesFrame(
    StructuralBenchmarkData& data,
    StructuralCommandBuffer& commandBuffer,
    std::vector<kb::ecs::Entity>& createdEntities,
    std::size_t frame) {
    commandBuffer.Clear();
    for (const kb::ecs::Entity entity : data.transientEntities) {
        commandBuffer.Destroy(entity);
    }

    for (std::size_t index = 0; index < data.operationCount; ++index) {
        commandBuffer.Create(StructuralPositionValue(index, frame + 1U), StructuralVelocityValue(index, frame + 1U));
    }

    for (std::size_t index = 0; index < data.operationCount; ++index) {
        commandBuffer.AddMarker(data.markerEntities[index], StructuralMarkerValue(index, frame + 1U));
    }

    for (std::size_t index = 0; index < data.operationCount; ++index) {
        commandBuffer.RemoveMarker(data.markerEntities[data.operationCount + index]);
    }

    commandBuffer.Playback(data.world, createdEntities);
    if (createdEntities.size() != data.operationCount) {
        throw std::runtime_error("Structural benchmark created an unexpected number of entities");
    }

    double checksum = 0.0;
    for (const kb::ecs::Entity entity : data.transientEntities) {
        if (data.world.IsAlive(entity)) {
            throw std::runtime_error("Structural benchmark failed to destroy a transient entity");
        }
    }

    for (const kb::ecs::Entity entity : createdEntities) {
        if (!data.world.IsAlive(entity) || !data.world.Has<BenchPosition>(entity) || !data.world.Has<BenchVelocity>(entity)) {
            throw std::runtime_error("Structural benchmark failed to create a complete transient entity");
        }
        checksum += static_cast<double>(entity.Id() & 0xFFFFU);
    }

    for (std::size_t index = 0; index < data.operationCount; ++index) {
        const kb::ecs::Entity addedEntity = data.markerEntities[index];
        const kb::ecs::Entity removedEntity = data.markerEntities[data.operationCount + index];
        const BenchStructuralMarker* marker = data.world.TryGet<BenchStructuralMarker>(addedEntity);
        if (marker == nullptr || marker->frame != static_cast<std::uint32_t>(frame + 1U)) {
            throw std::runtime_error("Structural benchmark failed to add a marker component");
        }
        if (data.world.Has<BenchStructuralMarker>(removedEntity)) {
            throw std::runtime_error("Structural benchmark failed to remove a marker component");
        }
        checksum += static_cast<double>(marker->value & 0xFFFFU);
    }

    data.transientEntities.swap(createdEntities);
    for (std::size_t index = 0; index < data.operationCount; ++index) {
        std::swap(data.markerEntities[index], data.markerEntities[data.operationCount + index]);
    }

    return checksum;
}

[[nodiscard]] kb::scene::TransformComponent PrefabNodeTransform(std::size_t nodeIndex) noexcept {
    return kb::scene::TransformComponent{
        .localPosition = kb::scene::Vec3{
            static_cast<float>(nodeIndex % 17U) * 0.25F,
            static_cast<float>((nodeIndex / 3U) % 13U) * 0.125F,
            static_cast<float>((nodeIndex / 7U) % 11U) * 0.0625F,
        },
        .localRotation = kb::scene::Quat{
            0.0F,
            static_cast<float>(nodeIndex % 5U) * 0.01F,
            static_cast<float>(nodeIndex % 9U) * 0.015F,
            1.0F,
        },
        .localScale = kb::scene::Vec3{
            1.0F + static_cast<float>(nodeIndex % 3U) * 0.01F,
            1.0F + static_cast<float>(nodeIndex % 5U) * 0.01F,
            1.0F,
        },
    };
}

[[nodiscard]] kb::scene::ScenePrefabNodeComponents PrefabNodeComponents(std::size_t nodeIndex) {
    kb::scene::ScenePrefabNodeComponents components;
    if (nodeIndex % 4U == 0U) {
        components.meshRenderer = kb::scene::MeshRendererComponent{
            .meshAssetId = static_cast<std::uint64_t>(100U + nodeIndex),
            .materialAssetId = static_cast<std::uint64_t>(200U + (nodeIndex % 16U)),
        };
    }
    if (nodeIndex % 8U == 1U) {
        components.light = kb::scene::LightComponent{
            .kind = kb::scene::LightKind::Point,
            .intensity = 1.0F + static_cast<float>(nodeIndex % 7U) * 0.25F,
        };
    }
    if (nodeIndex == 2U) {
        components.camera = kb::scene::CameraComponent{
            .primary = false,
        };
    }
    return components;
}

[[nodiscard]] kb::scene::ScenePrefab CreatePrefabSpawnTemplate(std::size_t nodeCount) {
    kb::scene::ScenePrefab prefab;
    prefab.Reserve(nodeCount);
    for (std::size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex) {
        const std::uint32_t parentNode = nodeIndex == 0U
            ? kb::scene::ScenePrefabNodeDesc::NoParent
            : static_cast<std::uint32_t>((nodeIndex - 1U) / 4U);
        static_cast<void>(prefab.AddNode(kb::scene::ScenePrefabNodeDesc{
            .name = "SpawnNode" + std::to_string(nodeIndex),
            .parentNode = parentNode,
            .transform = PrefabNodeTransform(nodeIndex),
            .visibility = kb::scene::VisibilityComponent{ .visible = (nodeIndex % 11U) != 0U },
            .components = PrefabNodeComponents(nodeIndex),
        }));
    }
    return prefab;
}

[[nodiscard]] PrefabSpawnBenchmarkData CreatePrefabSpawnBenchmarkData(std::size_t instanceCount, std::size_t nodeCount) {
    PrefabSpawnBenchmarkData data{
        .scene = std::make_unique<kb::scene::Scene>(),
        .prefab = {},
        .instanceCount = instanceCount,
        .nodeCount = nodeCount,
    };
    data.prefab = data.scene->Prefabs().Register(
        "SpawnBenchmark_" + std::to_string(instanceCount) + "_" + std::to_string(nodeCount),
        CreatePrefabSpawnTemplate(nodeCount));
    if (!data.prefab.IsValid()) {
        throw std::runtime_error("Prefab spawn benchmark failed to register prefab");
    }
    return data;
}

[[nodiscard]] double RunPrefabSpawnFrame(PrefabSpawnBenchmarkData& data) {
    kb::scene::Scene& scene = *data.scene;
    const std::size_t initialEntityCount = scene.Entities().Count();
    double checksum = 0.0;
    for (std::size_t instanceIndex = 0; instanceIndex < data.instanceCount; ++instanceIndex) {
        const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(data.prefab);
        if (instance.ObjectCount() != data.nodeCount || !instance.Handle().IsValid() || !instance.RootObject().IsValid()) {
            throw std::runtime_error("Prefab spawn benchmark failed to instantiate a complete registered prefab");
        }
        checksum += static_cast<double>(instance.RootObject().Entity().Id() & 0xFFFFU);
    }

    const std::size_t expectedEntityCount = initialEntityCount + data.instanceCount * data.nodeCount;
    if (scene.Entities().Count() != expectedEntityCount) {
        throw std::runtime_error("Prefab spawn benchmark created an unexpected number of entities");
    }
    if (data.nodeCount == 0 || data.instanceCount == 0) {
        throw std::runtime_error("Prefab spawn benchmark dimensions are invalid");
    }
    return checksum;
}

template <typename FrameFunction>
[[nodiscard]] BenchmarkResult RunTimedBenchmark(
    std::string name,
    std::string dataset,
    const BenchmarkOptions& options,
    std::size_t entities,
    FrameFunction&& frameFunction);

template <std::size_t Index>
[[nodiscard]] BenchFanoutComponent<Index> FanoutComponentValue(std::size_t entityIndex) noexcept {
    return BenchFanoutComponent<Index>{
        .value = static_cast<float>((entityIndex * (Index + 3U) + Index * 17U) % 4096U) * 0.03125F,
    };
}

template <std::size_t... Indices>
void RegisterFanoutComponents(kb::ecs::World& world, std::index_sequence<Indices...>) {
    const std::array<kb::ecs::ComponentId, sizeof...(Indices)> componentIds{
        world.RegisterComponent<BenchFanoutComponent<Indices>>("BenchFanoutComponent" + std::to_string(Indices))...
    };
    if (std::ranges::any_of(componentIds, [](kb::ecs::ComponentId componentId) { return componentId == 0; })) {
        throw std::runtime_error("Failed to register query fanout benchmark components");
    }
}

template <std::size_t... Indices>
void RegisterFanoutColdComponents(kb::ecs::World& world, std::index_sequence<Indices...>) {
    const std::array<kb::ecs::ComponentId, sizeof...(Indices)> componentIds{
        world.RegisterComponent<BenchFanoutColdComponent<Indices>>("BenchFanoutColdComponent" + std::to_string(Indices))...
    };
    if (std::ranges::any_of(componentIds, [](kb::ecs::ComponentId componentId) { return componentId == 0; })) {
        throw std::runtime_error("Failed to register query fanout cold benchmark components");
    }
}

template <std::size_t... Indices>
void SetFanoutComponents(kb::ecs::World& world, kb::ecs::Entity entity, std::size_t entityIndex, std::index_sequence<Indices...>) {
    (world.Set(entity, FanoutComponentValue<Indices>(entityIndex)), ...);
}

void SetFanoutColdComponent(kb::ecs::World& world, kb::ecs::Entity entity, std::size_t archetypeIndex) {
    switch (archetypeIndex) {
    case 0:
        world.Set(entity, BenchFanoutColdComponent<0>{ .value = static_cast<std::uint32_t>(archetypeIndex) });
        break;
    case 1:
        world.Set(entity, BenchFanoutColdComponent<1>{ .value = static_cast<std::uint32_t>(archetypeIndex) });
        break;
    case 2:
        world.Set(entity, BenchFanoutColdComponent<2>{ .value = static_cast<std::uint32_t>(archetypeIndex) });
        break;
    case 3:
        world.Set(entity, BenchFanoutColdComponent<3>{ .value = static_cast<std::uint32_t>(archetypeIndex) });
        break;
    case 4:
        world.Set(entity, BenchFanoutColdComponent<4>{ .value = static_cast<std::uint32_t>(archetypeIndex) });
        break;
    case 5:
        world.Set(entity, BenchFanoutColdComponent<5>{ .value = static_cast<std::uint32_t>(archetypeIndex) });
        break;
    case 6:
        world.Set(entity, BenchFanoutColdComponent<6>{ .value = static_cast<std::uint32_t>(archetypeIndex) });
        break;
    case 7:
        world.Set(entity, BenchFanoutColdComponent<7>{ .value = static_cast<std::uint32_t>(archetypeIndex) });
        break;
    default:
        throw std::invalid_argument("Invalid query fanout cold archetype index");
    }
}

[[nodiscard]] QueryFanoutBenchmarkData CreateQueryFanoutBenchmarkData(const BenchmarkOptions& options) {
    kb::ecs::WorldConfig config = kb::ecs::WorldConfigPresets::BenchmarkDefault();
    config.reserveEntities = options.entityCount;
    config.executionGrainSize = options.executionGrainSize;

    QueryFanoutBenchmarkData data{
        .hotWorld = kb::ecs::World{ config },
        .coldWorld = kb::ecs::World{ config },
        .entityCount = options.entityCount,
    };

    RegisterFanoutComponents(data.hotWorld, std::make_index_sequence<8>{});
    RegisterFanoutComponents(data.coldWorld, std::make_index_sequence<8>{});
    RegisterFanoutColdComponents(data.coldWorld, std::make_index_sequence<8>{});

    for (std::size_t entityIndex = 0; entityIndex < options.entityCount; ++entityIndex) {
        const kb::ecs::Entity hotEntity = data.hotWorld.CreateEntity();
        SetFanoutComponents(data.hotWorld, hotEntity, entityIndex, std::make_index_sequence<8>{});

        const kb::ecs::Entity coldEntity = data.coldWorld.CreateEntity();
        SetFanoutComponents(data.coldWorld, coldEntity, entityIndex, std::make_index_sequence<8>{});
        SetFanoutColdComponent(data.coldWorld, coldEntity, entityIndex % 8U);
    }

    return data;
}

template <typename... Components, std::size_t... Indices>
[[nodiscard]] double QueryFanoutBatchChecksum(const kb::ecs::QueryBatch<Components...>& batch, std::size_t row, std::index_sequence<Indices...>) noexcept {
    return (static_cast<double>(batch.template Components<Indices>()[row].value) + ...);
}

template <typename... Components>
void VisitQueryFanoutBatch(const kb::ecs::QueryBatch<Components...>& batch, void* context) {
    auto* fanoutContext = static_cast<QueryFanoutContext*>(context);
    for (std::size_t row = 0; row < batch.Count(); ++row) {
        fanoutContext->checksum += QueryFanoutBatchChecksum(batch, row, std::index_sequence_for<Components...>{});
    }
    fanoutContext->visited += batch.Count();
}

template <typename... Components>
[[nodiscard]] BenchmarkResult RunQueryFanoutBenchmark(
    kb::ecs::World& world,
    const BenchmarkOptions& options,
    std::string_view layoutName,
    std::size_t fanoutComponentCount,
    std::size_t entityCount) {
    kb::ecs::Query<Components...> query = world.CreateQuery<Components...>();
    if (!query.IsValid()) {
        throw std::runtime_error("Failed to create query fanout benchmark query");
    }

    return RunTimedBenchmark(
        "query_fanout_" + std::string{ layoutName } + "_" + std::to_string(fanoutComponentCount) + "_components",
        std::string{ layoutName } + " archetypes, " + std::to_string(fanoutComponentCount) + " query components",
        options,
        entityCount,
        [&query, &options, entityCount]() {
            QueryFanoutContext context;
            query.ForEachBatch(kb::ecs::QueryExecutionSettings{ .maxBatchSize = options.executionGrainSize }, &VisitQueryFanoutBatch<Components...>, &context);
            if (context.visited != entityCount) {
                throw std::runtime_error("Query fanout benchmark visited an unexpected number of entities");
            }
            if (options.debugValidationEnabled && context.checksum == 0.0) {
                throw std::runtime_error("Debug validation detected an empty query fanout checksum");
            }
            return context.checksum;
        });
}

template <std::size_t... Indices>
void RegisterSparseArchetypeComponents(kb::ecs::World& world, std::index_sequence<Indices...>) {
    const std::array<kb::ecs::ComponentId, sizeof...(Indices)> componentIds{
        world.RegisterComponent<BenchSparseArchetypeComponent<Indices>>("BenchSparseArchetypeComponent" + std::to_string(Indices))...
    };
    if (std::ranges::any_of(componentIds, [](kb::ecs::ComponentId componentId) { return componentId == 0; })) {
        throw std::runtime_error("Failed to register sparse query archetype components");
    }
}

void RegisterSparseQueryComponents(kb::ecs::World& world) {
    const kb::ecs::ComponentId matchComponent = world.RegisterComponent<BenchSparseMatch>("BenchSparseMatch");
    const kb::ecs::ComponentId payloadComponent = world.RegisterComponent<BenchSparsePayload>("BenchSparsePayload");
    if (matchComponent == 0 || payloadComponent == 0) {
        throw std::runtime_error("Failed to register sparse query benchmark components");
    }
    RegisterSparseArchetypeComponents(world, std::make_index_sequence<16>{});
}

void SetSparseArchetypeComponent(kb::ecs::World& world, kb::ecs::Entity entity, std::size_t componentIndex, std::uint32_t value) {
    switch (componentIndex) {
    case 0:
        world.Set(entity, BenchSparseArchetypeComponent<0>{ .value = value });
        break;
    case 1:
        world.Set(entity, BenchSparseArchetypeComponent<1>{ .value = value });
        break;
    case 2:
        world.Set(entity, BenchSparseArchetypeComponent<2>{ .value = value });
        break;
    case 3:
        world.Set(entity, BenchSparseArchetypeComponent<3>{ .value = value });
        break;
    case 4:
        world.Set(entity, BenchSparseArchetypeComponent<4>{ .value = value });
        break;
    case 5:
        world.Set(entity, BenchSparseArchetypeComponent<5>{ .value = value });
        break;
    case 6:
        world.Set(entity, BenchSparseArchetypeComponent<6>{ .value = value });
        break;
    case 7:
        world.Set(entity, BenchSparseArchetypeComponent<7>{ .value = value });
        break;
    case 8:
        world.Set(entity, BenchSparseArchetypeComponent<8>{ .value = value });
        break;
    case 9:
        world.Set(entity, BenchSparseArchetypeComponent<9>{ .value = value });
        break;
    case 10:
        world.Set(entity, BenchSparseArchetypeComponent<10>{ .value = value });
        break;
    case 11:
        world.Set(entity, BenchSparseArchetypeComponent<11>{ .value = value });
        break;
    case 12:
        world.Set(entity, BenchSparseArchetypeComponent<12>{ .value = value });
        break;
    case 13:
        world.Set(entity, BenchSparseArchetypeComponent<13>{ .value = value });
        break;
    case 14:
        world.Set(entity, BenchSparseArchetypeComponent<14>{ .value = value });
        break;
    case 15:
        world.Set(entity, BenchSparseArchetypeComponent<15>{ .value = value });
        break;
    default:
        throw std::invalid_argument("Invalid sparse archetype component index");
    }
}

void SetSparseArchetypeSignature(kb::ecs::World& world, kb::ecs::Entity entity, std::uint32_t archetypeMask) {
    for (std::size_t componentIndex = 0; componentIndex < 16U; ++componentIndex) {
        if ((archetypeMask & (1U << componentIndex)) != 0U) {
            SetSparseArchetypeComponent(world, entity, componentIndex, archetypeMask + static_cast<std::uint32_t>(componentIndex));
        }
    }
}

[[nodiscard]] SparseQueryBenchmarkData CreateSparseQueryBenchmarkData(const BenchmarkOptions& options) {
    if (options.sparseArchetypeCount + options.sparseMatchingArchetypeCount > (1U << 16U)) {
        throw std::invalid_argument("sparse archetype count exceeds the supported benchmark signature space");
    }

    kb::ecs::WorldConfig config = kb::ecs::WorldConfigPresets::BenchmarkDefault();
    config.reserveEntities = (options.sparseArchetypeCount + options.sparseMatchingArchetypeCount) * options.sparseEntitiesPerArchetype;
    config.executionGrainSize = options.executionGrainSize;

    SparseQueryBenchmarkData data{
        .world = kb::ecs::World{ config },
        .archetypeCount = options.sparseArchetypeCount,
        .matchingArchetypeCount = options.sparseMatchingArchetypeCount,
        .matchingEntityCount = options.sparseMatchingArchetypeCount * options.sparseEntitiesPerArchetype,
    };
    RegisterSparseQueryComponents(data.world);

    for (std::size_t archetypeIndex = 0; archetypeIndex < options.sparseArchetypeCount; ++archetypeIndex) {
        const std::uint32_t archetypeMask = static_cast<std::uint32_t>(archetypeIndex);
        for (std::size_t row = 0; row < options.sparseEntitiesPerArchetype; ++row) {
            const kb::ecs::Entity entity = data.world.CreateEntity();
            SetSparseArchetypeSignature(data.world, entity, archetypeMask);
        }
    }

    for (std::size_t archetypeIndex = 0; archetypeIndex < options.sparseMatchingArchetypeCount; ++archetypeIndex) {
        const std::uint32_t archetypeMask = static_cast<std::uint32_t>(options.sparseArchetypeCount + archetypeIndex);
        for (std::size_t row = 0; row < options.sparseEntitiesPerArchetype; ++row) {
            const std::size_t entityIndex = archetypeIndex * options.sparseEntitiesPerArchetype + row;
            const kb::ecs::Entity entity = data.world.CreateEntity();
            SetSparseArchetypeSignature(data.world, entity, archetypeMask);
            data.world.Set(entity, BenchSparseMatch{ .value = static_cast<float>(entityIndex % 1024U) * 0.5F });
            data.world.Set(entity, BenchSparsePayload{ .value = static_cast<float>((entityIndex * 7U) % 2048U) * 0.25F });
        }
    }

    return data;
}

void VisitSparseQueryBatch(const kb::ecs::QueryBatch<BenchSparseMatch, BenchSparsePayload>& batch, void* context) {
    auto* sparseContext = static_cast<SparseQueryContext*>(context);
    const BenchSparseMatch* matches = batch.Components<0>();
    const BenchSparsePayload* payloads = batch.Components<1>();
    for (std::size_t row = 0; row < batch.Count(); ++row) {
        sparseContext->checksum += static_cast<double>(matches[row].value) + static_cast<double>(payloads[row].value);
    }
    sparseContext->visited += batch.Count();
}

[[nodiscard]] BenchmarkResult RunSparseQueryBenchmark(SparseQueryBenchmarkData& data, const BenchmarkOptions& options) {
    kb::ecs::Query<BenchSparseMatch, BenchSparsePayload> query = data.world.CreateQuery<BenchSparseMatch, BenchSparsePayload>();
    if (!query.IsValid()) {
        throw std::runtime_error("Failed to create sparse query benchmark query");
    }

    return RunTimedBenchmark(
        "sparse_query_filtering",
        std::to_string(data.archetypeCount) + " non-matching archetypes, " + std::to_string(data.matchingArchetypeCount) + " matching archetypes",
        options,
        data.matchingEntityCount,
        [&query, &data, &options, expectedEntities = data.matchingEntityCount]() {
            SparseQueryContext context;
            query.ForEachBatch(kb::ecs::QueryExecutionSettings{ .maxBatchSize = options.executionGrainSize }, &VisitSparseQueryBatch, &context);
            if (context.visited != expectedEntities) {
                throw std::runtime_error("Sparse query benchmark visited an unexpected number of entities");
            }
            if (options.debugValidationEnabled) {
                ValidateQueryCount<BenchSparseMatch, BenchSparsePayload>(
                    data.world,
                    options,
                    expectedEntities,
                    "sparse matching components");
            }
            return context.checksum;
        });
}

class SystemChainStep final : public kb::ecs::System {
public:
    SystemChainStep(kb::ecs::Entity stateEntity, SystemChainAccess access, float weight) noexcept
        : stateEntity_(stateEntity)
        , access_(access)
        , weight_(weight) {}

    void OnUpdate(kb::ecs::World& world, float deltaSeconds) override {
        BenchSystemChainState* state = world.TryGetMutable<BenchSystemChainState>(stateEntity_);
        if (state == nullptr) {
            throw std::runtime_error("System chain benchmark state component is missing");
        }

        const float input = state->values[access_.readSlot];
        state->values[access_.writeSlot] = input + weight_ + deltaSeconds;
        ++state->writes[access_.writeSlot];
        world.MarkModified<BenchSystemChainState>(stateEntity_);
    }

private:
    kb::ecs::Entity stateEntity_;
    SystemChainAccess access_;
    float weight_ = 0.0F;
};

[[nodiscard]] SystemChainBenchmarkData CreateSystemChainBenchmarkData(std::size_t systemCount) {
    kb::ecs::WorldConfig config = kb::ecs::WorldConfigPresets::BenchmarkDefault();
    config.reserveEntities = 1;

    SystemChainBenchmarkData data{
        .world = kb::ecs::World{ config },
        .scheduler = kb::ecs::SystemScheduler{},
        .stateEntity = {},
        .accessChain = {},
        .systemCount = systemCount,
    };
    const kb::ecs::ComponentId stateComponent = data.world.RegisterComponent<BenchSystemChainState>("BenchSystemChainState");
    if (stateComponent == 0) {
        throw std::runtime_error("Failed to register system chain benchmark component");
    }

    BenchSystemChainState state;
    state.values[0] = 1.0F;
    data.stateEntity = data.world.CreateEntity();
    data.world.Set(data.stateEntity, state);
    data.accessChain.reserve(systemCount);

    for (std::size_t systemIndex = 0; systemIndex < systemCount; ++systemIndex) {
        const SystemChainAccess access{
            .readSlot = systemIndex,
            .writeSlot = (systemIndex + 1U) % systemCount,
        };
        data.accessChain.push_back(access);
        data.scheduler.Add(
            std::make_unique<SystemChainStep>(
                data.stateEntity,
                access,
                0.001F * static_cast<float>(systemIndex + 1U)),
            data.world);
    }

    return data;
}

[[nodiscard]] double RunSystemChainFrame(SystemChainBenchmarkData& data, const BenchmarkOptions& options, std::uint32_t expectedWritesPerSlot) {
    data.scheduler.Update(data.world, options.deltaSeconds);

    const BenchSystemChainState* state = data.world.TryGet<BenchSystemChainState>(data.stateEntity);
    if (state == nullptr) {
        throw std::runtime_error("System chain benchmark state component was lost");
    }
    if (options.debugValidationEnabled) {
        ValidateQueryCount<BenchSystemChainState>(data.world, options, 1, "system chain state");
    }

    double checksum = 0.0;
    for (const SystemChainAccess access : data.accessChain) {
        if (state->writes[access.writeSlot] != expectedWritesPerSlot) {
            throw std::runtime_error("System chain benchmark write dependency count is invalid");
        }
        checksum += static_cast<double>(state->values[access.writeSlot]) + static_cast<double>(state->writes[access.writeSlot]);
    }
    return checksum;
}

[[nodiscard]] BenchmarkResult RunSystemChainBenchmark(const BenchmarkOptions& options, std::size_t systemCount) {
    SystemChainBenchmarkData data = CreateSystemChainBenchmarkData(systemCount);
    std::uint32_t frame = 0;

    BenchmarkResult result = RunTimedBenchmark(
        "system_chain_" + std::to_string(systemCount) + "_systems",
        std::to_string(systemCount) + " sequential systems with read/write dependencies",
        options,
        systemCount,
        [&data, &options, &frame]() {
            ++frame;
            return RunSystemChainFrame(data, options, frame);
        });
    data.scheduler.Shutdown(data.world);
    return result;
}

[[nodiscard]] FrameStats ComputeFrameStats(std::vector<double> frameTimesMs) {
    std::sort(frameTimesMs.begin(), frameTimesMs.end());
    const double total = std::accumulate(frameTimesMs.begin(), frameTimesMs.end(), 0.0);
    const std::size_t p95Index = static_cast<std::size_t>(std::ceil(static_cast<double>(frameTimesMs.size()) * 0.95)) - 1U;
    return FrameStats{
        .minMs = frameTimesMs.front(),
        .avgMs = total / static_cast<double>(frameTimesMs.size()),
        .p95Ms = frameTimesMs[std::min(p95Index, frameTimesMs.size() - 1U)],
    };
}

template <typename FrameFunction>
[[nodiscard]] BenchmarkResult RunTimedBenchmark(
    std::string name,
    std::string dataset,
    const BenchmarkOptions& options,
    std::size_t entities,
    FrameFunction&& frameFunction) {
    using Clock = std::chrono::steady_clock;

    std::vector<double> measuredFramesMs;
    measuredFramesMs.reserve(options.frames);
    double checksum = 0.0;

    const std::size_t totalFrames = options.warmupFrames + options.frames;
    for (std::size_t frame = 0; frame < totalFrames; ++frame) {
        const auto start = Clock::now();
        checksum += frameFunction();
        const auto end = Clock::now();

        if (frame >= options.warmupFrames) {
            const std::chrono::duration<double, std::milli> elapsed = end - start;
            measuredFramesMs.push_back(elapsed.count());
        }
    }

    const FrameStats stats = ComputeFrameStats(std::move(measuredFramesMs));
    const double secondsPerFrame = stats.avgMs / 1000.0;
    return BenchmarkResult{
        .name = BenchmarkNameForMode(std::move(name), options),
        .dataset = BenchmarkDatasetForMode(std::move(dataset), options),
        .validationMode = std::string{ ValidationModeName(options.debugValidationEnabled) },
        .entities = entities,
        .frames = options.frames,
        .warmupFrames = options.warmupFrames,
        .executionGrainSize = options.executionGrainSize,
        .frameTime = stats,
        .throughputEntitiesPerSecond = secondsPerFrame == 0.0 ? 0.0 : static_cast<double>(entities) / secondsPerFrame,
        .checksum = checksum,
    };
}

[[nodiscard]] BenchmarkResult RunPrefabSpawnBenchmark(
    const BenchmarkOptions& options,
    std::size_t instanceCount,
    std::size_t nodeCount) {
    using Clock = std::chrono::steady_clock;

    std::vector<double> measuredFramesMs;
    measuredFramesMs.reserve(options.frames);
    double checksum = 0.0;

    const std::size_t totalFrames = options.warmupFrames + options.frames;
    for (std::size_t frame = 0; frame < totalFrames; ++frame) {
        PrefabSpawnBenchmarkData data = CreatePrefabSpawnBenchmarkData(instanceCount, nodeCount);

        const auto start = Clock::now();
        checksum += RunPrefabSpawnFrame(data);
        if (options.debugValidationEnabled) {
            const std::size_t expectedEntityCount = instanceCount * nodeCount;
            if (data.scene->Entities().Count() != expectedEntityCount) {
                throw std::runtime_error("Debug validation detected an invalid prefab spawn entity count");
            }
        }
        const auto end = Clock::now();

        if (frame >= options.warmupFrames) {
            const std::chrono::duration<double, std::milli> elapsed = end - start;
            measuredFramesMs.push_back(elapsed.count());
        }
    }

    const std::size_t spawnedObjects = instanceCount * nodeCount;
    const FrameStats stats = ComputeFrameStats(std::move(measuredFramesMs));
    const double secondsPerFrame = stats.avgMs / 1000.0;
    return BenchmarkResult{
        .name = BenchmarkNameForMode("prefab_spawn_" + std::to_string(instanceCount) + "_instances_" + std::to_string(nodeCount) + "_nodes", options),
        .dataset = BenchmarkDatasetForMode(
            std::to_string(instanceCount) + " prefab instances, " + std::to_string(nodeCount) + " hierarchy nodes each",
            options),
        .validationMode = std::string{ ValidationModeName(options.debugValidationEnabled) },
        .entities = spawnedObjects,
        .frames = options.frames,
        .warmupFrames = options.warmupFrames,
        .executionGrainSize = options.executionGrainSize,
        .frameTime = stats,
        .throughputEntitiesPerSecond = secondsPerFrame == 0.0 ? 0.0 : static_cast<double>(spawnedObjects) / secondsPerFrame,
        .checksum = checksum,
    };
}

[[nodiscard]] std::vector<BenchmarkResult> RunBenchmarks(const BenchmarkOptions& options) {
    kb::ecs::WorldConfig config = kb::ecs::WorldConfigPresets::BenchmarkDefault();
    config.reserveEntities = options.entityCount;
    config.executionGrainSize = options.executionGrainSize;

    kb::ecs::World world{ config };
    PopulateWorld(world, options.entityCount);
    ValidateDenseBenchmarkWorld(world, options);

    kb::ecs::Query<BenchPosition, BenchVelocity> query = world.CreateQuery<BenchPosition, BenchVelocity>();
    if (!query.IsValid()) {
        throw std::runtime_error("Failed to create Position+Velocity query");
    }

    std::vector<BenchmarkResult> results;
    results.push_back(RunTimedBenchmark(
        "position_velocity_batch_read",
        "Position+Velocity dense archetype",
        options,
        options.entityCount,
        [&query, &options]() {
            BatchReadContext context;
            query.ForEachBatch(kb::ecs::QueryExecutionSettings{ .maxBatchSize = options.executionGrainSize }, &VisitBatchRead, &context);
            if (context.visited != options.entityCount) {
                throw std::runtime_error("Batch read benchmark visited an unexpected number of entities");
            }
            if (options.debugValidationEnabled && context.checksum == 0.0) {
                throw std::runtime_error("Debug validation detected an empty batch read checksum");
            }
            return context.checksum;
        }));

    results.push_back(RunTimedBenchmark(
        "position_velocity_linear_update",
        "Position+Velocity dense archetype",
        options,
        options.entityCount,
        [&world, &options]() {
            MutableUpdateContext context{
                .world = &world,
                .deltaSeconds = options.deltaSeconds,
            };
            world.ForEachMutable<BenchPosition>(&VisitMutableUpdate, &context);
            if (context.visited != options.entityCount) {
                throw std::runtime_error("Mutable update benchmark visited an unexpected number of entities");
            }
            ValidateDenseBenchmarkWorld(world, options);
            return context.checksum;
        }));

    results.push_back(RunTimedBenchmark(
        "transform_local_to_world_no_hierarchy",
        "LocalTransform+WorldTransform dense archetype",
        options,
        options.entityCount,
        [&world, &options]() {
            TransformUpdateContext context{
                .world = &world,
            };
            world.ForEachMutable<BenchWorldTransform>(&VisitTransformUpdate, &context);
            if (context.visited != options.entityCount) {
                throw std::runtime_error("Transform benchmark visited an unexpected number of entities");
            }
            ValidateDenseBenchmarkWorld(world, options);
            return context.checksum;
        }));

    for (const std::size_t hierarchyDepth : { 8U, 32U, 128U }) {
        HierarchyBenchmarkData hierarchyData = CreateHierarchyBenchmarkData(options, hierarchyDepth);
        std::size_t frame = 0;
        results.push_back(RunTimedBenchmark(
            "transform_hierarchy_dirty_depth_" + std::to_string(hierarchyDepth),
            "Hierarchy forest depth " + std::to_string(hierarchyDepth),
            options,
            hierarchyData.entities.size(),
            [&hierarchyData, &options, &frame]() {
                const HierarchyFrameStats stats = PropagateHierarchyTransforms(hierarchyData, frame);
                ++frame;
                if (stats.updated != options.hierarchyEntityCount) {
                    throw std::runtime_error("Hierarchy benchmark did not update all dirty descendants");
                }
                if (options.debugValidationEnabled) {
                    ValidateQueryCount<BenchLocalTransform, BenchWorldTransform, BenchHierarchyNode>(
                        hierarchyData.world,
                        options,
                        options.hierarchyEntityCount,
                        "hierarchy transform components");
                }
                return stats.checksum;
            }));
    }

    StructuralBenchmarkData structuralData = CreateStructuralBenchmarkData(options);
    StructuralCommandBuffer structuralCommandBuffer{ options.structuralChangesPerFrame };
    std::vector<kb::ecs::Entity> structuralCreatedEntities;
    structuralCreatedEntities.reserve(structuralData.operationCount);
    std::size_t structuralFrame = 0;
    results.push_back(RunTimedBenchmark(
        "structural_changes_command_buffer",
        std::to_string(options.structuralChangesPerFrame) + " create/destroy/add/remove component commands per frame",
        options,
        options.structuralChangesPerFrame,
        [&structuralData, &structuralCommandBuffer, &structuralCreatedEntities, &structuralFrame, &options]() {
            const double checksum = RunStructuralChangesFrame(
                structuralData,
                structuralCommandBuffer,
                structuralCreatedEntities,
                structuralFrame);
            ++structuralFrame;
            if (options.debugValidationEnabled) {
                ValidateQueryCount<BenchPosition, BenchVelocity>(
                    structuralData.world,
                    options,
                    structuralData.operationCount * 3U,
                    "structural Position+Velocity components");
                ValidateQueryCount<BenchStructuralMarker>(
                    structuralData.world,
                    options,
                    structuralData.operationCount,
                    "structural marker components");
            }
            return checksum;
        }));

    for (const std::size_t instanceCount : options.prefabSpawnInstanceCounts) {
        for (const std::size_t nodeCount : options.prefabHierarchyNodeCounts) {
            results.push_back(RunPrefabSpawnBenchmark(options, instanceCount, nodeCount));
        }
    }

    QueryFanoutBenchmarkData fanoutData = CreateQueryFanoutBenchmarkData(options);
    results.push_back(RunQueryFanoutBenchmark<BenchFanoutComponent<0>>(
        fanoutData.hotWorld,
        options,
        "hot",
        1,
        fanoutData.entityCount));
    results.push_back(RunQueryFanoutBenchmark<BenchFanoutComponent<0>, BenchFanoutComponent<1>>(
        fanoutData.hotWorld,
        options,
        "hot",
        2,
        fanoutData.entityCount));
    results.push_back(RunQueryFanoutBenchmark<BenchFanoutComponent<0>, BenchFanoutComponent<1>, BenchFanoutComponent<2>, BenchFanoutComponent<3>>(
        fanoutData.hotWorld,
        options,
        "hot",
        4,
        fanoutData.entityCount));
    results.push_back(RunQueryFanoutBenchmark<
        BenchFanoutComponent<0>,
        BenchFanoutComponent<1>,
        BenchFanoutComponent<2>,
        BenchFanoutComponent<3>,
        BenchFanoutComponent<4>,
        BenchFanoutComponent<5>,
        BenchFanoutComponent<6>,
        BenchFanoutComponent<7>>(
        fanoutData.hotWorld,
        options,
        "hot",
        8,
        fanoutData.entityCount));

    results.push_back(RunQueryFanoutBenchmark<BenchFanoutComponent<0>>(
        fanoutData.coldWorld,
        options,
        "cold",
        1,
        fanoutData.entityCount));
    results.push_back(RunQueryFanoutBenchmark<BenchFanoutComponent<0>, BenchFanoutComponent<1>>(
        fanoutData.coldWorld,
        options,
        "cold",
        2,
        fanoutData.entityCount));
    results.push_back(RunQueryFanoutBenchmark<BenchFanoutComponent<0>, BenchFanoutComponent<1>, BenchFanoutComponent<2>, BenchFanoutComponent<3>>(
        fanoutData.coldWorld,
        options,
        "cold",
        4,
        fanoutData.entityCount));
    results.push_back(RunQueryFanoutBenchmark<
        BenchFanoutComponent<0>,
        BenchFanoutComponent<1>,
        BenchFanoutComponent<2>,
        BenchFanoutComponent<3>,
        BenchFanoutComponent<4>,
        BenchFanoutComponent<5>,
        BenchFanoutComponent<6>,
        BenchFanoutComponent<7>>(
        fanoutData.coldWorld,
        options,
        "cold",
        8,
        fanoutData.entityCount));

    SparseQueryBenchmarkData sparseData = CreateSparseQueryBenchmarkData(options);
    results.push_back(RunSparseQueryBenchmark(sparseData, options));

    for (const std::size_t systemCount : options.systemChainCounts) {
        results.push_back(RunSystemChainBenchmark(options, systemCount));
    }

    return results;
}

[[nodiscard]] std::vector<BenchmarkResult> RunSelectedBenchmarks(const BenchmarkOptions& options) {
    std::vector<BenchmarkResult> results;
    const auto appendMode = [&](bool debugValidationEnabled) {
        BenchmarkOptions modeOptions = options;
        modeOptions.debugValidationEnabled = debugValidationEnabled;
        std::vector<BenchmarkResult> modeResults = RunBenchmarks(modeOptions);
        results.insert(results.end(), std::make_move_iterator(modeResults.begin()), std::make_move_iterator(modeResults.end()));
    };

    switch (options.validationSelection) {
    case BenchmarkValidationSelection::Off:
        appendMode(false);
        break;
    case BenchmarkValidationSelection::Debug:
        appendMode(true);
        break;
    case BenchmarkValidationSelection::Both:
        appendMode(false);
        appendMode(true);
        break;
    }

    return results;
}

[[nodiscard]] BenchmarkRunData MakeBenchmarkRunData(std::vector<BenchmarkResult> results) {
    return BenchmarkRunData{
        .commit = KB_ECS_BENCHMARK_GIT_COMMIT,
        .branch = KB_ECS_BENCHMARK_GIT_BRANCH,
        .buildConfig = KB_ECS_BENCHMARK_BUILD_CONFIG,
        .cpu = CpuBrandString(),
        .threadCount = std::max(1U, std::thread::hardware_concurrency()),
        .results = std::move(results),
    };
}

void WriteRunJson(const std::filesystem::path& path, const BenchmarkRunData& run) {
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    std::ofstream output{ path, std::ios::trunc };
    if (!output) {
        throw std::runtime_error("Failed to open benchmark output JSON");
    }

    output << std::fixed << std::setprecision(6);
    output << "{\n";
    output << "  \"commit\": \"" << JsonEscape(run.commit) << "\",\n";
    output << "  \"branch\": \"" << JsonEscape(run.branch) << "\",\n";
    output << "  \"build_config\": \"" << JsonEscape(run.buildConfig) << "\",\n";
    output << "  \"cpu\": \"" << JsonEscape(run.cpu) << "\",\n";
    output << "  \"thread_count\": " << run.threadCount << ",\n";
    output << "  \"results\": [\n";
    for (std::size_t index = 0; index < run.results.size(); ++index) {
        const BenchmarkResult& result = run.results[index];
        output << "    {\n";
        output << "      \"name\": \"" << JsonEscape(result.name) << "\",\n";
        output << "      \"dataset\": \"" << JsonEscape(result.dataset) << "\",\n";
        output << "      \"validation_mode\": \"" << JsonEscape(result.validationMode) << "\",\n";
        output << "      \"entities\": " << result.entities << ",\n";
        output << "      \"frames\": " << result.frames << ",\n";
        output << "      \"warmup_frames\": " << result.warmupFrames << ",\n";
        output << "      \"execution_grain_size\": " << result.executionGrainSize << ",\n";
        output << "      \"time_ms_min\": " << result.frameTime.minMs << ",\n";
        output << "      \"time_ms_avg\": " << result.frameTime.avgMs << ",\n";
        output << "      \"time_ms_p95\": " << result.frameTime.p95Ms << ",\n";
        output << "      \"throughput_entities_per_second\": " << result.throughputEntitiesPerSecond << ",\n";
        output << "      \"checksum\": " << result.checksum << "\n";
        output << "    }" << (index + 1U == run.results.size() ? "\n" : ",\n");
    }
    output << "  ]\n";
    output << "}\n";
}

[[nodiscard]] BenchmarkRunData LoadBenchmarkRun(const std::filesystem::path& path) {
    std::ifstream input{ path };
    if (!input) {
        throw std::runtime_error("Failed to open benchmark JSON: " + path.string());
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    BenchmarkJsonReader reader{ buffer.str() };
    BenchmarkRunData run = reader.Parse();
    if (run.results.empty()) {
        throw std::runtime_error("Benchmark JSON has no results: " + path.string());
    }
    return run;
}

[[nodiscard]] std::string ResultKey(const BenchmarkResult& result) {
    return result.name + '\n' + result.dataset + '\n' + result.validationMode;
}

[[nodiscard]] double Ratio(double before, double after) {
    if (before == 0.0) {
        return after == 0.0 ? 1.0 : std::numeric_limits<double>::infinity();
    }
    return after / before;
}

[[nodiscard]] bool HasSameProfile(const BenchmarkResult& before, const BenchmarkResult& after) noexcept {
    return before.validationMode == after.validationMode && before.entities == after.entities && before.frames == after.frames && before.warmupFrames == after.warmupFrames
        && before.executionGrainSize == after.executionGrainSize;
}

[[nodiscard]] bool HasSameRunProfile(const BenchmarkRunData& before, const BenchmarkRunData& after) noexcept {
    return before.buildConfig == after.buildConfig && before.cpu == after.cpu && before.threadCount == after.threadCount;
}

void AddTimeRegression(
    BenchmarkComparison& comparison,
    const BenchmarkComparisonEntry& entry,
    std::string_view metric,
    double before,
    double after,
    double ratio,
    double limitRatio) {
    if (ratio > limitRatio) {
        comparison.regressions.push_back(BenchmarkRegressionFailure{
            .benchmarkName = entry.before.name,
            .dataset = entry.before.dataset,
            .metric = std::string{ metric },
            .before = before,
            .after = after,
            .ratio = ratio,
            .limitRatio = limitRatio,
        });
    }
}

void AddThroughputRegression(BenchmarkComparison& comparison, const BenchmarkComparisonEntry& entry, double limitRatio) {
    if (entry.throughputRatio < limitRatio) {
        comparison.regressions.push_back(BenchmarkRegressionFailure{
            .benchmarkName = entry.before.name,
            .dataset = entry.before.dataset,
            .metric = "throughput_entities_per_second",
            .before = entry.before.throughputEntitiesPerSecond,
            .after = entry.after.throughputEntitiesPerSecond,
            .ratio = entry.throughputRatio,
            .limitRatio = limitRatio,
        });
    }
}

void EvaluateRegressions(BenchmarkComparison& comparison) {
    const double threshold = comparison.regressionThresholdPercent / 100.0;
    const double timeLimitRatio = 1.0 + threshold;
    const double throughputLimitRatio = 1.0 - threshold;

    for (const BenchmarkComparisonEntry& entry : comparison.entries) {
        if (!entry.sameProfile) {
            continue;
        }

        AddTimeRegression(
            comparison,
            entry,
            "time_ms_avg",
            entry.before.frameTime.avgMs,
            entry.after.frameTime.avgMs,
            entry.avgTimeRatio,
            timeLimitRatio);
        AddTimeRegression(
            comparison,
            entry,
            "time_ms_p95",
            entry.before.frameTime.p95Ms,
            entry.after.frameTime.p95Ms,
            entry.p95TimeRatio,
            timeLimitRatio);
        AddThroughputRegression(comparison, entry, throughputLimitRatio);
    }
}

[[nodiscard]] BenchmarkComparison CompareBenchmarkRuns(
    const BenchmarkRunData& before,
    const BenchmarkRunData& after,
    std::string beforeLabel,
    std::string afterLabel,
    double regressionThresholdPercent,
    bool regressionGateEnabled) {
    BenchmarkComparison comparison{
        .beforeLabel = std::move(beforeLabel),
        .afterLabel = std::move(afterLabel),
        .regressionThresholdPercent = regressionThresholdPercent,
        .regressionGateEnabled = regressionGateEnabled,
    };
    const bool sameRunProfile = HasSameRunProfile(before, after);

    std::unordered_map<std::string, const BenchmarkResult*> afterByKey;
    afterByKey.reserve(after.results.size());
    for (const BenchmarkResult& result : after.results) {
        afterByKey.emplace(ResultKey(result), &result);
    }

    std::unordered_map<std::string, const BenchmarkResult*> beforeByKey;
    beforeByKey.reserve(before.results.size());
    for (const BenchmarkResult& result : before.results) {
        const std::string key = ResultKey(result);
        beforeByKey.emplace(key, &result);

        const auto afterIt = afterByKey.find(key);
        if (afterIt == afterByKey.end()) {
            comparison.missingAfter.push_back(result.name + " [" + result.dataset + "]");
            continue;
        }

        const BenchmarkResult& afterResult = *afterIt->second;
        comparison.entries.push_back(BenchmarkComparisonEntry{
            .before = result,
            .after = afterResult,
            .avgTimeRatio = Ratio(result.frameTime.avgMs, afterResult.frameTime.avgMs),
            .p95TimeRatio = Ratio(result.frameTime.p95Ms, afterResult.frameTime.p95Ms),
            .throughputRatio = Ratio(result.throughputEntitiesPerSecond, afterResult.throughputEntitiesPerSecond),
            .sameProfile = sameRunProfile && HasSameProfile(result, afterResult),
        });
    }

    for (const BenchmarkResult& result : after.results) {
        if (!beforeByKey.contains(ResultKey(result))) {
            comparison.missingBefore.push_back(result.name + " [" + result.dataset + "]");
        }
    }

    if (regressionGateEnabled) {
        EvaluateRegressions(comparison);
    }
    return comparison;
}

void WriteStringArray(std::ofstream& output, std::string_view name, std::span<const std::string> values, bool trailingComma) {
    output << "  \"" << name << "\": [";
    for (std::size_t index = 0; index < values.size(); ++index) {
        output << (index == 0 ? "" : ", ") << '"' << JsonEscape(values[index]) << '"';
    }
    output << ']' << (trailingComma ? "," : "") << '\n';
}

void WriteJsonNumber(std::ofstream& output, double value) {
    if (std::isfinite(value)) {
        output << value;
    } else {
        output << "null";
    }
}

void WriteComparisonJson(const std::filesystem::path& path, const BenchmarkComparison& comparison) {
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    std::ofstream output{ path, std::ios::trunc };
    if (!output) {
        throw std::runtime_error("Failed to open benchmark comparison JSON");
    }

    output << std::fixed << std::setprecision(6);
    output << "{\n";
    output << "  \"before\": \"" << JsonEscape(comparison.beforeLabel) << "\",\n";
    output << "  \"after\": \"" << JsonEscape(comparison.afterLabel) << "\",\n";
    output << "  \"regression_gate_enabled\": " << (comparison.regressionGateEnabled ? "true" : "false") << ",\n";
    output << "  \"regression_threshold_percent\": " << comparison.regressionThresholdPercent << ",\n";
    output << "  \"results\": [\n";
    for (std::size_t index = 0; index < comparison.entries.size(); ++index) {
        const BenchmarkComparisonEntry& entry = comparison.entries[index];
        output << "    {\n";
        output << "      \"name\": \"" << JsonEscape(entry.before.name) << "\",\n";
        output << "      \"dataset\": \"" << JsonEscape(entry.before.dataset) << "\",\n";
        output << "      \"validation_mode\": \"" << JsonEscape(entry.before.validationMode) << "\",\n";
        output << "      \"same_profile\": " << (entry.sameProfile ? "true" : "false") << ",\n";
        output << "      \"entities\": " << entry.before.entities << ",\n";
        output << "      \"frames\": " << entry.before.frames << ",\n";
        output << "      \"warmup_frames\": " << entry.before.warmupFrames << ",\n";
        output << "      \"execution_grain_size\": " << entry.before.executionGrainSize << ",\n";
        output << "      \"before_time_ms_avg\": " << entry.before.frameTime.avgMs << ",\n";
        output << "      \"after_time_ms_avg\": " << entry.after.frameTime.avgMs << ",\n";
        output << "      \"avg_time_ratio\": ";
        WriteJsonNumber(output, entry.avgTimeRatio);
        output << ",\n";
        output << "      \"before_time_ms_p95\": " << entry.before.frameTime.p95Ms << ",\n";
        output << "      \"after_time_ms_p95\": " << entry.after.frameTime.p95Ms << ",\n";
        output << "      \"p95_time_ratio\": ";
        WriteJsonNumber(output, entry.p95TimeRatio);
        output << ",\n";
        output << "      \"before_throughput_entities_per_second\": " << entry.before.throughputEntitiesPerSecond << ",\n";
        output << "      \"after_throughput_entities_per_second\": " << entry.after.throughputEntitiesPerSecond << ",\n";
        output << "      \"throughput_ratio\": ";
        WriteJsonNumber(output, entry.throughputRatio);
        output << "\n";
        output << "    }" << (index + 1U == comparison.entries.size() ? "\n" : ",\n");
    }
    output << "  ],\n";
    output << "  \"regressions\": [\n";
    for (std::size_t index = 0; index < comparison.regressions.size(); ++index) {
        const BenchmarkRegressionFailure& regression = comparison.regressions[index];
        output << "    {\n";
        output << "      \"name\": \"" << JsonEscape(regression.benchmarkName) << "\",\n";
        output << "      \"dataset\": \"" << JsonEscape(regression.dataset) << "\",\n";
        output << "      \"metric\": \"" << JsonEscape(regression.metric) << "\",\n";
        output << "      \"before\": " << regression.before << ",\n";
        output << "      \"after\": " << regression.after << ",\n";
        output << "      \"ratio\": ";
        WriteJsonNumber(output, regression.ratio);
        output << ",\n";
        output << "      \"limit_ratio\": ";
        WriteJsonNumber(output, regression.limitRatio);
        output << "\n";
        output << "    }" << (index + 1U == comparison.regressions.size() ? "\n" : ",\n");
    }
    output << "  ],\n";
    WriteStringArray(output, "missing_after", comparison.missingAfter, true);
    WriteStringArray(output, "missing_before", comparison.missingBefore, false);
    output << "}\n";
}

void PrintResults(const std::filesystem::path& outputPath, std::span<const BenchmarkResult> results) {
    for (const BenchmarkResult& result : results) {
        std::cout << result.name << ": avg " << result.frameTime.avgMs << " ms, p95 " << result.frameTime.p95Ms
                  << " ms, throughput " << result.throughputEntitiesPerSecond << " entities/s\n";
    }
    std::cout << "Wrote " << outputPath.string() << '\n';
}

void PrintComparison(const std::filesystem::path& outputPath, const BenchmarkComparison& comparison) {
    for (const BenchmarkComparisonEntry& entry : comparison.entries) {
        std::cout << entry.before.name << " [" << entry.before.dataset << "]: avg x" << entry.avgTimeRatio
                  << ", p95 x" << entry.p95TimeRatio << ", throughput x" << entry.throughputRatio << '\n';
    }
    if (!comparison.missingAfter.empty()) {
        std::cout << "Missing after: " << comparison.missingAfter.size() << '\n';
    }
    if (!comparison.missingBefore.empty()) {
        std::cout << "Missing before: " << comparison.missingBefore.size() << '\n';
    }
    if (!comparison.regressions.empty()) {
        std::cout << "Regression failures: " << comparison.regressions.size() << '\n';
        for (const BenchmarkRegressionFailure& regression : comparison.regressions) {
            std::cout << regression.benchmarkName << " [" << regression.dataset << "] " << regression.metric
                      << ": ratio x" << regression.ratio << ", limit x" << regression.limitRatio << '\n';
        }
    }
    std::cout << "Wrote " << outputPath.string() << '\n';
}

} // namespace

int main(int argc, char** argv) {
    try {
        const BenchmarkOptions options = ParseOptions(argc, argv);
        if (!options.compareBeforePath.empty()) {
            const BenchmarkRunData before = LoadBenchmarkRun(options.compareBeforePath);
            const BenchmarkRunData after = LoadBenchmarkRun(options.compareAfterPath);
            const BenchmarkComparison comparison = CompareBenchmarkRuns(
                before,
                after,
                options.compareBeforePath.string(),
                options.compareAfterPath.string(),
                options.regressionThresholdPercent,
                options.failOnRegression);
            WriteComparisonJson(options.comparisonOutputPath, comparison);
            PrintComparison(options.comparisonOutputPath, comparison);
            return comparison.regressions.empty() ? 0 : 2;
        }

        BenchmarkRunData run = MakeBenchmarkRunData(RunSelectedBenchmarks(options));
        WriteRunJson(options.outputPath, run);
        PrintResults(options.outputPath, run.results);

        if (!options.saveBaselinePath.empty()) {
            WriteRunJson(options.saveBaselinePath, run);
            std::cout << "Saved baseline " << options.saveBaselinePath.string() << '\n';
        }

        if (!options.compareBaselinePath.empty()) {
            const BenchmarkRunData baseline = LoadBenchmarkRun(options.compareBaselinePath);
            const BenchmarkComparison comparison = CompareBenchmarkRuns(
                baseline,
                run,
                options.compareBaselinePath.string(),
                options.outputPath.string(),
                options.regressionThresholdPercent,
                options.failOnRegression);
            WriteComparisonJson(options.comparisonOutputPath, comparison);
            PrintComparison(options.comparisonOutputPath, comparison);
            if (!comparison.regressions.empty()) {
                return 2;
            }
        }
    } catch (const std::exception& exception) {
        std::cerr << "kb_ecs_benchmarks: " << exception.what() << '\n';
        return 1;
    }

    return 0;
}
