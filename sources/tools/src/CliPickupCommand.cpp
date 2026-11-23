#include "CliCommands.hpp"
#include "CliProgram.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// pickup: a coin (or any object) the player collects by touching it, with a counter on the screen, made in one step: the
// object with a trigger Collider, a Canvas with a Text, the two Lua scripts, their attachment and a validate of both scripts.
// It is a transaction of the ordinary commands (`entity add`, `component set`, `script-new`, `scene-attach`, `validate`),
// so it takes effect completely or, when one of them fails, not at all.

namespace kb::cli {
namespace {

constexpr std::string_view kUsage =
    "usage: kb_cli pickup new --project <dir> [--name Coin] [--count 1] [--position x,y,z] [--player Player] [--label Coins]\n"
    "                         [--with-player yes] [--side-view yes] [--scene <path>]\n";

[[nodiscard]] bool IsName(std::string_view text) {
    return !text.empty() && std::isalpha(static_cast<unsigned char>(text.front())) != 0 &&
        std::ranges::all_of(text, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; });
}

[[nodiscard]] std::string Quoted(std::string_view text) {
    std::string quoted = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') quoted += '\\';
        quoted += c;
    }
    return quoted + '"';
}

int New(const ArgumentList& arguments, CommandIo io) {
    static constexpr std::array<std::string_view, 9> kAllowed{ "--project", "--name", "--count", "--position", "--player", "--label", "--with-player", "--side-view", "--scene" };
    const std::vector<std::string> unknown = arguments.UnknownOptions(kAllowed);
    const std::optional<std::string> project = arguments.Option("--project");
    if (!unknown.empty() || !project.has_value()) {
        io.err << (unknown.empty() ? "error: pickup new needs --project <dir>\n" : "error: unknown option " + unknown.front() + "\n") << kUsage;
        return 1;
    }
    const std::string name = arguments.Option("--name").value_or("Coin");
    const std::string player = arguments.Option("--player").value_or("Player");
    const std::string label = arguments.Option("--label").value_or(name + "s");
    const std::string position = arguments.Option("--position").value_or("0,1,3");
    const std::optional<std::string> scene = arguments.Option("--scene");
    const bool sideView = arguments.Option("--side-view").value_or("no") == "yes";
    const std::string countText = arguments.Option("--count").value_or("1");
    const int count = countText.size() <= 2U && std::ranges::all_of(countText, [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }) ? std::atoi(countText.c_str()) : 0;
    double start[3]{};
    char comma1 = 0;
    char comma2 = 0;
    std::istringstream coordinates{ position };
    if (!(coordinates >> start[0] >> comma1 >> start[1] >> comma2 >> start[2]) || comma1 != ',' || comma2 != ',' || !coordinates.eof()) {
        io.err << "error: --position is x,y,z, for example 0,1,3\n";
        return 1;
    }
    if (count < 1 || count > 20) {
        io.err << "error: --count is a number from 1 to 20\n";
        return 1;
    }
    if (!IsName(name) || !IsName(player)) {
        io.err << "error: --name and --player are one word of letters and digits (Coin, Gem, Player)\n";
        return 1;
    }
    if (label.find('"') != std::string::npos || label.find('\\') != std::string::npos) {
        io.err << "error: --label has no quotes or backslashes\n";
        return 1;
    }
    const std::string counter = name + "Counter";
    const std::string hud = name + "Hud";
    const std::string event = name + "Collected";

    const std::string pickupScript = "function OnTriggerEnter(self, event)\n"
                                     "    if World.Name(event.args.other) ~= " + Quoted(player) + " then return end\n"
                                     "    Emit(" + Quoted(event) + ", {})\n"
                                     "    World.Destroy(self.entity)\n"
                                     "end\n";
    const std::string counterScript = "local count = 0\n"
                                      "function " + event + "(self, event)\n"
                                      "    count = count + 1\n"
                                      "    self:SetProperty(\"Text\", \"content\", " + Quoted(label + ": ") + " .. count)\n"
                                      "    Log(" + Quoted(name + " count: ") + " .. count)\n"
                                      "end\n";

    using Words = std::vector<std::string>;
    const auto onScene = [&scene](Words words) {
        if (scene.has_value()) {
            words.emplace_back("--scene");
            words.push_back(*scene);
        }
        return words;
    };
    std::vector<Words> operations;
    if (arguments.Option("--with-player").value_or("no") == "yes") {
        operations.push_back(onScene({ "entity", "add", "--create", "Third Person Player", "--position", "0,1,0" }));
    }
    // Coin, Coin2, Coin3, ... in a row along Z (along X in a side view), 1.2 units apart. Each is a flat disc whose trigger zone is wider and
    // taller than it (1.8 by 1 by 1.8 units), so a player passing near it still touches it; all of them share the one script. In a side view
    // a coin is a ball with a 2D circle trigger instead, which is what a 2D character touches.
    std::vector<std::string> coins;
    for (int index = 1; index <= count; ++index) {
        coins.push_back(index == 1 ? name : name + std::to_string(index));
        std::ostringstream place;
        const double along = 1.2 * (index - 1);
        place << start[0] + (sideView ? along : 0.0) << ',' << start[1] << ',' << start[2] + (sideView ? 0.0 : along);
        if (sideView) {
            operations.push_back(onScene({ "entity", "add", "--create", "Sphere", "--name", coins.back(), "--position", place.str(), "--scale", "0.6,0.6,0.2", "--color", "#F5B921" }));
            operations.push_back(onScene({ "component", "remove", "--entity", coins.back(), "--type", "Collider" }));
            operations.push_back(onScene({ "component", "add", "--entity", coins.back(), "--type", "Collider2D", "--set", "shape=Circle", "--set", "isTrigger=true", "--set", "radius=0.8" }));
            continue;
        }
        operations.push_back(onScene({ "entity", "add", "--create", "Cylinder", "--name", coins.back(), "--position", place.str(), "--scale", "0.6,0.1,0.6", "--color", "#F5B921" }));
        operations.push_back(onScene({ "component", "set", "--entity", coins.back(), "--type", "Collider", "--set", "shape=Box", "--set", "trigger=true", "--set", "boxSize.x=3", "--set", "boxSize.y=10", "--set", "boxSize.z=3" }));
    }
    operations.push_back(onScene({ "entity", "add", "--create", "Canvas", "--name", hud }));
    operations.push_back(onScene({ "entity", "add", "--create", "Text", "--name", counter, "--parent", hud }));
    operations.push_back(onScene({ "component", "set", "--entity", counter, "--type", "Text", "--set", "content=" + label + ": 0", "--set", "fontSize=32" }));
    operations.push_back({ "script-new", "--name", name, "--language", "lua", "--folder", "Assets/Scripts", "--code", pickupScript });
    operations.push_back({ "script-new", "--name", counter, "--language", "lua", "--folder", "Assets/Scripts", "--code", counterScript });
    for (const std::string& coin : coins) {
        operations.push_back(onScene({ "scene-attach", "--node", coin, "--script", "/Game/Scripts/" + name + ".lua" }));
    }
    operations.push_back(onScene({ "scene-attach", "--node", counter, "--script", "/Game/Scripts/" + counter + ".lua" }));
    operations.push_back({ "validate", "Assets/Scripts/" + name + ".lua", "Assets/Scripts/" + counter + ".lua" });

    std::istringstream noInput;
    std::ostringstream out;
    std::ostringstream err;
    const int code = RunBatchOperations(*project, std::move(operations), false, false, true, CommandIo{ .out = out, .err = err },
        [&noInput](std::span<const std::string> words, CommandIo target) { return RunCli(words, noInput, target); });
    if (code != 0) {
        io.out << out.str();
        io.err << err.str();
        return code;
    }
    io.out << "added " << (count == 1 ? name : std::to_string(count) + " coins " + name + ".." + coins.back()) << " from " << position << " (a trigger Collider: each is collected when " << player << " touches it) and a counter '" << counter
           << "' on the screen; both scripts validate\n"
           << "check: run --frames 150 --press W@5+90   (the log shows '" << name << " count: 1' when the player walked into it)\n";
    return 0;
}

} // namespace

int RunPickupCommand(const ArgumentList& arguments, CommandIo io) {
    const std::string action = arguments.Positionals().size() == 1U ? arguments.Positionals().front() : std::string{};
    if (action == "new") return New(arguments, io);
    io.err << kUsage;
    return 1;
}

} // namespace kb::cli
