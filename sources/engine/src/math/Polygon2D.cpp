#include "engine/math/Polygon2D.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace kb::math {
namespace {
using Point=std::array<float,2>;
double Cross(Point a,Point b,Point c) {
    return (static_cast<double>(b[1])-a[1])*(static_cast<double>(c[0])-a[0])-
        (static_cast<double>(b[0])-a[0])*(static_cast<double>(c[1])-a[1]);
}
bool Touches(Point a,Point b,Point p) {
    return std::abs(Cross(a,b,p))<1.0e-9 && p[0]>=std::min(a[0],b[0])&&p[0]<=std::max(a[0],b[0])&&p[1]>=std::min(a[1],b[1])&&p[1]<=std::max(a[1],b[1]);
}
bool Intersects(Point a,Point b,Point c,Point d) {
    const double ab=Cross(a,b,c),ac=Cross(a,b,d),ca=Cross(c,d,a),cb=Cross(c,d,b);
    return (((ab>0&&ac<0)||(ab<0&&ac>0))&&((ca>0&&cb<0)||(ca<0&&cb>0))) || Touches(a,b,c)||Touches(a,b,d)||Touches(c,d,a)||Touches(c,d,b);
}
}
bool TriangulatePolygon2D(std::span<const Point> points,Polygon2DTriangulation& output,std::string& error) {
    error.clear();
    const auto fail=[&](const char* message){error=message;return false;};
    if(points.size()<3||points.size()>256) return fail("polygon needs 3..256 vertices without repeating the closing point");
    for(std::size_t i=0;i<points.size();++i) {
        const auto a=points[i],b=points[(i+1)%points.size()];
        for(float n:a) if(!std::isfinite(n)||std::abs(n)>1000000) return fail("polygon coordinates must be finite and bounded");
        if(a==b) return fail("polygon has a zero-length edge");
        for(std::size_t j=i+1;j<points.size();++j) {
            if(j==i+1||(i==0&&j+1==points.size())) continue;
            if(Intersects(a,b,points[j],points[(j+1)%points.size()])) return fail("polygon outline intersects itself");
        }
    }
    double area=0;for(std::size_t i=0;i<points.size();++i) {const auto a=points[i],b=points[(i+1)%points.size()];area+=static_cast<double>(b[0])*a[1]-static_cast<double>(a[0])*b[1];}
    if(std::abs(area)<1.0e-9) return fail("polygon has zero area");
    Polygon2DTriangulation result;result.points.assign(points.begin(),points.end());
    if(area<0) std::ranges::reverse(result.points);
    std::vector<std::uint32_t> remaining(points.size());std::iota(remaining.begin(),remaining.end(),0U);
    while(remaining.size()>2) {
        bool clipped=false;
        for(std::size_t i=0;i<remaining.size();++i) {
            const auto a=remaining[(i+remaining.size()-1)%remaining.size()],b=remaining[i],c=remaining[(i+1)%remaining.size()];
            const double turn=Cross(result.points[a],result.points[b],result.points[c]);
            if(std::abs(turn)<1.0e-9) {remaining.erase(remaining.begin()+static_cast<std::ptrdiff_t>(i));clipped=true;break;}
            if(turn<0) continue;
            bool contains=false;
            for(const auto p:remaining) if(p!=a&&p!=b&&p!=c&&Cross(result.points[a],result.points[b],result.points[p])>=-1.0e-9&&
                Cross(result.points[b],result.points[c],result.points[p])>=-1.0e-9&&Cross(result.points[c],result.points[a],result.points[p])>=-1.0e-9) {contains=true;break;}
            if(contains) continue;
            result.indices.insert(result.indices.end(),{a,b,c});remaining.erase(remaining.begin()+static_cast<std::ptrdiff_t>(i));clipped=true;break;
        }
        if(!clipped) return fail("polygon cannot be triangulated");
    }
    if(result.indices.empty()) return fail("polygon has no triangles");
    output=std::move(result);return true;
}
}
