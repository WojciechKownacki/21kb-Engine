#pragma once

#include "engine/ecs/Entity.hpp"
#include "engine/ecs/WorkerPool.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

struct ecs_table_t;

namespace kb::ecs {

inline constexpr std::size_t kQueryExecutionScratchMaxTerms = 32;

struct QueryTableDispatchRecord {
    const ecs_table_t* table = nullptr;
    const Entity::IdType* entityIds = nullptr;
    std::size_t entityCount = 0;
    std::array<const void*, kQueryExecutionScratchMaxTerms> fieldComponents{};
    std::array<std::uint64_t, kQueryExecutionScratchMaxTerms> componentVersions{};
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

} // namespace kb::ecs
