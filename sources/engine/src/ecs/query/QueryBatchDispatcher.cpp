#include "ecs/query/QueryBatchDispatcher.hpp"

#include "ecs/query/QueryFieldReader.hpp"
#include "ecs/query/QueryLimits.hpp"
#include "ecs/query/QueryTableBatchDispatcher.hpp"

#include <flecs.h>

#include <algorithm>
#include <vector>

namespace kb::ecs {
namespace {

[[nodiscard]] std::size_t ResolveBatchSize(QueryExecutionSettings settings, std::size_t defaultExecutionGrainSize) noexcept {
    const std::size_t resolved = settings.maxBatchSize == 0 ? defaultExecutionGrainSize : settings.maxBatchSize;
    return resolved == 0 ? kDefaultQueryExecutionGrainSize : resolved;
}

struct QueryTableDispatchRecord {
    const ecs_table_t* table = nullptr;
    const Entity::IdType* entityIds = nullptr;
    std::size_t entityCount = 0;
    QueryComponentPointerBlock fieldComponents{};
    std::vector<ecs_id_t> tableTypeIds;
    Entity::IdType firstEntityId = 0;
    std::size_t sequence = 0;
};

struct MutableQueryTableDispatchRecord {
    const ecs_table_t* table = nullptr;
    const Entity::IdType* entityIds = nullptr;
    std::size_t entityCount = 0;
    MutableQueryComponentPointerBlock fieldComponents{};
    std::vector<ecs_id_t> tableTypeIds;
    Entity::IdType firstEntityId = 0;
    std::size_t sequence = 0;
};

struct MutableDispatchContext {
    ecs_world_t* world = nullptr;
    std::span<const ComponentId> componentIds;
    QueryRawMutableBatchVisitor visitor = nullptr;
    void* context = nullptr;
};

[[nodiscard]] std::vector<ecs_id_t> ReadTableTypeIds(const ecs_table_t* table) {
    if (table == nullptr) {
        return {};
    }

    const ecs_type_t* type = ecs_table_get_type(table);
    if (type == nullptr || type->array == nullptr || type->count <= 0) {
        return {};
    }
    return std::vector<ecs_id_t>{ type->array, type->array + type->count };
}

[[nodiscard]] bool CompareTableRecords(const QueryTableDispatchRecord& left, const QueryTableDispatchRecord& right) noexcept {
    if (left.tableTypeIds != right.tableTypeIds) {
        return std::lexicographical_compare(left.tableTypeIds.begin(), left.tableTypeIds.end(), right.tableTypeIds.begin(), right.tableTypeIds.end());
    }
    if (left.firstEntityId != right.firstEntityId) {
        return left.firstEntityId < right.firstEntityId;
    }
    return left.sequence < right.sequence;
}

[[nodiscard]] bool CompareMutableTableRecords(const MutableQueryTableDispatchRecord& left, const MutableQueryTableDispatchRecord& right) noexcept {
    if (left.tableTypeIds != right.tableTypeIds) {
        return std::lexicographical_compare(left.tableTypeIds.begin(), left.tableTypeIds.end(), right.tableTypeIds.begin(), right.tableTypeIds.end());
    }
    if (left.firstEntityId != right.firstEntityId) {
        return left.firstEntityId < right.firstEntityId;
    }
    return left.sequence < right.sequence;
}

void MarkMutableBatchChanged(ecs_world_t* world, std::span<const ComponentId> componentIds, const Entity::IdType* entityIds, std::size_t count) {
    for (ComponentId componentId : componentIds) {
        for (std::size_t index = 0; index < count; ++index) {
            ecs_modified_id(world, entityIds[index], componentId);
        }
    }
}

void DispatchMutableBatch(const Entity::IdType* entityIds, std::size_t count, void* const* components, void* context) {
    auto* dispatchContext = static_cast<MutableDispatchContext*>(context);
    dispatchContext->visitor(entityIds, count, components, dispatchContext->context);
    MarkMutableBatchChanged(dispatchContext->world, dispatchContext->componentIds, entityIds, count);
}

void DispatchRecord(
    const QueryTableDispatchRecord& record,
    std::span<const std::size_t> componentSizes,
    std::size_t maxBatchSize,
    std::size_t prefetchDistance,
    QueryRawBatchVisitor visitor,
    void* context) {
    QueryTableBatchDispatcher::Dispatch(
        record.entityIds,
        record.entityCount,
        componentSizes,
        record.fieldComponents,
        maxBatchSize,
        prefetchDistance,
        visitor,
        context);
}

void DispatchMutableRecord(
    const MutableQueryTableDispatchRecord& record,
    std::span<const std::size_t> componentSizes,
    std::size_t maxBatchSize,
    std::size_t prefetchDistance,
    MutableDispatchContext& dispatchContext) {
    QueryTableBatchDispatcher::DispatchMutable(
        record.entityIds,
        record.entityCount,
        componentSizes,
        record.fieldComponents,
        maxBatchSize,
        prefetchDistance,
        &DispatchMutableBatch,
        &dispatchContext);
}

void ExecuteStorageOrder(
    ecs_world_t* world,
    ecs_query_t* query,
    std::span<const std::size_t> componentSizes,
    bool filterChangedResults,
    std::size_t maxBatchSize,
    std::size_t prefetchDistance,
    QueryRawBatchVisitor visitor,
    void* context) {
    QueryComponentPointerBlock fieldComponents{};
    ecs_iter_t iterator = ecs_query_iter(world, query);
    while (ecs_query_next(&iterator)) {
        if (filterChangedResults && !ecs_iter_changed(&iterator)) {
            continue;
        }
        if (QueryFieldReader::Read(iterator, componentSizes, fieldComponents)) {
            QueryTableBatchDispatcher::Dispatch(iterator, componentSizes, fieldComponents, maxBatchSize, prefetchDistance, visitor, context);
        }
    }
}

void ExecuteMutableStorageOrder(
    ecs_world_t* world,
    ecs_query_t* query,
    std::span<const ComponentId> componentIds,
    std::span<const std::size_t> componentSizes,
    bool filterChangedResults,
    std::size_t maxBatchSize,
    std::size_t prefetchDistance,
    QueryRawMutableBatchVisitor visitor,
    void* context) {
    MutableDispatchContext dispatchContext{
        .world = world,
        .componentIds = componentIds,
        .visitor = visitor,
        .context = context,
    };

    MutableQueryComponentPointerBlock fieldComponents{};
    ecs_iter_t iterator = ecs_query_iter(world, query);
    while (ecs_query_next(&iterator)) {
        if (filterChangedResults && !ecs_iter_changed(&iterator)) {
            continue;
        }
        if (QueryFieldReader::ReadMutable(iterator, componentSizes, fieldComponents)) {
            QueryTableBatchDispatcher::DispatchMutable(iterator, componentSizes, fieldComponents, maxBatchSize, prefetchDistance, &DispatchMutableBatch, &dispatchContext);
        }
    }
}

void ExecuteDeterministicOrder(
    ecs_world_t* world,
    ecs_query_t* query,
    std::span<const std::size_t> componentSizes,
    bool filterChangedResults,
    std::size_t maxBatchSize,
    std::size_t prefetchDistance,
    QueryRawBatchVisitor visitor,
    void* context) {
    std::vector<QueryTableDispatchRecord> records;
    QueryComponentPointerBlock fieldComponents{};
    std::size_t sequence = 0;

    ecs_iter_t iterator = ecs_query_iter(world, query);
    while (ecs_query_next(&iterator)) {
        if (filterChangedResults && !ecs_iter_changed(&iterator)) {
            continue;
        }
        if (!QueryFieldReader::Read(iterator, componentSizes, fieldComponents) || iterator.count <= 0 || iterator.entities == nullptr) {
            continue;
        }

        const auto* entityIds = reinterpret_cast<const Entity::IdType*>(iterator.entities);
        records.push_back(QueryTableDispatchRecord{
            .table = iterator.table,
            .entityIds = entityIds,
            .entityCount = static_cast<std::size_t>(iterator.count),
            .fieldComponents = fieldComponents,
            .tableTypeIds = ReadTableTypeIds(iterator.table),
            .firstEntityId = entityIds[0],
            .sequence = sequence++,
        });
    }

    std::sort(records.begin(), records.end(), &CompareTableRecords);
    for (const QueryTableDispatchRecord& record : records) {
        DispatchRecord(record, componentSizes, maxBatchSize, prefetchDistance, visitor, context);
    }
}

void ExecuteMutableDeterministicOrder(
    ecs_world_t* world,
    ecs_query_t* query,
    std::span<const ComponentId> componentIds,
    std::span<const std::size_t> componentSizes,
    bool filterChangedResults,
    std::size_t maxBatchSize,
    std::size_t prefetchDistance,
    QueryRawMutableBatchVisitor visitor,
    void* context) {
    std::vector<MutableQueryTableDispatchRecord> records;
    MutableQueryComponentPointerBlock fieldComponents{};
    std::size_t sequence = 0;

    ecs_iter_t iterator = ecs_query_iter(world, query);
    while (ecs_query_next(&iterator)) {
        if (filterChangedResults && !ecs_iter_changed(&iterator)) {
            continue;
        }
        if (!QueryFieldReader::ReadMutable(iterator, componentSizes, fieldComponents) || iterator.count <= 0 || iterator.entities == nullptr) {
            continue;
        }

        const auto* entityIds = reinterpret_cast<const Entity::IdType*>(iterator.entities);
        records.push_back(MutableQueryTableDispatchRecord{
            .table = iterator.table,
            .entityIds = entityIds,
            .entityCount = static_cast<std::size_t>(iterator.count),
            .fieldComponents = fieldComponents,
            .tableTypeIds = ReadTableTypeIds(iterator.table),
            .firstEntityId = entityIds[0],
            .sequence = sequence++,
        });
    }

    MutableDispatchContext dispatchContext{
        .world = world,
        .componentIds = componentIds,
        .visitor = visitor,
        .context = context,
    };

    std::sort(records.begin(), records.end(), &CompareMutableTableRecords);
    for (const MutableQueryTableDispatchRecord& record : records) {
        DispatchMutableRecord(record, componentSizes, maxBatchSize, prefetchDistance, dispatchContext);
    }
}

} // namespace

void QueryBatchDispatcher::Execute(
    ecs_world_t* world,
    ecs_query_t* query,
    std::span<const std::size_t> componentSizes,
    bool filterChangedResults,
    std::size_t defaultExecutionGrainSize,
    QueryExecutionSettings settings,
    QueryRawBatchVisitor visitor,
    void* context) {
    if (world == nullptr || query == nullptr || componentSizes.empty() || visitor == nullptr) {
        return;
    }

    const std::size_t maxBatchSize = ResolveBatchSize(settings, defaultExecutionGrainSize);
    switch (settings.iterationOrder) {
    case QueryIterationOrder::Deterministic:
        ExecuteDeterministicOrder(world, query, componentSizes, filterChangedResults, maxBatchSize, settings.prefetchDistance, visitor, context);
        return;
    case QueryIterationOrder::StorageOrder:
    case QueryIterationOrder::ChunkOrder:
        ExecuteStorageOrder(world, query, componentSizes, filterChangedResults, maxBatchSize, settings.prefetchDistance, visitor, context);
        return;
    }
}

void QueryBatchDispatcher::ExecuteMutable(
    ecs_world_t* world,
    ecs_query_t* query,
    std::span<const ComponentId> componentIds,
    std::span<const std::size_t> componentSizes,
    bool filterChangedResults,
    std::size_t defaultExecutionGrainSize,
    QueryExecutionSettings settings,
    QueryRawMutableBatchVisitor visitor,
    void* context) {
    if (world == nullptr || query == nullptr || componentIds.empty() || componentSizes.empty() || componentIds.size() != componentSizes.size() || visitor == nullptr) {
        return;
    }

    const std::size_t maxBatchSize = ResolveBatchSize(settings, defaultExecutionGrainSize);
    switch (settings.iterationOrder) {
    case QueryIterationOrder::Deterministic:
        ExecuteMutableDeterministicOrder(world, query, componentIds, componentSizes, filterChangedResults, maxBatchSize, settings.prefetchDistance, visitor, context);
        return;
    case QueryIterationOrder::StorageOrder:
    case QueryIterationOrder::ChunkOrder:
        ExecuteMutableStorageOrder(world, query, componentIds, componentSizes, filterChangedResults, maxBatchSize, settings.prefetchDistance, visitor, context);
        return;
    }
}

} // namespace kb::ecs
