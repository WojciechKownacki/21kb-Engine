#include "CliCommands.hpp"
#include "CliProgram.hpp"
#include "CliRecipes.hpp"

#include "engine/script/ScriptAgentSearch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// The recipes of the three floors: every one runs on a fresh project and its scripts run without diagnostics; the ones for one object
// run on an object and a prefab the project has and keep what these have; no two recipes of a floor answer the same words; the
// customer's own words find the recipe.
namespace {

namespace fs = std::filesystem;
using kb::cli::recipes::Kind;
using kb::cli::recipes::Recipe;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        std::fputs(message.c_str(), stderr);
        std::fputs("\n", stderr);
        std::exit(EXIT_FAILURE);
    }
}

[[nodiscard]] bool Contains(const std::string& text, std::string_view part) {
    return text.find(part) != std::string::npos;
}

struct Run {
    int exitCode = 0;
    std::string out;
    std::string err;
    [[nodiscard]] std::string All() const { return out + err; }
};

[[nodiscard]] Run Cli(const std::vector<std::string>& words) {
    std::istringstream in;
    std::ostringstream out;
    std::ostringstream err;
    const int exitCode = kb::cli::RunCli(words, in, kb::cli::CommandIo{ .out = out, .err = err });
    return Run{ exitCode, out.str(), err.str() };
}

[[nodiscard]] fs::path Root() {
    return fs::temp_directory_path() / "21kb_kb_cli_recipe_tests";
}

[[nodiscard]] std::string Project(std::string_view name, const std::string& startTemplate = "Basic") {
    kb::cli::ResetCliConfinementForTests();
    std::error_code error;
    fs::remove_all(Root() / std::string{ name }, error);
    fs::create_directories(Root(), error);
    const Run made = Cli({ "new", "--parent", Root().string(), "--name", std::string{ name }, "--template", startTemplate });
    Require(made.exitCode == 0, "the project could not be made: " + made.out + made.err);
    return (Root() / std::string{ name }).string();
}

// A crate with a body and, where the build makes materials, a material of its own: what a recipe must leave as it is.
[[nodiscard]] bool AddCrate(const std::string& project) {
    std::vector<std::string> words{ "entity", "add", "--project", project, "--create", "Cube", "--name", "Crate", "--body", "Dynamic" };
#if KB_CLI_RENDERER
    words.insert(words.end(), { "--color", "#3366CC" });
#endif
    return Cli(words).exitCode == 0;
}

[[nodiscard]] std::string ScriptNames(const Recipe& recipe) {
    std::string names;
    for (const kb::cli::recipes::Script& script : kb::cli::recipes::RecipeScripts(recipe)) names += script.name + " ";
    return names;
}

// A recipe on a fresh project: made, and its scripts run for a while without a diagnostic. Games and systems that need a
// start the project does not have (a prefab, a scene) get it first.
void RunEveryRecipe() {
    const std::span<const Recipe> all = kb::cli::recipes::Recipes();
    Require(kb::cli::recipes::GameRecipes().size() >= 10U, "ten kinds of game: " + std::to_string(kb::cli::recipes::GameRecipes().size()));
    Require(kb::cli::recipes::SystemRecipes().size() >= 25U, "about thirty systems: " + std::to_string(kb::cli::recipes::SystemRecipes().size()));
    Require(kb::cli::recipes::BrickRecipes().size() >= 30U, "the bricks: " + std::to_string(kb::cli::recipes::BrickRecipes().size()));
    for (const Recipe& recipe : all) {
        const std::string project = Project("Each_" + std::string{ recipe.id });
        // A prefab the spawner can copy.
        Require(Cli({ "entity", "add", "--project", project, "--create", "Cube", "--name", "Enemy" }).exitCode == 0, "a cube");
        Require(Cli({ "prefab", "new", "--project", project, "--from-entity", "Enemy" }).exitCode == 0, "a prefab");
        Require(Cli({ "entity", "remove", "--project", project, "--name", "Enemy" }).exitCode == 0, "the cube goes");
        // The object a recipe is about when it only works on one that exists: the player to bring back.
        if (recipe.id == "checkpoint-respawn") Require(Cli({ "entity", "add", "--project", project, "--create", "Capsule", "--name", "Player" }).exitCode == 0, "a player");
        std::vector<std::string> words{ "recipe", "run", std::string{ recipe.id }, "--project", project };
        if (recipe.id == "spawner") words.insert(words.end(), { "--set", "prefab=/Game/Prefabs/Enemy.kbprefab" });
        if (recipe.onObject && recipe.actor.empty()) {
            // Made for an object that exists: a camera for the camera ones, else an object of the project's.
            const bool camera = std::string_view{ recipe.id }.starts_with("camera");
            if (!camera) Require(Cli({ "entity", "add", "--project", project, "--create", "Capsule", "--name", "Subject", "--position", "0,1,0" }).exitCode == 0, "a subject");
            words.insert(words.end(), { "--entity", camera ? "Main Camera" : "Subject" });
        }
        const Run made = Cli(words);
        Require(made.exitCode == 0 && Contains(made.out, "recipe " + std::string{ recipe.id } + " done"), "the recipe " + std::string{ recipe.id } + " must run on a fresh project: " + made.All());
        const bool slow = recipe.kind == Kind::Game || recipe.id == "spawner" || recipe.id == "enemy-waves";
        const Run ran = Cli({ "run", "--project", project, "--frames", slow ? "300" : "60" });
        Require(ran.exitCode == 0 && Contains(ran.out, " 0 diagnostics"), "the recipe " + std::string{ recipe.id } + " must run without a diagnostic: " + ran.All());
        // Done twice on one project it is refused as a whole (an object of the name exists), not half made.
    }
}

// Every floor points down only: a brick is made of nothing, a system of bricks, a game of systems and bricks.
void RunStructureTests() {
    std::set<std::string_view> ids;
    for (const Recipe& recipe : kb::cli::recipes::Recipes()) {
        Require(ids.insert(recipe.id).second, "the recipe " + std::string{ recipe.id } + " is listed twice");
        Require(!recipe.title.empty() && !recipe.purpose.empty() && recipe.words.size() > 30U, "the recipe " + std::string{ recipe.id } + " says what it is, what it is for and its words");
        std::istringstream lines{ std::string{ recipe.steps } };
        std::string line;
        while (std::getline(lines, line)) {
            const std::size_t at = line.find("use ");
            if (at == std::string::npos || line.find_first_not_of(' ') != at) continue;
            std::istringstream words{ line.substr(at + 4U) };
            std::string child;
            words >> child;
            const Recipe* found = kb::cli::recipes::FindRecipe(child);
            Require(found != nullptr, "the recipe " + std::string{ recipe.id } + " uses " + child + ", which is no recipe");
            Require(recipe.kind != Kind::Brick || found == nullptr || found->kind == Kind::Brick, "a brick uses a brick only for what it needs: " + std::string{ recipe.id });
            Require(recipe.kind != Kind::System || found->kind == Kind::Brick, "a system is made of bricks: " + std::string{ recipe.id } + " uses " + child);
            Require(recipe.kind != Kind::Game || found->kind != Kind::Game, "a game is not made of games: " + std::string{ recipe.id });
        }
    }
}

// One recipe per job: two recipes of a floor whose words are nearly the same would be two answers to one question.
void RunOverlapTests() {
    const std::span<const Recipe> all = kb::cli::recipes::Recipes();
    std::vector<std::set<std::string>> words;
    for (const Recipe& recipe : all) {
        std::set<std::string> set;
        for (const std::string& word : kb::script::SearchWords(std::string{ recipe.id } + " " + std::string{ recipe.title } + " " + std::string{ recipe.words })) set.insert(word);
        words.push_back(std::move(set));
    }
    for (std::size_t left = 0U; left < all.size(); ++left) {
        for (std::size_t right = left + 1U; right < all.size(); ++right) {
            if (all[left].kind != all[right].kind) continue;
            std::vector<std::string> shared;
            std::ranges::set_intersection(words[left], words[right], std::back_inserter(shared));
            const double similarity = static_cast<double>(shared.size()) / static_cast<double>(words[left].size() + words[right].size() - shared.size());
            Require(similarity < 0.45, "the recipes " + std::string{ all[left].id } + " and " + std::string{ all[right].id } + " share their words (" + std::to_string(similarity) + ")");
            Require(all[left].title != all[right].title, "the recipes " + std::string{ all[left].id } + " and " + std::string{ all[right].id } + " have the same title");
        }
    }
}

[[nodiscard]] std::string Search(const std::string& words) {
    const Run run = Cli({ "guide", "--search", words });
    Require(run.exitCode == 0, "guide --search: " + run.All());
    return run.out;
}

// The customer says what they want in their own words, in Polish or English, and the best answer is the recipe for it.
void RunSearchTests() {
    struct Wish {
        std::string_view words;
        std::string_view recipe;
    };
    const std::vector<Wish> wishes{
        { "I want a platformer game", "recipe platformer-2d" },
        { "chce zrobic gre platformowa 2D", "recipe platformer-2d" },
        { "a shooter where I look through the eyes of the character", "recipe fps-shooter" },
        { "strzelanka z widokiem z oczu", "recipe fps-shooter" },
        { "tower defense game with waves of enemies", "recipe tower-defense" },
        { "gra typu kliker", "recipe clicker-idle" },
        { "an endless runner", "recipe endless-runner" },
        { "wyscigi samochodowe", "recipe racing" },
        { "gra logiczna z zagadkami", "recipe puzzle-rooms" },
        { "my character should jump when I press space", "recipe jump" },
        { "postac ma skakac po nacisnieciu spacji", "recipe jump" },
        { "an enemy that chases the player", "recipe enemy-chaser" },
        { "wrog ktory goni gracza", "recipe enemy-chaser" },
        { "the player has health and can die", "recipe health" },
        { "zbieranie monet z licznikiem", "recipe coin-collecting" },
        { "camera that looks down from above", "recipe camera-top-down" },
        { "kamera z gory", "recipe camera-top-down" },
        { "a door that opens with a key", "recipe key-and-door" },
        { "drzwi i klucz", "recipe key-and-door" },
        { "a main menu with a play button", "recipe main-menu" },
        { "menu glowne z przyciskiem graj", "recipe main-menu" },
        { "pause the game with escape", "recipe pause-toggle" },
        { "zapisywanie gry", "recipe save-game" },
        { "a countdown timer on the screen", "recipe timer-hud" },
        { "a character I can talk to", "recipe dialog" },
        { "rozmowa z postacia", "recipe dialog" },
        { "player picks up items and sees an inventory", "recipe inventory-items" },
        { "a checkpoint where I come back after dying", "recipe checkpoint-respawn" },
        { "spikes that hurt", "recipe hurt-on-touch" },
        { "guard walking back and forth", "recipe patrol" },
        { "music in the background", "recipe music-loop" },
        { "a car I can drive", "recipe drive-car" },
        { "game over screen with restart", "recipe game-over-screen" },
    };
    for (const Wish& wish : wishes) {
        const std::string found = Search(std::string{ wish.words });
        const std::string first = found.substr(0U, found.find('\n'));
        Require(first.starts_with(wish.recipe) || Contains(found, std::string{ wish.recipe } + " ("),
            "'" + std::string{ wish.words } + "' must find " + std::string{ wish.recipe } + ", got: " + found);
        Require(std::ranges::count(found, '\n') <= 3, "at most three answers: " + found);
    }
    // A recipe says what it is for, in one line, with the command that runs it.
    const std::string line = Search("zrob mi gre platformowa").substr(0U, 400U);
    Require(Contains(line, "recipe run platformer-2d"), "the answer carries its command: " + line);
    Require(Contains(Search("xyzzy plugh"), "nothing fits"), "no match says so");
}

[[nodiscard]] bool ShowHas(const std::string& project, const std::string& entity, std::string_view part) {
    const Run shown = Cli({ "entity", "show", "--project", project, "--name", entity });
    return Contains(shown.out, part);
}

// On an object the project has, a recipe keeps what the object has: its components, its own script, its material.
void RunExistingObjectTests() {
    for (const Recipe& recipe : kb::cli::recipes::Recipes()) {
        if (!recipe.onObject || recipe.kind == Kind::Game) continue;
        const bool camera = std::string_view{ recipe.id }.starts_with("camera");
        const std::string project = Project("Keep_" + std::string{ recipe.id });
        Require(AddCrate(project), "a crate");
        Require(Cli({ "script-new", "--project", project, "--name", "Mine", "--language", "lua", "--code", "function Tick(self, dt) end" }).exitCode == 0, "its script");
        Require(Cli({ "scene-attach", "--project", project, "--node", "Crate", "--script", "/Game/Scripts/Mine.lua" }).exitCode == 0, "attached");
        const std::string before = Cli({ "entity", "show", "--project", project, "--name", camera ? "Main Camera" : "Crate" }).out;
        std::vector<std::string> words{ "recipe", "run", std::string{ recipe.id }, "--project", project, "--entity", camera ? "Main Camera" : "Crate" };
        if (recipe.id == "spawner") words.insert(words.end(), { "--set", "prefab=/Game/Prefabs/Enemy.kbprefab" });
        const Run made = Cli(words);
        Require(made.exitCode == 0, "the recipe " + std::string{ recipe.id } + " must run on an object of the project: " + made.All());
        const std::string name = camera ? "Main Camera" : "Crate";
        const std::string after = Cli({ "entity", "show", "--project", project, "--name", name }).out;
        // Every component line the object had is still there, and its own script and material with it.
        std::istringstream lines{ before };
        std::string line;
        while (std::getline(lines, line)) {
            const std::string component = line.substr(0U, line.find(':'));
            if (component == "Transform" || component == "Behaviour" || component == "Rigidbody" || component == "Collider" || component.find(' ') != std::string::npos || component.empty() || line.find(':') == std::string::npos) continue;
            Require(Contains(after, component + ":"), "the recipe " + std::string{ recipe.id } + " took the " + component + " off the object: " + after);
        }
        if (!camera) {
#if KB_CLI_RENDERER
            Require(ShowHas(project, "Crate", "/Game/Materials/Crate"), "the recipe " + std::string{ recipe.id } + " left the material: " + after);
#endif
            const std::string listed = Cli({ "scene-list", "--project", project }).out;
            Require(Contains(listed, "/Game/Scripts/Mine.lua"), "the recipe " + std::string{ recipe.id } + " took the object's own script off: " + listed);
            Require(Contains(after, "Rigidbody:") && Contains(after, "Collider:"), "the recipe " + std::string{ recipe.id } + " took the body or the collider off: " + after);
        }
    }
}

// On a prefab the project has, the copies placed afterwards carry what the recipe added and what the prefab had.
void RunPrefabTests() {
    std::size_t worked = 0U;
    for (const Recipe& recipe : kb::cli::recipes::Recipes()) {
        if (!recipe.onObject || recipe.kind == Kind::Game || std::string_view{ recipe.id }.starts_with("camera")) continue;
        const std::string project = Project("Pf_" + std::string{ recipe.id });
        Require(AddCrate(project), "a crate");
        Require(Cli({ "script-new", "--project", project, "--name", "Mine", "--language", "lua", "--code", "function Tick(self, dt) end" }).exitCode == 0, "its script");
        Require(Cli({ "scene-attach", "--project", project, "--node", "Crate", "--script", "/Game/Scripts/Mine.lua" }).exitCode == 0, "attached");
        Require(Cli({ "prefab", "new", "--project", project, "--from-entity", "Crate", "--name", "CratePrefab" }).exitCode == 0, "a prefab");
        Require(Cli({ "entity", "remove", "--project", project, "--name", "Crate" }).exitCode == 0, "the crate goes");
        std::vector<std::string> words{ "recipe", "run", std::string{ recipe.id }, "--project", project, "--prefab", "/Game/Prefabs/CratePrefab.kbprefab" };
        if (recipe.id == "spawner") words.insert(words.end(), { "--set", "prefab=/Game/Prefabs/Enemy.kbprefab" });
        const Run made = Cli(words);
        std::error_code ignored;
        // The scene the recipe works in is gone again, whatever came of it.
        for (const fs::directory_entry& entry : fs::directory_iterator(fs::path{ project } / "Assets" / "Scenes", ignored)) {
            Require(!Contains(entry.path().filename().string(), "RecipeScratch"), "a scratch scene is left behind: " + entry.path().string());
        }
        if (made.exitCode != 0) {
            // A recipe that adds objects cannot go on a prefab, and says so, changing nothing.
            Require(Contains(made.err, "cannot go on a prefab") || Contains(made.err, "adds objects"), "the recipe " + std::string{ recipe.id } + " refused a prefab for no reason: " + made.All());
            continue;
        }
        ++worked;
        Require(Cli({ "prefab", "place", "--project", project, "--prefab", "/Game/Prefabs/CratePrefab.kbprefab", "--name", "Copy", "--position", "0,1,0" }).exitCode == 0, "a copy");
        const std::string listed = Cli({ "scene-list", "--project", project }).out;
        Require(Contains(listed, "/Game/Scripts/Mine.lua"), "the recipe " + std::string{ recipe.id } + " took the prefab's own script off: " + listed);
        const std::string scripts = ScriptNames(recipe);
        const std::string first = scripts.substr(0U, scripts.find(' '));
        Require(Contains(listed, "/Game/Scripts/" + first), "a copy of the prefab has the script of " + std::string{ recipe.id } + ": " + listed);
#if KB_CLI_RENDERER
        Require(ShowHas(project, "Copy", "/Game/Materials/Crate"), "the recipe " + std::string{ recipe.id } + " changed the material the prefab had");
#endif
        Require(ShowHas(project, "Copy", "Rigidbody:"), "the recipe " + std::string{ recipe.id } + " changed what the prefab had");
    }
    Require(worked >= 20U, "bricks go on a prefab: " + std::to_string(worked));
    // Two recipes one after the other on the same prefab: its root keeps its own name (the copy the recipe works on has it too).
    const std::string project = Project("Pf_twice");
    Require(AddCrate(project) && Cli({ "prefab", "new", "--project", project, "--from-entity", "Crate", "--name", "CratePrefab" }).exitCode == 0 &&
                Cli({ "entity", "remove", "--project", project, "--name", "Crate" }).exitCode == 0, "a prefab of a crate");
    for (const char* id : { "patrol", "health" }) {
        const Run made = Cli({ "recipe", "run", id, "--project", project, "--prefab", "/Game/Prefabs/CratePrefab.kbprefab" });
        Require(made.exitCode == 0, std::string{ "the recipe " } + id + " on the same prefab again: " + made.All());
    }
    std::ifstream file{ fs::path{ project } / "Assets" / "Prefabs" / "CratePrefab.kbprefab", std::ios::binary };
    const std::string prefab{ std::istreambuf_iterator<char>{ file }, std::istreambuf_iterator<char>{} };
    Require(Contains(prefab, "\nname=Crate\n") && !Contains(prefab, "RecipeTarget"), "the prefab's root keeps its name");
}

// Where an object ended, from the `moved:` line of a run: "moved: Player 0.00,1.00,0.00 -> 0.00,1.01,4.92".
[[nodiscard]] std::vector<double> MovedTo(const std::string& out, const std::string& name) {
    const std::size_t line = out.find("moved: " + name + " ");
    std::vector<double> at;
    if (line == std::string::npos) return at;
    const std::size_t arrow = out.find("-> ", line);
    std::istringstream numbers{ out.substr(arrow + 3U, out.find('\n', arrow) - arrow - 3U) };
    std::string part;
    while (std::getline(numbers, part, ',')) at.push_back(std::atof(part.c_str()));
    return at;
}

[[nodiscard]] Run RunScene(const std::string& project, const std::vector<std::string>& more) {
    std::vector<std::string> words{ "run", "--project", project };
    words.insert(words.end(), more.begin(), more.end());
    const Run ran = Cli(words);
    Require(ran.exitCode == 0 && Contains(ran.out, " 0 diagnostics"), "the scene must run without a diagnostic: " + ran.All());
    return ran;
}

void RunRecipe(const std::string& project, const std::vector<std::string>& words) {
    std::vector<std::string> command{ "recipe", "run" };
    command.insert(command.end(), words.begin(), words.end());
    command.insert(command.end(), { "--project", project });
    const Run made = Cli(command);
    Require(made.exitCode == 0, "the recipe " + words.front() + " must run: " + made.All());
}

// What the bricks do, seen in a headless run: the log and the `moved:` lines say it.
void RunBehaviourTests() {
    {
        const std::string project = Project("Beh_walk");
        RunRecipe(project, { "player-controls" });
        const std::vector<double> to = MovedTo(RunScene(project, { "--frames", "120", "--press", "W@5+60" }).out, "Player");
        Require(to.size() == 3U && to[2] > 3.0 && std::abs(to[0]) < 0.5, "walk: W takes the player forward");
        const std::vector<double> up = MovedTo(RunScene(project, { "--frames", "120", "--press", "Space@10" }).out, "Player");
        Require(up.size() == 3U, "jump: the player lands again");
    }
    {
        const std::string project = Project("Beh_coins2d", "Basic2D");
        RunRecipe(project, { "platformer-2d" });
        Require(Contains(RunScene(project, { "--frames", "260", "--press", "D@5+200", "--press", "Space@30" }).out, "Coin count: 5"), "platformer-2d: running right and jumping onto the platform collects the five coins");
    }
    {
        const std::string project = Project("Beh_spin");
        Require(AddCrate(project), "a crate");
        RunRecipe(project, { "spin", "--entity", "Crate", "--set", "speed=90" });
        Require(Contains(RunScene(project, { "--frames", "120" }).out, "Crate turned a quarter"), "spin: the crate turns");
    }
    for (const char* game : { "top-down-adventure", "fps-shooter" }) {
        // The walls and everyone in them stand on the floor of a new project: nobody falls out of the arena.
        const std::string project = Project(std::string{ "Beh_arena_" } + game);
        RunRecipe(project, { game });
        const std::string out = RunScene(project, { "--frames", "150" }).out;
        for (const char* enemy : { "Enemy1", "Enemy2" }) {
            const std::vector<double> to = MovedTo(out, enemy);
            Require(to.size() == 3U && to[1] > 0.3 && std::abs(to[0]) < 5.0 && std::abs(to[2]) < 5.0, std::string{ game } + ": " + enemy + " stays on the floor inside the walls: " + out);
        }
    }
    {
        const std::string project = Project("Beh_chase");
        RunRecipe(project, { "player-controls", "--position", "0,1,0" });
        RunRecipe(project, { "enemy-chaser" });
        const std::vector<double> to = MovedTo(RunScene(project, { "--frames", "120" }).out, "Enemy");
        Require(to.size() == 3U && std::hypot(to[0], to[2]) < std::hypot(4.0, 4.0) - 2.0 && to[1] > 0.3, "chase: the enemy, which starts on the floor of a new project, goes after the player");
    }
    {
        const std::string project = Project("Beh_patrol");
        RunRecipe(project, { "patrol" });
        const std::vector<double> to = MovedTo(RunScene(project, { "--frames", "90" }).out, "Guard");
        Require(to.size() == 3U && std::abs(to[0] - 6.0) > 1.0, "patrol: the guard moves along its way");
    }
    {
        const std::string project = Project("Beh_car");
        RunRecipe(project, { "drive-car" });
        const std::vector<double> to = MovedTo(RunScene(project, { "--frames", "120", "--press", "W@5+90" }).out, "Car");
        Require(to.size() == 3U && std::abs(to[2]) > 3.0, "drive-car: W drives the car");
    }
    {
        // The player starts on the coin: touching it is collecting it.
        const std::string project = Project("Beh_collect");
        RunRecipe(project, { "player-controls", "--position", "0,1,4" });
        RunRecipe(project, { "collectible" });
        RunRecipe(project, { "score-hud" });
        Require(Contains(RunScene(project, { "--frames", "30" }).out, "collected Gem"), "collectible: touching it collects it");
    }
    {
        const std::string project = Project("Beh_hurt");
        RunRecipe(project, { "player-controls", "--position", "4,0.8,0" });
        RunRecipe(project, { "health", "--entity", "Player", "--set", "maxHealth=20" });
        RunRecipe(project, { "hurt-on-touch", "--set", "damage=30" });
        const std::string out = RunScene(project, { "--frames", "90" }).out;
        Require(Contains(out, "Player hurt by Hazard") && Contains(out, "Player died"), "hurt-on-touch and health: the zone kills the player: " + out);
    }
    {
        const std::string project = Project("Beh_over");
        RunRecipe(project, { "player-controls", "--position", "4,0.8,0" });
        RunRecipe(project, { "health", "--entity", "Player", "--set", "maxHealth=20" });
        RunRecipe(project, { "hurt-on-touch", "--set", "damage=30" });
        RunRecipe(project, { "game-over-screen" });
        Require(Contains(RunScene(project, { "--frames", "90" }).out, "game over"), "game-over-screen: the death ends the game");
    }
    {
        const std::string project = Project("Beh_door");
        RunRecipe(project, { "player-controls", "--position", "-4,1,3" });
        RunRecipe(project, { "key-and-door" });
        const std::string out = RunScene(project, { "--frames", "120" }).out;
        Require(Contains(out, "collected Key") && Contains(out, "LockedDoor opens"), "key-and-door: the key opens the door: " + out);
    }
    {
        const std::string project = Project("Beh_exit");
        RunRecipe(project, { "player-controls", "--position", "0,1,15" });
        RunRecipe(project, { "level-exit" });
        RunRecipe(project, { "checkpoint", "--position", "0,0.1,15" });
        const std::string out = RunScene(project, { "--frames", "30" }).out;
        Require(Contains(out, "level complete") && Contains(out, "checkpoint reached"), "level-exit and checkpoint: touched, they say so: " + out);
    }
    {
        const std::string project = Project("Beh_time");
        RunRecipe(project, { "timer-hud", "--set", "seconds=1" });
        Require(Contains(RunScene(project, { "--frames", "120" }).out, "time is up"), "timer-hud: the clock runs out");
    }
    {
        const std::string project = Project("Beh_spawn");
        Require(Cli({ "entity", "add", "--project", project, "--create", "Cube", "--name", "Enemy" }).exitCode == 0, "a cube");
        Require(Cli({ "prefab", "new", "--project", project, "--from-entity", "Enemy" }).exitCode == 0, "a prefab");
        Require(Cli({ "entity", "remove", "--project", project, "--name", "Enemy" }).exitCode == 0, "the cube goes");
        RunRecipe(project, { "spawner", "--set", "every=1", "--set", "max=2" });
        const std::string out = RunScene(project, { "--frames", "200" }).out;
        Require(Contains(out, "spawned copy 1") && Contains(out, "spawned copy 2") && !Contains(out, "spawned copy 3"), "spawner: a copy every second, up to max: " + out);
    }
    {
        const std::string project = Project("Beh_talk");
        RunRecipe(project, { "player-controls", "--position", "5,1,4" });
        RunRecipe(project, { "dialog" });
        Require(Contains(RunScene(project, { "--frames", "40", "--press", "E@10" }).out, "Villager says: Hello there!"), "dialog: E near the villager speaks its first line");
    }
    {
        const std::string project = Project("Beh_fight");
        RunRecipe(project, { "player-controls", "--position", "0,1,0" });
        RunRecipe(project, { "enemy-chaser", "--position", "0,1,1.5", "--set", "damage=0" });
        RunRecipe(project, { "melee-attack", "--entity", "Player" });
        Require(Contains(RunScene(project, { "--frames", "40", "--press", "F@10" }).out, "hit Enemy"), "melee-attack: F hits the enemy next to the player");
    }
    {
        const std::string project = Project("Beh_tower");
        RunRecipe(project, { "enemy-chaser", "--position", "6,1,8", "--set", "speed=0" });
        RunRecipe(project, { "turret" });
        Require(Contains(RunScene(project, { "--frames", "120" }).out, "Tower shot Enemy"), "turret: the tower shoots the enemy in range");
    }
    {
        const std::string project = Project("Beh_shoot");
        RunRecipe(project, { "player-controls", "--position", "0,1,0" });
        RunRecipe(project, { "shoot", "--entity", "Player" });
        RunRecipe(project, { "enemy-chaser", "--position", "0,1,6", "--set", "speed=0" });
        Require(Contains(RunScene(project, { "--frames", "40", "--press", "F@10" }).out, "shot Enemy"), "shoot: F hits what is ahead");
    }
    {
        const std::string project = Project("Beh_save");
        RunRecipe(project, { "player-controls", "--position", "0,1,0" });
        RunRecipe(project, { "save-game", "--entity", "Player", "--set", "every=1" });
        Require(!Contains(RunScene(project, { "--frames", "120", "--press", "W@5+60", "--fresh-saves" }).out, "restored"), "save-game: a first run restores nothing");
        Require(Contains(RunScene(project, { "--frames", "10" }).out, "restored Player"), "save-game: the next run restores the place");
    }
    {
        const std::string project = Project("Beh_heal");
        RunRecipe(project, { "player-controls", "--position", "-3,1,4" });
        RunRecipe(project, { "health", "--entity", "Player" });
        RunRecipe(project, { "heal-pickup" });
        Require(Contains(RunScene(project, { "--frames", "30" }).out, "Player healed"), "heal-pickup: touching it heals");
    }
    {
        const std::string project = Project("Beh_bounce");
        RunRecipe(project, { "player-controls", "--position", "3,1,4" });
        RunRecipe(project, { "bounce-pad" });
        Require(Contains(RunScene(project, { "--frames", "30" }).out, "Player bounced"), "bounce-pad: stepping on it throws the player up");
    }
    {
        const std::string project = Project("Beh_click");
        RunRecipe(project, { "click-counter" });
        Require(Contains(RunScene(project, { "--frames", "20", "--click", "Canvas/ClickButton@10" }).out, "answered by 1"), "click-counter: a click reaches its script");
    }
}

// The commands around: a script of the customer's with the same name is kept, a wrong name is helped, a failed recipe changes nothing.
void RunCommandTests() {
    const std::string project = Project("RecipeCommand");
    Require(Cli({ "script-new", "--project", project, "--name", "Walk", "--language", "lua", "--code", "-- my own walking\nfunction Tick(self, dt) end" }).exitCode == 0, "their own Walk");
    Require(Cli({ "recipe", "run", "walk", "--project", project }).exitCode == 0, "walk on a project that has a Walk script");
    std::ifstream mine{ fs::path{ project } / "Assets" / "Scripts" / "Walk.lua" };
    const std::string text{ std::istreambuf_iterator<char>{ mine }, std::istreambuf_iterator<char>{} };
    Require(Contains(text, "my own walking") && fs::exists(fs::path{ project } / "Assets" / "Scripts" / "Walk2.lua"), "the customer's script is kept and the recipe's gets another name");
    const Run again = Cli({ "recipe", "run", "walk", "--project", project });
    Require(again.exitCode == 1 && Contains(again.err, "Player") && Contains(again.err, "changed nothing"), "the same object twice is refused as a whole: " + again.All());
    const Run wrong = Cli({ "recipe", "run", "wlak", "--project", project });
    Require(wrong.exitCode == 1 && Contains(wrong.err, "did you mean walk"), "a wrong id is helped: " + wrong.err);
    const Run value = Cli({ "recipe", "run", "jump", "--project", project, "--entity", "Player", "--set", "jumpspeed=3" });
    Require(value.exitCode == 1 && Contains(value.err, "jumpSpeed (7)"), "a wrong value names the values: " + value.err);
    const Run noObject = Cli({ "recipe", "run", "jump", "--project", project, "--entity", "Nobody" });
    Require(noObject.exitCode == 1 && Contains(noObject.err, "no entity named 'Nobody'") && Contains(noObject.err, "changed nothing"), "a missing object is named: " + noObject.All());
    const Run ownObjects = Cli({ "recipe", "run", "platformer-2d", "--project", project, "--entity", "Player" });
    Require(ownObjects.exitCode == 1 && Contains(ownObjects.err, "builds objects of its own"), "a game does not go on an object: " + ownObjects.err);
    const Run dry = Cli({ "recipe", "run", "health", "--project", project, "--entity", "Player", "--dry-run" });
    Require(dry.exitCode == 0 && Contains(dry.out, "component add") && Contains(dry.out, "--if-missing") && Contains(dry.out, "nothing was written"), "a dry run lists the commands: " + dry.All());
    Require(!Contains(Cli({ "scene-list", "--project", project }).out, "Combatant"), "a dry run writes nothing");
    const Run value2 = Cli({ "recipe", "run", "health", "--project", project, "--entity", "Player", "--set", "maxHealth=250" });
    Require(value2.exitCode == 0 && ShowHas(project, "Player", "maximumHealth=250"), "a value changes what the recipe makes: " + value2.All());
    // An object that has the component keeps it as it is.
    Require(Cli({ "recipe", "run", "health", "--project", project, "--entity", "Player", "--set", "maxHealth=10" }).exitCode == 0 && ShowHas(project, "Player", "maximumHealth=250"), "a component the object has is not overwritten");
    Require(Contains(Cli({ "recipe", "list", "--kind", "game" }).out, "platformer-2d") && Contains(Cli({ "recipe", "show", "walk" }).out, "run: kb_cli recipe run walk"), "list and show");
}

} // namespace

void RunRecipeTests() {
    RunStructureTests();
    RunOverlapTests();
    RunSearchTests();
    RunCommandTests();
    RunEveryRecipe();
    RunBehaviourTests();
    RunExistingObjectTests();
    RunPrefabTests();
}
