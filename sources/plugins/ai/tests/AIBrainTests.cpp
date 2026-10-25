#include "AIBrainSystem.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/gameplay/AbilityAsset.hpp"
#include "engine/scene/SceneAssets.hpp"

#include "engine/scene/AIBrainComponent.hpp"
#include "engine/scene/CombatantComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneGameplay.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <cmath>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

kb::scene::SceneEntity Spawn(kb::scene::Scene& scene, const char* name, kb::scene::Vec3 position, std::uint32_t team, float health = 100.0F) {
    const kb::scene::SceneObject object = scene.Entities().CreateObject({ .name = name });
    scene.Transforms().Set(object.Entity(), kb::scene::TransformComponent{ .localPosition = position });
    kb::scene::CombatantComponent combatant{};
    combatant.team = team;
    combatant.maximumHealth = health;
    combatant.health = health;
    combatant.requireLineOfSight = false;
    combatant.sightAngleDegrees = 360.0F;
    Require(scene.Components().Combatants().Set(object.Entity(), combatant), "a combatant could not be authored");
    return object.Entity();
}

void Brain(kb::scene::Scene& scene, kb::scene::SceneEntity entity, kb::scene::AIBrainPreset preset) {
    kb::scene::AIBrainComponent brain{};
    kb::scene::ApplyAIBrainPreset(brain, preset);
    Require(scene.Components().AIBrains().Set(entity, brain), "a brain could not be authored");
}

kb::scene::Vec3 At(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) { return scene.Transforms().Get(entity).worldPosition; }

float Flat(kb::scene::Vec3 a, kb::scene::Vec3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); }

void Run(kb::scene::Scene& scene, float seconds) {
    constexpr float deltaSeconds = 0.05F;
    const int steps = static_cast<int>(std::ceil(seconds / deltaSeconds));
    for (int step = 0; step < steps; ++step) static_cast<void>(scene.Runtime().Update(deltaSeconds));
    Require(scene.Runtime().DrainSceneSystemErrors().empty(), "the AI produced scene system errors");
}

} // namespace

// The brain says why it acts: the rule it took, its condition and action, and the enemy; a brain whose rules never hold
// reports no rule and idles without errors; a decision follows the situation, not the preset.
void RunDecisionReasonTest() {
    kb::scene::Scene scene;
    auto system = std::make_unique<kb::ai_plugin::AIBrainSystem>();
    const kb::ai_plugin::AIBrainSystem* const brains = system.get();
    Require(scene.Runtime().AddSceneSystem(std::move(system)).IsValid(), "the AI system could not be added");
    const kb::scene::SceneEntity goblin = Spawn(scene, "Goblin", { 0.0F, 0.0F, 10.0F }, 2U);
    Brain(scene, goblin, kb::scene::AIBrainPreset::Melee);
    const kb::scene::SceneEntity idler = Spawn(scene, "Idler", { 60.0F, 0.0F, 60.0F }, 2U);
    kb::scene::AIBrainComponent silent{};
    for (kb::scene::AIRule& rule : silent.rules) rule = kb::scene::AIRule{};
    Require(scene.Components().AIBrains().Set(idler, silent), "a silent brain could not be authored");
    scene.Runtime().SetPlaying(true);
    Run(scene, 1.0F);
    const kb::scene::AIBrainComponent* melee = scene.Components().AIBrains().TryGet(goblin);
    const auto alone = brains->LastDecision(goblin);
    Require(alone.has_value() && alone->ruleIndex >= 0 && !alone->enemy.IsValid(), "a monster with nobody around must report the rule it follows and no enemy");
    Require(melee->rules[static_cast<std::size_t>(alone->ruleIndex)].action == alone->action &&
                melee->rules[static_cast<std::size_t>(alone->ruleIndex)].when == alone->condition &&
                alone->action != kb::scene::AIAction::Chase,
        "the reported rule must be the one whose action the monster is doing, and it cannot be a chase with no enemy");
    const kb::scene::SceneEntity hero = Spawn(scene, "Hero", { 0.0F, 0.0F, 0.0F }, 1U, 500.0F);
    Run(scene, 1.0F);
    const auto hunting = brains->LastDecision(goblin);
    Require(hunting.has_value() && hunting->enemy == hero && hunting->ruleIndex != alone->ruleIndex && hunting->action != alone->action,
        "a monster that senses an enemy must report another rule, its action and that enemy");
    Require(melee->rules[static_cast<std::size_t>(hunting->ruleIndex)].action == hunting->action, "the reported rule must match the action of the monster");
    const auto silentDecision = brains->LastDecision(idler);
    Require(silentDecision.has_value() && silentDecision->ruleIndex == -1 && silentDecision->action == kb::scene::AIAction::Idle &&
                !silentDecision->enemy.IsValid(),
        "a brain whose rules never hold must report no rule and idle");
    Require(!brains->LastDecision(hero).has_value(), "an object without a brain has no decision");
    static_cast<void>(scene.Gameplay().ApplyDamage(goblin, 1000.0F));
    Run(scene, 1.0F);
}

int main() {
    RunDecisionReasonTest();
    // A melee monster runs to the hero and hits it; a rabbit (no team) runs from the hero; a guard
    // attacks only what comes near and goes back to its post; the dead stop thinking.
    {
        kb::scene::Scene scene;
        Require(scene.Runtime().AddSceneSystem(std::make_unique<kb::ai_plugin::AIBrainSystem>()).IsValid(), "the AI system could not be added");
        const kb::scene::SceneEntity hero = Spawn(scene, "Hero", { 0.0F, 0.0F, 0.0F }, 1U, 200.0F);
        const kb::scene::SceneEntity goblin = Spawn(scene, "Goblin", { 0.0F, 0.0F, 10.0F }, 2U);
        Brain(scene, goblin, kb::scene::AIBrainPreset::Melee);
        const kb::scene::SceneEntity rabbit = Spawn(scene, "Rabbit", { 4.0F, 0.0F, 0.0F }, 0U);
        Brain(scene, rabbit, kb::scene::AIBrainPreset::Coward);
        const kb::scene::SceneEntity guard = Spawn(scene, "Guard", { 40.0F, 0.0F, 0.0F }, 2U);
        Brain(scene, guard, kb::scene::AIBrainPreset::Guard);
        scene.Runtime().SetPlaying(true);

        Run(scene, 5.0F);
        const kb::scene::CombatantComponent* heroStats = scene.Components().Combatants().TryGet(hero);
        Require(Flat(At(scene, goblin), At(scene, hero)) < 2.0F, "a melee monster must close in on the enemy it senses");
        Require(heroStats->health < 200.0F, "a melee monster with an empty slot must hit with its plain attack");
        Require(Flat(At(scene, rabbit), At(scene, hero)) > 8.0F, "a coward must run from anyone with a team");
        Require(Flat(At(scene, guard), { 40.0F, 0.0F, 0.0F }) < 0.01F, "a guard must not leave its post for an enemy far away");

        // Killed, the monster stops; the hero takes no more hits.
        static_cast<void>(scene.Gameplay().ApplyDamage(goblin, 1000.0F));
        const float healthAtDeath = heroStats->health;
        const kb::scene::Vec3 corpse = At(scene, goblin);
        Run(scene, 3.0F);
        Require(heroStats->health == healthAtDeath && Flat(At(scene, goblin), corpse) < 0.01F, "a dead brain must stop acting");
    }
    // An archer with a 12 m bolt backs away from an enemy that comes too close, then shoots from
    // where it stands.
    {
        const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb-ai-brain-tests";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "Assets" / "Abilities");
        Require(kb::gameplay::AbilityAssetIO::Save(root / "Assets/Abilities/Bolt.kbability",
                    kb::gameplay::AbilityAsset{ .name = "Bolt", .range = 12.0F, .amount = 5.0F, .cooldownSeconds = 0.5F }),
            "the bolt could not be saved");
        kb::scene::Scene scene;
        Require(scene.Assets().MountProject(root) && scene.Assets().Discover() == 1U, "the archer test project could not be mounted");
        const std::uint64_t bolt = scene.Assets().Manager().Registry().FindByPath("/Game/Abilities/Bolt.kbability")->id.value;
        Require(scene.Runtime().AddSceneSystem(std::make_unique<kb::ai_plugin::AIBrainSystem>()).IsValid(), "the AI system could not be added");
        const kb::scene::SceneEntity hero = Spawn(scene, "Hero", { 0.0F, 0.0F, 0.0F }, 1U);
        const kb::scene::SceneEntity archer = Spawn(scene, "Archer", { 0.0F, 0.0F, 3.0F }, 2U);
        scene.Components().Combatants().TryGet(archer)->abilities[0] = bolt;
        Brain(scene, archer, kb::scene::AIBrainPreset::Ranged);
        scene.Runtime().SetPlaying(true);
        Run(scene, 4.0F);
        Require(Flat(At(scene, archer), At(scene, hero)) >= 7.5F, "a ranged brain must back away to its distance and stay there");
        Require(scene.Components().Combatants().TryGet(hero)->health < 100.0F, "a ranged brain must shoot from its distance");
        std::filesystem::remove_all(root);
    }
    std::printf("AI brain tests passed\n");
    return 0;
}
