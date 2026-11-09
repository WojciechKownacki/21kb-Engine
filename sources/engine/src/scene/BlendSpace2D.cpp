#include "engine/scene/BlendSpace2D.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>

namespace kb::scene {
namespace {
constexpr double epsilon = 64.0 * std::numeric_limits<double>::epsilon();

template <class Point>
double Orient(const Point& a, const Point& b, const Point& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

template <class Point>
double InCircle(const Point& a, const Point& b, const Point& c, const Point& p) {
    const double ax = a.x - p.x, ay = a.y - p.y;
    const double bx = b.x - p.x, by = b.y - p.y;
    const double cx = c.x - p.x, cy = c.y - p.y;
    return (ax * ax + ay * ay) * (bx * cy - by * cx) -
        (bx * bx + by * by) * (ax * cy - ay * cx) +
        (cx * cx + cy * cy) * (ax * by - ay * bx);
}

std::array<std::size_t, 2> Edge(std::size_t a, std::size_t b) {
    return {std::min(a, b), std::max(a, b)};
}
} // namespace

std::optional<BlendSpace2DPlan> BlendSpace2DPlan::Build(const BlendSpace2DAsset& asset, std::string& error) {
    error.clear();
    if (asset.samples.size() > 256) { error = "BlendSpace2D supports at most 256 samples"; return std::nullopt; }
    BlendSpace2DPlan plan;
    if (asset.samples.empty()) return plan;
    double minX = std::numeric_limits<double>::infinity(), minY = minX;
    double maxX = -minX, maxY = -minX;
    for (const auto& sample : asset.samples) {
        if (!std::isfinite(sample.x) || !std::isfinite(sample.y) || sample.clipReference.empty() ||
            sample.clipReference.size()>4096 || sample.clipReference.find_first_of("\r\n")!=std::string::npos ||
            sample.clipReference.find('\0')!=std::string::npos) {
            error = "BlendSpace2D requires finite coordinates and valid clip references"; return std::nullopt;
        }
        minX = std::min(minX, static_cast<double>(sample.x)); maxX = std::max(maxX, static_cast<double>(sample.x));
        minY = std::min(minY, static_cast<double>(sample.y)); maxY = std::max(maxY, static_cast<double>(sample.y));
    }
    plan.originX_ = (minX + maxX) * 0.5;
    plan.originY_ = (minY + maxY) * 0.5;
    plan.extent_ = std::max(maxX - minX, maxY - minY);
    if (plan.extent_ == 0.0) plan.extent_ = 1.0;
    for (std::size_t i = 0; i < asset.samples.size(); ++i) {
        const auto& sample = asset.samples[i];
        plan.points_.push_back({(sample.x - plan.originX_) / plan.extent_,
            (sample.y - plan.originY_) / plan.extent_, i});
    }
    std::ranges::sort(plan.points_, [](const Point& a, const Point& b) { return std::tie(a.x, a.y) < std::tie(b.x, b.y); });
    for (std::size_t i = 1; i < plan.points_.size(); ++i) {
        if (plan.points_[i - 1].x == plan.points_[i].x && plan.points_[i - 1].y == plan.points_[i].y) {
            error = "BlendSpace2D sample coordinates must be unique"; return std::nullopt;
        }
    }
    const std::size_t count = plan.points_.size();
    // Seed with real points. A finite synthetic outer triangle can swallow
    // narrow domains whose circumcircles extend far beyond their bounds.
    std::vector<Triangle> triangles;
    std::size_t third = 0;
    double largestArea = 0.0;
    for (std::size_t i = 1; i + 1 < count; ++i) {
        const double area = std::abs(Orient(plan.points_.front(), plan.points_.back(), plan.points_[i]));
        if (area > largestArea) { largestArea = area; third = i; }
    }
    const auto oriented = [&](std::size_t a, std::size_t b, std::size_t c) {
        return Orient(plan.points_[a], plan.points_[b], plan.points_[c]) > 0.0 ? Triangle{a,b,c} : Triangle{b,a,c};
    };
    if (largestArea > epsilon) {
        triangles.push_back(oriented(0, count - 1, third));
        for (std::size_t index = 1; index + 1 < count; ++index) {
            if (index == third) continue;
            std::map<std::array<std::size_t, 2>, std::size_t> removedEdges;
            std::erase_if(triangles, [&](const Triangle& triangle) {
                for (std::size_t i = 0; i < 3; ++i) {
                    if (Orient(plan.points_[triangle[i]], plan.points_[triangle[(i+1)%3]], plan.points_[index]) < -epsilon)
                        return false;
                }
                for (std::size_t i = 0; i < 3; ++i) ++removedEdges[Edge(triangle[i], triangle[(i+1)%3])];
                return true;
            });
            const auto append = [&](std::size_t a, std::size_t b) {
                if (std::abs(Orient(plan.points_[a], plan.points_[b], plan.points_[index])) > epsilon)
                    triangles.push_back(oriented(a,b,index));
            };
            if (!removedEdges.empty()) {
                // Splitting all incident faces also handles insertion on an
                // internal edge without leaving an unmatched boundary vertex.
                for (const auto& [edge, uses] : removedEdges) if (uses == 1) append(edge[0],edge[1]);
            } else {
                std::map<std::array<std::size_t, 2>, std::size_t> uses;
                for (const auto& triangle : triangles) for (std::size_t i = 0; i < 3; ++i)
                    ++uses[Edge(triangle[i],triangle[(i+1)%3])];
                std::vector<std::array<std::size_t,2>> visible;
                for (const auto& triangle : triangles) for (std::size_t i = 0; i < 3; ++i) {
                    const auto a = triangle[i], b = triangle[(i+1)%3];
                    if (uses[Edge(a,b)] == 1 && Orient(plan.points_[a],plan.points_[b],plan.points_[index]) < -epsilon)
                        visible.push_back({a,b});
                }
                for (const auto& edge : visible) append(edge[0],edge[1]);
            }
        }
        // Legalize convex shared edges. Strict improvement leaves cocircular
        // ties in canonical insertion order and avoids diagonal oscillation.
        bool stable = false;
        for (std::size_t iteration = 0; iteration < count * count * 8; ++iteration) {
            std::map<std::array<std::size_t,2>,std::vector<std::size_t>> incident;
            for (std::size_t t = 0; t < triangles.size(); ++t) for (std::size_t i = 0; i < 3; ++i)
                incident[Edge(triangles[t][i],triangles[t][(i+1)%3])].push_back(t);
            bool flipped = false;
            for (const auto& [edge, faces] : incident) {
                if (faces.size() != 2) continue;
                const auto opposite = [&](const Triangle& triangle) {
                    for (const auto vertex : triangle) if (vertex != edge[0] && vertex != edge[1]) return vertex;
                    return edge[0];
                };
                const auto c = opposite(triangles[faces[0]]), d = opposite(triangles[faces[1]]);
                const double sideA = Orient(plan.points_[c],plan.points_[d],plan.points_[edge[0]]);
                const double sideB = Orient(plan.points_[c],plan.points_[d],plan.points_[edge[1]]);
                if (!((sideA > epsilon && sideB < -epsilon) || (sideA < -epsilon && sideB > epsilon))) continue;
                const auto triangle = oriented(edge[0],edge[1],c);
                if (InCircle(plan.points_[triangle[0]],plan.points_[triangle[1]],plan.points_[triangle[2]],plan.points_[d]) <= epsilon) continue;
                triangles[faces[0]] = oriented(c,d,edge[0]);
                triangles[faces[1]] = oriented(d,c,edge[1]);
                flipped = true;
                break;
            }
            if (!flipped) { stable = true; break; }
        }
        if (!stable) { error = "BlendSpace2D triangulation did not converge"; return std::nullopt; }
    }
    std::map<std::array<std::size_t, 2>, std::size_t> edges;
    for (const auto& triangle : triangles) {
        plan.triangles_.push_back(triangle);
        for (std::size_t i = 0; i < 3; ++i) ++edges[Edge(triangle[i], triangle[(i + 1) % 3])];
    }
    if (plan.triangles_.empty()) {
        // A line uses nearest-point projection onto adjacent canonical samples.
        // This also handles vertical lines and two-sample documents.
        for (std::size_t i = 1; i < count; ++i) plan.boundary_.push_back({i - 1, i});
    } else {
        for (const auto& [edge, uses] : edges) if (uses == 1) plan.boundary_.push_back(edge);
    }
    for (auto& triangle : plan.triangles_) for (auto& index : triangle) index = plan.points_[index].sample;
    for (auto& edge : plan.boundary_) for (auto& index : edge) index = plan.points_[index].sample;
    std::ranges::sort(plan.points_, {}, &Point::sample);
    return plan;
}

std::optional<BlendSpace2DWeights> BlendSpace2DPlan::Sample(float x, float y, std::string& error) const {
    error.clear();
    if (!std::isfinite(x) || !std::isfinite(y)) { error = "BlendSpace2D query must be finite"; return std::nullopt; }
    BlendSpace2DWeights result;
    if (points_.empty()) return result;
    if (points_.size() == 1) { result.values[0] = {points_[0].sample, 1.0F}; result.count = 1; return result; }
    const Point query{(x - originX_) / extent_, (y - originY_) / extent_};
    const auto emit = [&](std::span<const std::size_t> indices, std::span<const double> weights) {
        BlendSpace2DWeights values;
        double sum = 0.0;
        for (const double weight : weights) sum += std::max(0.0, weight);
        for (std::size_t i = 0; i < indices.size(); ++i) {
            if (weights[i] <= 0.0) continue;
            values.values[values.count++] = {points_[indices[i]].sample, static_cast<float>(weights[i] / sum)};
        }
        return values;
    };
    for (const auto& triangle : triangles_) {
        const auto& a = points_[triangle[0]];
        const auto& b = points_[triangle[1]];
        const auto& c = points_[triangle[2]];
        const double area = Orient(a, b, c);
        const std::array weights{Orient(b, c, query) / area, Orient(c, a, query) / area, Orient(a, b, query) / area};
        if (std::ranges::all_of(weights, [](double value) { return value >= -epsilon; })) return emit(triangle, weights);
    }
    double bestDistance = std::numeric_limits<double>::infinity();
    for (const auto& edge : boundary_) {
        const auto& a = points_[edge[0]];
        const auto& b = points_[edge[1]];
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double alpha = std::clamp(((query.x - a.x) * dx + (query.y - a.y) * dy) / (dx * dx + dy * dy), 0.0, 1.0);
        const double ex = query.x - (a.x + alpha * dx), ey = query.y - (a.y + alpha * dy);
        const double distance = ex * ex + ey * ey;
        if (distance >= bestDistance) continue;
        bestDistance = distance;
        const std::array weights{1.0 - alpha, alpha};
        result = emit(edge, weights);
    }
    return result;
}

} // namespace kb::scene
