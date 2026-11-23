#pragma once

#include "CliCommon.hpp"

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Recipes in three floors, all data: a game (platformer, shooter ...) is made of systems (camera, health, menu ...), a system of
// bricks (walk, jump, collect ...), and a brick is one small job with its Lua script. `recipe run <id>` turns one into the
// ordinary commands of one transaction: on a new object it makes, or on an object or prefab the customer has, which keeps
// everything the object has. `guide --search` finds a recipe by the customer's words.
namespace kb::cli::recipes {

enum class Kind { Brick, System, Game };

struct Recipe {
    std::string_view id;          // walk, camera-follow, platformer-2d: one word or hyphenated, unique
    Kind kind;
    std::string_view title;       // what it is, in a few words
    std::string_view purpose;     // one line: what it is for
    std::string_view words;       // the words a customer says for it (English and Polish, with endings)
    std::string_view actor;       // the object it works on when no --entity is given: "Player:Capsule@0,1,0" (name, shape, place); empty: none
    std::string_view parameters;  // "speed=5|jumpSpeed=7": the values `--set` changes, with their defaults
    std::string_view steps;       // one line each: a kb_cli command, `use <id> name=value ...` or `attach <Script>`; {name} is a value
    std::string_view scripts;     // the Lua files: "--- Name" starts each one
    bool onObject = false;        // works on one object (--entity, --prefab) and makes nothing else
    std::string_view check;       // how to see it work, after the run; empty: run --frames 120
};

struct Script {
    std::string name;
    std::string code;
};

[[nodiscard]] std::span<const Recipe> Recipes();
[[nodiscard]] const Recipe* FindRecipe(std::string_view id);
[[nodiscard]] std::string_view KindName(Kind kind) noexcept;
[[nodiscard]] std::vector<std::pair<std::string, std::string>> RecipeParameters(const Recipe& recipe);
[[nodiscard]] std::vector<Script> RecipeScripts(const Recipe& recipe);
// The names a script declares with `-- @expose name Type`.
[[nodiscard]] std::vector<std::string> ExposedNames(std::string_view code);

// The three floors of the data (CliRecipeBricks.cpp, CliRecipeSystems.cpp).
[[nodiscard]] std::span<const Recipe> BrickRecipes();
[[nodiscard]] std::span<const Recipe> SystemRecipes();
[[nodiscard]] std::span<const Recipe> GameRecipes();

struct RecipeHit {
    int score = 0;
    std::string family;                // what keeps three answers different
    std::string line;                  // the recipe, what it is for and the command that runs it
    std::vector<std::string> covered;  // the words of the request it answers
};
// The recipes that best fit the customer's words, best first.
[[nodiscard]] std::vector<RecipeHit> SearchRecipes(std::string_view words);

} // namespace kb::cli::recipes
