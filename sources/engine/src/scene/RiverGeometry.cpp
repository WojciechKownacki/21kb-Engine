#include "engine/scene/RiverGeometry.hpp"

#include "engine/library/EngineLibraryParsing.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace kb::scene {
namespace {

constexpr float kRadiansToDegrees = 57.29577951308232F;
constexpr std::uint32_t kMaxSubdivisions = 64U;
constexpr float kMinimumWaterfallDrop = 0.5F;

struct Flat {
    float x = 0.0F;
    float z = 0.0F;
};

[[nodiscard]] Flat Catmull(Flat p0, Flat p1, Flat p2, Flat p3, float t) noexcept {
    const float t2 = t * t;
    const float t3 = t2 * t;
    const auto axis = [&](float a, float b, float c, float d) {
        return 0.5F * (2.0F * b + (c - a) * t + (2.0F * a - 5.0F * b + 4.0F * c - d) * t2 + (3.0F * b - a - 3.0F * c + d) * t3);
    };
    return { axis(p0.x, p1.x, p2.x, p3.x), axis(p0.z, p1.z, p2.z, p3.z) };
}

[[nodiscard]] Flat CatmullTangent(Flat p0, Flat p1, Flat p2, Flat p3, float t) noexcept {
    const float t2 = t * t;
    const auto axis = [&](float a, float b, float c, float d) {
        return 0.5F * ((c - a) + 2.0F * (2.0F * a - 5.0F * b + 4.0F * c - d) * t + 3.0F * (3.0F * b - a - 3.0F * c + d) * t2);
    };
    return { axis(p0.x, p1.x, p2.x, p3.x), axis(p0.z, p1.z, p2.z, p3.z) };
}

[[nodiscard]] Flat FlatOf(const RiverPoint& point) noexcept { return { point.position.x, point.position.z }; }

[[nodiscard]] float Length(float x, float y, float z) noexcept { return std::sqrt(x * x + y * y + z * z); }

// The shortest plain decimal text that reads back as the same float (an exponent only for the extremes).
[[nodiscard]] std::string FloatText(float value) {
    char buffer[48];
    if (std::abs(value) >= 1.0e-4F && std::abs(value) < 1.0e7F) {
        for (int decimals = 0; decimals <= 9; ++decimals) {
            std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, static_cast<double>(value));
            double back = 0.0;
            if (kb::library::TryParseDouble(buffer, back) && static_cast<float>(back) == value) return buffer;
        }
    }
    std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
    return buffer;
}

} // namespace

bool SampleRiver(const RiverComponent& river, float maxStep, std::uint32_t maxSamples, std::vector<RiverSample>& samples) {
    samples.clear();
    if (!IsRiverComponentValid(river) || !std::isfinite(maxStep) || maxStep <= 0.0F || maxSamples < river.pointCount) return false;
    const std::uint32_t segments = river.pointCount - 1U;
    // Subdivisions of each segment, by its length; one step size for the whole river, raised if it would not fit.
    std::vector<float> lengths(segments);
    float total = 0.0F;
    for (std::uint32_t index = 0U; index < segments; ++index) {
        const Vec3 a = river.points[index].position;
        const Vec3 b = river.points[index + 1U].position;
        lengths[index] = Length(b.x - a.x, b.y - a.y, b.z - a.z);
        total += lengths[index];
    }
    float step = maxStep;
    std::vector<std::uint32_t> divisions(segments);
    for (int attempt = 0; attempt < 8; ++attempt) {
        std::uint32_t count = 1U;
        for (std::uint32_t index = 0U; index < segments; ++index) {
            divisions[index] = std::clamp(static_cast<std::uint32_t>(std::ceil(lengths[index] / step)), 1U, kMaxSubdivisions);
            count += divisions[index];
        }
        if (count <= maxSamples) break;
        step *= std::max(1.25F, static_cast<float>(count) / static_cast<float>(maxSamples));
        if (attempt == 7) return false;
    }
    samples.reserve(static_cast<std::size_t>(total / step) + river.pointCount + 1U);
    for (std::uint32_t index = 0U; index < segments; ++index) {
        const RiverPoint& from = river.points[index];
        const RiverPoint& to = river.points[index + 1U];
        const Flat p0 = FlatOf(river.points[index == 0U ? 0U : index - 1U]);
        const Flat p1 = FlatOf(from);
        const Flat p2 = FlatOf(to);
        const Flat p3 = FlatOf(river.points[std::min(index + 2U, river.pointCount - 1U)]);
        const std::uint32_t last = index + 1U == segments ? divisions[index] : divisions[index] - 1U;
        // The stations lie evenly along the curve, so the height, width and speed that change linearly between the
        // points change at the same rate along the way and a fall keeps one slope: t is found by arc length.
        constexpr std::uint32_t kFine = 8U;
        const std::uint32_t fineCount = divisions[index] * kFine;
        std::vector<float> arc(fineCount + 1U, 0.0F);
        Flat previousFlat = p1;
        for (std::uint32_t fine = 1U; fine <= fineCount; ++fine) {
            const Flat next = Catmull(p0, p1, p2, p3, static_cast<float>(fine) / static_cast<float>(fineCount));
            arc[fine] = arc[fine - 1U] + std::sqrt((next.x - previousFlat.x) * (next.x - previousFlat.x) + (next.z - previousFlat.z) * (next.z - previousFlat.z));
            previousFlat = next;
        }
        std::uint32_t cursor = 0U;
        for (std::uint32_t part = 0U; part <= last; ++part) {
            const float fraction = static_cast<float>(part) / static_cast<float>(divisions[index]);
            float t = fraction;
            if (arc[fineCount] > 1.0e-6F) {
                const float wanted = fraction * arc[fineCount];
                while (cursor + 1U < fineCount && arc[cursor + 1U] < wanted) ++cursor;
                const float span = arc[cursor + 1U] - arc[cursor];
                t = (static_cast<float>(cursor) + (span > 1.0e-9F ? std::clamp((wanted - arc[cursor]) / span, 0.0F, 1.0F) : 0.0F)) / static_cast<float>(fineCount);
            }
            const Flat flat = Catmull(p0, p1, p2, p3, t);
            Flat tangent = CatmullTangent(p0, p1, p2, p3, t);
            float tangentLength = std::sqrt(tangent.x * tangent.x + tangent.z * tangent.z);
            if (tangentLength < 1.0e-5F) {
                tangent = { p2.x - p1.x, p2.z - p1.z };
                tangentLength = std::sqrt(tangent.x * tangent.x + tangent.z * tangent.z);
            }
            RiverSample sample{};
            sample.position = { flat.x, std::lerp(from.position.y, to.position.y, fraction), flat.z };
            sample.direction = tangentLength > 1.0e-5F ? Vec3{ tangent.x / tangentLength, 0.0F, tangent.z / tangentLength }
                                                       : (samples.empty() ? Vec3{ 1.0F, 0.0F, 0.0F } : samples.back().direction);
            sample.width = std::lerp(from.width, to.width, fraction);
            sample.depth = std::lerp(from.depth, to.depth, fraction);
            sample.speed = std::lerp(from.speed, to.speed, fraction);
            if (!samples.empty()) {
                const Vec3 before = samples.back().position;
                sample.distance = samples.back().distance + Length(sample.position.x - before.x, sample.position.y - before.y, sample.position.z - before.z);
            }
            samples.push_back(sample);
        }
    }
    return samples.size() >= 2U;
}

bool SampleRiverInFrame(const RiverComponent& river, const RiverPlacement& riverPlacement, const RiverPlacement& frame,
    float maxStep, std::uint32_t maxSamples, std::vector<RiverSample>& samples) {
    if (!SampleRiver(river, maxStep, maxSamples, samples)) return false;
    const auto scaled = [](Vec3 value, Vec3 scale) { return Vec3{ value.x * scale.x, value.y * scale.y, value.z * scale.z }; };
    const Quat inverse = kb::math::Conjugate(frame.rotation);
    const float widthScale = frame.scale.x != 0.0F ? riverPlacement.scale.x / frame.scale.x : 1.0F;
    const float depthScale = frame.scale.y != 0.0F ? riverPlacement.scale.y / frame.scale.y : 1.0F;
    for (RiverSample& sample : samples) {
        const Vec3 world = riverPlacement.position + kb::math::Rotate(riverPlacement.rotation, scaled(sample.position, riverPlacement.scale));
        const Vec3 local = kb::math::Rotate(inverse, world - frame.position);
        sample.position = { frame.scale.x != 0.0F ? local.x / frame.scale.x : local.x, frame.scale.y != 0.0F ? local.y / frame.scale.y : local.y,
            frame.scale.z != 0.0F ? local.z / frame.scale.z : local.z };
        const Vec3 direction = kb::math::Rotate(inverse, kb::math::Rotate(riverPlacement.rotation, sample.direction));
        const float length = std::sqrt(direction.x * direction.x + direction.z * direction.z);
        if (length > 1.0e-6F) sample.direction = { direction.x / length, 0.0F, direction.z / length };
        sample.width *= widthScale;
        sample.depth *= depthScale;
    }
    return true;
}

std::vector<RiverWaterfall> FindRiverWaterfalls(std::span<const RiverSample> samples, float slopeDegrees) {
    std::vector<RiverWaterfall> falls;
    const auto steep = [&](std::size_t index) {
        const RiverSample& a = samples[index];
        const RiverSample& b = samples[index + 1U];
        const float run = std::sqrt((b.position.x - a.position.x) * (b.position.x - a.position.x) +
            (b.position.z - a.position.z) * (b.position.z - a.position.z));
        const float drop = a.position.y - b.position.y;
        return drop > 0.0F && std::atan2(drop, std::max(run, 1.0e-4F)) * kRadiansToDegrees >= slopeDegrees;
    };
    for (std::size_t index = 0U; index + 1U < samples.size();) {
        if (!steep(index)) {
            ++index;
            continue;
        }
        std::size_t last = index + 1U;
        while (last + 1U < samples.size() && steep(last)) ++last;
        const float top = samples[index].position.y;
        const float bottom = samples[last].position.y;
        if (top - bottom >= kMinimumWaterfallDrop) {
            falls.push_back({ static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(last), top, bottom });
        }
        index = last;
    }
    return falls;
}

namespace {

// The vertex of one lateral position (-1 left bank, 0 middle, 1 right bank) of a sample.
[[nodiscard]] SurfaceGeometryVertex WaterVertex(const RiverSample& sample, int lateral) noexcept {
    const float half = sample.width * 0.5F * static_cast<float>(lateral);
    const float flow = std::min(sample.speed / kRiverFlowScale, 0.95F);
    SurfaceGeometryVertex vertex{};
    vertex.position = { sample.position.x - sample.direction.z * half, sample.position.y, sample.position.z + sample.direction.x * half };
    vertex.normal = { sample.direction.x * flow, std::sqrt(1.0F - flow * flow), sample.direction.z * flow };
    vertex.u = (static_cast<float>(lateral) + 1.0F) * 0.5F;
    vertex.v = sample.distance / kRiverUvLength;
    return vertex;
}

// A strip of the samples [first, last]: three vertices across each, four triangles per interval.
void AppendStrip(std::span<const RiverSample> samples, std::uint32_t first, std::uint32_t last,
    std::vector<SurfaceGeometryVertex>& vertices, std::vector<std::uint32_t>& indices) {
    const auto base = static_cast<std::uint32_t>(vertices.size());
    for (std::uint32_t index = first; index <= last; ++index) {
        for (int lateral = -1; lateral <= 1; ++lateral) vertices.push_back(WaterVertex(samples[index], lateral));
    }
    for (std::uint32_t index = 0U; index < last - first; ++index) {
        const std::uint32_t row = base + index * 3U;
        for (std::uint32_t side = 0U; side < 2U; ++side) {
            const std::uint32_t a = row + side;
            const std::uint32_t b = a + 1U;
            const std::uint32_t c = a + 3U;
            const std::uint32_t d = a + 4U;
            indices.insert(indices.end(), { a, c, b, b, c, d });
        }
    }
}

} // namespace

bool BuildRiverSurface(const RiverComponent& river, RiverSurface& surface) {
    surface = {};
    // Two units between stations; no more than a few hundred triangles per river segment of 30 units.
    constexpr float kStep = 2.0F;
    constexpr std::uint32_t kMaxSamples = 1024U;
    if (!SampleRiver(river, kStep, kMaxSamples, surface.samples)) return false;
    surface.waterfalls = FindRiverWaterfalls(surface.samples, river.waterfallSlopeDegrees);
    const auto count = static_cast<std::uint32_t>(surface.samples.size());
    std::uint32_t cursor = 0U;
    const auto stream = [&](std::uint32_t first, std::uint32_t last) {
        if (last > first) AppendStrip(surface.samples, first, last, surface.streamVertices, surface.streamIndices);
    };
    surface.fallMeshes.resize(surface.waterfalls.size());
    for (std::size_t index = 0U; index < surface.waterfalls.size(); ++index) {
        const RiverWaterfall& fall = surface.waterfalls[index];
        stream(cursor, fall.first);
        AppendStrip(surface.samples, fall.first, fall.last, surface.fallMeshes[index].vertices, surface.fallMeshes[index].indices);
        cursor = fall.last;
    }
    stream(cursor, count - 1U);
    return true;
}

std::string RiverPointsToText(const RiverComponent& river) {
    std::string text;
    for (std::uint32_t index = 0U; index < river.pointCount && index < RiverComponent::MaxPoints; ++index) {
        const RiverPoint& point = river.points[index];
        if (index != 0U) text += ';';
        const float values[6]{ point.position.x, point.position.y, point.position.z, point.width, point.depth, point.speed };
        for (int value = 0; value < 6; ++value) {
            if (value != 0) text += ',';
            text += FloatText(values[value]);
        }
    }
    return text;
}

bool ParseRiverPointList(std::string_view text, std::vector<RiverPoint>& points, std::string& error) {
    points.clear();
    std::uint32_t count = 0U;
    while (!text.empty()) {
        const std::size_t end = text.find(';');
        std::string_view part = text.substr(0U, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1U);
        if (part.find_first_not_of(" \t\r\n") == std::string_view::npos) continue;
        if (count == RiverComponent::MaxPoints) {
            error = "a river has at most " + std::to_string(RiverComponent::MaxPoints) + " points";
            return false;
        }
        float values[6]{};
        std::uint32_t numbers = 0U;
        while (!part.empty()) {
            const std::size_t comma = part.find(',');
            std::string_view item = part.substr(0U, comma);
            part = comma == std::string_view::npos ? std::string_view{} : part.substr(comma + 1U);
            while (!item.empty() && (item.front() == ' ' || item.front() == '\t')) item.remove_prefix(1U);
            while (!item.empty() && (item.back() == ' ' || item.back() == '\t' || item.back() == '\r' || item.back() == '\n')) item.remove_suffix(1U);
            if (numbers == 6U) {
                error = "point " + std::to_string(count) + " has more than 6 numbers";
                return false;
            }
            double parsed = 0.0;
            if (item.empty() || !kb::library::TryParseDouble(item, parsed) || !std::isfinite(parsed) || std::abs(parsed) > 3.0e38) {
                error = "point " + std::to_string(count) + ": '" + std::string{ item } + "' is not a number";
                return false;
            }
            values[numbers++] = static_cast<float>(parsed);
        }
        if (numbers != 3U && numbers != 6U) {
            error = "point " + std::to_string(count) + " takes x,y,z or x,y,z,width,depth,speed";
            return false;
        }
        RiverPoint point{};
        point.position = { values[0], values[1], values[2] };
        if (numbers == 6U) {
            point.width = values[3];
            point.depth = values[4];
            point.speed = values[5];
        }
        if (!IsRiverPointValid(point)) {
            error = "point " + std::to_string(count) + " is out of range (width and depth above 0, speed 0 or more)";
            return false;
        }
        points.push_back(point);
        ++count;
    }
    return true;
}

bool SetRiverPoints(RiverComponent& river, std::span<const RiverPoint> points, std::string& error) {
    if (!IsRiverPointCountValid(static_cast<std::uint32_t>(points.size()))) {
        error = "a river needs 2 to " + std::to_string(RiverComponent::MaxPoints) + " points, not " + std::to_string(points.size());
        return false;
    }
    river.points = {};
    std::copy(points.begin(), points.end(), river.points.begin());
    river.pointCount = static_cast<std::uint32_t>(points.size());
    return true;
}

bool ParseRiverPointsText(std::string_view text, RiverComponent& river, std::string& error) {
    std::vector<RiverPoint> points;
    return ParseRiverPointList(text, points, error) && SetRiverPoints(river, points, error);
}

} // namespace kb::scene
