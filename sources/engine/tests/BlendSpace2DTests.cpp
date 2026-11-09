#include "engine/scene/BlendSpace2D.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>

namespace {
using namespace kb::scene;
void Require(bool value, const char* error) { if (!value) throw std::runtime_error(error); }
bool Near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }

BlendSpace2DPlan Build(const BlendSpace2DAsset& asset) {
    std::string error;
    auto plan = BlendSpace2DPlan::Build(asset, error);
    Require(plan.has_value() && error.empty(), "Valid blend space rejected");
    return std::move(*plan);
}

std::vector<double> Weights(const BlendSpace2DPlan& plan, float x, float y) {
    std::string error;
    const auto sample = plan.Sample(x, y, error);
    Require(sample.has_value() && error.empty() && sample->count <= 3, "Blend space sample failed");
    std::vector<double> weights(plan.SampleCount());
    double sum = 0;
    for (std::size_t i = 0; i < sample->count; ++i) {
        const auto& value = sample->values[i];
        Require(value.sample < weights.size() && value.weight > 0.0F && std::isfinite(value.weight), "Invalid sample weight");
        Require(weights[value.sample] == 0.0, "Repeated sample contribution");
        weights[value.sample] = value.weight;
        sum += value.weight;
    }
    Require(weights.empty() ? sum == 0.0 : Near(sum, 1.0), "Blend weights do not form a partition of unity");
    return weights;
}

void DegenerateCases() {
    const auto empty = Build({});
    Require(Weights(empty, 0, 0).empty(), "Empty document must have no active sample");
    const auto single = Build({{{-4, 7, "Single"}}});
    Require(Weights(single, 123, -456) == std::vector<double>{1.0}, "Single sample must be constant");
    const auto horizontal = Build({{{-2, 0, "Left"}, {2, 0, "Right"}}});
    auto weights = Weights(horizontal, -1, 50);
    Require(Near(weights[0], 0.75) && Near(weights[1], 0.25), "Two samples must project onto their segment");
    Require(Weights(horizontal, 10, 2) == std::vector<double>({0, 1}), "Outside line query must clamp to its endpoint");
    const auto vertical = Build({{{1, 3, "Top"}, {1, -3, "Bottom"}, {1, 0, "Middle"}}});
    weights = Weights(vertical, -100, 1.5F);
    Require(Near(weights[0], 0.5) && weights[1] == 0.0 && Near(weights[2], 0.5), "Vertical collinear samples must use adjacent intervals");
    const auto diagonal = Build({{{-1, -1, "A"}, {1, 1, "B"}, {0, 0, "C"}}});
    weights = Weights(diagonal, -1, 0);
    Require(Near(weights[0], 0.5) && weights[1] == 0 && Near(weights[2], 0.5), "Diagonal projection must preserve authored indices");
}

void TriangleCases() {
    const auto plan = Build({{{-1, -1, "A"}, {1, -1, "B"}, {-1, 1, "C"}}});
    Require(plan.Triangles().size() == 1, "Triangle document must retain one triangle");
    auto weights = Weights(plan, -0.5F, -0.5F);
    Require(Near(weights[0], 0.5) && Near(weights[1], 0.25) && Near(weights[2], 0.25), "Interior barycentric weights are incorrect");
    weights = Weights(plan, 2, 2);
    Require(weights[0] == 0 && Near(weights[1], 0.5) && Near(weights[2], 0.5), "Outside query must project onto the nearest hull edge");
    Require(Weights(plan, -1, 1) == std::vector<double>({0, 0, 1}), "Authored vertex must reproduce its clip exactly");
    const auto skinny = Build({{{0, 0, "A"}, {1, 0, "B"}, {0.5F, 0.00001F, "C"}}});
    Require(skinny.Triangles().size() == 1, "A narrow noncollinear domain must retain its triangle");
    weights = Weights(skinny, 0.5F, 0.000005F);
    Require(Near(weights[0], 0.25) && Near(weights[1], 0.25) && Near(weights[2], 0.5), "Narrow triangle lost a sample");
    const auto extreme = Build({{{-3e38F, -3e38F, "A"}, {3e38F, -3e38F, "B"}, {0, 3e38F, "C"}}});
    weights = Weights(extreme, 0, 0);
    Require(Near(weights[0], 0.25) && Near(weights[1], 0.25) && Near(weights[2], 0.5), "Finite extreme coordinates overflowed geometry");
}

void PermutationCases() {
    const BlendSpace2DAsset asset{{{-1,-1,"A"},{1,-1,"B"},{1,1,"C"},{-1,1,"D"},{0,0,"E"}}};
    const auto expected = Weights(Build(asset), 0.25F, 0.375F);
    std::array<std::size_t, 5> order{0,1,2,3,4};
    do {
        BlendSpace2DAsset permuted;
        for (const auto index : order) permuted.samples.push_back(asset.samples[index]);
        const auto weights = Weights(Build(permuted), 0.25F, 0.375F);
        for (std::size_t i = 0; i < order.size(); ++i) Require(Near(weights[i], expected[order[i]]), "Input order changed geometric contributions");
    } while (std::next_permutation(order.begin(), order.end()));
}

void GridCases() {
    BlendSpace2DAsset asset;
    for (int x = 0; x < 16; ++x) for (int y = 0; y < 16; ++y)
        asset.samples.push_back({static_cast<float>(x - 8), static_cast<float>(y - 8), "Clip"});
    const auto plan = Build(asset);
    Require(plan.Triangles().size() == 450, "Grid triangulation lost cells or introduced overlapping triangles");
    std::mt19937 random{721};
    std::uniform_real_distribution<float> coordinate{-8, 7};
    for (int iteration = 0; iteration < 512; ++iteration) {
        const float x = coordinate(random), y = coordinate(random);
        const auto weights = Weights(plan, x, y);
        double reconstructedX = 0, reconstructedY = 0;
        for (std::size_t i = 0; i < weights.size(); ++i) {
            reconstructedX += asset.samples[i].x * weights[i];
            reconstructedY += asset.samples[i].y * weights[i];
        }
        Require(Near(reconstructedX, x, 2e-6) && Near(reconstructedY, y, 2e-6), "Triangulation failed to reproduce an affine motion field");
    }
    for (const auto& triangle : plan.Triangles()) {
        const auto& a = asset.samples[triangle[0]], &b = asset.samples[triangle[1]], &c = asset.samples[triangle[2]];
        const double area = (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
        Require(area > 0, "Triangulation contains a reversed or degenerate triangle");
        for (const auto& sample : asset.samples) {
            const double ax=a.x-sample.x, ay=a.y-sample.y, bx=b.x-sample.x, by=b.y-sample.y, cx=c.x-sample.x, cy=c.y-sample.y;
            const double determinant=(ax*ax+ay*ay)*(bx*cy-by*cx)-(bx*bx+by*by)*(ax*cy-ay*cx)+(cx*cx+cy*cy)*(ax*by-ay*bx);
            Require(determinant <= 1e-8, "Triangulation violates the empty circumcircle condition");
        }
    }
}

void InvalidCases() {
    std::string error;
    for (const BlendSpace2DAsset asset : {BlendSpace2DAsset{{{0,0,"A"},{0,0,"B"}}},
        BlendSpace2DAsset{{{0,0,""}}}, BlendSpace2DAsset{{{std::numeric_limits<float>::infinity(),0,"A"}}},
        BlendSpace2DAsset{{{0,std::numeric_limits<float>::quiet_NaN(),"A"}}}})
        Require(!BlendSpace2DPlan::Build(asset,error) && !error.empty(), "Invalid blend space was accepted");
    BlendSpace2DAsset large;
    large.samples.resize(257);
    Require(!BlendSpace2DPlan::Build(large,error), "Unbounded blend space was accepted");
    Require(!Build({}).Sample(std::numeric_limits<float>::quiet_NaN(),0,error) && !error.empty(), "Nonfinite query was accepted");
}
} // namespace

int main() {
    try {
        DegenerateCases(); TriangleCases(); PermutationCases(); GridCases(); InvalidCases();
        std::cout << "BlendSpace2D geometry checks PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
