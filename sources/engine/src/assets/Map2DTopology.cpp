#include "engine/assets/Map2DAsset.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace kb::assets {
namespace {
constexpr double kRoot3 = 1.7320508075688772935;
constexpr std::array<std::array<int,2>,4> kSquare{{{0,-1},{1,0},{0,1},{-1,0}}};
constexpr std::array<std::array<int,2>,8> kSquareEight{{{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}}};
constexpr std::array<std::array<int,2>,6> kHex{{{1,0},{0,1},{-1,1},{-1,0},{0,-1},{1,-1}}};
constexpr std::array<std::array<float,2>,4> kRectOutline{{{0,0},{1,0},{1,1},{0,1}}};
constexpr std::array<std::array<float,2>,4> kDiamondOutline{{{.5F,0},{1,.5F},{.5F,1},{0,.5F}}};
constexpr std::array<std::array<float,2>,6> kHexOutline{{{.5F,0},{1,.25F},{1,.75F},{.5F,1},{0,.75F},{0,.25F}}};
}

std::string_view Map2DTopologyName(Map2DTopology topology) noexcept {
    switch(topology) {
    case Map2DTopology::Rectangular:return "rectangular";
    case Map2DTopology::Isometric:return "isometric";
    case Map2DTopology::Hexagonal:return "hexagonal";
    }
    return "invalid";
}
double Map2DCellHeight(const Map2DAsset& map) noexcept {
    if(map.topology==Map2DTopology::Isometric) return map.tileSize*.5;
    if(map.topology==Map2DTopology::Hexagonal) return map.tileSize*2.0/kRoot3;
    return map.topology==Map2DTopology::Rectangular?map.tileSize:std::numeric_limits<double>::quiet_NaN();
}
std::array<double,2> Map2DProjectedSize(const Map2DAsset& map) noexcept {
    const double w=map.width,h=map.height,s=map.tileSize;
    if(map.topology==Map2DTopology::Isometric) return {(w+h)*s*.5,(w+h)*s*.25};
    if(map.topology==Map2DTopology::Hexagonal) return {(w+std::max(0.0,h-1)*.5)*s,(std::max(0.0,h-1)*kRoot3*.5+2.0/kRoot3)*s};
    if(map.topology==Map2DTopology::Rectangular) return {w*s,h*s};
    return {std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::quiet_NaN()};
}
std::array<double,2> Map2DProjectGrid(const Map2DAsset& map,double x,double y) noexcept {
    const double s=map.tileSize;
    if(map.topology==Map2DTopology::Isometric) return {(x-y+map.height)*.5*s,(x+y)*.25*s};
    if(map.topology==Map2DTopology::Hexagonal) return {(x+y*.5-.25)*s,(y*kRoot3*.5+1/(4*kRoot3))*s};
    if(map.topology==Map2DTopology::Rectangular) return {x*s,y*s};
    return {std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::quiet_NaN()};
}
std::optional<std::array<int,2>> Map2DCellAtPlane(const Map2DAsset& map,double x,double down) noexcept {
    if(!std::isfinite(x)||!std::isfinite(down)||!std::isfinite(map.tileSize)||map.tileSize<=0) return {};
    const double u=x/map.tileSize,v=down/map.tileSize;double q=u,r=v;
    if(map.topology==Map2DTopology::Isometric) {q=u-map.height*.5+2*v;r=2*v-u+map.height*.5;}
    else if(map.topology==Map2DTopology::Hexagonal) {
        r=(v-1/kRoot3)/(kRoot3*.5);q=u-.5-r*.5;
        const double third=-q-r;
        double rq=std::round(q),rr=std::round(r),rs=std::round(third);
        const double dq=std::abs(rq-q),dr=std::abs(rr-r),ds=std::abs(rs-third);
        if(dq>dr&&dq>ds) rq=-rr-rs;else if(dr>ds) rr=-rq-rs;
        q=rq;r=rr;
    } else if(map.topology!=Map2DTopology::Rectangular) return {};
    if(map.topology!=Map2DTopology::Hexagonal) {q=std::floor(q);r=std::floor(r);}
    if(!std::isfinite(q)||!std::isfinite(r)||q<0||r<0||q>=map.width||r>=map.height) return {};
    return std::array<int,2>{static_cast<int>(q),static_cast<int>(r)};
}
std::array<double,3> Map2DCellLocalPoint(const Map2DAsset& map,double x,double y,std::int32_t floor,bool footpoint) noexcept {
    auto point=Map2DProjectGrid(map,x+.5,y+.5);const auto bounds=Map2DProjectedSize(map);
    if(footpoint) point[1]+=Map2DCellHeight(map)*.5;
    const double lift=static_cast<double>(floor)*map.floorHeight;
    if(map.orientation==Map2DOrientation::Wall) return {point[0]-bounds[0]*.5,bounds[1]-point[1],-lift};
    return {point[0]-bounds[0]*.5,lift,bounds[1]*.5-point[1]};
}
std::span<const std::array<int,2>> Map2DNeighbourOffsets(Map2DTopology topology,bool diagonals) noexcept {
    if(topology==Map2DTopology::Hexagonal) return kHex;
    if(topology!=Map2DTopology::Rectangular&&topology!=Map2DTopology::Isometric) return {};
    return diagonals?std::span<const std::array<int,2>>{kSquareEight}:std::span<const std::array<int,2>>{kSquare};
}
std::span<const std::array<float,2>> Map2DCellOutline(Map2DTopology topology) noexcept {
    if(topology==Map2DTopology::Hexagonal) return kHexOutline;
    if(topology==Map2DTopology::Isometric) return kDiamondOutline;
    return topology==Map2DTopology::Rectangular?std::span<const std::array<float,2>>{kRectOutline}:std::span<const std::array<float,2>>{};
}
} // namespace kb::assets
