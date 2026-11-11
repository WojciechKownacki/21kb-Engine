#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/render/StaticLightmapBaker.hpp"

#include <algorithm>
#include <cmath>
#include <array>
#include <cstdint>
#include <vector>

namespace kb::tests {
namespace {
using kb::render::LightmapLight;
using kb::render::LightmapTriangle;
using kb::render::LightmapVertex;

LightmapTriangle Triangle(LightmapVertex a, LightmapVertex b, LightmapVertex c) { return { a, b, c }; }

// Floor receiver (UV atlas), a plate that shadows the floor corner from the light, and a bright ceiling.
struct Room {
    std::vector<LightmapTriangle> floor, others;
    std::vector<LightmapLight> lights;
    Room() {
        const kb::math::Vec3 up{ 0, 1, 0 };
        const LightmapVertex a{ { -2, 0, -2 }, up, 0, 0 }, b{ { 2, 0, -2 }, up, 1, 0 }, c{ { -2, 0, 2 }, up, 0, 1 }, d{ { 2, 0, 2 }, up, 1, 1 };
        floor = { Triangle(a, b, c), Triangle(b, d, c) };
        const auto plane = [&](float y, float lo, float hi, float albedo) {
            LightmapVertex p[4];
            const float xs[]{ lo, hi, lo, hi }, zs[]{ lo, lo, hi, hi };
            for (int i = 0; i < 4; ++i) p[i] = { { xs[i], y, zs[i] }, { 0, -1, 0 }, 0, 0, { albedo, albedo, albedo } };
            others.push_back(Triangle(p[0], p[1], p[2]));
            others.push_back(Triangle(p[1], p[3], p[2]));
        };
        plane(2.0F, -6.0F, 6.0F, 0.9F);
        // The plate sits between the light and the floor corner at x = 1.8.
        const float zs[]{ -1.0F, 1.0F };
        const kb::math::Vec3 down{ 0, -1, 0 };
        const LightmapVertex p0{ { 0.5F, 0.25F, zs[0] }, down }, p1{ { 1.5F, 0.25F, zs[0] }, down }, p2{ { 0.5F, 0.25F, zs[1] }, down }, p3{ { 1.5F, 0.25F, zs[1] }, down };
        others.push_back(Triangle(p0, p1, p2));
        others.push_back(Triangle(p1, p3, p2));
        lights.push_back({ .position = { 0, 0.5F, 0 }, .intensity = 30, .range = 20 });
    }
};

std::uint64_t Fnv(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto byte : bytes) hash = (hash ^ byte) * 1099511628211ULL;
    return hash;
}

void RunBounceTests() {
    const Room room;
    kb::render::StaticLightmapSettings settings;
    settings.resolution = 32;
    settings.bounces = 0;
    const auto direct = kb::render::BakeStaticLightmap(room.floor, room.others, room.lights, settings);
    kb::tests::Require(direct.Succeeded() && direct.shadowedSamples > 0, "direct-only lightmap bake succeeds with shadows");
    // Golden of the direct pass taken before the bounce pass existed.
    kb::tests::Require(Fnv(direct.rgba) == 0x29d2a8dc35f9375bULL, "bounces == 0 keeps the direct-only bytes unchanged");

    settings.bounces = 1;
    const auto one = kb::render::BakeStaticLightmap(room.floor, room.others, room.lights, settings);
    const auto again = kb::render::BakeStaticLightmap(room.floor, room.others, room.lights, settings);
    kb::tests::Require(one.Succeeded() && one.rgba == again.rgba, "bounced bake is deterministic");
    // Floor corner at x = 1.8, z = 0 is in the plate's shadow: only the ambient term remains without bounces.
    const std::size_t corner = (16U * 32U + 30U) * 4U;
    kb::tests::Require(direct.rgba[corner + 1] < 60 && one.rgba[corner + 1] > direct.rgba[corner + 1] + 20,
        "one bounce brightens a texel that only receives indirect light");

    settings.bounces = 2;
    const auto two = kb::render::BakeStaticLightmap(room.floor, room.others, room.lights, settings);
    kb::tests::Require(two.Succeeded() && two.rgba[corner + 1] >= one.rgba[corner + 1], "a second bounce does not darken the corner");
    settings.bounces = 5;
    kb::tests::Require(!kb::render::BakeStaticLightmap(room.floor, room.others, room.lights, settings).Succeeded(), "more than four bounces is rejected");
}

std::vector<LightmapTriangle> Cube() {
    std::vector<LightmapTriangle> triangles;
    for (int axis = 0; axis < 3; ++axis) for (const float sign : { -1.0F, 1.0F }) {
        const auto point = [&](float s, float t) {
            float p[3]{}; p[axis] = sign; p[(axis + 1) % 3] = s; p[(axis + 2) % 3] = t;
            float n[3]{}; n[axis] = sign;
            return LightmapVertex{ { p[0], p[1], p[2] }, { n[0], n[1], n[2] }, (s + 1) * 0.5F, (t + 1) * 0.5F };
        };
        triangles.push_back(Triangle(point(-1, -1), point(1, -1), point(-1, 1)));
        triangles.push_back(Triangle(point(1, -1), point(1, 1), point(-1, 1)));
    }
    return triangles;
}

void RunGeneratedUvTests() {
    auto cube = Cube();
    kb::tests::Require(!kb::render::HasUsableLightmapUv(cube, 128), "per-face 0..1 UVs overlap and are not a lightmap atlas");
    const auto layout = kb::render::GenerateLightmapUv(cube, 128);
    kb::tests::Require(layout.Succeeded() && layout.uv.size() == cube.size() * 6U, "a cube gets a lightmap UV layout");
    bool inside = true;
    for (std::size_t i = 0; i < cube.size(); ++i) for (std::size_t k = 0; k < 3; ++k) {
        cube[i][k].u = layout.uv[i * 6 + k * 2];
        cube[i][k].v = layout.uv[i * 6 + k * 2 + 1];
        inside = inside && cube[i][k].u >= 1.0F / 128.0F && cube[i][k].u <= 1.0F - 1.0F / 128.0F && cube[i][k].v >= 1.0F / 128.0F && cube[i][k].v <= 1.0F - 1.0F / 128.0F;
    }
    kb::tests::Require(inside, "generated UVs stay inside the atlas");
    kb::tests::Require(kb::render::HasUsableLightmapUv(cube, 128), "generated UVs do not overlap");
    const auto again = kb::render::GenerateLightmapUv(Cube(), 128);
    kb::tests::Require(again.uv == layout.uv, "UV generation is deterministic");
    const std::array lights{ LightmapLight{ .position = { 0, 4, 0 }, .intensity = 20, .range = 20 } };
    const auto baked = kb::render::BakeStaticLightmap(cube, cube, lights, { .resolution = 128 });
    kb::tests::Require(baked.Succeeded() && baked.coveredTexels > 12U * 20U, "the generated atlas bakes without an overlap error");
}
// Quad corners a (0,0), b (1,0), c (0,1), d (1,1) as two triangles with matching UVs.
std::vector<LightmapTriangle> Quad(kb::math::Vec3 a, kb::math::Vec3 b, kb::math::Vec3 c, kb::math::Vec3 d, kb::math::Vec3 normal, kb::math::Vec3 albedo) {
    const LightmapVertex va{ a, normal, 0, 0, albedo }, vb{ b, normal, 1, 0, albedo }, vc{ c, normal, 0, 1, albedo }, vd{ d, normal, 1, 1, albedo };
    return { Triangle(va, vb, vc), Triangle(vb, vd, vc) };
}

void RunSpotLightTests() {
    const auto floor = Quad({ -4, 0, -4 }, { 4, 0, -4 }, { -4, 0, 4 }, { 4, 0, 4 }, { 0, 1, 0 }, { 0.5F, 0.5F, 0.5F });
    LightmapLight spot{ .position = { 0, 4, 0 }, .direction = { 0, -1, 0 }, .intensity = 40, .range = 30, .spot = true, .innerConeDegrees = 10, .outerConeDegrees = 20 };
    kb::render::StaticLightmapSettings settings;
    settings.resolution = 32; settings.bounces = 0;
    const std::array spotLights{ spot };
    const auto lit = kb::render::BakeStaticLightmap(floor, {}, spotLights, settings);
    const auto green = [](const kb::render::StaticLightmapResult& result, std::size_t x, std::size_t y) { return result.rgba[(y * 32U + x) * 4U + 1U]; };
    kb::tests::Require(lit.Succeeded() && green(lit, 16, 16) > 200 && green(lit, 28, 16) < 70, "a spot light lights the floor inside its cone and not outside");
    spot.spot = false;
    const std::array pointLights{ spot };
    const auto point = kb::render::BakeStaticLightmap(floor, {}, pointLights, settings);
    kb::tests::Require(point.Succeeded() && green(point, 28, 16) > 150, "the same light without a cone reaches the outside texel");
    spot.spot = true; spot.outerConeDegrees = 5;
    const std::array bad{ spot };
    kb::tests::Require(!kb::render::BakeStaticLightmap(floor, {}, bad, settings).Succeeded(), "a spot with an outer angle below the inner one is rejected");
}

void RunLightProbeTests() {
    kb::render::ShL2 sh;
    kb::render::AddShL2Direction(sh, { 0, 1, 0 }, { 1, 1, 1 });
    kb::tests::Require(sh.c[1].x > 0.4F && std::abs(sh.c[2].x) < 1.0e-5F && std::abs(sh.c[3].x) < 1.0e-5F,
        "light from +y projects onto the band-1 y coefficient");
    kb::tests::Require(kb::render::EvaluateShL2Irradiance(sh, { 0, 1, 0 }).x > kb::render::EvaluateShL2Irradiance(sh, { 1, 0, 0 }).x &&
        kb::render::EvaluateShL2Irradiance(sh, { 1, 0, 0 }).x > kb::render::EvaluateShL2Irradiance(sh, { 0, -1, 0 }).x,
        "SH irradiance is largest facing the light and smallest facing away");

    // Gray floor and left wall, red right wall, one light; every surface is a receiver with its own lightmap.
    const kb::math::Vec3 gray{ 0.6F, 0.6F, 0.6F }, red{ 0.8F, 0.05F, 0.05F };
    const auto floor = Quad({ -4, 0, -4 }, { 4, 0, -4 }, { -4, 0, 4 }, { 4, 0, 4 }, { 0, 1, 0 }, gray);
    const auto left = Quad({ -4, 0, -4 }, { -4, 0, 4 }, { -4, 4, -4 }, { -4, 4, 4 }, { 1, 0, 0 }, gray);
    const auto right = Quad({ 4, 0, 4 }, { 4, 0, -4 }, { 4, 4, 4 }, { 4, 4, -4 }, { -1, 0, 0 }, red);
    const std::array lights{ LightmapLight{ .position = { 0, 3, 0 }, .intensity = 40, .range = 30 } };
    kb::render::StaticLightmapSettings settings;
    settings.resolution = 32; settings.bounces = 1;
    std::vector<LightmapTriangle> everything;
    for (const auto* part : { &floor, &left, &right }) everything.insert(everything.end(), part->begin(), part->end());
    std::array<kb::render::StaticLightmapResult, 3> maps;
    std::array<kb::render::BakedLightmapSurface, 3> surfaces;
    const std::vector<LightmapTriangle>* parts[]{ &floor, &left, &right };
    for (std::size_t i = 0; i < 3; ++i) {
        maps[i] = kb::render::BakeStaticLightmap(*parts[i], everything, lights, settings);
        surfaces[i] = { *parts[i], maps[i].rgba, settings.resolution };
    }
    kb::tests::Require(maps[0].Succeeded() && maps[1].Succeeded() && maps[2].Succeeded(), "probe test room bakes");
    const auto grid = kb::render::BakeLightProbes(surfaces, lights, { -3, 0.5F, -3 }, { 3, 2.5F, 3 }, { .spacing = 3 });
    kb::tests::Require(grid.Succeeded() && grid.dimensions[0] == 3 && grid.dimensions[1] == 1 && grid.dimensions[2] == 3 && grid.probes.size() == 9U,
        "a probe grid covers the bounds at the requested spacing");
    const auto again = kb::render::BakeLightProbes(surfaces, lights, { -3, 0.5F, -3 }, { 3, 2.5F, 3 }, { .spacing = 3 });
    kb::tests::Require(kb::render::SerializeLightProbeGrid(grid) == kb::render::SerializeLightProbeGrid(again), "probe baking is deterministic");
    const auto parsed = kb::render::ParseLightProbeGrid(kb::render::SerializeLightProbeGrid(grid));
    kb::tests::Require(parsed.Succeeded() && kb::render::SerializeLightProbeGrid(parsed) == kb::render::SerializeLightProbeGrid(grid), "a probe grid survives its binary form");
    kb::tests::Require(!kb::render::ParseLightProbeGrid(kb::render::SerializeLightProbeGrid(grid).substr(0, 40)).Succeeded(), "a truncated probe grid is rejected");
    const auto nearRed = kb::render::EvaluateShL2Irradiance(kb::render::SampleLightProbeGrid(grid, { 3, 0.5F, 0 }), { 1, 0, 0 });
    const auto nearGray = kb::render::EvaluateShL2Irradiance(kb::render::SampleLightProbeGrid(grid, { -3, 0.5F, 0 }), { -1, 0, 0 });
    kb::tests::Require(nearRed.x > 1.5F * nearRed.z, "a probe facing the red wall receives red-shifted light");
    kb::tests::Require(std::abs(nearGray.x - nearGray.z) < 0.05F * nearGray.x + 1.0e-4F, "a probe facing the gray wall stays neutral");
    const auto leftProbe = kb::render::SampleLightProbeGrid(grid, { 0, 0.5F, 0 }), rightProbe = kb::render::SampleLightProbeGrid(grid, { 3, 0.5F, 0 });
    const auto middle = kb::render::SampleLightProbeGrid(grid, { 1.5F, 0.5F, 0 });
    kb::tests::Require(std::abs(middle.c[0].x - 0.5F * (leftProbe.c[0].x + rightProbe.c[0].x)) < 1.0e-4F, "sampling between probes blends them linearly");
    const auto coarse = kb::render::BakeLightProbes(surfaces, lights, { -3, 0.5F, -3 }, { 3, 2.5F, 3 }, { .spacing = 0.2F, .maxProbes = 20 });
    kb::tests::Require(coarse.Succeeded() && coarse.probes.size() <= 20U, "the probe count is capped by coarsening the spacing");
}
} // namespace

void RunStaticLightmapBakerTests() {
    RunBounceTests();
    RunGeneratedUvTests();
    RunSpotLightTests();
    RunLightProbeTests();
}
} // namespace kb::tests
