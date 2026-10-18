#include "engine/script/ScriptGameplayApi.hpp"

#include "engine/gameplay/AbilityAsset.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneGameplay.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <span>
#include <string>
#include <utility>

namespace kb::script {
namespace {

const ScriptValue* Arg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) {
    for (const auto& argument : arguments) if (argument.name == name) return &argument.value;
    return nullptr;
}

ScriptFunctionCallResult Error(std::string message) {
    return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { std::move(message) } };
}

kb::scene::SceneEntity EntityArg(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments, std::string_view name) {
    const ScriptValue* value = Arg(arguments, name);
    return value == nullptr ? context.caller : kb::scene::SceneEntity{ value->AsUInt64() };
}

ScriptValue EntityValue(kb::scene::SceneEntity entity) { return ScriptValue{ entity.Id(), ScriptValueType::Entity }; }

ScriptFunctionCallResult Outcome(const kb::scene::DamageOutcome& outcome) {
    return ScriptFunctionCallResult{ .executed = true, .outputs = {
        { "applied", ScriptValue{ outcome.applied } }, { "amount", ScriptValue{ outcome.amount } },
        { "killed", ScriptValue{ outcome.killed } }, { "reason", ScriptValue{ outcome.reason } } }, .errors = {} };
}

ScriptFunctionCallResult ApplyDamage(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("Gameplay.ApplyDamage requires an active scene");
    kb::gameplay::DamageType type = kb::gameplay::DamageType::Physical;
    if (const ScriptValue* typeName = Arg(arguments, "type")) {
        const auto parsed = kb::gameplay::ParseDamageType(typeName->AsString());
        if (!parsed) return Error("Gameplay.ApplyDamage: type is Physical, Fire, Cold, Lightning, Poison or Healing");
        type = *parsed;
    }
    const ScriptValue* source = Arg(arguments, "source");
    return Outcome(context.scene->Gameplay().ApplyDamage(EntityArg(context, arguments, "target"), Arg(arguments, "amount")->AsFloat(), type,
        source == nullptr ? context.caller : kb::scene::SceneEntity{ source->AsUInt64() }));
}

ScriptFunctionCallResult Heal(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("Gameplay.Heal requires an active scene");
    return Outcome(context.scene->Gameplay().Heal(EntityArg(context, arguments, "target"), Arg(arguments, "amount")->AsFloat(), context.caller));
}

ScriptFunctionCallResult Revive(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("Gameplay.Revive requires an active scene");
    const ScriptValue* health = Arg(arguments, "health");
    return Outcome(context.scene->Gameplay().Revive(EntityArg(context, arguments, "target"), health == nullptr ? 0.0F : health->AsFloat()));
}

ScriptFunctionCallResult UseAbility(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("Gameplay.UseAbility requires an active scene");
    const int slot = Arg(arguments, "slot")->AsInt();
    const ScriptValue* target = Arg(arguments, "target");
    const kb::scene::AbilityOutcome outcome = context.scene->Gameplay().UseAbility(EntityArg(context, arguments, "entity"),
        slot < 0 ? 0U : static_cast<std::uint32_t>(slot), target == nullptr ? kb::scene::SceneEntity{} : kb::scene::SceneEntity{ target->AsUInt64() });
    return ScriptFunctionCallResult{ .executed = true, .outputs = {
        { "used", ScriptValue{ outcome.used } }, { "target", EntityValue(outcome.target) },
        { "hits", ScriptValue{ static_cast<int>(outcome.hits) } }, { "reason", ScriptValue{ outcome.reason } } }, .errors = {} };
}

ScriptFunctionCallResult Health(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("Gameplay.Health requires an active scene");
    const kb::scene::SceneEntity entity = EntityArg(context, arguments, "entity");
    const kb::scene::CombatantComponent* combatant = context.scene->Components().Combatants().TryGet(entity);
    if (combatant == nullptr) {
        // Asking is not an error: a health bar may point at anything; it learns there is no health.
        return ScriptFunctionCallResult{ .executed = true, .outputs = {
            { "found", ScriptValue{ false } }, { "health", ScriptValue{ 0.0F } }, { "maximumHealth", ScriptValue{ 0.0F } },
            { "healthFraction", ScriptValue{ 0.0F } }, { "mana", ScriptValue{ 0.0F } }, { "maximumMana", ScriptValue{ 0.0F } },
            { "manaFraction", ScriptValue{ 0.0F } }, { "alive", ScriptValue{ false } } }, .errors = {} };
    }
    const float fraction = combatant->maximumHealth > 0.0F ? combatant->health / combatant->maximumHealth : 0.0F;
    const float manaFraction = combatant->maximumMana > 0.0F ? combatant->mana / combatant->maximumMana : 0.0F;
    return ScriptFunctionCallResult{ .executed = true, .outputs = {
        { "found", ScriptValue{ true } }, { "health", ScriptValue{ combatant->health } }, { "maximumHealth", ScriptValue{ combatant->maximumHealth } },
        { "healthFraction", ScriptValue{ fraction } },
        { "mana", ScriptValue{ combatant->mana } }, { "maximumMana", ScriptValue{ combatant->maximumMana } },
        { "manaFraction", ScriptValue{ manaFraction } },
        { "alive", ScriptValue{ kb::scene::IsCombatantAlive(*combatant) } } }, .errors = {} };
}

ScriptFunctionCallResult AbilityCooldown(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("Gameplay.AbilityCooldown requires an active scene");
    const int slot = Arg(arguments, "slot")->AsInt();
    return ScriptFunctionCallResult{ .executed = true, .outputs = {
        { "seconds", ScriptValue{ context.scene->Gameplay().AbilityCooldown(EntityArg(context, arguments, "entity"),
            slot < 0 ? 0U : static_cast<std::uint32_t>(slot)) } } }, .errors = {} };
}

ScriptFunctionCallResult NearestEnemy(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("Gameplay.NearestEnemy requires an active scene");
    const std::optional<kb::scene::PerceivedCombatant> enemy = context.scene->Gameplay().NearestEnemy(EntityArg(context, arguments, "entity"));
    const kb::scene::PerceivedCombatant value = enemy.value_or(kb::scene::PerceivedCombatant{});
    return ScriptFunctionCallResult{ .executed = true, .outputs = {
        { "found", ScriptValue{ enemy.has_value() } }, { "enemy", EntityValue(value.entity) },
        { "distance", ScriptValue{ value.distance } }, { "sensed", ScriptValue{ value.sensed } },
        { "x", ScriptValue{ value.lastKnownPosition.x } }, { "y", ScriptValue{ value.lastKnownPosition.y } },
        { "z", ScriptValue{ value.lastKnownPosition.z } } }, .errors = {} };
}

ScriptFunctionCallResult IsHostile(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("Gameplay.IsHostile requires an active scene");
    return ScriptFunctionCallResult{ .executed = true, .outputs = {
        { "hostile", ScriptValue{ context.scene->Gameplay().AreHostile(EntityArg(context, arguments, "entity"),
            kb::scene::SceneEntity{ Arg(arguments, "other")->AsUInt64() }) } } }, .errors = {} };
}

bool RegisterFunction(ScriptRuntimeHost& host, std::string name, std::vector<ScriptFunctionPin> inputs,
    std::vector<ScriptFunctionPin> outputs, ScriptFunctionCallback callback) {
    ScriptFunctionDesc function{};
    function.signature.name = std::move(name);
    function.signature.inputs = std::move(inputs);
    function.signature.outputs = std::move(outputs);
    function.callback = std::move(callback);
    return host.RegisterFunction(std::move(function));
}

} // namespace

bool ScriptGameplayApi::Register(ScriptRuntimeHost& host) {
    const std::vector<ScriptFunctionPin> outcome{ { "applied", ScriptValueType::Bool, true }, { "amount", ScriptValueType::Float, true },
        { "killed", ScriptValueType::Bool, true }, { "reason", ScriptValueType::String, true } };
    return RegisterFunction(host, "Gameplay.ApplyDamage", { { "target", ScriptValueType::Entity, true }, { "amount", ScriptValueType::Float, true },
               { "type", ScriptValueType::String, false }, { "source", ScriptValueType::Entity, false } }, outcome, &ApplyDamage) &&
        RegisterFunction(host, "Gameplay.Heal", { { "target", ScriptValueType::Entity, true }, { "amount", ScriptValueType::Float, true } }, outcome, &Heal) &&
        RegisterFunction(host, "Gameplay.Revive", { { "target", ScriptValueType::Entity, true }, { "health", ScriptValueType::Float, false } }, outcome, &Revive) &&
        RegisterFunction(host, "Gameplay.UseAbility", { { "slot", ScriptValueType::Int, true }, { "target", ScriptValueType::Entity, false },
               { "entity", ScriptValueType::Entity, false } },
            { { "used", ScriptValueType::Bool, true }, { "target", ScriptValueType::Entity, true }, { "hits", ScriptValueType::Int, true },
              { "reason", ScriptValueType::String, true } }, &UseAbility) &&
        RegisterFunction(host, "Gameplay.Health", { { "entity", ScriptValueType::Entity, false } },
            { { "found", ScriptValueType::Bool, true }, { "health", ScriptValueType::Float, true }, { "maximumHealth", ScriptValueType::Float, true }, { "healthFraction", ScriptValueType::Float, true },
              { "mana", ScriptValueType::Float, true }, { "maximumMana", ScriptValueType::Float, true }, { "manaFraction", ScriptValueType::Float, true },
              { "alive", ScriptValueType::Bool, true } }, &Health) &&
        RegisterFunction(host, "Gameplay.AbilityCooldown", { { "slot", ScriptValueType::Int, true }, { "entity", ScriptValueType::Entity, false } },
            { { "seconds", ScriptValueType::Float, true } }, &AbilityCooldown) &&
        RegisterFunction(host, "Gameplay.NearestEnemy", { { "entity", ScriptValueType::Entity, false } },
            { { "found", ScriptValueType::Bool, true }, { "enemy", ScriptValueType::Entity, true }, { "distance", ScriptValueType::Float, true },
              { "sensed", ScriptValueType::Bool, true }, { "x", ScriptValueType::Float, true }, { "y", ScriptValueType::Float, true },
              { "z", ScriptValueType::Float, true } }, &NearestEnemy) &&
        RegisterFunction(host, "Gameplay.IsHostile", { { "other", ScriptValueType::Entity, true }, { "entity", ScriptValueType::Entity, false } },
            { { "hostile", ScriptValueType::Bool, true } }, &IsHostile);
}

} // namespace kb::script
