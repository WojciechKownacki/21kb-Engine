#include "CliRecipes.hpp"

#include "CliCommands.hpp"
#include "CliProgram.hpp"

#include "engine/script/ScriptAgentSearch.hpp"
#include "engine/script/ScriptNameSuggestion.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>

// recipe: list, show and run the recipes (CliRecipes.hpp). A run is expanded into the ordinary kb_cli commands and sent as one
// transaction, so it takes effect completely or not at all, and `undo` takes it back like any other.
namespace kb::cli::recipes {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kUsage =
    "usage: kb_cli recipe list [--kind game|system|brick]\n"
    "       kb_cli recipe show <id>\n"
    "       kb_cli recipe run <id> --project <dir> [--entity <Object> | --prefab /Game/<path>.kbprefab] [--name <Name>] [--position x,y,z]\n"
    "                              [--set name=value ...] [--scene <path>] [--dry-run]\n";

[[nodiscard]] std::string Trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1U);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1U);
    return std::string{ text };
}

[[nodiscard]] std::vector<std::string> Split(std::string_view text, char separator) {
    std::vector<std::string> parts;
    while (!text.empty()) {
        const std::size_t at = text.find(separator);
        parts.emplace_back(text.substr(0U, at));
        text = at == std::string_view::npos ? std::string_view{} : text.substr(at + 1U);
    }
    return parts;
}

// The words of a step line; 'single' and "double" quotes keep spaces inside one word.
[[nodiscard]] std::vector<std::string> Words(std::string_view line) {
    std::vector<std::string> words;
    std::string current;
    bool inWord = false;
    char quote = 0;
    for (const char character : line) {
        if (quote != 0) {
            if (character == quote) quote = 0;
            else current.push_back(character);
        } else if (character == '\'' || character == '"') {
            quote = character;
            inWord = true;
        } else if (std::isspace(static_cast<unsigned char>(character)) != 0) {
            if (inWord) words.push_back(std::move(current));
            current.clear();
            inWord = false;
        } else {
            current.push_back(character);
            inWord = true;
        }
    }
    if (inWord) words.push_back(std::move(current));
    return words;
}

[[nodiscard]] std::string Lower(std::string_view text) {
    std::string lower{ text };
    std::ranges::transform(lower, lower.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lower;
}

struct ActorSpec {
    std::string name;
    std::string item;
    std::string position;
    std::string color;
    std::string scale;
};

// "Player:Capsule@0,1,0#C0392B*1,2,1" is a red Capsule named Player at 0,1,0, one by two by one in size; each part may be left out.
[[nodiscard]] ActorSpec ParseActor(std::string_view text) {
    ActorSpec actor;
    if (const std::size_t star = text.find('*'); star != std::string_view::npos) {
        actor.scale = std::string{ text.substr(star + 1U) };
        text = text.substr(0U, star);
    }
    if (const std::size_t hash = text.find('#'); hash != std::string_view::npos) {
        actor.color = std::string{ text.substr(hash) };
        text = text.substr(0U, hash);
    }
    const std::size_t at = text.find('@');
    if (at != std::string_view::npos) {
        actor.position = std::string{ text.substr(at + 1U) };
        text = text.substr(0U, at);
    }
    const std::size_t colon = text.find(':');
    actor.name = std::string{ text.substr(0U, colon) };
    if (colon != std::string_view::npos) actor.item = std::string{ text.substr(colon + 1U) };
    return actor;
}

using Values = std::map<std::string, std::string>;

[[nodiscard]] Values Defaults(const Recipe& recipe) {
    Values values;
    for (auto& [name, fallback] : RecipeParameters(recipe)) values[name] = std::move(fallback);
    return values;
}

[[nodiscard]] std::string ParameterNames(const Recipe& recipe) {
    std::string names;
    for (const auto& [name, fallback] : RecipeParameters(recipe)) names += (names.empty() ? "" : ", ") + name + " (" + fallback + ")";
    return names.empty() ? "none" : names;
}

// {name} in a word becomes the value; a name that is not a value of the recipe is a mistake in the recipe.
[[nodiscard]] bool Substitute(std::string& word, const Values& values, std::string& error) {
    for (std::size_t open = word.find('{'); open != std::string::npos; open = word.find('{', open)) {
        const std::size_t close = word.find('}', open);
        if (close == std::string::npos) return true;
        const std::string name = word.substr(open + 1U, close - open - 1U);
        const auto found = values.find(name);
        if (found == values.end()) {
            error = "the recipe uses {" + name + "}, which it does not have";
            return false;
        }
        word.replace(open, close - open + 1U, found->second);
        open += found->second.size();
    }
    return true;
}

[[nodiscard]] std::string ReadFile(const fs::path& file) {
    std::ifstream input{ file, std::ios::binary };
    return std::string{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

[[nodiscard]] std::string Normalized(std::string_view text) {
    std::string out;
    for (const char character : text) {
        if (character != '\r') out.push_back(character);
    }
    return Trim(out);
}

// The name of the root object of a prefab file (/Game/<path>.kbprefab), which the copy a recipe works on keeps: `prefab apply` writes the
// copy's name back to the prefab, so a made-up name would rename the prefab's root. Empty when the file is not there or has no root.
[[nodiscard]] std::string PrefabRootName(const fs::path& projectRoot, std::string_view prefab) {
    constexpr std::string_view kGame = "/Game/";
    if (!prefab.starts_with(kGame)) return {};
    std::istringstream lines{ ReadFile(projectRoot / "Assets" / std::string{ prefab.substr(kGame.size()) }) };
    std::string line;
    std::string name;
    bool inNode = false;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "node") inNode = true;
        else if (inNode && line.starts_with("name=")) name = line.substr(5U);
        else if (inNode && line == "parent=-1") return name;
    }
    return {};
}

// The commands that act on a scene: with --scene they act on that one.
[[nodiscard]] bool IsSceneCommand(std::string_view command) {
    return command == "entity" || command == "component" || command == "scene-attach" || command == "scene-detach" || command == "prefab";
}

using Operation = std::vector<std::string>;

struct Expansion {
    fs::path root;
    std::optional<std::string> scene;  // --scene, or the scratch scene of a prefab
    bool prefab = false;
    std::vector<Operation> operations;
    std::vector<std::string> notes;
    std::vector<std::string> scriptFiles;                 // every script the recipe uses, by file name
    std::map<std::string, std::string> scriptByCode;      // name and code -> the file name it got
    std::set<std::string> taken;                          // file names chosen in this run
    std::set<std::string> made;                           // objects this run makes
    std::string error;
};

// The file a recipe's script goes to: its own name when that is free or holds the same text, else the name with a number,
// so a script of the customer's with the same name is never overwritten.
[[nodiscard]] std::string ScriptFileName(Expansion& expansion, const Script& script) {
    const std::string key = script.name + '\n' + script.code;
    if (const auto known = expansion.scriptByCode.find(key); known != expansion.scriptByCode.end()) return known->second;
    for (int number = 1; number <= 9; ++number) {
        const std::string name = script.name + (number == 1 ? std::string{} : std::to_string(number));
        if (expansion.taken.contains(name)) continue;
        const fs::path file = expansion.root / "Assets" / "Scripts" / (name + ".lua");
        std::error_code error;
        if (fs::exists(file, error)) {
            if (Normalized(ReadFile(file)) != Normalized(script.code)) continue;
            expansion.notes.push_back("script " + name + " was there already, used as it is");
        } else {
            expansion.operations.push_back({ "script-new", "--name", name, "--language", "lua", "--folder", "Assets/Scripts", "--code", script.code });
            expansion.notes.push_back("script " + name);
        }
        expansion.taken.insert(name);
        expansion.scriptByCode[key] = name;
        if (std::ranges::find(expansion.scriptFiles, name) == expansion.scriptFiles.end()) expansion.scriptFiles.push_back(name);
        return name;
    }
    expansion.error = "the script " + script.name + " and its numbered names are taken by other scripts of the project; rename yours";
    return {};
}

struct Place {
    std::string target;
    bool create = false;
    std::string position;
};

// Where a corner word puts a widget of the given size: anchors, pivot and the offsets from them.
[[nodiscard]] bool CornerSettings(std::string_view corner, const std::string& width, const std::string& height, std::vector<std::string>& settings) {
    struct Corner {
        std::string_view name;
        int anchorX;  // 0 left, 1 right, 2 middle
        int anchorY;  // 0 bottom, 1 top, 2 middle
    };
    constexpr std::array<Corner, 7> kCorners{ { { "top-left", 0, 1 }, { "top-right", 1, 1 }, { "top-center", 2, 1 }, { "bottom-left", 0, 0 },
        { "bottom-right", 1, 0 }, { "bottom-center", 2, 0 }, { "center", 2, 2 } } };
    const auto found = std::ranges::find_if(kCorners, [&](const Corner& each) { return each.name == corner; });
    if (found == kCorners.end()) return false;
    const double w = std::atof(width.c_str());
    const double h = std::atof(height.c_str());
    constexpr double kMargin = 20.0;
    // The widget's own corner sits at the screen's, a margin in; a middle one is centred on the axis.
    const auto low = [&](int side, double size) { return side == 0 ? kMargin : side == 1 ? -kMargin - size : -size / 2.0; };
    const auto number = [](double value) { std::ostringstream text; text << value; return text.str(); };
    // A new widget has its anchors and its minimum at 0 and its maximum at its size. A minimum may not pass the maximum, so of each pair
    // the one the widget moves towards is set first: the maximum when it moves up, else the minimum.
    settings.clear();
    const std::array<int, 2> sides{ found->anchorX, found->anchorY };
    const std::array<std::string, 2> axes{ "x", "y" };
    for (std::size_t axis = 0U; axis < 2U; ++axis) {
        const double anchorValue = sides[axis] == 2 ? 0.5 : static_cast<double>(sides[axis]);
        const bool up = anchorValue > 0.0;
        const std::string anchorName = axes[axis];
        const std::string value = number(anchorValue);
        settings.push_back(std::string{ up ? "anchorMax." : "anchorMin." } + anchorName + "=" + value);
        settings.push_back(std::string{ up ? "anchorMin." : "anchorMax." } + anchorName + "=" + value);
    }
    settings.push_back("pivot.x=0.5");
    settings.push_back("pivot.y=0.5");
    for (std::size_t axis = 0U; axis < 2U; ++axis) {
        const double size = axis == 0U ? w : h;
        const double minimum = low(sides[axis], size);
        const bool up = minimum > 0.0;
        const std::string first = std::string{ up ? "offsetMax." : "offsetMin." } + axes[axis];
        const std::string second = std::string{ up ? "offsetMin." : "offsetMax." } + axes[axis];
        settings.push_back(first + "=" + number(up ? minimum + size : minimum));
        settings.push_back(second + "=" + number(up ? minimum : minimum + size));
    }
    return true;
}

[[nodiscard]] bool Expand(Expansion& expansion, const Recipe& recipe, Values values, const Place& place, int depth);

[[nodiscard]] bool ExpandUse(Expansion& expansion, const Recipe& parent, const std::vector<std::string>& words, const std::string& parentTarget, int depth) {
    if (words.size() < 2U) {
        expansion.error = "a use line of " + std::string{ parent.id } + " names no recipe";
        return false;
    }
    const Recipe* child = FindRecipe(words[1]);
    if (child == nullptr) {
        expansion.error = std::string{ parent.id } + " uses " + words[1] + ", which is not a recipe";
        return false;
    }
    Values childValues = Defaults(*child);
    const ActorSpec actor = ParseActor(child->actor);
    Place place{ .target = actor.name.empty() ? parentTarget : actor.name, .create = !actor.name.empty(), .position = actor.position };
    for (std::size_t index = 2U; index < words.size(); ++index) {
        const std::size_t equals = words[index].find('=');
        if (equals == std::string::npos) {
            expansion.error = "the use line " + std::string{ parent.id } + " > " + std::string{ child->id } + " has '" + words[index] + "' without a name= before it";
            return false;
        }
        const std::string name = words[index].substr(0U, equals);
        std::string value = words[index].substr(equals + 1U);
        // A value the recipe has by that name is that; else target, name and at say which object it works on, and where.
        if (childValues.contains(name)) {
            childValues[name] = std::move(value);
        } else if (name == "target") {
            place = Place{ .target = value, .create = false, .position = {} };
        } else if (name == "name" && !actor.name.empty()) {
            place.target = value;
            place.create = true;
        } else if (name == "at" && !actor.name.empty()) {
            place.position = value;
        } else {
            expansion.error = std::string{ child->id } + " has no value '" + name + "' (" + ParameterNames(*child) + ")";
            return false;
        }
    }
    return Expand(expansion, *child, std::move(childValues), place, depth + 1);
}

[[nodiscard]] bool Expand(Expansion& expansion, const Recipe& recipe, Values values, const Place& place, int depth) {
    if (depth > 4) {
        expansion.error = "recipes use each other too deep at " + std::string{ recipe.id };
        return false;
    }
    values["target"] = place.target;
    // A value may name the target ("{target}Died").
    for (auto& [name, value] : values) {
        if (name != "target" && !Substitute(value, values, expansion.error)) return false;
    }
    std::map<std::string, std::string> files;
    for (const Script& script : RecipeScripts(recipe)) {
        const std::string file = ScriptFileName(expansion, script);
        if (file.empty()) return false;
        files[script.name] = file;
    }
    if (place.create) {
        const ActorSpec actor = ParseActor(recipe.actor);
        if (expansion.prefab) {
            expansion.error = std::string{ recipe.id } + " makes new objects, so it cannot go on a prefab; a recipe for one object can (recipe list --kind brick)";
            return false;
        }
        if (!expansion.made.insert(place.target).second) {
            expansion.error = "two objects would be named '" + place.target + "'; give one its own name= in the use line or --name";
            return false;
        }
        Operation add{ "entity", "add" };
        if (!actor.item.empty()) add.insert(add.end(), { "--create", actor.item });
        add.insert(add.end(), { "--name", place.target });
        if (!place.position.empty()) add.insert(add.end(), { "--position", place.position });
#if KB_CLI_RENDERER
        // A colour is a material, which only a build with the renderer makes.
        if (!actor.color.empty()) add.insert(add.end(), { "--color", actor.color });
#endif
        if (!actor.scale.empty()) add.insert(add.end(), { "--scale", actor.scale });
        if (expansion.scene.has_value()) add.insert(add.end(), { "--scene", *expansion.scene });
        expansion.operations.push_back(std::move(add));
        expansion.notes.push_back("object " + place.target);
    }
    std::istringstream lines{ std::string{ recipe.steps } };
    std::string line;
    while (std::getline(lines, line)) {
        std::vector<std::string> words = Words(Trim(line));
        if (words.empty() || words.front().starts_with('#')) continue;
        for (std::string& word : words) {
            if (!Substitute(word, values, expansion.error)) return false;
        }
        // @new: only for an object this run makes. @once <file>: only when the project does not have that file yet.
        bool skip = false;
        while (!words.empty() && !skip && (words.front() == "@new" || (words.front() == "@once" && words.size() > 2U))) {
            if (words.front() == "@new") {
                skip = !place.create;
                words.erase(words.begin());
            } else {
                std::error_code ignored;
                skip = fs::exists(expansion.root / words[1], ignored) || !expansion.taken.insert(words[1]).second;
                words.erase(words.begin(), words.begin() + 2);
            }
        }
        if (skip || words.empty()) continue;
        // @place <widget> <corner> <width> <height>: a widget at a corner of the screen.
        if (words.front() == "@place") {
            std::vector<std::string> settings;
            if (words.size() != 5U || !CornerSettings(words[2], words[3], words[4], settings)) {
                expansion.error = std::string{ recipe.id } + " has a @place line that is not '@place <widget> <corner> <width> <height>'";
                return false;
            }
            Operation set{ "component", "set", "--entity", words[1], "--type", "Rect Transform" };
            for (const std::string& setting : settings) set.insert(set.end(), { "--set", setting });
            if (expansion.scene.has_value()) set.insert(set.end(), { "--scene", *expansion.scene });
            expansion.operations.push_back(std::move(set));
            continue;
        }
        if (words.front() == "use") {
            if (!ExpandUse(expansion, recipe, words, place.target, depth)) return false;
            continue;
        }
        if (words.front() == "attach") {
            const auto file = words.size() > 1U ? files.find(words[1]) : files.end();
            if (file == files.end()) {
                expansion.error = std::string{ recipe.id } + " attaches a script it does not have";
                return false;
            }
            std::string code;
            for (const Script& script : RecipeScripts(recipe)) {
                if (script.name == words[1]) code = script.code;
            }
            // On the target, or on the node a --node after the script names.
            const bool otherNode = std::ranges::find(words, "--node") != words.end();
            Operation attach{ "scene-attach", "--script", "/Game/Scripts/" + file->second + ".lua" };
            if (!otherNode) attach.insert(attach.end(), { "--node", place.target });
            for (const std::string& name : ExposedNames(code)) {
                if (values.contains(name)) attach.insert(attach.end(), { "--set", name + "=" + values.at(name) });
            }
            attach.insert(attach.end(), words.begin() + 2, words.end());
            if (expansion.scene.has_value()) attach.insert(attach.end(), { "--scene", *expansion.scene });
            expansion.operations.push_back(std::move(attach));
            continue;
        }
        if (expansion.prefab && words.front() == "entity" && words.size() > 1U && words[1] == "add") {
            expansion.error = std::string{ recipe.id } + " adds objects, so it cannot go on a prefab";
            return false;
        }
        if (IsSceneCommand(words.front()) && expansion.scene.has_value() && std::ranges::find(words, "--scene") == words.end()) {
            words.insert(words.end(), { "--scene", *expansion.scene });
        }
        expansion.operations.push_back(std::move(words));
    }
    return true;
}

[[nodiscard]] std::string OperationText(const Operation& operation) {
    std::string text;
    for (std::size_t index = 0U; index < operation.size(); ++index) {
        text += (index == 0U ? "" : " ") + (operation[index].find(' ') != std::string::npos || operation[index].find('\n') != std::string::npos
            ? "'" + operation[index].substr(0U, operation[index].find('\n')) + (operation[index].find('\n') != std::string::npos ? " ...'" : "'") : operation[index]);
    }
    return text;
}

// Of a transaction's report, the command that failed with its reasons and the verdict: not every script that went before it.
[[nodiscard]] std::string FailureText(const std::string& report) {
    std::istringstream lines{ report };
    std::string line;
    std::string text;
    bool failed = false;
    while (std::getline(lines, line)) {
        if (line.find("  FAILED") != std::string::npos) {
            failed = true;
            text += line.substr(0U, 300U) + "\n";
        } else if (failed || line.starts_with("error:") || line.starts_with("failed:") || line.starts_with("rolled back:")) {
            text += line + "\n";
        }
    }
    return text.empty() ? report : text;
}

[[nodiscard]] int Run(const ArgumentList& arguments, CommandIo io) {
    static constexpr std::array<std::string_view, 8> kAllowed{ "--project", "--entity", "--prefab", "--name", "--position", "--set", "--scene", "--dry-run" };
    const std::vector<std::string> unknown = arguments.UnknownOptions(kAllowed);
    const std::vector<std::string>& positionals = arguments.Positionals();
    const std::optional<std::string> project = arguments.Option("--project");
    if (!unknown.empty() || positionals.size() != 2U || !project.has_value()) {
        io.err << (!unknown.empty() ? "error: unknown option " + unknown.front() + "\n" : "error: recipe run needs a recipe and --project <dir>\n") << kUsage;
        return 1;
    }
    const Recipe* recipe = FindRecipe(positionals[1]);
    if (recipe == nullptr) {
        std::vector<std::string_view> ids;
        for (const Recipe& candidate : Recipes()) ids.push_back(candidate.id);
        const std::string closest = kb::script::NearestScriptName(positionals[1], std::span<const std::string_view>{ ids });
        io.err << "error: there is no recipe '" << positionals[1] << '\'' << (closest.empty() ? std::string{} : "; did you mean " + closest + "?")
               << " (guide --search \"<the customer's words>\" finds one, recipe list lists them)\n";
        return 1;
    }
    Values values = Defaults(*recipe);
    for (const std::string& setting : arguments.Options("--set")) {
        const std::size_t equals = setting.find('=');
        const std::string name = setting.substr(0U, equals);
        if (equals == std::string::npos || !values.contains(name)) {
            io.err << "error: '" << setting << "' is not a value of " << recipe->id << "; its values are " << ParameterNames(*recipe) << " (--set name=value)\n";
            return 1;
        }
        values[name] = setting.substr(equals + 1U);
    }
    const std::optional<std::string> entity = arguments.Option("--entity");
    const std::optional<std::string> prefab = arguments.Option("--prefab");
    if (entity.has_value() && prefab.has_value()) {
        io.err << "error: --entity and --prefab are two different targets; give one\n";
        return 1;
    }
    if ((entity.has_value() || prefab.has_value()) && !recipe->onObject) {
        io.err << "error: " << recipe->id << " builds objects of its own, so it is run without --entity or --prefab; a recipe for one existing object is one of `recipe list --kind brick`\n";
        return 1;
    }
    const ActorSpec actor = ParseActor(recipe->actor);
    std::string root = *project;
    std::string error;
    Expansion expansion;
    expansion.root = fs::path{ root };
    expansion.scene = arguments.Option("--scene");
    Place place{ .target = entity.value_or(arguments.Option("--name").value_or(actor.name)), .create = !entity.has_value() && !actor.name.empty(),
        .position = arguments.Option("--position").value_or(actor.position) };

    std::string scratch;
    if (prefab.has_value()) {
        // A prefab is changed through a copy of it in a scene made for the purpose: the recipe's commands act on the copy, `prefab apply`
        // writes it back, and the scene is deleted again.
        const std::string rootName = PrefabRootName(fs::path{ root }, *prefab);
        if (rootName.empty()) {
            io.err << "error: no prefab " << *prefab << " in the project (a path like /Game/Prefabs/Name.kbprefab)" << '\n';
            return 1;
        }
        scratch = "RecipeScratch";
        for (int number = 2; fs::exists(fs::path{ root } / "Assets" / "Scenes" / (scratch + ".21kbscene")); ++number) scratch = "RecipeScratch" + std::to_string(number);
        std::istringstream none;
        std::ostringstream out;
        std::ostringstream err;
        if (RunCli(std::vector<std::string>{ "scene-new", "--project", root, "--name", scratch, "--template", "Empty", "--default", "no" }, none, CommandIo{ .out = out, .err = err }) != 0) {
            io.err << "error: the prefab could not be opened for the recipe: " << err.str();
            return 1;
        }
        expansion.prefab = true;
        expansion.scene = scratch;
        place = Place{ .target = rootName, .create = false, .position = {} };
        // The copy is named as the prefab's root already; naming it so again would collide with itself.
        expansion.operations.push_back({ "prefab", "place", "--scene", scratch, "--prefab", *prefab });
    }
    const auto leave = [&]() {
        if (scratch.empty()) return;
        std::error_code ignored;
        fs::remove(fs::path{ root } / "Assets" / "Scenes" / (scratch + ".21kbscene"), ignored);
        fs::remove(fs::path{ root } / "Assets" / "Scenes" / (scratch + ".meta"), ignored);
    };
    if (recipe->onObject && place.target.empty()) {
        io.err << "error: " << recipe->id << " works on an object: say which one with --entity <Name> (scene-list shows the objects) or --prefab, or a --name for the new one\n";
        return 1;
    }
    if (!Expand(expansion, *recipe, std::move(values), place, 0)) {
        leave();
        io.err << "error: " << expansion.error << '\n';
        return 1;
    }
    if (prefab.has_value()) expansion.operations.push_back({ "prefab", "apply", "--scene", scratch, "--entity", place.target });
    if (!expansion.scriptFiles.empty()) {
        Operation validate{ "validate" };
        for (const std::string& file : expansion.scriptFiles) validate.push_back("Assets/Scripts/" + file + ".lua");
        expansion.operations.push_back(std::move(validate));
    }

    if (arguments.Flag("--dry-run")) {
        for (const Operation& operation : expansion.operations) io.out << OperationText(operation) << '\n';
        leave();
        io.out << "dry run: " << expansion.operations.size() << " command(s), nothing was written\n";
        return 0;
    }
    std::istringstream noInput;
    std::ostringstream out;
    std::ostringstream err;
    const int code = RunBatchOperations(*project, expansion.operations, false, false, true, CommandIo{ .out = out, .err = err },
        [&noInput](std::span<const std::string> words, CommandIo target) { return RunCli(words, noInput, target); });
    leave();
    if (code != 0) {
        io.err << "error: the recipe " << recipe->id << " failed and changed nothing:\n" << FailureText(out.str() + err.str());
        return code;
    }
    io.out << "recipe " << recipe->id << " done";
    if (entity.has_value() || prefab.has_value()) io.out << " on " << (entity ? *entity : *prefab) << " (what it has is kept)";
    io.out << ": ";
    for (std::size_t index = 0U; index < expansion.notes.size(); ++index) io.out << (index == 0U ? "" : ", ") << expansion.notes[index];
    if (expansion.notes.empty()) io.out << "the components and scripts it needs were added";
    io.out << "\ncheck: " << (recipe->check.empty() ? std::string_view{ "run --frames 120 (ends with 0 diagnostics)" } : recipe->check) << '\n';
    return 0;
}

[[nodiscard]] int List(const ArgumentList& arguments, CommandIo io) {
    const std::string kind = Lower(arguments.Option("--kind").value_or(std::string{}));
    if (!kind.empty() && kind != "game" && kind != "system" && kind != "brick") {
        io.err << "error: --kind is game, system or brick\n";
        return 1;
    }
    for (const Kind each : { Kind::Game, Kind::System, Kind::Brick }) {
        if (!kind.empty() && kind != KindName(each)) continue;
        std::size_t count = 0U;
        std::string text;
        for (const Recipe& recipe : Recipes()) {
            if (recipe.kind != each) continue;
            text += "  " + std::string{ recipe.id } + " - " + std::string{ recipe.purpose } + "\n";
            ++count;
        }
        io.out << KindName(each) << "s (" << count << "):\n" << text;
    }
    return 0;
}

[[nodiscard]] int Show(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    const Recipe* recipe = positionals.size() == 2U ? FindRecipe(positionals[1]) : nullptr;
    if (recipe == nullptr) {
        io.err << "error: recipe show needs the id of a recipe (recipe list lists them)\n";
        return 1;
    }
    io.out << recipe->id << " (" << KindName(recipe->kind) << "): " << recipe->title << "\n" << recipe->purpose << "\nvalues (--set name=value): " << ParameterNames(*recipe) << '\n';
    std::string uses;
    std::istringstream lines{ std::string{ recipe->steps } };
    std::string line;
    while (std::getline(lines, line)) {
        const std::vector<std::string> words = Words(Trim(line));
        if (words.size() > 1U && words.front() == "use") uses += (uses.empty() ? "" : ", ") + words[1];
    }
    if (!uses.empty()) io.out << "made of: " << uses << '\n';
    io.out << "run: kb_cli recipe run " << recipe->id << " --project <dir>" << (recipe->onObject ? " [--entity <Object> | --prefab /Game/<path>.kbprefab]" : "") << '\n';
    return 0;
}

} // namespace

std::string_view KindName(Kind kind) noexcept {
    return kind == Kind::Game ? "game" : kind == Kind::System ? "system" : "brick";
}

std::span<const Recipe> Recipes() {
    static const std::vector<Recipe> all = [] {
        std::vector<Recipe> recipes;
        // The small one first: of two answers that fit equally well, the one that does less is the one asked for.
        for (const std::span<const Recipe> floor : { BrickRecipes(), NetworkBrickRecipes(), SystemRecipes(), NetworkSystemRecipes(), GameRecipes(), NetworkGameRecipes() }) {
            recipes.insert(recipes.end(), floor.begin(), floor.end());
        }
        return recipes;
    }();
    return all;
}

const Recipe* FindRecipe(std::string_view id) {
    const std::span<const Recipe> all = Recipes();
    const auto found = std::ranges::find_if(all, [&](const Recipe& recipe) { return recipe.id == id; });
    return found != all.end() ? &*found : nullptr;
}

std::vector<std::pair<std::string, std::string>> RecipeParameters(const Recipe& recipe) {
    std::vector<std::pair<std::string, std::string>> parameters;
    for (const std::string& part : Split(recipe.parameters, '|')) {
        const std::size_t equals = part.find('=');
        parameters.emplace_back(part.substr(0U, equals), equals == std::string::npos ? std::string{} : part.substr(equals + 1U));
    }
    return parameters;
}

std::vector<Script> RecipeScripts(const Recipe& recipe) {
    std::vector<Script> scripts;
    std::istringstream lines{ std::string{ recipe.scripts } };
    std::string line;
    while (std::getline(lines, line)) {
        if (line.starts_with("--- ")) {
            scripts.push_back({ Trim(std::string_view{ line }.substr(4U)), {} });
        } else if (!scripts.empty()) {
            scripts.back().code += line + "\n";
        }
    }
    return scripts;
}

std::vector<std::string> ExposedNames(std::string_view code) {
    std::vector<std::string> names;
    std::istringstream lines{ std::string{ code } };
    std::string line;
    while (std::getline(lines, line)) {
        const std::vector<std::string> words = Words(line);
        if (words.size() >= 3U && words[0] == "--" && words[1] == "@expose") names.push_back(words[2]);
    }
    return names;
}

std::vector<RecipeHit> SearchRecipes(std::string_view words) {
    const std::vector<std::string> query = kb::script::SearchWords(words);
    std::vector<RecipeHit> hits;
    if (query.empty()) return hits;
    for (const Recipe& recipe : Recipes()) {
        const std::vector<std::string> name = kb::script::SearchWords(std::string{ recipe.id } + " " + std::string{ recipe.title });
        // A word with a + before it is a gate: the recipe answers a request only when the request says one of its gates (or a word that
        // starts like one), so a recipe about a network is not offered for "the player" and "a character" alone.
        std::vector<std::string> gates;
        std::string plain;
        for (const std::string& token : Words(recipe.words)) {
            const bool gate = token.starts_with('+');
            for (std::string& word : kb::script::SearchWords(token)) {
                if (gate) gates.push_back(word);
            }
            plain += (gate ? token.substr(1U) : token) + ' ';
        }
        if (!gates.empty() && std::ranges::none_of(query, [&](const std::string& word) { return kb::script::SearchWordScore(word, {}, gates) >= 1; })) continue;
        std::vector<std::string> other = kb::script::SearchWords(plain);
        // A game's purpose lists its parts; only what a customer calls the kind of game finds it, so the parts are found as themselves.
        if (recipe.kind != Kind::Game) {
            for (std::string& word : kb::script::SearchWords(recipe.purpose)) other.push_back(std::move(word));
        }
        const int score = kb::script::SearchScore(query, name, other);
        if (score == 0) continue;
        std::vector<std::string> covered;
        for (const std::string& word : query) {
            if (kb::script::SearchWordScore(word, name, other) >= 2) covered.push_back(word);
        }
        // The floor it is on counts as something answered of its own: the whole (a game, a system) and its part (a brick) are both worth
        // showing when they fit about equally well.
        covered.push_back("#" + std::string{ KindName(recipe.kind) });
        hits.push_back({ score, std::string{ KindName(recipe.kind) },
            "recipe " + std::string{ recipe.id } + " (" + std::string{ KindName(recipe.kind) } + ") - " + std::string{ recipe.purpose } +
                ": recipe run " + std::string{ recipe.id } + (recipe.onObject ? " [--entity <Object> | --prefab /Game/<path>.kbprefab]" : ""),
            std::move(covered) });
    }
    std::ranges::stable_sort(hits, [](const RecipeHit& left, const RecipeHit& right) { return left.score > right.score; });
    return hits;
}

} // namespace kb::cli::recipes

namespace kb::cli {

int RunRecipeCommand(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    const std::string action = positionals.empty() ? std::string{} : positionals.front();
    if (action == "run") return recipes::Run(arguments, io);
    if (action == "list") return recipes::List(arguments, io);
    if (action == "show") return recipes::Show(arguments, io);
    io.err << recipes::kUsage;
    return 1;
}

} // namespace kb::cli
