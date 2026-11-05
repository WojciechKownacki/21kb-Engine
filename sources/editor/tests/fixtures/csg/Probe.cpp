#include "engine/script/NativeScriptPlugin.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/CsgAsset.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneMeshRendererComponents.hpp"
#include <array>
#include <map>
#include <cmath>
#include <limits>
#include <stdexcept>
extern "C" void ecs_os_set_api_defaults(void);
static std::map<std::pair<std::uint64_t,std::uint64_t>,int> frames;
static void Require(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
static void Ready(kb::script::ScriptExecutionContext* c){frames[{c->GetScene().Id(),c->Self().Id()}]=0;}
static void Tick(kb::script::ScriptExecutionContext* c){
 using namespace kb::scene;auto& s=c->GetScene();auto& frame=frames[{s.Id(),c->Self().Id()}];++frame;std::string error;
 const bool intersect=s.Entities().Name(c->Self())=="NativeIntersect";
 if(frame==5){
  CsgAsset a;a.brushes={{.size={2,2,1}},{.size={1,1,2},.position={0,0,.25F}}};CompiledCsg built;
  for(auto op:{CsgOperation::Union,CsgOperation::Subtract,CsgOperation::Intersect}) {a.brushes[1].operation=op;Require(CompileCsg(a,built,error),error);const double expected=op==CsgOperation::Union?5:op==CsgOperation::Subtract?3:1;Require(std::abs(built.volume-expected)<.00001,"CSG analytic volume is wrong");}
  a.brushes[1].operation=CsgOperation::Subtract;a.brushes[1].rotationDegrees={0,0,30};Require(CompileCsg(a,built,error)&&std::abs(built.volume-3)<.00001,"rotated boolean did not cut expected volume");
  CsgAsset meshSource;meshSource.brushes={{.size={1,1,2}}};CompiledCsg meshBrush;Require(CompileCsg(meshSource,meshBrush,error),error);a.brushes[1].useMesh=true;a.brushes[1].mesh=meshBrush.mesh;Require(CompileCsg(a,built,error)&&std::abs(built.volume-3)<.00001,"closed triangle mesh boolean failed");
  CsgAsset reloaded;Require(DecodeCsg(EncodeCsg(a),reloaded,error)&&CompileCsg(reloaded,built,error)&&std::abs(built.volume-3)<.00001,"CSG source roundtrip lost operation/mesh/transform");const auto before=built.mesh.indices;
  a.brushes[1].mesh.indices.resize(3);Require(!CompileCsg(a,built,error)&&built.mesh.indices==before,"open mesh changed compiled result");a.brushes[0].size[0]=std::numeric_limits<float>::quiet_NaN();Require(!CompileCsg(a,built,error)&&built.mesh.indices==before,"nonfinite CSG changed compiled result");
  const std::string base=intersect?"/Game/Compiled/Intersect/":"/Game/Compiled/Subtract/";
  Require(AttachCompiledCsg(s,c->Self(),base+"Geometry.21kb",base+"Geometry.kbcollision",error),error);
  const auto old=*s.Components().MeshRenderers().TryGet(c->Self());Require(!AttachCompiledCsg(s,c->Self(),base+"Missing.21kb",base+"Geometry.kbcollision",error)&&s.Components().MeshRenderers().TryGet(c->Self())->meshAssetId==old.meshAssetId,"invalid CSG attachment replaced existing mesh");
  auto temporary=old;temporary.transient=true;s.Components().MeshRenderers().Set(c->Self(),temporary);Require(!AttachCompiledCsg(s,c->Self(),base+"Geometry.21kb",base+"Geometry.kbcollision",error)&&s.Components().MeshRenderers().TryGet(c->Self())->transient,"CSG replaced a system-owned renderer");s.Components().MeshRenderers().Set(c->Self(),old);
 }
 if(frame==50){
  Require(PhysicsBackend::HasBackend(s),"Jolt backend missing");std::array<PhysicsCastResult,16> storage;kb::library::ArrayNonAlloc<PhysicsCastResult> hits{storage};
  const float center=intersect?2.7F:0;PhysicsBackend::RaycastAll(s,{center,0,-4},{0,0,1},8,0x7fffffff,hits);Require(intersect?!hits.Empty():hits.Empty(),"CSG center collision did not match boolean");
  if(intersect) Require(std::abs(hits.GetAt(0)->point.z+.5F)<.001F,"intersection front surface is wrong");
  PhysicsBackend::RaycastAll(s,{center+.8F,0,-4},{0,0,1},8,0x7fffffff,hits);Require(intersect?hits.Empty():!hits.Empty(),"CSG outer collision did not match boolean");
  if(!intersect) Require(std::abs(hits.GetAt(0)->point.z+.5F)<.001F,"subtraction outer surface is wrong");
  Require(c->CallFunction("Log",std::array{kb::script::ScriptFunctionArgument{"message",kb::script::ScriptValue{std::string{"native-csg PASS "}+(intersect?"intersect":"subtract")}}}).Succeeded(),"CSG log failed");
 }
}
KB_NATIVE_SCRIPT_PLUGIN_EXPORT bool kb_register_native_scripts(kb::script::NativeScriptPluginApi* api){ecs_os_set_api_defaults();return api&&api->version==kb::script::kNativeScriptPluginApiVersion&&api->registerLifecycle(api->user,"project.NativeCsgProbe",kb::script::ScriptLifecycleEvent::Ready,&Ready)&&api->registerLifecycle(api->user,"project.NativeCsgProbe",kb::script::ScriptLifecycleEvent::Tick,&Tick);}
