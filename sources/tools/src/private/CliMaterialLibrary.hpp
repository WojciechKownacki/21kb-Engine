#pragma once

#include "CliCommon.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The library of ready materials: a few master materials (graphs written as graph text, with exposed parameters) and
// presets that are instances of them (a master and the parameter values that make the look). `material-new --preset
// <name>` puts the master in the project once and creates the instance; `guide --search` finds a preset by words.
namespace kb::cli::material {

struct MaterialMaster {
    std::string_view name;  // Pbr, Mottled, ...
    std::string_view text;  // graph text; its Parameter lines are what a preset sets
};

struct MaterialPreset {
    std::string_view name;    // gold, oak, neon-pink
    std::string_view family;  // metal, wood, stone, ground, fabric, plastic, glass, water, emissive, toon, scifi, effect, ui, post
    std::string_view master;  // one of the masters
    std::string_view tags;    // the words a customer uses for it (names, synonyms)
    std::string_view values;  // "Name=value|Name=value": the master's parameters this preset changes
};

[[nodiscard]] std::span<const MaterialMaster> MaterialMasters() noexcept;
[[nodiscard]] std::span<const MaterialPreset> MaterialPresets() noexcept;
[[nodiscard]] const MaterialMaster* FindMaterialMaster(std::string_view name) noexcept;
[[nodiscard]] const MaterialPreset* FindMaterialPreset(std::string_view name) noexcept;
// The "Name=value" settings of a preset, in order.
[[nodiscard]] std::vector<std::string> PresetSettings(const MaterialPreset& preset);
// The parameters a master exposes, by name.
[[nodiscard]] std::vector<std::string> MasterParameters(const MaterialMaster& master);

struct LibraryHit {
    int score = 0;
    std::string kind;    // preset, node
    std::string family;  // what keeps three answers different
    std::string line;    // what to tell the agent: the name and the command that uses it
    std::vector<std::string> covered;  // the words of the request this entry answers
};
// Every preset by family, one line per family: what `guide --topic material-presets` prints.
[[nodiscard]] std::string MaterialPresetIndex();
// The presets and nodes that best fit the customer's words, best first.
[[nodiscard]] std::vector<LibraryHit> SearchMaterialLibrary(std::string_view words);

// `material-new --preset <name> [--name <Name>] [--set Parameter=value ...] [--entity <Object> [--scene <path>]] [--prefab /Game/<path>.kbprefab]`:
// the master in the project (once), an instance of it with the preset's values, and the instance on the object or prefab.
[[nodiscard]] int RunMaterialPresetNew(const ArgumentList& arguments, CommandIo io);

} // namespace kb::cli::material
