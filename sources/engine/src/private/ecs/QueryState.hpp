#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/Entity.hpp"
#include "engine/ecs/Query.hpp"
#include "engine/ecs/QueryExecutionScratch.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>

namespace kb::ecs {

class NativeArchetypeStorage;
class MutableComponentBorrowLocks;
class QueryPlan;
class QueryBatchExecutionScratch;
class StructuralChangeValidator;
struct WorldTelemetryCounters;

class QueryState {
public:
    QueryState(
        NativeArchetypeStorage* nativeStorage,
        std::shared_ptr<QueryPlan> plan,
        std::size_t defaultExecutionGrainSize,
        MutableComponentBorrowLocks* mutableBorrowLocks,
        StructuralChangeValidator* structuralChangeValidator,
        WorldTelemetryCounters* telemetryCounters);
    ~QueryState() = default;

    QueryState(const QueryState&) = delete;
    QueryState& operator=(const QueryState&) = delete;
    QueryState(QueryState&&) = delete;
    QueryState& operator=(QueryState&&) = delete;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] std::span<const ComponentId> ComponentIds() const noexcept;
    [[nodiscard]] std::span<const std::size_t> ComponentSizes() const noexcept;
    void PrepareBatchExecution(QueryExecutionSettings settings, QueryBatchExecutionScratch& scratch) const;
    void PrepareMutableBatchExecution(QueryExecutionSettings settings, QueryBatchExecutionScratch& scratch) const;
    void ForEach(QueryRawVisitor visitor, void* context) const;
    void ForEachBatch(QueryExecutionSettings settings, QueryRawBatchVisitor visitor, void* context) const;
    void ForEachBatch(QueryExecutionSettings settings, QueryRawBatchVisitor visitor, void* context, QueryBatchExecutionScratch& scratch) const;
    void ForEachMutableBatch(QueryExecutionSettings settings, QueryRawMutableBatchVisitor visitor, void* context) const;
    void ForEachMutableBatch(QueryExecutionSettings settings, QueryRawMutableBatchVisitor visitor, void* context, QueryBatchExecutionScratch& scratch) const;

private:
    struct ChangeVersionKey {
        std::size_t archetypeIndex = 0;
        ComponentId componentId = 0;

        [[nodiscard]] bool operator==(const ChangeVersionKey& other) const noexcept {
            return archetypeIndex == other.archetypeIndex && componentId == other.componentId;
        }
    };

    struct ChangeVersionKeyHash {
        [[nodiscard]] std::size_t operator()(const ChangeVersionKey& key) const noexcept {
            return std::hash<std::size_t>{}(key.archetypeIndex) ^ (std::hash<ComponentId>{}(key.componentId) + 0x9E3779B97F4A7C15ULL);
        }
    };

    [[nodiscard]] bool RecordChanged(const QueryTableDispatchRecord& record) const;
    [[nodiscard]] bool RecordChanged(const MutableQueryTableDispatchRecord& record) const;
    void CommitRecordVersions(const QueryTableDispatchRecord& record) const;
    void CommitRecordVersions(const MutableQueryTableDispatchRecord& record) const;

    NativeArchetypeStorage* nativeStorage_ = nullptr;
    std::shared_ptr<QueryPlan> plan_;
    MutableComponentBorrowLocks* mutableBorrowLocks_ = nullptr;
    StructuralChangeValidator* structuralChangeValidator_ = nullptr;
    WorldTelemetryCounters* telemetryCounters_ = nullptr;
    std::size_t defaultExecutionGrainSize_ = kDefaultQueryExecutionGrainSize;
    mutable std::unordered_map<ChangeVersionKey, std::uint64_t, ChangeVersionKeyHash> observedVersions_;
};

} // namespace kb::ecs
