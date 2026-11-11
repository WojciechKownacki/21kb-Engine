#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/render/StaticLightmapBaker.hpp"

#include <algorithm>
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
} // namespace

void RunStaticLightmapBakerTests() {
    RunBounceTests();
    RunGeneratedUvTests();
}
} // namespace kb::tests
