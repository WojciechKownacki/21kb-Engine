#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kb::scene {

inline constexpr const char* kBlendSpace2DAssetType = "BlendSpace2D";
inline constexpr const char* kBlendSpace2DAssetExtension = ".kbblend2d";

struct BlendSpace2DSample {
    float x = 0.0F;
    float y = 0.0F;
    std::string clipReference;
};

struct BlendSpace2DAsset {
    std::vector<BlendSpace2DSample> samples;
};

struct BlendSpace2DWeight {
    std::size_t sample = 0;
    float weight = 0.0F;
};

struct BlendSpace2DWeights {
    std::array<BlendSpace2DWeight, 3> values{};
    std::size_t count = 0;
};

// Built once after an asset load/edit. Triangles and weights retain authored
// sample indices; canonical geometric ordering makes input permutations stable.
class BlendSpace2DPlan final {
public:
    using Triangle = std::array<std::size_t, 3>;
    [[nodiscard]] static std::optional<BlendSpace2DPlan> Build(
        const BlendSpace2DAsset& asset, std::string& error);
    [[nodiscard]] std::optional<BlendSpace2DWeights> Sample(float x, float y, std::string& error) const;
    [[nodiscard]] std::span<const Triangle> Triangles() const noexcept { return triangles_; }
    [[nodiscard]] std::size_t SampleCount() const noexcept { return points_.size(); }

private:
    struct Point { double x = 0.0, y = 0.0; std::size_t sample = 0; };
    std::vector<Point> points_;
    std::vector<Triangle> triangles_;
    std::vector<std::array<std::size_t, 2>> boundary_;
    double originX_ = 0.0, originY_ = 0.0, extent_ = 1.0;
};

} // namespace kb::scene
