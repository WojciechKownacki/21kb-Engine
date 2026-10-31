#include "ModuleTypeFixture.hpp"
#include "ecs/ComponentRegistry.hpp"
#include "ecs/type/TagTypeRegistry.hpp"
#include "ecs/type/RelationTypeRegistry.hpp"
#include "engine/modules/EngineModuleLoader.hpp"
#include "engine/modules/EngineModuleSdk.h"

#include <flecs.h>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
void Require(bool result, const char* message) { if (!result) throw std::runtime_error(message); }
void Run() {
    std::unique_ptr<ecs_world_t, decltype(&ecs_fini)> world{ ecs_init(), &ecs_fini };
    kb::ecs::ComponentRegistry components;
    kb::ecs::TagTypeRegistry tags;
    kb::ecs::RelationTypeRegistry relations;
    kb::modules::EngineModuleLoader loader;
    kb::ecs::ComponentId component = 0;
    kb::ecs::TagId tag = 0;
    kb::ecs::RelationId relation = 0;
    const auto entity = ecs_new(world.get());
    const auto target = ecs_new(world.get());
    for (unsigned iteration = 0; iteration < 16; ++iteration) {
        auto loaded = loader.Load({ .key = "ModuleTypes", .modulePath = KB_MODULE_TYPE_PATH });
        Require(loaded.Succeeded(), "Type fixture did not load");
        std::vector<std::string> diagnostics;
        using TypeFunction = const std::type_info* (KB_MODULE_CALL *)(unsigned);
        const auto getType = reinterpret_cast<TypeFunction>(loaded.library.FindSymbol("kb_test_module_type", diagnostics));
        Require(getType != nullptr, "Type fixture entry is missing");
        Require(getType(0) != &typeid(kb::tests::ModuleValue), "Fixture RTTI was not independently allocated");
        const auto nextComponent = components.Register(world.get(), *getType(0), "ModuleValue",
            sizeof(kb::tests::ModuleValue), alignof(kb::tests::ModuleValue), {});
        const auto nextTag = tags.Register(world.get(), *getType(1), "ModuleTag");
        const auto nextRelation = relations.Register(world.get(), *getType(2), "ModuleRelation");
        if (iteration != 0)
            Require(nextComponent == component && nextTag == tag && nextRelation == relation,
                "Reload changed registered type identities");
        component = nextComponent; tag = nextTag; relation = nextRelation;
        Require(component && tag && relation, "Type registration failed");
        const kb::tests::ModuleValue value{ static_cast<int>(iteration + 10) };
        ecs_set_id(world.get(), entity, component, sizeof(value), &value);
        ecs_add_id(world.get(), entity, tag);
        ecs_add_pair(world.get(), entity, relation, target);
        loaded.library.Reset();
        // These lookups must not dereference type_info freed with the DLL.
        Require(components.Find(typeid(kb::tests::ModuleValue)) == component &&
            components.Find(std::type_index{ typeid(kb::tests::ModuleValue) }) == component &&
            tags.Find(typeid(kb::tests::ModuleTag)) == tag && relations.Find(typeid(kb::tests::ModuleRelation)) == relation,
            "Type lookup failed after the defining library was unloaded");
        const auto* retained = static_cast<const kb::tests::ModuleValue*>(ecs_get_id(world.get(), entity, component));
        Require(retained && retained->value == value.value && ecs_has_id(world.get(), entity, tag) &&
            ecs_has_pair(world.get(), entity, relation, target), "Live component, tag or relation state changed on unload");
    }
    ecs_delete(world.get(), entity);
    components.Clear(); tags.Clear(); relations.Clear();
}
} // namespace

int main() {
    try { Run(); std::cout << "Module type lifetime tests passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
