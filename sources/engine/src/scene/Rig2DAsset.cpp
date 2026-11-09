#include "engine/scene/Rig2DAsset.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/core/JsonValue.hpp"
#include "scene/asset/io/SceneAssetBinaryIO.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace kb::scene {
namespace {
using J=kb::core::JsonValue;
bool Number(float n,float limit=1000000) {return std::isfinite(n)&&std::abs(n)<=limit;}
bool Pose(const Rig2DPose& p) {
    return Number(p.position[0])&&Number(p.position[1])&&Number(p.rotationDegrees,36000)&&
        Number(p.scale[0],100)&&Number(p.scale[1],100)&&p.scale[0]>=.001F&&p.scale[1]>=.001F;
}
LocalTransform Transform(const Rig2DPose& p) {return {.position={p.position[0],p.position[1],0},.rotation=kb::math::FromEulerDegrees({0,0,p.rotationDegrees}),.scale={p.scale[0],p.scale[1],1}};}
J Pair(std::array<float,2> value) {auto a=J::MakeArray();for(auto n:value) a.Append(J::MakeNumber(n));return a;}
J PoseJson(const Rig2DPose& p) {auto a=J::MakeObject();a.Set("position",Pair(p.position));a.Set("rotation",J::MakeNumber(p.rotationDegrees));a.Set("scale",Pair(p.scale));return a;}
bool Fields(const J& o,std::initializer_list<std::string_view> allowed) {
    if(o.GetKind()!=J::Kind::Object) return false;
    for(std::size_t i=0;i<o.Size();++i) if(std::find(allowed.begin(),allowed.end(),o.MemberName(i))==allowed.end()) return false;
    return true;
}
bool Float(const J* v,float& n) {
    if(!v||v->GetKind()!=J::Kind::Number) return false;
    const auto d=v->AsNumber();if(!std::isfinite(d)||std::abs(d)>1000000) return false;n=static_cast<float>(d);return true;
}
bool Integer(const J* v,int low,int high,int& n) {if(!v||v->GetKind()!=J::Kind::Number) return false;const auto f=v->AsNumber();if(!std::isfinite(f)||std::floor(f)!=f||f<low||f>high) return false;n=static_cast<int>(f);return true;}
bool ReadPair(const J* v,std::array<float,2>& p) {return v&&v->GetKind()==J::Kind::Array&&v->Size()==2&&Float(v->At(0),p[0])&&Float(v->At(1),p[1]);}
bool ReadPose(const J* v,Rig2DPose& p) {return v&&Fields(*v,{"position","rotation","scale"})&&ReadPair(v->Find("position"),p.position)&&Float(v->Find("rotation"),p.rotationDegrees)&&ReadPair(v->Find("scale"),p.scale);}
}
std::string ValidateRig2D(const Rig2DAsset& a) {
    if(a.bones.empty()||a.bones.size()>128||a.vertices.size()<3||a.vertices.size()>32768||a.indices.empty()||a.indices.size()%3||a.indices.size()>196608||a.tracks.size()>128) return "rig2d exceeds bone, mesh or track limits";
    std::set<std::string> names;
    for(std::size_t i=0;i<a.bones.size();++i) {
        const auto& b=a.bones[i];if(b.name.empty()||b.name.size()>128||b.name.find_first_of("\r\n")!=std::string::npos||!names.insert(b.name).second||b.parent< -1||b.parent>=static_cast<std::int32_t>(i)||!Pose(b.bind)) return "rig2d requires unique named bones, earlier parents and finite planar bind poses";
    }
    for(const auto& v:a.vertices) {
        for(float n:{v.position[0],v.position[1],v.uv[0],v.uv[1]}) if(!Number(n)) return "rig2d vertex is not finite or bounded";
        float sum=0;for(std::size_t j=0;j<4;++j) {if(v.bones[j]>=a.bones.size()||!Number(v.weights[j],1)||v.weights[j]<0) return "rig2d has an invalid bone influence";sum+=v.weights[j];}
        if(std::abs(sum-1)>0.0001F) return "rig2d vertex weights must sum to one";
    }
    for(std::size_t i=0;i<a.indices.size();i+=3) {
        for(std::size_t j=0;j<3;++j) if(a.indices[i+j]>=a.vertices.size()) return "rig2d triangle index is out of range";
        const auto p=a.vertices[a.indices[i]].position,q=a.vertices[a.indices[i+1]].position,r=a.vertices[a.indices[i+2]].position;
        const double cross=(static_cast<double>(q[0])-p[0])*(static_cast<double>(r[1])-p[1])-(static_cast<double>(q[1])-p[1])*(static_cast<double>(r[0])-p[0]);
        if(std::abs(cross)<1.0e-10) return "rig2d has a degenerate triangle";
    }
    if(!Number(a.duration,3600)||a.duration<=0) return "rig2d clip duration must be positive and at most 3600 seconds";
    std::set<unsigned> tracks;std::size_t keys=0;
    for(const auto& t:a.tracks) {
        if(t.bone>=a.bones.size()||!tracks.insert(t.bone).second||t.keys.empty()||t.keys.size()>4096) return "rig2d requires unique valid bone tracks with keys";
        keys+=t.keys.size();if(keys>16384) return "rig2d clip exceeds key budget";
        float previous=-1;for(const auto& k:t.keys) {if(!Number(k.time)||k.time<0||k.time>a.duration||k.time<=previous||!Pose(k.pose)) return "rig2d requires increasing key times and valid planar poses";previous=k.time;}
    }
    return {};
}
bool DecodeRig2D(std::string_view source,Rig2DAsset& output,std::string& error) {
    error.clear();const auto fail=[&](){error="invalid rig2d schema or field types";return false;};
    if(source.size()>16U*1024U*1024U) {error="rig2d source exceeds 16 MiB";return false;}
    J root;if(!J::Parse(source,root,error)) return false;
    if(!Fields(root,{"schema","bones","vertices","indices","duration","looping","tracks"})) return fail();
    const auto* schema=root.Find("schema"),*bones=root.Find("bones"),*vertices=root.Find("vertices"),*indices=root.Find("indices"),*tracks=root.Find("tracks"),*loop=root.Find("looping");
    if(!schema||schema->AsString()!="21kb.rig2d/v1"||!bones||bones->GetKind()!=J::Kind::Array||bones->Size()>128||!vertices||vertices->GetKind()!=J::Kind::Array||vertices->Size()>32768||!indices||indices->GetKind()!=J::Kind::Array||indices->Size()>196608||!tracks||tracks->GetKind()!=J::Kind::Array||tracks->Size()>128||!loop||loop->GetKind()!=J::Kind::Bool) return fail();
    Rig2DAsset a;if(!Float(root.Find("duration"),a.duration)) return fail();a.looping=loop->AsBool();
    for(std::size_t i=0;i<bones->Size();++i) {
        const auto& v=*bones->At(i);Rig2DBone b;int parent=0;const auto* name=v.Find("name");
        if(!Fields(v,{"name","parent","bind"})||!name||name->GetKind()!=J::Kind::String||!Integer(v.Find("parent"),-1,127,parent)||!ReadPose(v.Find("bind"),b.bind)) return fail();
        b.name=name->AsString();b.parent=parent;a.bones.push_back(std::move(b));
    }
    for(std::size_t i=0;i<vertices->Size();++i) {
        const auto& v=*vertices->At(i);Rig2DVertex p;const auto* weights=v.Find("weights"),*joints=v.Find("bones");
        if(!Fields(v,{"position","uv","bones","weights"})||!ReadPair(v.Find("position"),p.position)||!ReadPair(v.Find("uv"),p.uv)||!weights||weights->GetKind()!=J::Kind::Array||weights->Size()!=4||!joints||joints->GetKind()!=J::Kind::Array||joints->Size()!=4) return fail();
        for(std::size_t j=0;j<4;++j) {int n=0;if(!Integer(joints->At(j),0,127,n)||!Float(weights->At(j),p.weights[j])) return fail();p.bones[j]=static_cast<std::uint16_t>(n);}a.vertices.push_back(p);
    }
    for(std::size_t i=0;i<indices->Size();++i) {int n=0;if(!Integer(indices->At(i),0,32767,n)) return fail();a.indices.push_back(static_cast<std::uint32_t>(n));}
    std::size_t count=0;
    for(std::size_t i=0;i<tracks->Size();++i) {
        const auto& v=*tracks->At(i);Rig2DTrack t;int bone=0;const auto* keys=v.Find("keys");
        if(!Fields(v,{"bone","keys"})||!Integer(v.Find("bone"),0,127,bone)||!keys||keys->GetKind()!=J::Kind::Array||keys->Size()>4096) return fail();t.bone=static_cast<std::uint16_t>(bone);count+=keys->Size();if(count>16384) return fail();
        for(std::size_t j=0;j<keys->Size();++j) {const auto& k=*keys->At(j);Rig2DKey key;if(!Fields(k,{"time","pose"})||!Float(k.Find("time"),key.time)||!ReadPose(k.Find("pose"),key.pose)) return fail();t.keys.push_back(key);}a.tracks.push_back(std::move(t));
    }
    error=ValidateRig2D(a);if(!error.empty()) return false;output=std::move(a);return true;
}
std::string EncodeRig2D(const Rig2DAsset& a) {
    if(!ValidateRig2D(a).empty()) return {};
    auto root=J::MakeObject();root.Set("schema",J::MakeString("21kb.rig2d/v1"));root.Set("duration",J::MakeNumber(a.duration));root.Set("looping",J::MakeBool(a.looping));
    auto bones=J::MakeArray();for(const auto& b:a.bones) {auto v=J::MakeObject();v.Set("name",J::MakeString(b.name));v.Set("parent",J::MakeNumber(b.parent));v.Set("bind",PoseJson(b.bind));bones.Append(std::move(v));}root.Set("bones",std::move(bones));
    auto vertices=J::MakeArray();for(const auto& p:a.vertices) {auto v=J::MakeObject();v.Set("position",Pair(p.position));v.Set("uv",Pair(p.uv));auto joints=J::MakeArray(),weights=J::MakeArray();for(std::size_t j=0;j<4;++j){joints.Append(J::MakeNumber(p.bones[j]));weights.Append(J::MakeNumber(p.weights[j]));}v.Set("bones",std::move(joints));v.Set("weights",std::move(weights));vertices.Append(std::move(v));}root.Set("vertices",std::move(vertices));
    auto indices=J::MakeArray();for(auto i:a.indices) indices.Append(J::MakeNumber(i));root.Set("indices",std::move(indices));
    auto tracks=J::MakeArray();for(const auto& t:a.tracks) {auto v=J::MakeObject();v.Set("bone",J::MakeNumber(t.bone));auto keys=J::MakeArray();for(const auto& k:t.keys){auto key=J::MakeObject();key.Set("time",J::MakeNumber(k.time));key.Set("pose",PoseJson(k.pose));keys.Append(std::move(key));}v.Set("keys",std::move(keys));tracks.Append(std::move(v));}root.Set("tracks",std::move(tracks));return root.Dump()+"\n";
}
bool SaveRig2D(const std::filesystem::path& path,const Rig2DAsset& rig,std::string& error) {
    error=ValidateRig2D(rig);if(!error.empty()) return false;const auto text=EncodeRig2D(rig);
    if(!SceneAssetBinaryIO::WriteBytesAtomically(path,{reinterpret_cast<const std::uint8_t*>(text.data()),text.size()})) {error="could not save rig2d source";return false;}return true;
}
bool CompileRig2D(const Rig2DAsset& a,std::uint64_t skeletonId,std::uint64_t material,std::string_view clipPath,CompiledRig2D& output,std::string& error) {
    error=ValidateRig2D(a);if(!error.empty()) return false;
    if(!skeletonId||clipPath.empty()||clipPath.front()!='/') {error="rig2d compilation requires skeleton identity and a virtual clip reference";return false;}
    CompiledRig2D compiled;std::vector<kb::math::Mat4> bind;
    for(std::size_t i=0;i<a.bones.size();++i) {
        const auto& bone=a.bones[i];const auto p=Transform(bone.bind);auto m=kb::math::FromTRS(p.position,p.rotation,p.scale);if(bone.parent>=0) m=bind[static_cast<std::size_t>(bone.parent)]*m;
        bind.push_back(m);const auto& x=m.columns[0];const auto& y=m.columns[1];const auto& t=m.columns[3];const double determinant=static_cast<double>(x.x)*y.y-static_cast<double>(y.x)*x.y;
        if(!std::isfinite(determinant)||std::abs(determinant)<1.0e-12) {error="rig2d bind hierarchy is singular";return false;}
        const auto f=[&](double n){return static_cast<float>(n/determinant);};
        const kb::math::Mat4 inverse{{{f(y.y),f(-x.y),0,0},{f(-y.x),f(x.x),0,0},{0,0,1,0},{f(static_cast<double>(y.x)*t.y-static_cast<double>(y.y)*t.x),f(static_cast<double>(x.y)*t.x-static_cast<double>(x.x)*t.y),0,1}}};
        compiled.skeleton.bones.push_back({i+1,bone.parent,bone.name,p,inverse});
    }
    const auto signature=SkeletonCompatibilitySignature(compiled.skeleton);if(!signature) {error="compiled rig2d skeleton is invalid";return false;}
    auto& mesh=compiled.mesh;mesh.skeletonAssetId=skeletonId;mesh.skeletonCompatibilitySignature=signature;SkeletalMeshLod lod;
    std::array<float,2> low=a.vertices.front().position,high=low;
    for(const auto& v:a.vertices) {for(std::size_t i=0;i<2;++i){low[i]=std::min(low[i],v.position[i]);high[i]=std::max(high[i],v.position[i]);}lod.vertices.push_back({.position={v.position[0],v.position[1],0},.normal={0,0,-1},.uv=v.uv,.jointIndices=v.bones,.jointWeights=v.weights});}
    lod.indices=a.indices;
    for(std::size_t i=0;i<lod.indices.size();i+=3){const auto p=lod.vertices[lod.indices[i]].position,q=lod.vertices[lod.indices[i+1]].position,r=lod.vertices[lod.indices[i+2]].position;if((q.x-p.x)*(r.y-p.y)-(q.y-p.y)*(r.x-p.x)>0) std::swap(lod.indices[i+1],lod.indices[i+2]);}
    SkeletalMeshSection section{.firstIndex=0,.indexCount=static_cast<std::uint32_t>(lod.indices.size()),.materialAssetId=material};
    for(std::size_t i=0;i<a.bones.size();++i) {section.boneMap.push_back(i+1);lod.requiredBones.push_back(i+1);}lod.sections.push_back(std::move(section));BuildSkeletalMeshLodBoneBounds(lod);mesh.lods.push_back(std::move(lod));
    mesh.conservativeBounds={.center={(low[0]+high[0])*.5F,(low[1]+high[1])*.5F,0},.extents={(high[0]-low[0])*.5F,(high[1]-low[1])*.5F,.01F}};mesh.fixedBounds=mesh.conservativeBounds;
    const auto valid=ValidateSkeletalMeshAsset(mesh);if(!valid.valid) {error=valid.error;return false;}
    auto& clip=compiled.clip;clip.durationSeconds=a.duration;clip.looping=a.looping;clip.targetSkeletonAssetId=skeletonId;clip.targetSkeletonCompatibilitySignature=signature;
    for(const auto& t:a.tracks) {AnimationBoneTrack track;track.boneId=static_cast<SkeletonBoneId>(t.bone)+1;for(const auto& k:t.keys) track.keyframes.push_back({k.time,Transform(k.pose)});clip.skeletalTracks.push_back(std::move(track));}
    AnimatorControllerLayer layer;layer.name="Base";layer.defaultState="Play";layer.states.push_back({.id=1,.name="Play",.clipReference=std::string{clipPath}});compiled.controller.layers.push_back(std::move(layer));if(!AnimationAssetIO::ValidateController(compiled.controller,&error)) return false;output=std::move(compiled);error.clear();return true;
}
}
