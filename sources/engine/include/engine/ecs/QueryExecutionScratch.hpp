#pragma once

#include "engine/ecs/Entity.hpp"
#include "engine/ecs/QueryExecutionSettings.hpp"
#include "engine/ecs/WorkerPool.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

struct ecs_table_t;

namespace kb::ecs {

inline constexpr std::size_t kQueryExecutionScratchMaxTerms = 32;
inline constexpr std::size_t kQueryReductionSlotCacheLineBytes = 64;

struct QueryTableDispatchRecord {
    const ecs_table_t* table = nullptr;
    const Entity::IdType* entityIds = nullptr;
    std::size_t entityCount = 0;
    std::array<const void*, kQueryExecutionScratchMaxTerms> fieldComponents{};
    std::array<std::uint64_t, kQueryExecutionScratchMaxTerms> componentVersions{};
    std::array<std::size_t, kQueryExecutionScratchMaxTerms> componentDirtyCounts{};
    std::size_t nativeArchetypeIndex = 0;
    std::size_t nativeChunkIndex = 0;
    Entity::IdType firstEntityId = 0;
    std::size_t sequence = 0;
};

struct MutableQueryTableDispatchRecord {
    const ecs_table_t* table = nullptr;
    const Entity::IdType* entityIds = nullptr;
    std::size_t entityCount = 0;
    std::array<void*, kQueryExecutionScratchMaxTerms> fieldComponents{};
    std::array<std::uint64_t, kQueryExecutionScratchMaxTerms> componentVersions{};
    std::array<std::size_t, kQueryExecutionScratchMaxTerms> componentDirtyCounts{};
    std::size_t nativeArchetypeIndex = 0;
    std::size_t nativeChunkIndex = 0;
    Entity::IdType firstEntityId = 0;
    std::size_t sequence = 0;
};

struct QueryBatchWorkItem {
    std::size_t recordIndex = 0;
    std::size_t offset = 0;
    std::size_t count = 0;
};

class QueryBatchExecutionScratch {
public:
    void Clear() noexcept {
        records_.clear();
        mutableRecords_.clear();
        workItems_.clear();
        chunks_.clear();
    }

    std::vector<QueryTableDispatchRecord> records_;
    std::vector<MutableQueryTableDispatchRecord> mutableRecords_;
    std::vector<QueryBatchWorkItem> workItems_;
    std::vector<WorkerPoolChunk> chunks_;
};

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

template <typename T>
struct alignas(kQueryReductionSlotCacheLineBytes) QueryReductionSlot {
    T value{};
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

template <typename T>
class QueryReductionScratch {
public:
    static_assert(std::is_default_constructible_v<T>, "ECS query reduction value must be default constructible");
    static_assert(std::is_copy_assignable_v<T>, "ECS query reduction value must be copy assignable");

    void Reset(std::size_t slotCount, const T& initialValue = T{}) {
        const std::size_t resolvedSlotCount = std::max<std::size_t>(slotCount, 1U);
        slots_.resize(resolvedSlotCount);
        for (QueryReductionSlot<T>& slot : slots_) {
            slot.value = initialValue;
        }
    }

    void ResetForSettings(QueryExecutionSettings settings, const T& initialValue = T{}) {
        std::size_t slotCount = 1U;
        if (settings.workerPool != nullptr &&
            settings.reductionMode == QueryReductionMode::PerWorker &&
            QueryExecutionPolicyUsesParallelism(settings.policy)) {
            const std::size_t poolWorkerCount = settings.workerPool->WorkerCount();
            slotCount = settings.workerCountOverride == 0U
                ? poolWorkerCount
                : std::min(poolWorkerCount, settings.workerCountOverride);
        }
        Reset(slotCount, initialValue);
    }

    [[nodiscard]] T& SlotForCurrentWorker() noexcept {
        const QueryWorkerContext workerContext = CurrentQueryWorkerContext();
        if (workerContext.active && workerContext.workerIndex < slots_.size()) {
            return slots_[workerContext.workerIndex].value;
        }
        return slots_.front().value;
    }

    [[nodiscard]] const T& Slot(std::size_t index) const noexcept {
        return slots_[index].value;
    }

    [[nodiscard]] T& Slot(std::size_t index) noexcept {
        return slots_[index].value;
    }

    [[nodiscard]] std::size_t SlotCount() const noexcept {
        return slots_.size();
    }

    template <typename Reducer>
    [[nodiscard]] T Reduce(T initialValue, Reducer&& reducer) const {
        for (const QueryReductionSlot<T>& slot : slots_) {
            reducer(initialValue, slot.value);
        }
        return initialValue;
    }

private:
    std::vector<QueryReductionSlot<T>> slots_{ QueryReductionSlot<T>{} };
};

} // namespace kb::ecs
