#pragma once
#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {
enum class VectorShape2DKind { Polygon, Line };
struct VectorShape2DAsset {
    VectorShape2DKind kind=VectorShape2DKind::Polygon;
    std::vector<std::array<float,2>> points{{-.5F,-.5F},{.5F,-.5F},{0,.5F}};
    float width=.1F;
    bool closed=false;
    std::string material;
};
[[nodiscard]] std::string ValidateVectorShape2D(const VectorShape2DAsset& shape);
[[nodiscard]] bool DecodeVectorShape2D(std::string_view source,VectorShape2DAsset& output,std::string& error);
[[nodiscard]] std::string EncodeVectorShape2D(const VectorShape2DAsset& shape);
[[nodiscard]] bool SaveVectorShape2D(const std::filesystem::path& path,const VectorShape2DAsset& shape,std::string& error);
}
