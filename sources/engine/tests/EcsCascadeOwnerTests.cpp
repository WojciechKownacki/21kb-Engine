#include "engine/ecs/World.hpp"
#include <flecs.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

struct CascadePosition { std::uint32_t value = 0U; };
struct CascadeEvent { kb::ecs::Entity entity; std::uint32_t value; };
struct CascadeContext {
    kb::ecs::World* world = nullptr;
    kb::ecs::Entity parent;
    kb::ecs::Entity child;
    kb::ecs::Entity replacement;
    bool created = false;
    std::vector<CascadeEvent> events;
};

static void Removed(kb::ecs::Entity entity, kb::ecs::ComponentEventKind,
    const CascadePosition* position, void* opaque) {
    auto& context = *static_cast<CascadeContext*>(opaque);
    const std::uint32_t value = position != nullptr ? position->value : 0U;
    context.events.push_back({entity, value});
    if (!context.created && entity == context.parent) {
        context.created = true;
        // Flecs ChildOf cascade deletes the old backend child before removing
        // the parent. Native bulk destruction has already freed both slots.
        context.replacement = context.world->CreateEntity();
        context.world->Set(context.replacement, CascadePosition{99U});
    }
}

int main() {
    try {
        CascadeContext context; // Outlives World, including exceptional cleanup.
        kb::ecs::World world;
        context.world = &world;
        const auto observer = world.ObserveComponent<CascadePosition>(
            kb::ecs::ComponentEventKind::Removed, &Removed, &context);
        if (observer == 0U) { std::fprintf(stderr, "observer_init_failed\n"); return 3; }
        context.parent = world.CreateEntity();
        context.child = world.CreateEntity();
        world.Set(context.parent, CascadePosition{1U});
        world.Set(context.child, CascadePosition{2U});
        world.SetParent(context.child, context.parent);
        const std::array victims{context.parent, context.child};
        world.DestroyEntities(victims);

        const bool created = context.created && context.replacement.IsValid();
        const bool reusedChild = created && kb::ecs::GeneratedEntityIndex(context.replacement)
            == kb::ecs::GeneratedEntityIndex(context.child) && context.replacement != context.child;
        const bool nativeAlive = created && world.IsAlive(context.replacement);
        const auto* native = nativeAlive ? world.TryGet<CascadePosition>(context.replacement) : nullptr;
        const auto backendId = created ? ecs_strip_generation(context.replacement.Id()) : 0U;
        const bool backendAlive = created && ecs_is_alive(world.NativeHandle(), backendId);
        const auto component = world.RegisterComponent<CascadePosition>();
        const auto* backend = backendAlive
            ? static_cast<const CascadePosition*>(ecs_get_id(world.NativeHandle(), backendId, component)) : nullptr;
        std::size_t replacementRemovals = 0U;
        for (const auto& event : context.events) {
            if (event.value == 99U) ++replacementRemovals;
            std::printf("removed,entity=%llu,value=%u\n", static_cast<unsigned long long>(event.entity.Id()), event.value);
        }
        std::printf("cascade,parent=%llu,old_child=%llu,replacement=%llu,reused_child=%d,native_alive=%d,backend_alive=%d,native_value=%u,backend_value=%u,replacement_removals=%zu\n",
            static_cast<unsigned long long>(context.parent.Id()), static_cast<unsigned long long>(context.child.Id()),
            static_cast<unsigned long long>(context.replacement.Id()), reusedChild, nativeAlive, backendAlive,
            native != nullptr ? native->value : 0U, backend != nullptr ? backend->value : 0U, replacementRemovals);
        // End the receiver while its stack context is still alive.
        world.DestroyObserver(observer);
        if (!reusedChild) { std::fprintf(stderr, "fixture_did_not_exercise_child_reuse\n"); return 3; }
        const bool correct = native != nullptr && native->value == 99U && backend != nullptr && backend->value == 99U
            && replacementRemovals == 0U;
        return correct ? 0 : 1;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "cascade_exception,%s\n", error.what());
        return 2;
    }
}
