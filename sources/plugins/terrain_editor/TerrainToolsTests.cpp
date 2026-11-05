// Terrain tools shared by the editor, kb_cli terrain and automation scenarios: sampling, statistics,
// heightmap export, the operations format, auto paint.
#include "TerrainAnalysis.hpp"
#include "TerrainBrush.hpp"
#include "TerrainHeightmapImporter.hpp"
#include "TerrainImageExport.hpp"
#include "TerrainLayerPainter.hpp"
#include "TerrainOps.hpp"

#include "engine/core/JsonValue.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

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
    Require(kb::terrain_editor::ApplyTerrainOp(viaOp,
                Json(R"({"op":"brush","mode":"raise","x":4,"z":-6,"radius":9,"strength":1.5,"falloff":0.5})"), summary, error),
        error.c_str());
    kb::terrain_editor::TerrainBrushSettings brush{ .radius = 9.0F, .strength = 1.5F, .falloff = 0.5F };
    static_cast<void>(kb::terrain_editor::ApplyTerrainBrush(direct, brush, { .localX = 4.0F, .localZ = -6.0F }));
    Require(viaOp.heights == direct.heights && summary.operations == 1U && summary.changedSamples > 0U,
        "an operation does what the brush does");

    // The same stroke written as text (how the command line passes it) is the same stroke.
    kb::assets::TerrainAsset viaText = kb::assets::MakeFlatTerrainAsset(65U, 64.0F, 64.0F);
    kb::terrain_editor::TerrainOpSummary textSummary{};
    Require(kb::terrain_editor::ApplyTerrainOp(viaText,
                Json(R"({"op":"brush","x":"4","z":"-6","radius":"9","strength":"1.5","falloff":"0.5"})"), textSummary, error), error.c_str());
    Require(viaText.heights == viaOp.heights, "numbers written as text mean the same");

    // A path stamps at a fixed spacing, however its points are written.
    kb::assets::TerrainAsset coarse = kb::assets::MakeFlatTerrainAsset(65U, 64.0F, 64.0F);
    kb::assets::TerrainAsset fine = coarse;
    kb::terrain_editor::TerrainOpSummary pathSummary{};
    Require(kb::terrain_editor::ApplyTerrainOp(coarse,
                Json(R"({"op":"brush","path":[[-20,0],[20,0]],"radius":4,"strength":0.5,"spacing":2})"), pathSummary, error), error.c_str());
    Require(kb::terrain_editor::ApplyTerrainOp(fine,
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
        Require(!kb::terrain_editor::ApplyTerrainOp(viaOp, Json(bad), summary, reason) && !reason.empty() && viaOp.heights == before.heights, bad);
    }
    Require(!kb::terrain_editor::ApplyTerrainOps(viaOp, Json(R"([{"op":"brush","x":0,"z":0},{"op":"nope"}])"), summary, error) &&
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
    Require(kb::terrain_editor::ApplyTerrainOp(terrain,
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
    Require(kb::terrain_editor::ApplyTerrainOp(terrain, Json(R"({"op":"paint","layer":1,"path":"-30,-20;-10,-20","radius":4})"), handSummary, error), error.c_str());
    Require(weightOf(-20.0F, -20.0F) > 0.99F, "painting a path colours its middle");
    Require(kb::terrain_editor::ApplyTerrainOp(terrain, Json(R"({"op":"paint","layer":1,"x":-20,"z":-20,"radius":6,"erase":true})"), handSummary, error), error.c_str());
    Require(weightOf(-20.0F, -20.0F) < 0.01F, "erasing takes the layer away");
    Require(!kb::terrain_editor::ApplyTerrainOp(terrain, Json(R"({"op":"paint","layer":2,"x":0,"z":0})"), handSummary, error) &&
            error.find("layer") != std::string::npos, "a layer the terrain does not have is refused");
}

} // namespace

void RunTerrainToolsTests() {
    RunSamplingTest();
    RunHeightHashTest();
    RunHeightmapExportTest();
    RunPreviewTest();
    RunOperationsTest();
    RunPaintOperationsTest();
}
