#pragma once

#include "TerrainBrush.hpp"
#include "engine/assets/TerrainAsset.hpp"
#include "engine/core/JsonValue.hpp"

#include <cstdint>
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
struct TerrainOpSummary {
    std::uint32_t operations = 0U;
    std::uint32_t changedSamples = 0U;  // height samples and hole cells changed
    std::uint32_t changedTexels = 0U;   // material weight texels changed
};

// Applies one operation; false with the reason (and the terrain as it was before the operation
// started to fail) when the operation is wrong. Unknown members are errors, so a misspelled
// option cannot pass silently.
[[nodiscard]] bool ApplyTerrainOp(
    kb::assets::TerrainAsset& terrain,
    const kb::core::JsonValue& operation,
    TerrainOpSummary& summary,
    std::string& error);

// Applies a JSON array of operations, or an object whose "ops" member is one, in order. Stops at
// the first failing operation (the error names its index).
[[nodiscard]] bool ApplyTerrainOps(
    kb::assets::TerrainAsset& terrain,
    const kb::core::JsonValue& operations,
    TerrainOpSummary& summary,
    std::string& error);

// Brush settings from the members of an operation (mode, shape, radius, strength, falloff, ...).
// The members of other keys are left alone, so an automation step can carry its own as well.
[[nodiscard]] bool ParseTerrainBrushSettings(
    const kb::core::JsonValue& operation,
    TerrainBrushSettings& settings,
    std::string& error);

} // namespace kb::terrain_editor
