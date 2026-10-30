#include "engine/ecs/World.hpp"

#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace {
struct Handle { std::uint32_t slot = 0U; std::uint32_t generation = 0U; };
struct Migration { std::array<float, 32> values{}; };
struct Resource { std::array<std::byte, 4096> payload{}; };
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

struct Pool {
    struct Slot { std::unique_ptr<Resource> resource; std::uint32_t generation = 1U; std::size_t references = 0U; };
    std::array<Slot, 8> slots;
    std::unordered_map<std::uint64_t, Handle> owners;
    std::size_t created = 0U, destroyed = 0U;
    Handle Allocate(bool fail = false) {
        if (fail) throw std::runtime_error("Resource initialization failed");
        for (std::size_t i = 0; i < slots.size(); ++i) if (!slots[i].resource) {
            auto& slot = slots[i];
            slot.resource = std::make_unique<Resource>();
            ++created;
            return {static_cast<std::uint32_t>(i), slot.generation};
        }
        throw std::runtime_error("Resource pool exhausted");
    }
    bool Alive(Handle handle) const {
        return handle.slot < slots.size() && slots[handle.slot].resource && slots[handle.slot].generation == handle.generation;
    }
    void Release(Handle handle) {
        Require(Alive(handle), "Released a stale resource");
        auto& slot = slots[handle.slot];
        Require(slot.references > 0U, "Resource released twice");
        if (--slot.references == 0U) { slot.resource.reset(); ++slot.generation; ++destroyed; }
    }
    static void Changed(kb::ecs::Entity entity, kb::ecs::ComponentEventKind kind, const Handle* value, void* context) {
        auto& pool = *static_cast<Pool*>(context);
        if (kind == kb::ecs::ComponentEventKind::Modified) {
            Require(value && pool.Alive(*value), "Observer saw a stale resource handle");
            ++pool.slots[value->slot].references;
            const auto old = pool.owners.find(entity.Id());
            if (old != pool.owners.end()) pool.Release(old->second);
            pool.owners[entity.Id()] = *value;
        } else {
            const auto old = pool.owners.find(entity.Id());
            Require(old != pool.owners.end(), "Removed resource owner was not registered");
            pool.Release(old->second);
            pool.owners.erase(old);
        }
    }
};

void Run() {
    Pool pool;
    Handle stale{};
    {
        kb::ecs::World world;
        Require(world.ObserveComponent<Handle>(kb::ecs::ComponentEventKind::Modified, &Pool::Changed, &pool) != 0U &&
                world.ObserveComponent<Handle>(kb::ecs::ComponentEventKind::Removed, &Pool::Changed, &pool) != 0U,
            "Resource observers did not register");
        const auto parent = world.CreateEntity();
        stale = pool.Allocate();
        world.Set(parent, stale);
        const auto clone = world.CreateEntity();
        world.Set(clone, *world.TryGet<Handle>(parent));
        Require(pool.slots[stale.slot].references == 2U, "Clone did not retain its shared resource");
        for (int i = 0; i < 32; ++i) {
            world.Set(parent, Migration{});
            world.Remove<Migration>(parent);
            Require(pool.Alive(*world.TryGet<Handle>(parent)) && pool.slots[stale.slot].references == 2U,
                "Archetype migration changed resource ownership");
        }
        world.Remove<Handle>(parent);
        Require(pool.Alive(stale) && pool.slots[stale.slot].references == 1U, "Removing one owner destroyed a shared resource");
        world.DestroyEntity(clone);
        Require(!pool.Alive(stale), "Destroying the last owner leaked its resource");
        const auto replacement = pool.Allocate();
        Require(replacement.slot == stale.slot && replacement.generation != stale.generation && !pool.Alive(stale),
            "Reused resource slot accepted a stale generation");
        world.Set(parent, replacement);
        const auto failed = world.CreateEntity();
        try { world.Set(failed, pool.Allocate(true)); Require(false, "Initialization should fail"); }
        catch (const std::runtime_error&) {}
        Require(!world.Has<Handle>(failed) && pool.owners.size() == 1U, "Failed initialization left an owner or handle");
        world.DestroyEntity(failed);
        // World teardown must release the remaining provider-owned resource.
    }
    Require(pool.owners.empty() && pool.created == pool.destroyed && !pool.Alive(stale),
        "World teardown leaked an external resource");
    std::cout << "resource_handles add=PASS clone=PASS migrate=PASS remove=PASS destroy=PASS initialization_failure=PASS stale_generation=PASS created="
              << pool.created << " destroyed=" << pool.destroyed << '\n';
}

void ObserverModes() {
    for (const bool mirror : {true, false}) {
        kb::ecs::WorldConfig config;
        config.mirrorEntitiesToBackend = mirror;
        config.mirrorNativeComponentChangesToBackend = mirror;
        config.trackEntityCatalog = mirror;
        kb::ecs::World world{config};
        const auto entity = world.CreateEntity();
        world.Set(entity, Handle{1U, 1U});
        world.TryGetMutable<Handle>(entity)->generation = 7U;
        world.MarkModified<Handle>(entity);
        struct Observation { std::size_t count = 0U; std::uint32_t generation = 0U; } observed;
        const auto observer = world.ObserveComponent<Handle>(kb::ecs::ComponentEventKind::Modified,
            [](kb::ecs::Entity, kb::ecs::ComponentEventKind, const Handle* value, void* context) {
                auto& result = *static_cast<Observation*>(context);
                ++result.count;
                result.generation = value->generation;
            }, &observed, true);
        Require(observer != 0U && observed.count == (mirror ? 1U : 0U), "Observer scope differs from mirroring policy");
        if (mirror) Require(observed.generation == 7U, "Late observer yielded stale mirrored data");
        world.Set(entity, Handle{1U, 9U});
        Require(world.TryGet<Handle>(entity)->generation == 9U, "Native data diverged between modes");
        Require(observed.count == (mirror ? 2U : 0U), "Native-only storage unexpectedly published backend events");
        if (mirror) Require(observed.generation == 9U, "Observer received stale modified data");
        world.DestroyObserver(observer);
        std::cout << "observer_mode mirror=" << mirror << " native_value=9 callbacks=" << observed.count
                  << " scope=" << (mirror ? "fresh-backend-events" : "backend-events-disabled") << '\n';
    }
}
}

int main() {
    try { Run(); ObserverModes(); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
