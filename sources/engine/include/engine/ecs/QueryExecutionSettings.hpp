#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace kb::ecs {

class WorkerPool;

inline constexpr std::size_t kDefaultQueryExecutionGrainSize = 256;

enum class QueryIterationOrder {
    StorageOrder,
    Deterministic,
    ChunkOrder,
};

enum class QueryExecutionPolicy {
    SingleThread,
    ParallelChunks,
    ParallelRanges,
    SIMDPreferred,
    Deterministic,
};

enum class QueryExecutionPolicyParseError : std::uint8_t {
    None,
    Empty,
    UnsupportedValue,
};

struct QueryExecutionPolicyParseResult {
    QueryExecutionPolicy policy = QueryExecutionPolicy::ParallelChunks;
    QueryExecutionPolicyParseError error = QueryExecutionPolicyParseError::None;
    std::string_view diagnostic;

    [[nodiscard]] constexpr bool HasValue() const noexcept {
        return error == QueryExecutionPolicyParseError::None;
    }
};

enum class QueryReductionMode {
    None,
    PerWorker,
    Deterministic,
};

struct QueryExecutionSettings {
    std::size_t maxBatchSize = 0;
    QueryIterationOrder iterationOrder = QueryIterationOrder::StorageOrder;
    QueryExecutionPolicy policy = QueryExecutionPolicy::ParallelChunks;
    QueryReductionMode reductionMode = QueryReductionMode::None;
    std::size_t prefetchDistance = 0;
    std::size_t workerCountOverride = 0;
    WorkerPool* workerPool = nullptr;
    bool telemetryEnabled = false;
};

struct QueryWorkerContext {
    std::size_t workerIndex = 0;
    std::size_t workerCount = 1;
    bool active = false;
};

[[nodiscard]] QueryWorkerContext CurrentQueryWorkerContext() noexcept;

[[nodiscard]] constexpr char NormalizeQueryExecutionPolicyCharacter(char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

[[nodiscard]] constexpr bool QueryExecutionPolicyTokenEquals(std::string_view value, std::string_view expected) noexcept {
    if (value.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (NormalizeQueryExecutionPolicyCharacter(value[index]) != NormalizeQueryExecutionPolicyCharacter(expected[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr std::string_view QueryExecutionPolicyName(QueryExecutionPolicy policy) noexcept {
    switch (policy) {
    case QueryExecutionPolicy::SingleThread:
        return "single_thread";
    case QueryExecutionPolicy::ParallelChunks:
        return "parallel_chunks";
    case QueryExecutionPolicy::ParallelRanges:
        return "parallel_ranges";
    case QueryExecutionPolicy::SIMDPreferred:
        return "simd_preferred";
    case QueryExecutionPolicy::Deterministic:
        return "deterministic";
    }
    return "parallel_chunks";
}

[[nodiscard]] constexpr QueryExecutionPolicyParseResult ParseQueryExecutionPolicyWithDiagnostics(std::string_view value) noexcept {
    if (value.empty()) {
        return QueryExecutionPolicyParseResult{
            .error = QueryExecutionPolicyParseError::Empty,
            .diagnostic = "query execution policy is empty; expected one of single_thread, parallel_chunks, parallel_ranges, simd_preferred, deterministic",
        };
    }
    if (QueryExecutionPolicyTokenEquals(value, "single") ||
        QueryExecutionPolicyTokenEquals(value, "single_thread") ||
        QueryExecutionPolicyTokenEquals(value, "single-thread")) {
        return QueryExecutionPolicyParseResult{ .policy = QueryExecutionPolicy::SingleThread };
    }
    if (QueryExecutionPolicyTokenEquals(value, "parallel") ||
        QueryExecutionPolicyTokenEquals(value, "parallel_chunks") ||
        QueryExecutionPolicyTokenEquals(value, "parallel-chunks")) {
        return QueryExecutionPolicyParseResult{ .policy = QueryExecutionPolicy::ParallelChunks };
    }
    if (QueryExecutionPolicyTokenEquals(value, "parallel_ranges") ||
        QueryExecutionPolicyTokenEquals(value, "parallel-ranges")) {
        return QueryExecutionPolicyParseResult{ .policy = QueryExecutionPolicy::ParallelRanges };
    }
    if (QueryExecutionPolicyTokenEquals(value, "simd") ||
        QueryExecutionPolicyTokenEquals(value, "simd_preferred") ||
        QueryExecutionPolicyTokenEquals(value, "simd-preferred")) {
        return QueryExecutionPolicyParseResult{ .policy = QueryExecutionPolicy::SIMDPreferred };
    }
    if (QueryExecutionPolicyTokenEquals(value, "deterministic")) {
        return QueryExecutionPolicyParseResult{ .policy = QueryExecutionPolicy::Deterministic };
    }
    return QueryExecutionPolicyParseResult{
        .error = QueryExecutionPolicyParseError::UnsupportedValue,
        .diagnostic = "unsupported query execution policy; expected one of single_thread, parallel_chunks, parallel_ranges, simd_preferred, deterministic",
    };
}

[[nodiscard]] constexpr std::optional<QueryExecutionPolicy> ParseQueryExecutionPolicy(std::string_view value) noexcept {
    const QueryExecutionPolicyParseResult result = ParseQueryExecutionPolicyWithDiagnostics(value);
    if (!result.HasValue()) {
        return std::nullopt;
    }
    return result.policy;
}

} // namespace kb::ecs
