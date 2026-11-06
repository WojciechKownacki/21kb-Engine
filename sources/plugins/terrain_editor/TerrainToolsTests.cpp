// Terrain tools shared by the editor, kb_cli terrain and automation scenarios: sampling, statistics,
// heightmap export, the operations format, auto paint.
#include "TerrainAnalysis.hpp"
#include "TerrainBrush.hpp"
#include "TerrainErosion.hpp"
#include "TerrainGenerators.hpp"
#include "TerrainHeightmapImporter.hpp"
#include "TerrainImageExport.hpp"
#include "TerrainLayerPainter.hpp"
#include "TerrainNoise.hpp"
#include "TerrainOps.hpp"
#include "TerrainPresets.hpp"
#include "TerrainTask.hpp"

#include "engine/core/JsonValue.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// A golden hash that fails says what it was, so a deliberate change of the maths is a one line update.
void RequireHash(std::uint64_t actual, std::uint64_t golden, const char* what) {
    if (actual == golden) return;
    char text[96];
    std::snprintf(text, sizeof(text), " hash is 0x%016llXULL", static_cast<unsigned long long>(actual));
    throw std::runtime_error(std::string{ "golden " } + what + text);
}

[[nodiscard]] kb::core::JsonValue Json(const std::string& text) {
    kb::core::JsonValue value;
    std::string error;
    if (!kb::core::JsonValue::Parse(text, value, error)) throw std::runtime_error("test JSON: " + error);
    return value;
}

// One operation, or a list of them, on a fresh context.
[[nodiscard]] bool Op(kb::assets::TerrainAsset& terrain, const kb::core::JsonValue& operation, kb::terrain_editor::TerrainOpSummary& summary, std::string& error) {
    kb::terrain_editor::TerrainOpContext context;
    return kb::terrain_editor::ApplyTerrainOp(terrain, operation, context, summary, error);
}

[[nodiscard]] bool Ops(kb::assets::TerrainAsset& terrain, const kb::core::JsonValue& operations, kb::terrain_editor::TerrainOpSummary& summary, std::string& error) {
    kb::terrain_editor::TerrainOpContext context;
    return kb::terrain_editor::ApplyTerrainOps(terrain, operations, context, summary, error);
}

[[nodiscard]] std::filesystem::path TestDirectory() {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "kb_terrain_tools_tests";
    std::filesystem::create_directories(directory);
    return directory;
}

// A slope of exactly 45 degrees rising along x.
[[nodiscard]] kb::assets::TerrainAsset MakeRamp(std::uint32_t resolution, float size) {
    kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset(resolution, size, size);
    const float cell = size / static_cast<float>(resolution - 1U);
    for (std::uint32_t z = 0U; z < resolution; ++z) {
        for (std::uint32_t x = 0U; x < resolution; ++x) {
            terrain.heights[static_cast<std::size_t>(z) * resolution + x] = static_cast<float>(x) * cell;
        }
    }
    return terrain;
}

void RunSamplingTest() {
    const kb::assets::TerrainAsset ramp = MakeRamp(33U, 32.0F);
    // Local x runs -16..16 and the height equals x + 16.
    Require(std::abs(kb::terrain_editor::SampleTerrainHeight(ramp, 0.0F, 0.0F) - 16.0F) < 0.001F, "ramp height at the centre");
    Require(std::abs(kb::terrain_editor::SampleTerrainHeight(ramp, 3.25F, -7.5F) - 19.25F) < 0.001F, "ramp height between samples");
    Require(std::abs(kb::terrain_editor::SampleTerrainHeight(ramp, 100.0F, 0.0F) - 32.0F) < 0.001F, "sampling outside clamps to the border");
    const kb::terrain_editor::TerrainSurfaceSample sample = kb::terrain_editor::SampleTerrainSurface(ramp, 2.0F, 2.0F);
    Require(std::abs(sample.slopeDegrees - 45.0F) < 0.01F, "a 1:1 ramp is 45 degrees");
    Require(std::abs(sample.normal[0] + 0.70711F) < 0.001F && std::abs(sample.normal[1] - 0.70711F) < 0.001F && std::abs(sample.normal[2]) < 0.001F,
        "ramp normal leans away from the rise");
    Require(!sample.hole && sample.layerWeights[0] == 0.0F, "no hole and no layer weights on a plain terrain");

    const kb::terrain_editor::TerrainStats stats = kb::terrain_editor::ComputeTerrainStats(ramp);
    std::uint32_t triangles = 0U;
    for (const std::uint32_t count : stats.slopeHistogram) triangles += count;
    Require(triangles == 32U * 32U * 2U, "every triangle is counted in the slope histogram");
    Require(stats.slopeHistogram[4] == triangles, "a 45 degree ramp lands in the 40-50 degree bin");
    Require(stats.minHeight == 0.0F && std::abs(stats.maxHeight - 32.0F) < 0.001F, "min and max height");
    kb::assets::TerrainAsset hole = ramp;
    hole.holes[0] = 1U;
    Require(kb::terrain_editor::ComputeTerrainStats(hole).holeCells == 1U &&
            kb::terrain_editor::SampleTerrainSurface(hole, -15.9F, -15.9F).hole,
        "a cut hole is reported by stats and sampling");
}

void RunHeightHashTest() {
    kb::assets::TerrainAsset a = kb::assets::MakeFlatTerrainAsset(65U, 64.0F, 64.0F);
    kb::assets::TerrainAsset b = a;
    Require(kb::terrain_editor::HashTerrainHeights(a) == kb::terrain_editor::HashTerrainHeights(b), "equal terrain hashes equal");
    b.heights[100] += 0.0001F;
    Require(kb::terrain_editor::HashTerrainHeights(a) == kb::terrain_editor::HashTerrainHeights(b),
        "a difference below 1/1024 unit does not change the hash");
    b.heights[100] += 0.01F;
    Require(kb::terrain_editor::HashTerrainHeights(a) != kb::terrain_editor::HashTerrainHeights(b), "a visible change changes the hash");
    kb::terrain_editor::TerrainBrushSettings brush{ .mode = kb::terrain_editor::TerrainBrushMode::Raise, .radius = 10.0F, .strength = 2.0F };
    static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(a, brush, { .localX = 5.0F, .localZ = -3.0F }));
    // Golden: this exact stroke on this terrain, so a change of the brush maths is noticed.
    RequireHash(kb::terrain_editor::HashTerrainHeights(a), 0x299889B58E89FB5DULL, "raise stroke");
}

void RunHeightmapExportTest() {
    const std::filesystem::path directory = TestDirectory();
    kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset(65U, 64.0F, 64.0F);
    kb::terrain_editor::TerrainBrushSettings brush{ .mode = kb::terrain_editor::TerrainBrushMode::Raise, .radius = 12.0F, .strength = 9.0F };
    static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(terrain, brush, { .localX = -6.0F, .localZ = 4.0F }));
    for (const char* name : { "export.png", "export.raw" }) {
        const std::filesystem::path file = directory / name;
        std::string error;
        const auto format = kb::terrain_editor::TerrainHeightFormatFromPath(file);
        Require(format.has_value(), "the export format follows the extension");
        const auto exported = kb::terrain_editor::ExportTerrainHeightmap(terrain, file, *format, std::nullopt, &error);
        Require(exported.has_value(), error.c_str());
        Require(exported->minHeight == 0.0F && exported->maxHeight > 8.0F, "the export reports the height range it used");
        const auto imported = kb::terrain_editor::TerrainHeightmapImporter::Import(
            file, { .minimumHeight = exported->minHeight, .maximumHeight = exported->maxHeight }, &error);
        Require(imported.has_value(), error.c_str());
        Require(imported->width == 65U && imported->height == 65U, "the import keeps the resolution");
        float worst = 0.0F;
        for (std::size_t index = 0U; index < terrain.heights.size(); ++index) {
            worst = std::max(worst, std::abs(imported->heights[index] - terrain.heights[index]));
        }
        Require(worst <= (exported->maxHeight - exported->minHeight) / 65535.0F + 0.0001F, "a 16 bit export round-trips within one step");
        std::filesystem::remove(file);
    }
    std::string error;
    Require(!kb::terrain_editor::ExportTerrainHeightmap(terrain, directory / "bad.raw", kb::terrain_editor::TerrainHeightFormat::Raw16,
                std::pair{ 5.0F, 5.0F }, &error).has_value() && !error.empty(), "an empty range is refused with a reason");
}

void RunPreviewTest() {
    const std::filesystem::path file = TestDirectory() / "preview.png";
    kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset(33U, 64.0F, 32.0F);
    Require(kb::terrain_editor::AddTerrainMaterialLayer(terrain, 1U) && kb::terrain_editor::AddTerrainMaterialLayer(terrain, 2U), "layers for the splat preview");
    for (const char* kind : { "height", "slope", "splat" }) {
        std::string error;
        const auto parsed = kb::terrain_editor::ParseTerrainPreviewKind(kind);
        Require(parsed.has_value() && kb::terrain_editor::WriteTerrainPreviewPng(terrain, *parsed, 128U, file, &error), error.c_str());
        std::ifstream input{ file, std::ios::binary };
        const std::string bytes{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
        Require(bytes.size() > 33U && bytes.compare(1U, 3U, "PNG") == 0, "the preview is a PNG");
        const auto big = [&bytes](std::size_t at) {
            return (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at])) << 24U) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 1U])) << 16U) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 2U])) << 8U) |
                static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 3U]));
        };
        Require(big(16U) == 128U && big(20U) == 64U, "the preview keeps the terrain's aspect (2:1 here)");
    }
    std::string error;
    Require(!kb::terrain_editor::WriteTerrainPreviewPng(terrain, kb::terrain_editor::TerrainPreviewKind::Height, 8U, file, &error), "a tiny preview is refused");
    std::filesystem::remove(file);
}

void RunOperationsTest() {
    kb::assets::TerrainAsset viaOp = kb::assets::MakeFlatTerrainAsset(65U, 64.0F, 64.0F);
    kb::assets::TerrainAsset direct = viaOp;
    kb::terrain_editor::TerrainOpSummary summary{};
    std::string error;
    Require(Op(viaOp,
                Json(R"({"op":"brush","mode":"raise","x":4,"z":-6,"radius":9,"strength":1.5,"falloff":0.5})"), summary, error),
        error.c_str());
    kb::terrain_editor::TerrainBrushSettings brush{ .radius = 9.0F, .strength = 1.5F, .falloff = 0.5F };
    static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(direct, brush, { .localX = 4.0F, .localZ = -6.0F }));
    Require(viaOp.heights == direct.heights && summary.operations == 1U && summary.changedSamples > 0U,
        "an operation does what the brush does");

    // The same stroke written as text (how the command line passes it) is the same stroke.
    kb::assets::TerrainAsset viaText = kb::assets::MakeFlatTerrainAsset(65U, 64.0F, 64.0F);
    kb::terrain_editor::TerrainOpSummary textSummary{};
    Require(Op(viaText,
                Json(R"({"op":"brush","x":"4","z":"-6","radius":"9","strength":"1.5","falloff":"0.5"})"), textSummary, error), error.c_str());
    Require(viaText.heights == viaOp.heights, "numbers written as text mean the same");

    // A path stamps at a fixed spacing, however its points are written.
    kb::assets::TerrainAsset coarse = kb::assets::MakeFlatTerrainAsset(65U, 64.0F, 64.0F);
    kb::assets::TerrainAsset fine = coarse;
    kb::terrain_editor::TerrainOpSummary pathSummary{};
    Require(Op(coarse,
                Json(R"({"op":"brush","path":[[-20,0],[20,0]],"radius":4,"strength":0.5,"spacing":2})"), pathSummary, error), error.c_str());
    Require(Op(fine,
                Json(R"({"op":"brush","path":"-20,0;0,0;20,0","radius":4,"strength":0.5,"spacing":2})"), pathSummary, error), error.c_str());
    float worstDifference = 0.0F;
    for (std::size_t index = 0U; index < coarse.heights.size(); ++index) {
        worstDifference = std::max(worstDifference, std::abs(coarse.heights[index] - fine.heights[index]));
    }
    Require(worstDifference < 0.001F, "splitting a path in two does not change the stroke");
    Require(std::abs(kb::terrain_editor::SampleTerrainHeight(coarse, 20.0F, 0.0F) - kb::terrain_editor::SampleTerrainHeight(coarse, -20.0F, 0.0F)) < 0.01F,
        "both ends of a path are covered alike");

    // Wrong operations fail with a reason and change nothing.
    const kb::assets::TerrainAsset before = viaOp;
    for (const char* bad : { R"({"op":"brush","mode":"melt","x":0,"z":0})", R"({"op":"brush","x":0})", R"({"op":"brush","x":0,"z":0,"radious":4})",
             R"({"op":"brush","x":0,"z":0,"radius":-1})", R"({"op":"carve"})", R"({"mode":"raise"})", R"({"op":"paint","layer":0,"x":0,"z":0})",
             R"({"op":"brush","path":"1,2;3","radius":3})" }) {
        std::string reason;
        Require(!Op(viaOp, Json(bad), summary, reason) && !reason.empty() && viaOp.heights == before.heights, bad);
    }
    Require(!Ops(viaOp, Json(R"([{"op":"brush","x":0,"z":0},{"op":"nope"}])"), summary, error) &&
            error.starts_with("op 1:"),
        "a failing batch names the operation");
}

void RunPaintOperationsTest() {
    kb::assets::TerrainAsset terrain = MakeRamp(65U, 64.0F);
    // The right half of the ramp is steeper: add a cliff.
    for (std::uint32_t z = 0U; z < 65U; ++z) {
        for (std::uint32_t x = 40U; x < 65U; ++x) terrain.heights[static_cast<std::size_t>(z) * 65U + x] += static_cast<float>(x - 40U) * 3.0F;
    }
    Require(kb::terrain_editor::AddTerrainMaterialLayer(terrain, 11U) && kb::terrain_editor::AddTerrainMaterialLayer(terrain, 22U), "two layers");
    kb::terrain_editor::TerrainOpSummary summary{};
    std::string error;
    Require(Op(terrain,
                Json(R"({"op":"auto-paint","layer":1,"slope":"60..90","slopeBlend":2})"), summary, error), error.c_str());
    Require(summary.changedTexels > 0U, "auto paint changed texels");
    const auto weightOf = [&terrain](float x, float z) { return kb::terrain_editor::SampleTerrainSurface(terrain, x, z).layerWeights[1]; };
    Require(weightOf(-10.0F, 0.0F) < 0.01F, "the gentle 45 degree ground keeps the base layer");
    Require(weightOf(28.0F, 0.0F) > 0.99F, "the cliff takes the cliff layer");
    for (std::size_t texel = 0U; texel < terrain.layerWeights.size(); texel += 4U) {
        const int sum = terrain.layerWeights[texel] + terrain.layerWeights[texel + 1U] + terrain.layerWeights[texel + 2U] + terrain.layerWeights[texel + 3U];
        Require(sum == 255, "layer weights keep summing to 255");
    }
    // Painting by hand and erasing it again.
    kb::terrain_editor::TerrainOpSummary handSummary{};
    Require(Op(terrain, Json(R"({"op":"paint","layer":1,"path":"-30,-20;-10,-20","radius":4})"), handSummary, error), error.c_str());
    Require(weightOf(-20.0F, -20.0F) > 0.99F, "painting a path colours its middle");
    Require(Op(terrain, Json(R"({"op":"paint","layer":1,"x":-20,"z":-20,"radius":6,"erase":true})"), handSummary, error), error.c_str());
    Require(weightOf(-20.0F, -20.0F) < 0.01F, "erasing takes the layer away");
    Require(!Op(terrain, Json(R"({"op":"paint","layer":2,"x":0,"z":0})"), handSummary, error) &&
            error.find("layer") != std::string::npos, "a layer the terrain does not have is refused");
}

// ---- brushes: noise, terrace, invert, symmetry, ramp, stamp, eyedropper -------------------------------

using kb::terrain_editor::TerrainBrushMode;
using kb::terrain_editor::TerrainBrushSettings;

[[nodiscard]] float Height(const kb::assets::TerrainAsset& terrain, int x, int z) {
    return terrain.heights[static_cast<std::size_t>(z) * terrain.width + static_cast<std::size_t>(x)];
}

[[nodiscard]] float LargestStep(const kb::assets::TerrainAsset& terrain) {
    float largest = 0.0F;
    for (std::uint32_t z = 0U; z < terrain.height; ++z) {
        for (std::uint32_t x = 0U; x + 1U < terrain.width; ++x) {
            largest = std::max(largest, std::abs(Height(terrain, static_cast<int>(x) + 1, static_cast<int>(z)) - Height(terrain, static_cast<int>(x), static_cast<int>(z))));
        }
    }
    return largest;
}

void RunNoiseBrushTest() {
    // Coherent noise: neighbours are close, the range is bounded, seeds and kinds differ.
    kb::terrain_editor::TerrainNoiseSettings noise{ .scale = 16.0F, .octaves = 3U, .seed = 7U };
    float lowest = 1.0F;
    float highest = -1.0F;
    float largestStep = 0.0F;
    for (int step = 0; step < 4000; ++step) {
        const float x = static_cast<float>(step) * 0.05F - 100.0F;
        const float value = kb::terrain_editor::SampleTerrainNoise(noise, x, 3.5F);
        lowest = std::min(lowest, value);
        highest = std::max(highest, value);
        largestStep = std::max(largestStep, std::abs(value - kb::terrain_editor::SampleTerrainNoise(noise, x + 0.05F, 3.5F)));
    }
    Require(lowest >= -1.0F && highest <= 1.0F && highest - lowest > 0.5F, "noise fills a useful part of -1..1");
    Require(largestStep < 0.08F, "noise changes smoothly from point to point");
    Require(kb::terrain_editor::SampleTerrainNoise(noise, 12.25F, -3.5F) == kb::terrain_editor::SampleTerrainNoise(noise, 12.25F, -3.5F), "noise is repeatable");
    noise.seed = 8U;
    Require(kb::terrain_editor::SampleTerrainNoise(noise, 12.25F, -3.5F) != kb::terrain_editor::SampleTerrainNoise({ .scale = 16.0F, .octaves = 3U, .seed = 7U }, 12.25F, -3.5F), "another seed gives other noise");
    noise.kind = kb::terrain_editor::TerrainNoiseKind::Value;
    Require(kb::terrain_editor::IsTerrainNoiseValid(noise) && !kb::terrain_editor::IsTerrainNoiseValid({ .octaves = 0U }) &&
            !kb::terrain_editor::IsTerrainNoiseValid({ .scale = 0.0F }), "noise settings are validated");

    // The brush: bumps join up (no white noise), and the same stroke twice is the same terrain.
    TerrainBrushSettings brush{ .mode = TerrainBrushMode::Noise, .radius = 40.0F, .strength = 4.0F, .falloff = 0.5F, .noiseSeed = 5U, .noiseScale = 20.0F };
    kb::assets::TerrainAsset a = kb::assets::MakeFlatTerrainAsset(129U, 128.0F, 128.0F);
    kb::assets::TerrainAsset b = a;
    Require(kb::terrain_editor::ApplyTerrainBrush(a, brush, { .localX = 5.0F, .localZ = -9.0F }).Changed(), "noise brush changed the terrain");
    static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(b, brush, { .localX = 5.0F, .localZ = -9.0F }));
    Require(a.heights == b.heights, "noise brush is deterministic");
    Require(LargestStep(a) < 1.0F, "noise brush bumps are smooth, not per-sample static");
    RequireHash(kb::terrain_editor::HashTerrainHeights(a), 0xF2531C440455431DULL, "noise brush");
    // A second stroke beside the first continues the same noise field.
    const kb::assets::TerrainAsset single = a;
    static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(b, brush, { .localX = 30.0F, .localZ = -9.0F }));
    Require(LargestStep(b) < 2.0F, "overlapping noise strokes stay continuous");
    Require(b.heights != single.heights, "the second stroke changed more terrain");
}

void RunTerraceBrushTest() {
    kb::assets::TerrainAsset hard = MakeRamp(65U, 64.0F);
    kb::assets::TerrainAsset soft = hard;
    TerrainBrushSettings brush{ .mode = TerrainBrushMode::Terrace, .radius = 100.0F, .strength = 1.0F, .falloff = 1.0F, .terraceStep = 8.0F };
    Require(kb::terrain_editor::ApplyTerrainBrush(hard, brush, {}).Changed(), "terrace changed a slope");
    for (int x = 0; x < 65; ++x) {
        const float rest = std::fmod(Height(hard, x, 10), 8.0F);
        Require(rest < 0.001F || rest > 7.999F, "a hard terrace leaves heights on whole steps");
    }
    brush.terraceSoftness = 0.6F;
    static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(soft, brush, {}));
    Require(LargestStep(soft) < LargestStep(hard) - 1.0F, "softness rounds the risers between steps");
    Require(Height(soft, 45, 10) != Height(hard, 45, 10) && LargestStep(soft) > 0.9F, "a soft terrace still climbs in steps");
    Require(!kb::terrain_editor::IsTerrainBrushSettingsValid({ .terraceSoftness = 1.5F }) &&
            kb::terrain_editor::IsTerrainBrushSettingsValid({ .terraceSoftness = 1.0F }), "softness is 0..1");
}

void RunInvertAndSymmetryTest() {
    TerrainBrushSettings raise{ .mode = TerrainBrushMode::Raise, .radius = 10.0F, .strength = 2.0F };
    kb::assets::TerrainAsset viaInvert = kb::assets::MakeFlatTerrainAsset(65U, 64.0F, 64.0F);
    kb::assets::TerrainAsset viaLower = viaInvert;
    raise.invert = true;
    static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(viaInvert, raise, { .localX = 3.0F, .localZ = 4.0F }));
    raise.invert = false;
    raise.mode = TerrainBrushMode::Lower;
    static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(viaLower, raise, { .localX = 3.0F, .localZ = 4.0F }));
    Require(viaInvert.heights == viaLower.heights && Height(viaInvert, 35, 36) < -1.9F, "Ctrl turns raise into lower");
    kb::assets::TerrainAsset hole = kb::assets::MakeFlatTerrainAsset(65U, 64.0F, 64.0F);
    TerrainBrushSettings cut{ .mode = TerrainBrushMode::CutHole, .radius = 6.0F, .invert = true };
    Require(!kb::terrain_editor::ApplyTerrainBrush(hole, cut, {}).Changed(), "inverted cut hole fills, and there is no hole to fill");
    cut.invert = false;
    Require(kb::terrain_editor::ApplyTerrainBrush(hole, cut, {}).Changed(), "cut hole cuts");
    cut.invert = true;
    Require(kb::terrain_editor::ApplyTerrainBrush(hole, cut, {}).Changed(), "inverted cut hole fills the hole again");

    // Mirrored strokes are mirror images, noise included.
    for (const TerrainBrushMode mode : { TerrainBrushMode::Raise, TerrainBrushMode::Noise }) {
        kb::assets::TerrainAsset terrain = MakeRamp(65U, 64.0F);
        TerrainBrushSettings brush{ .mode = mode, .radius = 9.0F, .strength = 1.5F, .noiseScale = 7.0F, .symmetryX = true, .symmetryZ = true };
        Require(kb::terrain_editor::ApplyTerrainBrush(terrain, brush, { .localX = 12.0F, .localZ = 6.0F }).Changed(), "symmetric stroke changed the terrain");
        kb::assets::TerrainAsset plain = MakeRamp(65U, 64.0F);
        brush.symmetryX = brush.symmetryZ = false;
        static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(plain, brush, { .localX = 12.0F, .localZ = 6.0F }));
        static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(plain, brush, { .localX = -12.0F, .localZ = 6.0F }));
        static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(plain, brush, { .localX = 12.0F, .localZ = -6.0F }));
        static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(plain, brush, { .localX = -12.0F, .localZ = -6.0F }));
        if (mode == TerrainBrushMode::Raise) {
            for (std::size_t index = 0U; index < plain.heights.size(); ++index) {
                Require(std::abs(terrain.heights[index] - plain.heights[index]) < 0.0005F, "symmetry equals the four stamps");
            }
            Require(std::abs((Height(terrain, 20, 38) - 20.0F) - (Height(terrain, 44, 38) - 44.0F)) < 0.0005F, "raise is mirrored in x");
        }
        if (mode == TerrainBrushMode::Noise) {
            // The ramp rises along x, so compare the noise that was added: the stroke minus the ground.
            for (int z = 20; z < 44; ++z) {
                for (int x = 20; x < 44; ++x) {
                    const float added = Height(terrain, x, z) - static_cast<float>(x);
                    const float mirrored = Height(terrain, 64 - x, z) - static_cast<float>(64 - x);
                    Require(std::abs(added - mirrored) < 0.002F, "noise strokes are mirror images");
                }
            }
        }
    }
    // A stroke on the axis is applied once, not twice.
    kb::assets::TerrainAsset onAxis = kb::assets::MakeFlatTerrainAsset(65U, 64.0F, 64.0F);
    kb::assets::TerrainAsset once = onAxis;
    TerrainBrushSettings centre{ .mode = TerrainBrushMode::Raise, .radius = 8.0F, .strength = 3.0F, .symmetryX = true, .symmetryZ = true };
    static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(onAxis, centre, {}));
    centre.symmetryX = centre.symmetryZ = false;
    static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(once, centre, {}));
    Require(onAxis.heights == once.heights, "a stroke on the mirror axis is not doubled");
}

void RunRampBrushTest() {
    kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset(129U, 128.0F, 128.0F);
    TerrainBrushSettings bump{ .mode = TerrainBrushMode::Raise, .radius = 12.0F, .strength = 20.0F, .falloff = 1.0F };
    static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(terrain, bump, { .localX = 40.0F, .localZ = 0.0F }));
    const float endHeight = kb::terrain_editor::SampleTerrainHeight(terrain, 40.0F, 0.0F);
    Require(endHeight > 19.0F, "the hill the ramp climbs");
    // Heights from the terrain: the ramp joins the flat ground at the start to the top of the hill.
    TerrainBrushSettings ramp{ .mode = TerrainBrushMode::Ramp, .radius = 6.0F, .strength = 1.0F, .falloff = 0.5F,
        .sampleTargetHeight = true, .rampEndX = 40.0F, .rampEndZ = 0.0F };
    const kb::terrain_editor::TerrainBrushStamp start{ .localX = -40.0F, .localZ = 0.0F };
    kb::terrain_editor::BeginTerrainStroke(terrain, ramp, start);
    Require(!ramp.sampleTargetHeight && std::abs(ramp.targetHeight) < 0.001F && std::abs(ramp.rampEndHeight - endHeight) < 0.001F,
        "a ramp takes the terrain's heights at its ends when the stroke begins");
    Require(kb::terrain_editor::ApplyTerrainBrush(terrain, ramp, start).Changed(), "the ramp changed the terrain");
    for (const float x : { -20.0F, 0.0F, 20.0F }) {
        const float expected = endHeight * (x + 40.0F) / 80.0F;
        Require(std::abs(kb::terrain_editor::SampleTerrainHeight(terrain, x, 0.0F) - expected) < 0.05F, "the ramp climbs in a straight line");
        Require(std::abs(kb::terrain_editor::SampleTerrainHeight(terrain, x, 3.0F) - expected) < 0.05F, "the strip is level across its flat share");
    }
    Require(std::abs(kb::terrain_editor::SampleTerrainHeight(terrain, -20.0F, 12.0F)) < 0.001F, "beyond the strip's edge nothing changes");
    const float edge = kb::terrain_editor::SampleTerrainHeight(terrain, -20.0F, 5.0F);
    Require(edge > 0.0F && edge < endHeight * 0.25F, "the sides blend into the ground");
    // Explicit heights.
    kb::assets::TerrainAsset flat = kb::assets::MakeFlatTerrainAsset(65U, 64.0F, 64.0F);
    TerrainBrushSettings explicitRamp{ .mode = TerrainBrushMode::Ramp, .radius = 4.0F, .targetHeight = 2.0F, .rampEndX = 20.0F, .rampEndZ = 0.0F, .rampEndHeight = 12.0F };
    static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(flat, explicitRamp, { .localX = -20.0F, .localZ = 0.0F }));
    Require(std::abs(kb::terrain_editor::SampleTerrainHeight(flat, 0.0F, 0.0F) - 7.0F) < 0.05F, "a ramp between given heights");
    // Mirrored ramps.
    explicitRamp.rampEndZ = 10.0F;
    explicitRamp.symmetryZ = true;
    kb::assets::TerrainAsset mirrored = kb::assets::MakeFlatTerrainAsset(65U, 64.0F, 64.0F);
    static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(mirrored, explicitRamp, { .localX = -20.0F, .localZ = 10.0F }));
    Require(std::abs(kb::terrain_editor::SampleTerrainHeight(mirrored, 0.0F, 10.0F) - kb::terrain_editor::SampleTerrainHeight(mirrored, 0.0F, -10.0F)) < 0.001F &&
            kb::terrain_editor::SampleTerrainHeight(mirrored, 0.0F, 10.0F) > 6.0F, "a ramp is mirrored across z");
}

void RunStampBrushTest() {
    auto image = std::make_shared<kb::terrain_editor::TerrainStampImage>();
    image->width = 9U;
    image->height = 9U;
    image->samples.assign(81U, 0.0F);
    for (std::size_t column = 4U; column < 9U; ++column) image->samples[4U * 9U + column] = 1.0F; // a bar from the centre to the +x side
    TerrainBrushSettings stamp{ .mode = TerrainBrushMode::Stamp, .shape = kb::terrain_editor::TerrainBrushShape::HardRound, .radius = 8.0F, .strength = 5.0F,
        .stampImage = image };
    kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset(65U, 64.0F, 64.0F);
    Require(kb::terrain_editor::ApplyTerrainBrush(terrain, stamp, {}).Changed(), "the stamp changed the terrain");
    Require(std::abs(kb::terrain_editor::SampleTerrainHeight(terrain, 0.0F, 0.0F) - 5.0F) < 0.01F, "the image's white is `strength` high");
    Require(kb::terrain_editor::SampleTerrainHeight(terrain, 4.0F, 0.0F) > 4.0F && std::abs(kb::terrain_editor::SampleTerrainHeight(terrain, -4.0F, 0.0F)) < 0.01F,
        "the bar lies on the +x side");
    Require(std::abs(kb::terrain_editor::SampleTerrainHeight(terrain, 0.0F, 6.0F)) < 0.01F, "dark parts of the image leave the ground alone");
    // A half turn puts the bar on the -x side; so does flipping the stamp in a mirror.
    kb::assets::TerrainAsset turned = kb::assets::MakeFlatTerrainAsset(65U, 64.0F, 64.0F);
    stamp.stampRotationDegrees = 180.0F;
    static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(turned, stamp, {}));
    Require(kb::terrain_editor::SampleTerrainHeight(turned, -4.0F, 0.0F) > 4.0F && std::abs(kb::terrain_editor::SampleTerrainHeight(turned, 4.0F, 0.0F)) < 0.05F, "rotation turns the stamp");
    stamp.stampRotationDegrees = 0.0F;
    kb::assets::TerrainAsset mirrored = kb::assets::MakeFlatTerrainAsset(65U, 64.0F, 64.0F);
    stamp.symmetryX = true;
    static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(mirrored, stamp, { .localX = 20.0F }));
    Require(std::abs(kb::terrain_editor::SampleTerrainHeight(mirrored, 24.0F, 0.0F) - kb::terrain_editor::SampleTerrainHeight(mirrored, -24.0F, 0.0F)) < 0.001F &&
            kb::terrain_editor::SampleTerrainHeight(mirrored, 24.0F, 0.0F) > 4.0F, "the mirrored stamp is a mirror image");
    stamp.symmetryX = false;
    // Max and min against the ground.
    kb::assets::TerrainAsset ground = kb::assets::MakeFlatTerrainAsset(65U, 64.0F, 64.0F);
    std::fill(ground.heights.begin(), ground.heights.end(), 3.0F);
    stamp.stampBlend = kb::terrain_editor::TerrainStampBlend::Max;
    static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(ground, stamp, {}));
    Require(std::abs(kb::terrain_editor::SampleTerrainHeight(ground, 0.0F, 0.0F) - 5.0F) < 0.01F && std::abs(kb::terrain_editor::SampleTerrainHeight(ground, 0.0F, 6.0F) - 3.0F) < 0.001F,
        "max keeps the higher of ground and image");
    stamp.stampBlend = kb::terrain_editor::TerrainStampBlend::Min;
    stamp.targetHeight = 1.0F;
    static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(ground, stamp, {}));
    Require(std::abs(kb::terrain_editor::SampleTerrainHeight(ground, 0.0F, 6.0F) - 1.0F) < 0.001F && std::abs(kb::terrain_editor::SampleTerrainHeight(ground, 0.0F, 0.0F) - 5.0F) < 0.01F,
        "min takes the lower of ground and image, from the base height");
    stamp.stampImage = nullptr;
    Require(!kb::terrain_editor::IsTerrainBrushSettingsValid(stamp), "a stamp needs its image");

    // From a file, through an operation, relative to the base directory.
    const std::filesystem::path directory = TestDirectory();
    kb::assets::TerrainAsset source = kb::assets::MakeFlatTerrainAsset(33U, 32.0F, 32.0F);
    TerrainBrushSettings hill{ .mode = TerrainBrushMode::Raise, .radius = 12.0F, .strength = 6.0F, .falloff = 0.0F };
    static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(source, hill, {}));
    std::string error;
    Require(kb::terrain_editor::ExportTerrainHeightmap(source, directory / "stamp.png", kb::terrain_editor::TerrainHeightFormat::Png16, std::nullopt, &error).has_value(), error.c_str());
    kb::assets::TerrainAsset viaFile = kb::assets::MakeFlatTerrainAsset(129U, 128.0F, 128.0F);
    kb::terrain_editor::TerrainOpContext context{ .baseDirectory = directory };
    kb::terrain_editor::TerrainOpSummary summary{};
    Require(kb::terrain_editor::ApplyTerrainOp(viaFile, Json(R"({"op":"brush","mode":"stamp","image":"stamp.png","x":10,"z":-10,"radius":20,"strength":8,"rotation":30,"blend":"add"})"),
                context, summary, error), error.c_str());
    Require(summary.changedSamples > 100U && context.images.size() == 1U && kb::terrain_editor::SampleTerrainHeight(viaFile, 10.0F, -10.0F) > 7.0F, "a stamp from a PNG file");
    Require(!kb::terrain_editor::ApplyTerrainOp(viaFile, Json(R"({"op":"brush","mode":"stamp","x":0,"z":0})"), context, summary, error) && error.find("image") != std::string::npos, "a stamp without an image is refused");
    Require(!kb::terrain_editor::ApplyTerrainOp(viaFile, Json(R"({"op":"brush","mode":"stamp","image":"missing.png","x":0,"z":0})"), context, summary, error) && error.find("missing.png") != std::string::npos, "a missing image is named");
    std::filesystem::remove(directory / "stamp.png");
}

void RunBrushOperationsTest() {
    // The new brushes through operations: eyedropper, ramp, symmetry, invert and noise written as data.
    kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset(129U, 128.0F, 128.0F);
    std::string error;
    kb::terrain_editor::TerrainOpSummary summary{};
    Require(Ops(terrain, Json(R"([
        {"op":"brush","mode":"raise","x":30,"z":30,"radius":10,"strength":9,"falloff":1},
        {"op":"brush","mode":"flatten","x":-30,"z":-30,"radius":10,"height":"sample"},
        {"op":"brush","mode":"flatten","x":30,"z":30,"radius":4,"height":"sample","falloff":1,"strength":1}
    ])"), summary, error), error.c_str());
    // Flatten "sample" to the height under the stroke's own start changes nothing there, and flat ground stays flat.
    Require(std::abs(kb::terrain_editor::SampleTerrainHeight(terrain, 30.0F, 30.0F) - 9.0F) < 0.01F, "sampling the height under the start keeps it");
    Require(Ops(terrain, Json(R"([{"op":"brush","mode":"flatten","path":"30,30;30,-10","radius":4,"height":"sample","falloff":1}])"), summary, error), error.c_str());
    Require(std::abs(kb::terrain_editor::SampleTerrainHeight(terrain, 30.0F, -8.0F) - 9.0F) < 0.5F, "a flatten stroke carries the sampled height along its path");

    kb::assets::TerrainAsset ramped = kb::assets::MakeFlatTerrainAsset(129U, 128.0F, 128.0F);
    Require(Ops(ramped, Json(R"([{"op":"brush","mode":"ramp","x":-30,"z":0,"to":"30,0","height":0,"toHeight":10,"radius":5}])"), summary, error), error.c_str());
    Require(std::abs(kb::terrain_editor::SampleTerrainHeight(ramped, 0.0F, 0.0F) - 5.0F) < 0.1F, "a ramp operation");
    Require(!Ops(ramped, Json(R"([{"op":"brush","mode":"ramp","x":-30,"z":0,"radius":5}])"), summary, error) && error.find("'to'") != std::string::npos, "a ramp needs its end");
    Require(!Ops(ramped, Json(R"([{"op":"brush","mode":"ramp","x":-30,"z":0,"to":"1,1","height":3,"radius":5}])"), summary, error) && error.find("toHeight") != std::string::npos, "a ramp takes both heights or neither");

    kb::assets::TerrainAsset mirrored = kb::assets::MakeFlatTerrainAsset(129U, 128.0F, 128.0F);
    Require(Ops(mirrored, Json(R"([{"op":"brush","mode":"noise","x":20,"z":10,"radius":15,"strength":3,"symmetry":"xz","noiseKind":"value","noiseScale":9,"noiseOctaves":2,"seed":3,"invert":true}])"), summary, error), error.c_str());
    Require(std::abs(Height(mirrored, 84, 74) - Height(mirrored, 44, 54)) < 0.002F && std::abs(Height(mirrored, 84, 74) - Height(mirrored, 44, 74)) < 0.002F &&
            Height(mirrored, 84, 74) != 0.0F, "symmetry, invert and noise options reach the brush");
    for (const char* bad : { R"({"op":"brush","x":0,"z":0,"symmetry":"y"})", R"({"op":"brush","x":0,"z":0,"noiseKind":"cell"})",
             R"({"op":"brush","x":0,"z":0,"noiseOctaves":9})", R"({"op":"brush","x":0,"z":0,"blend":"mul"})", R"({"op":"brush","x":0,"z":0,"invert":"maybe"})",
             R"({"op":"brush","mode":"terrace","x":0,"z":0,"softness":2})" }) {
        std::string reason;
        Require(!Op(mirrored, Json(bad), summary, reason) && !reason.empty(), bad);
    }
    RequireHash(kb::terrain_editor::HashTerrainHeights(mirrored), 0xA9747224628D7405ULL, "mirrored inverted value noise");
}

void RunBrushPerformanceTest() {
    // A big stamp is shared out over threads; the heights must still be what the weights say.
    kb::assets::TerrainAsset big = kb::assets::MakeFlatTerrainAsset(1025U, 1024.0F, 1024.0F);
    TerrainBrushSettings wide{ .mode = TerrainBrushMode::Raise, .radius = 300.0F, .strength = 2.0F, .falloff = 0.3F };
    Require(kb::terrain_editor::ApplyTerrainBrushToValidatedTerrain(big, wide, { .localX = 20.0F, .localZ = -30.0F }).changedSamples > 100'000U, "a very large stamp");
    for (int probe = 0; probe < 400; ++probe) {
        const int x = 512 + (probe * 37) % 600 - 300;
        const int z = 512 + (probe * 91) % 600 - 300;
        const float dx = static_cast<float>(x - 512) - 20.0F;
        const float dz = static_cast<float>(z - 512) + 30.0F;
        const float distance = std::sqrt(dx * dx + dz * dz);
        const float expected = distance > 300.0F ? 0.0F : 2.0F * kb::terrain_editor::TerrainBrushWeight(distance, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z), wide);
        Require(std::abs(Height(big, x, z) - expected) < 0.0002F, "threads leave the stamp as the weights say");
    }
    // Stamp radius 64 on a 1025 x 1025 terrain: under 2 ms.
    auto image = std::make_shared<kb::terrain_editor::TerrainStampImage>();
    image->width = 128U;
    image->height = 128U;
    image->samples.resize(128U * 128U);
    for (std::uint32_t index = 0U; index < image->samples.size(); ++index) image->samples[index] = static_cast<float>(index % 97U) / 96.0F;
    TerrainBrushSettings stamp{ .mode = TerrainBrushMode::Stamp, .radius = 64.0F, .strength = 3.0F, .stampImage = image, .stampRotationDegrees = 25.0F };
    kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset(1025U, 1024.0F, 1024.0F);
    std::vector<float> scratch;
    static_cast<void>(kb::terrain_editor::ApplyTerrainBrushToValidatedTerrain(terrain, stamp, {}, scratch));
    const auto begin = std::chrono::steady_clock::now();
    constexpr int kRuns = 50;
    for (int run = 0; run < kRuns; ++run) {
        static_cast<void>(kb::terrain_editor::ApplyTerrainBrushToValidatedTerrain(
            terrain, stamp, { .localX = static_cast<float>(run) * 3.0F - 80.0F, .localZ = static_cast<float>(run) * 2.0F - 50.0F }, scratch));
    }
    const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() / kRuns;
    std::printf("Terrain stamp radius 64 on 1025x1025: %.3f ms per stamp\n", milliseconds);
    Require(milliseconds < 2.0, "a radius 64 stamp takes under 2 ms on a 1025x1025 terrain");
}

// ---- generators, erosion, presets, background jobs ------------------------------------------------------

[[nodiscard]] kb::assets::TerrainAsset Generated(std::uint32_t resolution, float size, const kb::terrain_editor::TerrainGeneratorSettings& settings, std::uint32_t workers = 0U) {
    kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset(resolution, size, size);
    kb::terrain_editor::TerrainTaskControl control;
    control.maxWorkers = workers;
    std::string error;
    Require(kb::terrain_editor::GenerateTerrain(terrain, settings, control, error), error.c_str());
    return terrain;
}

[[nodiscard]] double MeanHeightWhere(const kb::assets::TerrainAsset& terrain, bool centre) {
    double sum = 0.0;
    std::size_t count = 0U;
    const std::uint32_t quarter = terrain.width / 4U;
    for (std::uint32_t z = 0U; z < terrain.height; ++z) {
        for (std::uint32_t x = 0U; x < terrain.width; ++x) {
            const bool inCentre = x >= terrain.width / 2U - quarter / 2U && x <= terrain.width / 2U + quarter / 2U &&
                z >= terrain.height / 2U - quarter / 2U && z <= terrain.height / 2U + quarter / 2U;
            const bool onEdge = x < 3U || z < 3U || x + 3U >= terrain.width || z + 3U >= terrain.height;
            if (centre ? inCentre : onEdge) {
                sum += terrain.heights[static_cast<std::size_t>(z) * terrain.width + x];
                ++count;
            }
        }
    }
    return sum / static_cast<double>(count);
}

void RunGeneratorTest() {
    kb::terrain_editor::TerrainGeneratorSettings fbm{ .seed = 3U, .amplitude = 40.0F, .scale = 90.0F };
    const kb::assets::TerrainAsset first = Generated(129U, 256.0F, fbm);
    const kb::assets::TerrainAsset again = Generated(129U, 256.0F, fbm);
    const kb::assets::TerrainAsset oneThread = Generated(129U, 256.0F, fbm, 1U);
    Require(first.heights == again.heights && first.heights == oneThread.heights, "the same seed gives the same terrain on any number of threads");
    RequireHash(kb::terrain_editor::HashTerrainHeights(first), 0xAACF0C55EAC2EBC9ULL, "fbm 129");
    fbm.seed = 4U;
    Require(Generated(129U, 256.0F, fbm).heights != first.heights, "another seed gives another terrain");
    const auto [lowest, highest] = std::minmax_element(first.heights.begin(), first.heights.end());
    Require(*lowest >= 0.0F && *highest <= 40.0F && *highest - *lowest > 15.0F, "fBm fills a good part of baseHeight..baseHeight+amplitude");
    Require(LargestStep(first) < 6.0F, "generated ground is smooth");

    kb::terrain_editor::TerrainGeneratorSettings ridged{ .kind = kb::terrain_editor::TerrainGeneratorKind::Ridged, .seed = 3U, .amplitude = 60.0F, .baseHeight = 5.0F, .scale = 100.0F };
    const kb::assets::TerrainAsset ridges = Generated(129U, 256.0F, ridged);
    const auto [ridgeLow, ridgeHigh] = std::minmax_element(ridges.heights.begin(), ridges.heights.end());
    Require(*ridgeLow >= 5.0F && *ridgeHigh <= 65.0F && *ridgeHigh - *ridgeLow > 20.0F, "ridged noise stays between base and base+amplitude");
    RequireHash(kb::terrain_editor::HashTerrainHeights(ridges), 0xE33E35BF5A359C08ULL, "ridged 129");
    // Warped lookups differ from plain ones, and are as repeatable.
    ridged.warp = 30.0F;
    const kb::assets::TerrainAsset warped = Generated(129U, 256.0F, ridged);
    Require(warped.heights != ridges.heights && warped.heights == Generated(129U, 256.0F, ridged, 2U).heights, "warped terrain differs and is repeatable");

    kb::terrain_editor::TerrainGeneratorSettings island{ .kind = kb::terrain_editor::TerrainGeneratorKind::Island, .seed = 9U, .amplitude = 50.0F, .scale = 100.0F };
    const kb::assets::TerrainAsset isle = Generated(129U, 256.0F, island);
    Require(MeanHeightWhere(isle, true) > MeanHeightWhere(isle, false) + 15.0 && MeanHeightWhere(isle, false) < 0.0, "an island rises from the sea");
    kb::terrain_editor::TerrainGeneratorSettings plateau{ .kind = kb::terrain_editor::TerrainGeneratorKind::Plateau, .seed = 9U, .amplitude = 50.0F, .scale = 100.0F };
    const kb::assets::TerrainAsset mesas = Generated(129U, 256.0F, plateau);
    std::uint32_t nearTop = 0U;
    for (const float height : mesas.heights) nearTop += height > 47.0F ? 1U : 0U;
    Require(nearTop > 200U && nearTop < mesas.heights.size() - 200U, "plateaus have flat tops and lower ground");
    // Adding to what is there.
    kb::assets::TerrainAsset layered = first;
    kb::terrain_editor::TerrainGeneratorSettings extra{ .seed = 3U, .amplitude = 40.0F, .scale = 90.0F, .additive = true };
    kb::terrain_editor::TerrainTaskControl control;
    std::string error;
    Require(kb::terrain_editor::GenerateTerrain(layered, extra, control, error), error.c_str());
    Require(std::abs(layered.heights[500] - 2.0F * first.heights[500]) < 0.001F, "additive generation adds to the heights");
    Require(!kb::terrain_editor::GenerateTerrain(layered, { .scale = 0.0F }, control, error) && !error.empty(), "wrong settings are refused");
    Require(kb::terrain_editor::ParseTerrainGeneratorKind("ridged") == kb::terrain_editor::TerrainGeneratorKind::Ridged &&
            !kb::terrain_editor::ParseTerrainGeneratorKind("dunes").has_value(), "generator names");

    const auto begin = std::chrono::steady_clock::now();
    const kb::assets::TerrainAsset large = Generated(2049U, 2048.0F, { .seed = 1U, .amplitude = 300.0F, .scale = 600.0F });
    const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    std::printf("Terrain generator 2049x2049, 6 octaves: %.0f ms\n", milliseconds);
    Require(large.heights.size() == 2049U * 2049U && milliseconds < 4000.0, "a 2049x2049 terrain is generated in seconds");
}

void RunErosionTest() {
    // Thermal erosion keeps the material and lowers the steepest slopes to the talus angle.
    kb::assets::TerrainAsset cliff = kb::assets::MakeFlatTerrainAsset(65U, 64.0F, 64.0F);
    for (std::uint32_t z = 0U; z < 65U; ++z) {
        for (std::uint32_t x = 0U; x < 65U; ++x) cliff.heights[static_cast<std::size_t>(z) * 65U + x] = x < 32U ? 0.0F : 20.0F;
    }
    double massBefore = 0.0;
    for (const float height : cliff.heights) massBefore += height;
    kb::terrain_editor::TerrainThermalSettings thermal{ .iterations = 400U, .talusDegrees = 35.0F, .strength = 0.5F };
    kb::terrain_editor::TerrainTaskControl control;
    std::string error;
    kb::assets::TerrainAsset slumped = cliff;
    Require(kb::terrain_editor::ErodeTerrainThermal(slumped, thermal, control, error), error.c_str());
    double massAfter = 0.0;
    for (const float height : slumped.heights) massAfter += height;
    Require(std::abs(massAfter - massBefore) < 0.01, "thermal erosion moves material without losing any");
    const kb::terrain_editor::TerrainStats stats = kb::terrain_editor::ComputeTerrainStats(slumped);
    Require(stats.slopeHistogram[8] == 0U && stats.slopeHistogram[7] == 0U && stats.slopeHistogram[6] == 0U && stats.slopeHistogram[5] == 0U,
        "no slope is left steeper than about the talus angle");
    kb::assets::TerrainAsset slumpedOneThread = cliff;
    control.maxWorkers = 1U;
    Require(kb::terrain_editor::ErodeTerrainThermal(slumpedOneThread, thermal, control, error) && slumpedOneThread.heights == slumped.heights, "thermal erosion is the same on one thread");
    control.maxWorkers = 0U;
    RequireHash(kb::terrain_editor::HashTerrainHeights(slumped), 0xF76550B979584852ULL, "thermal slump");
    thermal.talusDegrees = 120.0F;
    Require(!kb::terrain_editor::ErodeTerrainThermal(slumped, thermal, control, error) && !error.empty(), "a talus angle over 90 degrees is refused");

    // Hydraulic erosion: channels form, it is deterministic on any number of threads.
    const kb::assets::TerrainAsset mountains = Generated(129U, 256.0F, { .kind = kb::terrain_editor::TerrainGeneratorKind::Ridged, .seed = 2U, .amplitude = 60.0F, .scale = 120.0F });
    kb::assets::TerrainAsset wet = mountains;
    kb::assets::TerrainAsset wetOneThread = mountains;
    const kb::terrain_editor::TerrainHydraulicSettings rain{ .iterations = 60U, .strength = 0.6F };
    Require(kb::terrain_editor::ErodeTerrainHydraulic(wet, rain, control, error), error.c_str());
    control.maxWorkers = 1U;
    Require(kb::terrain_editor::ErodeTerrainHydraulic(wetOneThread, rain, control, error), error.c_str());
    control.maxWorkers = 0U;
    Require(wet.heights == wetOneThread.heights, "hydraulic erosion gives the same terrain on one thread and on many");
    Require(wet.heights != mountains.heights && kb::terrain_editor::HashTerrainHeights(wet) != kb::terrain_editor::HashTerrainHeights(mountains), "hydraulic erosion changed the terrain");
    for (const float height : wet.heights) Require(std::isfinite(height) && std::abs(height) < 1000.0F, "eroded heights stay finite and sane");
    RequireHash(kb::terrain_editor::HashTerrainHeights(wet), 0xE9198BF04D0ED5FFULL, "hydraulic erosion");
    double lowered = 0.0;
    double raised = 0.0;
    for (std::size_t index = 0U; index < wet.heights.size(); ++index) {
        const double difference = static_cast<double>(wet.heights[index]) - mountains.heights[index];
        (difference < 0.0 ? lowered : raised) += difference;
    }
    Require(lowered < -1.0 && raised > 0.1, "erosion cuts channels and lays sediment down elsewhere");
    Require(!kb::terrain_editor::ErodeTerrainHydraulic(wet, { .iterations = 0U }, control, error) && !error.empty(), "zero iterations are refused");

    const auto begin = std::chrono::steady_clock::now();
    kb::assets::TerrainAsset timed = Generated(257U, 256.0F, { .kind = kb::terrain_editor::TerrainGeneratorKind::Ridged, .seed = 1U, .amplitude = 60.0F, .scale = 120.0F });
    Require(kb::terrain_editor::ErodeTerrainHydraulic(timed, { .iterations = 100U }, control, error), error.c_str());
    const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    std::printf("Terrain hydraulic erosion 257x257, 100 iterations: %.0f ms\n", milliseconds);
    Require(milliseconds < 4000.0, "100 iterations of hydraulic erosion on 257x257 take seconds, not minutes");
}

void RunBackgroundTaskTest() {
    const kb::assets::TerrainAsset flat = kb::assets::MakeFlatTerrainAsset(1025U, 1024.0F, 1024.0F);
    // A job that runs to its end delivers the terrain, and leaves the original alone.
    const kb::terrain_editor::TerrainGeneratorSettings settings{ .seed = 5U, .amplitude = 30.0F, .scale = 200.0F };
    kb::terrain_editor::TerrainBackgroundTask task{ flat, [settings](kb::assets::TerrainAsset& terrain, kb::terrain_editor::TerrainTaskControl& control, std::string& error) {
        return kb::terrain_editor::GenerateTerrain(terrain, settings, control, error);
    } };
    std::string error;
    const std::optional<kb::assets::TerrainAsset> done = task.Wait(0.0, &error);
    Require(done.has_value() && task.Finished() && task.Progress() > 0.99F, error.c_str());
    Require(done->heights != flat.heights && flat.heights[1000] == 0.0F, "the job worked on its own copy");
    Require(done->heights == Generated(1025U, 1024.0F, settings).heights, "a background job gives what the same call gives");

    // Cancelling stops a job that would take long, and nothing is delivered.
    kb::terrain_editor::TerrainBackgroundTask slow{ flat, [](kb::assets::TerrainAsset& terrain, kb::terrain_editor::TerrainTaskControl& control, std::string& failure) {
        return kb::terrain_editor::ErodeTerrainHydraulic(terrain, { .iterations = 100000U }, control, failure);
    } };
    const auto begin = std::chrono::steady_clock::now();
    slow.Cancel();
    const std::optional<kb::assets::TerrainAsset> stopped = slow.Wait(0.0, &error);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    Require(!stopped.has_value() && error == "cancelled" && seconds < 5.0, "cancel stops a long job quickly and delivers nothing");
    // A time limit does the same.
    kb::terrain_editor::TerrainBackgroundTask limited{ flat, [](kb::assets::TerrainAsset& terrain, kb::terrain_editor::TerrainTaskControl& control, std::string& failure) {
        return kb::terrain_editor::ErodeTerrainHydraulic(terrain, { .iterations = 100000U }, control, failure);
    } };
    const auto limitBegin = std::chrono::steady_clock::now();
    Require(!limited.Wait(0.3, &error).has_value() && error.find("time limit") != std::string::npos &&
            std::chrono::duration<double>(std::chrono::steady_clock::now() - limitBegin).count() < 6.0, "a time limit stops the job");
    // A failing job says why.
    kb::terrain_editor::TerrainBackgroundTask failing{ flat, [](kb::assets::TerrainAsset&, kb::terrain_editor::TerrainTaskControl&, std::string& failure) {
        failure = "no luck";
        return false;
    } };
    Require(!failing.Wait(0.0, &error).has_value() && error == "no luck", "a failed job reports its reason");
}

void RunPresetTest() {
    Require(kb::terrain_editor::IsTerrainPreset("mountains") && !kb::terrain_editor::IsTerrainPreset("moon") && kb::terrain_editor::TerrainPresets().size() == 5U, "presets are named");
    kb::terrain_editor::TerrainTaskControl control;
    std::string error;
    kb::assets::TerrainAsset flat = kb::assets::MakeFlatTerrainAsset(129U, 256.0F, 256.0F);
    const kb::assets::TerrainAsset untouched = flat;
    Require(kb::terrain_editor::ApplyTerrainPreset(flat, "flat", 1U, control, error) && flat.heights == untouched.heights, "the flat preset leaves the terrain flat");
    Require(!kb::terrain_editor::ApplyTerrainPreset(flat, "moon", 1U, control, error) && error.find("hills") != std::string::npos, "an unknown preset lists the known ones");
    for (const std::string_view name : { "hills", "mountains", "island", "plateau" }) {
        kb::assets::TerrainAsset a = untouched;
        kb::assets::TerrainAsset b = untouched;
        Require(kb::terrain_editor::ApplyTerrainPreset(a, name, 12U, control, error), error.c_str());
        control.maxWorkers = 1U;
        Require(kb::terrain_editor::ApplyTerrainPreset(b, name, 12U, control, error), error.c_str());
        control.maxWorkers = 0U;
        Require(a.heights == b.heights && a.heights != untouched.heights, "a preset is the same world on one thread and on many");
        const kb::terrain_editor::TerrainStats stats = kb::terrain_editor::ComputeTerrainStats(a);
        Require(stats.maxHeight - stats.minHeight > 10.0F && std::isfinite(stats.maxHeight), "a preset makes relief");
        kb::assets::TerrainAsset other = untouched;
        Require(kb::terrain_editor::ApplyTerrainPreset(other, name, 13U, control, error) && other.heights != a.heights, "another seed is another world");
        if (name == "mountains") RequireHash(kb::terrain_editor::HashTerrainHeights(a), 0x963DCA1A8C73E1B4ULL, "mountains preset");
    }
}

} // namespace

void RunTerrainToolsTests() {
    RunSamplingTest();
    RunHeightHashTest();
    RunHeightmapExportTest();
    RunPreviewTest();
    RunOperationsTest();
    RunPaintOperationsTest();
    RunNoiseBrushTest();
    RunTerraceBrushTest();
    RunInvertAndSymmetryTest();
    RunRampBrushTest();
    RunStampBrushTest();
    RunBrushOperationsTest();
    RunBrushPerformanceTest();
    RunGeneratorTest();
    RunErosionTest();
    RunBackgroundTaskTest();
    RunPresetTest();
}
