#include "ecs/query/QueryRowDispatcher.hpp"

#include "ecs/QueryState.hpp"
#include "ecs/query/QueryLimits.hpp"

#include <cstdint>
#include <span>

namespace kb::ecs {
namespace {

struct RowAdapter {
    QueryRawVisitor visitor = nullptr;
    void* context = nullptr;
    std::span<const std::size_t> componentSizes;
};

void VisitRows(const Entity::IdType* entityIds, std::size_t count, const void* const* components, void* adapterContext) {
    auto* adapter = static_cast<RowAdapter*>(adapterContext);
    for (std::size_t row = 0; row < count; ++row) {
        QueryComponentPointerBlock rowComponents{};
        for (std::size_t field = 0; field < adapter->componentSizes.size(); ++field) {
            const auto* bytes = static_cast<const std::uint8_t*>(components[field]);
            rowComponents[field] = bytes + (row * adapter->componentSizes[field]);
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
        .componentSizes = state.ComponentSizes(),
    };
    state.ForEachBatch(QueryExecutionSettings{}, &VisitRows, &adapter);
}

} // namespace kb::ecs
