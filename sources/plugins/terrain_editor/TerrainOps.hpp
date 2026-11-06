#pragma once

#include "TerrainBrush.hpp"
#include "TerrainLayerPainter.hpp"
#include "engine/assets/TerrainAsset.hpp"
#include "engine/core/JsonValue.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

namespace kb::terrain_editor {

// Terrain edits as data: one JSON object per operation, the same words for the kb_cli terrain
// commands, a batch file (`terrain apply --ops`) and the editor's automation scenarios.
// Positions are in the terrain's local plane (x, z from its centre, in world units). A number may
// also be given as text ("12.5"), which is how command line options arrive.
//
//   {"op":"brush", "mode":"raise", "x":0, "z":0, "radius":8, "strength":1, "falloff":0.65}
//   {"op":"brush", "mode":"smooth", "path":[[0,0],[20,10]], "radius":6, "spacing":2}
//   {"op":"paint", "layer":1, "path":"0,0;30,0", "radius":5, "opacity":1, "erase":false}
//   {"op":"auto-paint", "layer":2, "slope":"35..90", "height":"-100..100"}
//   {"op":"channel", "nodes":[[x,z,level,halfWidth,depth],...], "bank":3}   (a river bed cut along the nodes)
//
// A brush op reads: mode (raise lower smooth flatten noise terrace cut-hole fill-hole ramp stamp),
// shape, radius, strength, falloff, spacing (along a path), and by mode: height (flatten: the
// target; "sample" takes the terrain's own at the start), step and softness (terrace), seed,
// noiseKind (gradient|value), noiseScale and noiseOctaves (noise), to, height and toHeight (ramp:
// its end and the two heights; none given joins the terrain's own), image, rotation and blend
// (stamp: a height image, its turn in degrees, add|max|min). invert (Ctrl: raise lowers, ...) and
// symmetry (x, z or xz: mirrored across x=0 and/or z=0) go with every mode.
struct TerrainOpSummary {
    std::uint32_t operations = 0U;
    std::uint32_t changedSamples = 0U;  // height samples and hole cells changed
    std::uint32_t changedTexels = 0U;   // material weight texels changed
};

// What operations share across a run: stamp images are read once, and a relative image path is
// looked for under `baseDirectory` (the project folder) before it is taken as written.
struct TerrainOpContext {
    std::filesystem::path baseDirectory;
    std::map<std::string, std::shared_ptr<const TerrainStampImage>> images;
};

// Applies one operation; false with the reason (and the terrain as it was before the operation
// started to fail) when the operation is wrong. Unknown members are errors, so a misspelled
// option cannot pass silently.
[[nodiscard]] bool ApplyTerrainOp(
    kb::assets::TerrainAsset& terrain,
    const kb::core::JsonValue& operation,
    TerrainOpContext& context,
    TerrainOpSummary& summary,
    std::string& error);

// Applies a JSON array of operations, or an object whose "ops" member is one, in order. Stops at
// the first failing operation (the error names its index).
[[nodiscard]] bool ApplyTerrainOps(
    kb::assets::TerrainAsset& terrain,
    const kb::core::JsonValue& operations,
    TerrainOpContext& context,
    TerrainOpSummary& summary,
    std::string& error);

// Brush settings from the members of an operation (mode, shape, radius, strength, falloff, ...).
// The members of other keys are left alone, so an automation step can carry its own as well.
[[nodiscard]] bool ParseTerrainBrushSettings(
    const kb::core::JsonValue& operation,
    TerrainOpContext& context,
    TerrainBrushSettings& settings,
    std::string& error);

// Layer paint settings from the members of a paint operation (layer, radius, opacity, falloff, shape, erase, seed).
[[nodiscard]] bool ParseTerrainLayerPaintSettings(
    const kb::core::JsonValue& operation,
    TerrainLayerPaintSettings& settings,
    std::string& error);

} // namespace kb::terrain_editor
