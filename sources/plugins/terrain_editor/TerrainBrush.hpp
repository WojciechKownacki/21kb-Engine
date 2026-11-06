#pragma once

#include "TerrainNoise.hpp"
#include "engine/assets/TerrainAsset.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace kb::terrain_editor {

enum class TerrainBrushMode : std::uint8_t {
    Raise,
    Lower,
    Smooth,
    Flatten,
    Noise,
    Terrace,
    CutHole,
    FillHole,
    Ramp,  // a sloped strip from the stamp to (rampEndX, rampEndZ), `radius` wide on each side
    Stamp, // a height image pressed into the terrain
};

enum class TerrainBrushShape : std::uint8_t {
    SoftRound,
    HardRound,
    LinearRound,
    Bell,
    Ring,
    Speckle,
};

enum class TerrainStampBlend : std::uint8_t {
    Add, // the image's heights are added to the terrain
    Max, // the terrain rises to the image where the image is higher
    Min, // the terrain drops to the image where the image is lower
};

// A greyscale height image, 0 (black) to 1 (white), row 0 at the low z side.
struct TerrainStampImage {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<float> samples;
};

struct TerrainBrushSettings {
    TerrainBrushMode mode = TerrainBrushMode::Raise;
    TerrainBrushShape shape = TerrainBrushShape::SoftRound;
    float radius = 8.0F;
    float strength = 1.0F;
    float falloff = 0.65F;
    // Flatten: the height to flatten to. Stamp: the base height of Max and Min. Ramp: the height at the start.
    float targetHeight = 0.0F;
    float terraceStep = 2.0F;
    std::uint32_t noiseSeed = 1U;
    // Terrace: 0 gives flat steps with sharp risers, 1 rounds every step into the next.
    float terraceSoftness = 0.0F;
    // Noise: coherent noise in world space, so strokes beside each other join up.
    TerrainNoiseKind noiseKind = TerrainNoiseKind::Gradient;
    float noiseScale = 16.0F;
    std::uint8_t noiseOctaves = 3U;
    // Ctrl: raise lowers, lower raises, noise and stamps carve, cut hole fills.
    bool invert = false;
    // Every stroke is repeated mirrored across the terrain's centre plane x = 0 and/or z = 0.
    bool symmetryX = false;
    bool symmetryZ = false;
    // Flatten, Stamp and Ramp take their heights from the terrain under the stroke's start (and a
    // ramp's end) when the stroke begins; see BeginTerrainStroke.
    bool sampleTargetHeight = false;
    float rampEndX = 0.0F;
    float rampEndZ = 0.0F;
    float rampEndHeight = 0.0F;
    // Stamp: the image covers a square of side 2 x radius, turned by the rotation, and is `strength` high.
    std::shared_ptr<const TerrainStampImage> stampImage;
    float stampRotationDegrees = 0.0F;
    TerrainStampBlend stampBlend = TerrainStampBlend::Add;
};

struct TerrainBrushStamp {
    float localX = 0.0F;
    float localZ = 0.0F;
    float pressure = 1.0F;
};

struct TerrainBrushResult {
    std::uint32_t changedSamples = 0U;
    std::uint32_t minX = UINT32_MAX;
    std::uint32_t minZ = UINT32_MAX;
    std::uint32_t maxX = 0U;
    std::uint32_t maxZ = 0U;

    [[nodiscard]] constexpr bool Changed() const noexcept { return changedSamples != 0U; }
};

// The words commands and scenarios use for modes and shapes (raise, lower, ..., soft-round, ...).
[[nodiscard]] std::string_view TerrainBrushModeName(TerrainBrushMode mode) noexcept;
[[nodiscard]] std::optional<TerrainBrushMode> ParseTerrainBrushMode(std::string_view text) noexcept;
[[nodiscard]] std::string_view TerrainBrushShapeName(TerrainBrushShape shape) noexcept;
[[nodiscard]] std::optional<TerrainBrushShape> ParseTerrainBrushShape(std::string_view text) noexcept;
[[nodiscard]] std::string_view TerrainStampBlendName(TerrainStampBlend blend) noexcept;
[[nodiscard]] std::optional<TerrainStampBlend> ParseTerrainStampBlend(std::string_view text) noexcept;

[[nodiscard]] bool IsTerrainBrushSettingsValid(const TerrainBrushSettings& settings) noexcept;
// Settles what depends on the terrain when a stroke begins at `first`: with sampleTargetHeight a
// Flatten stroke takes the height under its start as the target (so does a Stamp as its base), and
// a Ramp takes the heights under both of its ends. The settings are then what every stamp of the stroke uses.
void BeginTerrainStroke(
    const kb::assets::TerrainAsset& terrain,
    TerrainBrushSettings& settings,
    const TerrainBrushStamp& first) noexcept;
[[nodiscard]] float TerrainBrushWeight(
    float distance,
    std::uint32_t sampleX,
    std::uint32_t sampleZ,
    const TerrainBrushSettings& settings) noexcept;
[[nodiscard]] TerrainBrushResult ApplyTerrainBrush(
    kb::assets::TerrainAsset& terrain,
    const TerrainBrushSettings& settings,
    const TerrainBrushStamp& stamp) noexcept;
// Hot path for an asset that was fully validated when the edit session began.
// Brush mutations preserve the terrain sample invariants, while this entry
// still checks dimensions, buffer sizes and all brush/stamp inputs.
[[nodiscard]] TerrainBrushResult ApplyTerrainBrushToValidatedTerrain(
    kb::assets::TerrainAsset& terrain,
    const TerrainBrushSettings& settings,
    const TerrainBrushStamp& stamp) noexcept;
[[nodiscard]] TerrainBrushResult ApplyTerrainBrushToValidatedTerrain(
    kb::assets::TerrainAsset& terrain,
    const TerrainBrushSettings& settings,
    const TerrainBrushStamp& stamp,
    std::vector<float>& scratchHeights) noexcept;

} // namespace kb::terrain_editor
