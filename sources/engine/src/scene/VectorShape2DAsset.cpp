#include "engine/scene/VectorShape2DAssetLoader.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/math/Polygon2D.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace kb::scene {
namespace {
using J=kb::core::JsonValue;
bool CanonicalPath(std::string_view path) {
    if(path.empty()) return true;
    if(!path.starts_with('/')||path.starts_with("//")||path.find_first_of(":\\")!=std::string_view::npos) return false;
    const auto file=kb::assets::Utf8Path(path);
    return std::ranges::none_of(file,[](const auto& part){return part=="."||part=="..";});
}
bool Read(const kb::assets::AssetLoadRequest& request,VectorShape2DAsset& shape,std::string& error) {
    std::vector<std::uint8_t> bytes;
    return request.ReadSourceBytes(bytes,error)&&DecodeVectorShape2D(std::string_view{reinterpret_cast<const char*>(bytes.data()),bytes.size()},shape,error);
}
const kb::assets::AssetMetadata* Material(const VectorShape2DAsset& shape,const kb::assets::AssetRegistry& registry) {
    return shape.material.empty()?nullptr:registry.FindByPath(kb::assets::Utf8Path(shape.material));
}
}

std::string ValidateVectorShape2D(const VectorShape2DAsset& shape) {
    if(shape.kind!=VectorShape2DKind::Polygon&&shape.kind!=VectorShape2DKind::Line) return "invalid vector shape kind";
    if(shape.material.size()>2048||!CanonicalPath(shape.material)) return "material requires a bounded canonical virtual asset path";
    const std::size_t minimum=shape.kind==VectorShape2DKind::Polygon||shape.closed?3U:2U;
    const std::size_t maximum=shape.kind==VectorShape2DKind::Polygon?256U:2048U;
    if(shape.points.size()<minimum||shape.points.size()>maximum) return "invalid vector point count (polygon 3..256, line 2..2048)";
    for(const auto point:shape.points) for(float value:point) if(!std::isfinite(value)||std::abs(value)>1000000) return "vector points must be finite and bounded";
    if(shape.kind==VectorShape2DKind::Polygon) {
        kb::math::Polygon2DTriangulation mesh;std::string error;
        return kb::math::TriangulatePolygon2D(shape.points,mesh,error)?std::string{}:error;
    }
    if(!std::isfinite(shape.width)||shape.width<0.0001F||shape.width>100) return "line width must be 0.0001..100 local units";
    const std::size_t segments=shape.closed?shape.points.size():shape.points.size()-1;
    for(std::size_t i=0;i<segments;++i) {
        const auto a=shape.points[i],b=shape.points[(i+1)%shape.points.size()];
        const double dx=static_cast<double>(b[0])-a[0],dy=static_cast<double>(b[1])-a[1];
        if(dx*dx+dy*dy<1.0e-12) return "line has a zero-length edge";
    }
    return {};
}
bool DecodeVectorShape2D(std::string_view source,VectorShape2DAsset& output,std::string& error) {
    error.clear();if(source.size()>512U*1024U) {error="vector shape exceeds the 512 KiB source limit";return false;}
    J root;if(!J::Parse(source,root,error)) return false;
    const auto* schema=root.Find("schema"),*kind=root.Find("kind"),*points=root.Find("points");
    if(root.GetKind()!=J::Kind::Object||!schema||schema->AsString()!="21kb.vector-shape2d/v1"||!kind||kind->GetKind()!=J::Kind::String||
        (kind->AsString()!="Polygon"&&kind->AsString()!="Line")||!points||points->GetKind()!=J::Kind::Array||points->Size()>2048) {error="invalid vector shape schema, kind or points";return false;}
    VectorShape2DAsset shape;shape.kind=kind->AsString()=="Line"?VectorShape2DKind::Line:VectorShape2DKind::Polygon;shape.points.clear();
    for(std::size_t i=0;i<root.Size();++i) {
        const auto key=root.MemberName(i);
        if(key!="schema"&&key!="kind"&&key!="points"&&key!="material"&&!(shape.kind==VectorShape2DKind::Line&&(key=="width"||key=="closed"))) {error="unknown or inapplicable vector shape field: "+key;return false;}
    }
    for(std::size_t i=0;i<points->Size();++i) {
        const auto* p=points->At(i);if(p->GetKind()!=J::Kind::Array||p->Size()!=2) {error="point needs [x,y]";return false;}
        std::array<float,2> point{};
        for(std::size_t axis=0;axis<2;++axis) {
            const auto* n=p->At(axis);const double value=n->AsNumber(std::numeric_limits<double>::quiet_NaN());
            if(n->GetKind()!=J::Kind::Number||!std::isfinite(value)||std::abs(value)>1000000) {error="invalid vector point";return false;}
            point[axis]=static_cast<float>(value);
        }
        shape.points.push_back(point);
    }
    if(const auto* material=root.Find("material")) {if(material->GetKind()!=J::Kind::String) {error="material needs a string";return false;}shape.material=material->AsString();}
    if(const auto* width=root.Find("width")) {
        const double value=width->AsNumber(std::numeric_limits<double>::quiet_NaN());
        if(width->GetKind()!=J::Kind::Number||!std::isfinite(value)||value<0.0001||value>100) {error="invalid line width";return false;}
        shape.width=static_cast<float>(value);
    }
    if(const auto* closed=root.Find("closed")) {if(closed->GetKind()!=J::Kind::Bool) {error="closed needs a boolean";return false;}shape.closed=closed->AsBool();}
    error=ValidateVectorShape2D(shape);if(!error.empty()) return false;output=std::move(shape);return true;
}
std::string EncodeVectorShape2D(const VectorShape2DAsset& shape) {
    if(!ValidateVectorShape2D(shape).empty()) return {};
    auto root=J::MakeObject();root.Set("schema",J::MakeString("21kb.vector-shape2d/v1"));root.Set("kind",J::MakeString(shape.kind==VectorShape2DKind::Line?"Line":"Polygon"));
    auto points=J::MakeArray();for(const auto point:shape.points) {auto p=J::MakeArray();p.Append(J::MakeNumber(point[0]));p.Append(J::MakeNumber(point[1]));points.Append(std::move(p));}
    root.Set("points",std::move(points));root.Set("material",J::MakeString(shape.material));
    if(shape.kind==VectorShape2DKind::Line) {root.Set("width",J::MakeNumber(shape.width));root.Set("closed",J::MakeBool(shape.closed));}
    return root.Dump()+"\n";
}
bool SaveVectorShape2D(const std::filesystem::path& path,const VectorShape2DAsset& shape,std::string& error) {
    error=ValidateVectorShape2D(shape);if(!error.empty()) return false;
    const auto text=EncodeVectorShape2D(shape);
    if(!SceneAssetBinaryIO::WriteBytesAtomically(path,{reinterpret_cast<const std::uint8_t*>(text.data()),text.size()})) {error="could not save vector shape";return false;}return true;
}
kb::assets::AssetLoadResult VectorShape2DAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    VectorShape2DAsset shape;std::string error;if(!Read(request,shape,error)) return {.asset={},.error=std::move(error)};
    return {.asset=std::make_shared<VectorShape2DAsset>(std::move(shape)),.error={}};
}
std::vector<kb::assets::AssetId> VectorShape2DAssetLoader::DiscoverDependencies(const kb::assets::AssetMetadata& metadata,const kb::assets::AssetRegistry& registry) const {
    const kb::assets::AssetLoadRequest request{.metadata=metadata,.resolvedPath=metadata.physicalPath};VectorShape2DAsset shape;std::string error;
    if(!Read(request,shape,error)) return {};const auto* material=Material(shape,registry);return material?std::vector{material->id}:std::vector<kb::assets::AssetId>{};
}
std::optional<std::string> VectorShape2DAssetLoader::ValidateDependencies(const kb::assets::AssetMetadata& metadata,const kb::assets::AssetRegistry& registry) const {
    return ValidateRuntimeDependencies({.metadata=metadata,.resolvedPath=metadata.physicalPath},registry);
}
std::optional<std::string> VectorShape2DAssetLoader::ValidateRuntimeDependencies(const kb::assets::AssetLoadRequest& request,const kb::assets::AssetRegistry& registry) const {
    VectorShape2DAsset shape;std::string error;if(!Read(request,shape,error)) return error;
    if(!shape.material.empty()) {const auto* material=Material(shape,registry);if(!material||material->type!="RenderMaterial"||!material->runtimeLoadable) return "vector shape material was not found or is not a runtime material";}
    return std::nullopt;
}
}
