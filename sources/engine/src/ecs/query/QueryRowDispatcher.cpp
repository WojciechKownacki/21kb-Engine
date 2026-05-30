#include "ecs/query/QueryRowDispatcher.hpp"

#include "ecs/QueryState.hpp"
#include "ecs/query/QueryLimits.hpp"

namespace kb::ecs {
namespace {

struct RowAdapter {
    QueryRawVisitor visitor = nullptr;
    void* context = nullptr;
};

void VisitRows(const Entity::IdType* entityIds, std::size_t count, const void* const* components, void* adapterContext) {
    auto* adapter = static_cast<RowAdapter*>(adapterContext);
    for (std::size_t row = 0; row < count; ++row) {
        QueryComponentPointerBlock rowComponents{};
        for (std::size_t field = 0; field < kMaxQueryTerms && components[field] != nullptr; ++field) {
            rowComponents[field] = components[field];
        }
        adapter->visitor(Entity{ entityIds[row] }, rowComponents.data(), adapter->context);
    }
}

} // namespace

void QueryRowDispatcher::Execute(const QueryState& state, QueryRawVisitor visitor, void* context) {
    if (visitor == nullptr) {
        return;
    }

    RowAdapter adapter{
        .visitor = visitor,
        .context = context,
    };
    state.ForEachBatch(QueryExecutionSettings{ .maxBatchSize = 1 }, &VisitRows, &adapter);
}

} // namespace kb::ecs
