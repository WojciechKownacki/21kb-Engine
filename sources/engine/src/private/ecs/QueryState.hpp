#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/Entity.hpp"
#include "engine/ecs/Query.hpp"
#include "engine/ecs/QueryExecutionScratch.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

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
        std::size_t defaultPrefetchDistance,
        MutableComponentBorrowLocks* mutableBorrowLocks,
        StructuralChangeValidator* structuralChangeValidator,
        WorldTelemetryCounters* telemetryCounters,
        std::mutex* telemetryMutex);
    ~QueryState() = default;

    QueryState(const QueryState&) = delete;
    QueryState& operator=(const QueryState&) = delete;
    QueryState(QueryState&&) = delete;
    QueryState& operator=(QueryState&&) = delete;

    // A freed state's memory is kept for the next one: a query created and dropped every frame allocates nothing.
    [[nodiscard]] static void* operator new(std::size_t size);
    static void operator delete(void* pointer, std::size_t size) noexcept;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] std::span<const ComponentId> ComponentIds() const noexcept;
    [[nodiscard]] std::span<const std::size_t> ComponentSizes() const noexcept;
    [[nodiscard]] std::uint64_t StructuralVersion() const noexcept;
    void PrepareBatchExecution(QueryExecutionSettings settings, QueryBatchExecutionScratch& scratch) const;
    void PrepareMutableBatchExecution(QueryExecutionSettings settings, QueryBatchExecutionScratch& scratch) const;
    [[nodiscard]] bool RefreshMutableChunksAfterAppends(std::uint64_t structuralVersion, QueryBatchExecutionScratch& scratch, std::size_t& firstChangedRecord) const;
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

    struct ChangeVersionSnapshot {
        std::uint64_t version = 0;
        std::optional<std::uint64_t> previousObservation;
        bool changed = false;
    };

    using ChangeVersionSnapshots = std::optional<std::unordered_map<ChangeVersionKey, ChangeVersionSnapshot, ChangeVersionKeyHash>>;

    template <typename Record>
    [[nodiscard]] ChangeVersionSnapshots SnapshotRecordVersions(std::span<const Record> records) const;
    [[nodiscard]] bool RecordChanged(std::size_t archetypeIndex, const ChangeVersionSnapshots& snapshots) const;
    void PrepareReadRecords(QueryExecutionSettings settings, QueryBatchExecutionScratch& scratch, bool refreshMetadata) const;
    void PrepareMutableRecords(QueryExecutionSettings settings, QueryBatchExecutionScratch& scratch, bool refreshMetadata) const;
    void RefreshRecordMetadata(std::span<QueryTableDispatchRecord> records) const;
    void RefreshRecordMetadata(std::span<MutableQueryTableDispatchRecord> records) const;
    void CommitRecordVersions(const ChangeVersionSnapshots& snapshots) const;

    NativeArchetypeStorage* nativeStorage_ = nullptr;
    std::shared_ptr<QueryPlan> plan_;
    // Runtime borrow checks are deliberately compiled out with assertions; the constructor
    // contract stays identical between build types, so the stored hook is unused in release.
    [[maybe_unused]] MutableComponentBorrowLocks* mutableBorrowLocks_ = nullptr;
    StructuralChangeValidator* structuralChangeValidator_ = nullptr;
    WorldTelemetryCounters* telemetryCounters_ = nullptr;
    std::mutex* telemetryMutex_ = nullptr;
    std::size_t defaultExecutionGrainSize_ = kDefaultQueryExecutionGrainSize;
    std::size_t defaultPrefetchDistance_ = 0;
    // Created by the first commit of a query with change filters: the map allocates when it is constructed.
    mutable std::optional<std::unordered_map<ChangeVersionKey, std::uint64_t, ChangeVersionKeyHash>> observedVersions_;
    mutable std::vector<QueryTableDispatchRecord> cachedReadRecords_;
    mutable std::vector<MutableQueryTableDispatchRecord> cachedMutableRecords_;
    mutable std::uint64_t cachedReadStructuralVersion_ = 0;
    mutable std::uint64_t cachedMutableStructuralVersion_ = 0;
};

} // namespace kb::ecs
