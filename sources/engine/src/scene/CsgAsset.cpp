#include "engine/scene/CsgAsset.hpp"
#include "engine/core/JsonValue.hpp"
#include <manifold/manifold.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
namespace kb::scene {
namespace {
using J=kb::core::JsonValue;
constexpr std::size_t MaxVertices=65536,MaxIndices=393216;
bool Fields(const J& o,std::initializer_list<std::string_view> allowed) {
    if(o.GetKind()!=J::Kind::Object) return false;
    for(std::size_t i=0;i<o.Size();++i) if(std::find(allowed.begin(),allowed.end(),o.MemberName(i))==allowed.end()) return false;
    return true;
}
bool Triple(const J* a,std::array<float,3>& out) {
    if(!a||a->GetKind()!=J::Kind::Array||a->Size()!=3) return false;
    for(int i=0;i<3;++i) {const auto* v=a->At(i);if(!v||v->GetKind()!=J::Kind::Number||!std::isfinite(v->AsNumber())||std::abs(v->AsNumber())>1000000) return false;out[i]=static_cast<float>(v->AsNumber());}return true;
}
J TripleJson(std::array<float,3> a) {auto j=J::MakeArray();for(auto n:a) j.Append(J::MakeNumber(n));return j;}
bool Valid(const CsgAsset& a,std::string& error) {
    const auto fail=[&](const char* text){error=text;return false;};
    if(a.brushes.empty()||a.brushes.size()>64||a.brushes.front().operation!=CsgOperation::Union) return fail("CSG requires 1..64 brushes and an initial union brush");
    std::size_t vertices=0,indices=0;
    for(const auto& b:a.brushes) {
        if(b.operation!=CsgOperation::Union&&b.operation!=CsgOperation::Subtract&&b.operation!=CsgOperation::Intersect) return fail("invalid CSG operation");
        for(int i=0;i<3;++i) if(!std::isfinite(b.position[i])||std::abs(b.position[i])>1000000||!std::isfinite(b.rotationDegrees[i])||std::abs(b.rotationDegrees[i])>36000||!std::isfinite(b.scale[i])||b.scale[i]<.001F||b.scale[i]>1000||!std::isfinite(b.size[i])||b.size[i]<.001F||b.size[i]>10000) return fail("CSG transforms and positive dimensions must be finite and bounded");
        if(b.useMesh) {
            vertices+=b.mesh.positions.size();indices+=b.mesh.indices.size();
            if(vertices>MaxVertices||indices>MaxIndices||!kb::assets::ValidateCollisionMesh(b.mesh,error)) return fail("CSG input mesh exceeds limits or has invalid triangles");
            for(auto p:b.mesh.positions) if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||std::abs(p.x)>10000||std::abs(p.y)>10000||std::abs(p.z)>10000) return fail("CSG mesh position exceeds limits");
        }
    }return true;
}
}
bool DecodeCsg(std::string_view text,CsgAsset& output,std::string& error) {
    error.clear();const auto fail=[&](){error="invalid CSG schema, field type or operation";return false;};
    if(text.size()>16*1024*1024) return fail();J root;if(!J::Parse(text,root,error)) return false;
    if(!Fields(root,{"schema","brushes"})) return fail();const auto* version=root.Find("schema");const auto* brushes=root.Find("brushes");
    if(!version||version->GetKind()!=J::Kind::Number||version->AsNumber()!=1||!brushes||brushes->GetKind()!=J::Kind::Array||brushes->Size()>64) return fail();
    CsgAsset a;
    for(std::size_t i=0;i<brushes->Size();++i) {
        const auto& j=*brushes->At(i);CsgBrush b;
        if(!Fields(j,{"operation","kind","size","position","rotation","scale","positions","indices"})) return fail();
        const auto* op=j.Find("operation"),*kind=j.Find("kind");if(!op||op->GetKind()!=J::Kind::String||!kind||kind->GetKind()!=J::Kind::String) return fail();
        const auto name=op->AsString();if(name=="union") b.operation=CsgOperation::Union;else if(name=="subtract") b.operation=CsgOperation::Subtract;else if(name=="intersect") b.operation=CsgOperation::Intersect;else return fail();
        if(!Triple(j.Find("position"),b.position)||!Triple(j.Find("rotation"),b.rotationDegrees)||!Triple(j.Find("scale"),b.scale)) return fail();
        if(kind->AsString()=="box") {if(j.Find("positions")||j.Find("indices")||!Triple(j.Find("size"),b.size)) return fail();}
        else if(kind->AsString()=="mesh") {
            if(j.Find("size")) return fail();b.useMesh=true;const auto* positions=j.Find("positions"),*indices=j.Find("indices");
            if(!positions||positions->GetKind()!=J::Kind::Array||positions->Size()>MaxVertices||!indices||indices->GetKind()!=J::Kind::Array||indices->Size()>MaxIndices) return fail();
            for(std::size_t v=0;v<positions->Size();++v) {std::array<float,3> p;if(!Triple(positions->At(v),p)) return fail();b.mesh.positions.push_back({p[0],p[1],p[2]});}
            for(std::size_t v=0;v<indices->Size();++v) {auto* n=indices->At(v);if(n->GetKind()!=J::Kind::Number||!std::isfinite(n->AsNumber())||n->AsNumber()<0||n->AsNumber()>=b.mesh.positions.size()||std::floor(n->AsNumber())!=n->AsNumber()) return fail();b.mesh.indices.push_back(static_cast<std::uint32_t>(n->AsNumber()));}
        } else return fail();a.brushes.push_back(std::move(b));
    }
    if(!Valid(a,error)) return false;output=std::move(a);return true;
}
std::string EncodeCsg(const CsgAsset& a) {
    std::string error;if(!Valid(a,error)) return {};auto root=J::MakeObject();root.Set("schema",J::MakeNumber(1));auto brushes=J::MakeArray();
    for(const auto& b:a.brushes) {auto j=J::MakeObject();j.Set("operation",J::MakeString(b.operation==CsgOperation::Union?"union":b.operation==CsgOperation::Subtract?"subtract":"intersect"));j.Set("kind",J::MakeString(b.useMesh?"mesh":"box"));j.Set("position",TripleJson(b.position));j.Set("rotation",TripleJson(b.rotationDegrees));j.Set("scale",TripleJson(b.scale));
        if(!b.useMesh) j.Set("size",TripleJson(b.size));else {auto p=J::MakeArray(),ix=J::MakeArray();for(auto v:b.mesh.positions) p.Append(TripleJson({v.x,v.y,v.z}));for(auto n:b.mesh.indices) ix.Append(J::MakeNumber(n));j.Set("positions",std::move(p));j.Set("indices",std::move(ix));}brushes.Append(std::move(j));
    }root.Set("brushes",std::move(brushes));return root.Dump()+"\n";
}
bool CompileCsg(const CsgAsset& source,CompiledCsg& output,std::string& error) {
    error.clear();if(!Valid(source,error)) return false;
    try {
        manifold::Manifold result;bool first=true;
        for(const auto& b:source.brushes) {
            manifold::Manifold solid;
            if(b.useMesh) {manifold::MeshGL mesh;mesh.triVerts=b.mesh.indices;for(auto p:b.mesh.positions) mesh.vertProperties.insert(mesh.vertProperties.end(),{p.x,p.y,p.z});solid=manifold::Manifold(mesh);}
            else solid=manifold::Manifold::Cube({b.size[0],b.size[1],b.size[2]},true);
            if(solid.Status()!=manifold::Manifold::Error::NoError||solid.IsEmpty()||solid.Volume()<=0) {error="CSG brush must be a closed outward-wound manifold with positive volume";return false;}
            solid=solid.Scale({b.scale[0],b.scale[1],b.scale[2]}).Rotate(b.rotationDegrees[0],b.rotationDegrees[1],b.rotationDegrees[2]).Translate({b.position[0],b.position[1],b.position[2]});
            if(first) {result=std::move(solid);first=false;}else result=result.Boolean(solid,b.operation==CsgOperation::Union?manifold::OpType::Add:b.operation==CsgOperation::Subtract?manifold::OpType::Subtract:manifold::OpType::Intersect);
            if(result.Status()!=manifold::Manifold::Error::NoError||result.NumVert()>MaxVertices||result.NumTri()>MaxIndices/3) {error="CSG boolean failed or exceeded output geometry limits";return false;}
        }
        CompiledCsg compiled;compiled.volume=result.Volume();const auto mesh=result.GetMeshGL();
        if(result.IsEmpty()||!std::isfinite(compiled.volume)||compiled.volume<=0) {error="CSG result is empty; no renderable collision mesh can be emitted";return false;}
        for(std::size_t i=0;i<mesh.vertProperties.size();i+=mesh.numProp) compiled.mesh.positions.push_back({mesh.vertProperties[i],mesh.vertProperties[i+1],mesh.vertProperties[i+2]});compiled.mesh.indices=mesh.triVerts;
        if(!kb::assets::ValidateCollisionMesh(compiled.mesh,error)) return false;output=std::move(compiled);return true;
    }catch(const std::exception& e) {error=std::string{"CSG geometry error: "}+e.what();return false;}
}
bool WriteCsgObj(const std::filesystem::path& path,const CompiledCsg& c,std::string& error) {
    if(!kb::assets::ValidateCollisionMesh(c.mesh,error)) return false;
    std::ofstream out(path,std::ios::binary|std::ios::trunc);out.imbue(std::locale::classic());out<<std::setprecision(9);
    // Mirror X and reverse winding: exact inverse of the standard OBJ importer.
    // Separate face vertices preserve hard blockout normals and planar metre UVs.
    for(std::size_t i=0;i<c.mesh.indices.size();i+=3) {
        const auto p=c.mesh.positions[c.mesh.indices[i]],q=c.mesh.positions[c.mesh.indices[i+1]],r=c.mesh.positions[c.mesh.indices[i+2]];
        const float ux=q.x-p.x,uy=q.y-p.y,uz=q.z-p.z,vx=r.x-p.x,vy=r.y-p.y,vz=r.z-p.z;
        float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;const float len=std::sqrt(nx*nx+ny*ny+nz*nz);nx/=len;ny/=len;nz/=len;
        for(auto v:{p,r,q}) {out<<"v "<<-v.x<<' '<<v.y<<' '<<v.z<<'\n';out<<"vn "<<-nx<<' '<<ny<<' '<<nz<<'\n';out<<"vt "<<(std::abs(nx)>.707F?v.z:v.x)<<' '<<(std::abs(ny)>.707F?v.z:v.y)<<'\n';}
    }
    for(std::size_t i=0;i<c.mesh.indices.size();i+=3) {out<<"f";for(std::size_t j=1;j<=3;++j) out<<' '<<i+j<<'/'<<i+j<<'/'<<i+j;out<<'\n';}out.close();if(!out) {error="could not write CSG OBJ";return false;}error.clear();return true;
}
}
