#pragma once

#include "engine/ecs/Query.hpp"
#include "engine/ecs/System.hpp"
#include "engine/ecs/World.hpp"

#include <type_traits>

namespace kb::ecs {

template <typename... Components>
class QuerySystem : public System {
public:
    using Batch = QueryBatch<Components...>;

    explicit QuerySystem(QueryExecutionSettings settings = QueryExecutionSettings{}) noexcept
        : settings_(settings) {
        static_assert(sizeof...(Components) > 0, "ECS query system must declare at least one component");
        static_assert((std::is_trivially_copyable_v<Components> && ...), "ECS query system components must be trivially copyable");
        static_assert((std::is_trivially_destructible_v<Components> && ...), "ECS query system components must be trivially destructible");
    }

    [[nodiscard]] SystemAccess DeclareAccess(World& world) const override {
        SystemAccess access;
        (access.Read<Components>(world), ...);
        return access;
    }

    void OnCreate(World& world) override {
        query_ = world.CreateQuery<Components...>();
        OnQueryCreated(world);
    }

    void OnUpdate(World& world, float deltaSeconds) override {
        static_cast<void>(world);
        if (!query_.IsValid()) {
            return;
        }

        DispatchContext context{
            .system = this,
            .deltaSeconds = deltaSeconds,
        };
        query_.ForEachBatch(settings_, &DispatchBatch, &context);
    }

protected:
    virtual void OnQueryCreated(World& world) {
        static_cast<void>(world);
    }

    virtual void OnUpdateBatch(const Batch& batch, float deltaSeconds) = 0;

private:
    struct DispatchContext {
        QuerySystem* system = nullptr;
        float deltaSeconds = 0.0F;
    };

    static void DispatchBatch(const Batch& batch, void* context) {
        auto* dispatch = static_cast<DispatchContext*>(context);
        if (dispatch != nullptr && dispatch->system != nullptr) {
            dispatch->system->ReportProfilerWork(batch.Count(), batch.Count() * kBytesPerEntity);
            dispatch->system->OnUpdateBatch(batch, dispatch->deltaSeconds);
        }
    }

    static constexpr std::size_t kBytesPerEntity = (sizeof(Components) + ... + 0U);

    Query<Components...> query_;
    QueryExecutionSettings settings_{};
};

} // namespace kb::ecs
