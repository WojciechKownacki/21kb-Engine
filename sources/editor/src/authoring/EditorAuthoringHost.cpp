#include "authoring/EditorAuthoringHost.hpp"
#include "scene/EditorSceneContext.hpp"
#include "console/EditorConsoleState.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneCreateMenu.hpp"
#include "engine/scene/SceneComponentAuthoring.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
namespace kb::editor {
namespace {
using J=kb::core::JsonValue;using V=kb::script::ScriptValue;using T=kb::script::ScriptValueType;using Entity=kb::scene::SceneEntity;
bool Fields(const J& j,std::initializer_list<std::string_view> fields) {if(j.GetKind()!=J::Kind::Object) return false;for(std::size_t i=0;i<j.Size();++i) if(std::find(fields.begin(),fields.end(),j.MemberName(i))==fields.end()) return false;return true;}
bool Text(const J& j,std::string_view key,std::string& value) {auto* n=j.Find(key);if(!n||n->GetKind()!=J::Kind::String||n->AsString().find('\0')!=std::string::npos) return false;value=n->AsString();return true;}
bool Id(std::string_view text,std::uint64_t& n) {auto [end,ec]=std::from_chars(text.data(),text.data()+text.size(),n);return !text.empty()&&ec==std::errc{}&&end==text.data()+text.size();}
std::vector<Entity> All(const kb::scene::Scene& scene) {auto list=scene.Hierarchy().RootEntities();for(std::size_t i=0;i<list.size();++i) {auto children=scene.Hierarchy().ChildEntities(list[i]);list.insert(list.end(),children.begin(),children.end());}return list;}
bool Input(const J& j,T type,V& value) {
    if(type==T::Bool&&j.GetKind()==J::Kind::Bool) {value=V{j.AsBool()};return true;}
    if((type==T::String||type==T::Name||type==T::Guid)&&j.GetKind()==J::Kind::String) {value=V{j.AsString(),type};return j.AsString().size()<=65536;}
    if(type==T::Entity||type==T::Component||type==T::Hash) {std::uint64_t id;if(j.GetKind()!=J::Kind::String||!Id(j.AsString(),id)) return false;value=V{id,type};return true;}
    if(type==T::Int64) {std::int64_t n;if(j.GetKind()!=J::Kind::String) return false;auto& s=j.AsString();auto [end,ec]=std::from_chars(s.data(),s.data()+s.size(),n);if(ec!=std::errc{}||end!=s.data()+s.size()) return false;value=V{n};return true;}
    if(j.GetKind()!=J::Kind::Number||!std::isfinite(j.AsNumber())) return false;const double n=j.AsNumber();
    if(type==T::Float&&std::abs(n)<=std::numeric_limits<float>::max()) {value=V{static_cast<float>(n)};return true;}
    if(type==T::Double) {value=V{n};return true;}
    if(std::floor(n)!=n) return false;
    if(type==T::Int&&n>=std::numeric_limits<int>::min()&&n<=std::numeric_limits<int>::max()) {value=V{static_cast<int>(n)};return true;}
    if(type==T::UInt32&&n>=0&&n<=std::numeric_limits<std::uint32_t>::max()) {value=V{static_cast<std::uint32_t>(n)};return true;}return false;
}
J Output(const V& v) {switch(v.Type()) {
    case T::Bool:return J::MakeBool(v.AsBool());case T::Int:return J::MakeNumber(v.AsInt());case T::Float:return J::MakeNumber(v.AsFloat());case T::Double:return J::MakeNumber(v.AsDouble());case T::UInt32:return J::MakeNumber(v.AsUInt32());
    case T::Int64:return J::MakeString(std::to_string(v.AsInt64()));case T::Entity:case T::Component:case T::Hash:return J::MakeString(std::to_string(v.AsUInt64()));default:return J::MakeString(v.AsString());}}
}
bool EditorAuthoringHost::Request(std::string_view request,std::string& response,std::string& error) {
    error.clear();response.clear();const auto fail=[&](std::string text){error=std::move(text);return false;};
    if(std::this_thread::get_id()!=owner_) return fail("editor authoring requires its owner thread");
    if(context_.HasPlayModeSceneSession()) return fail("editor authoring is unavailable during Play; stop Play first");
    if(request.size()>1024*1024) return fail("authoring request exceeds 1 MiB");J j;if(!J::Parse(request,j,error)) return false;
    std::string op;if(!Text(j,"op",op)) return fail("authoring request needs a string op");auto& scene=context_.Scene();J out=J::MakeObject();
    const auto success=[&](){response=out.Dump();return true;};
    if(op=="schema") {
        if(!Fields(j,{"op"})) return fail("schema accepts only op");auto components=J::MakeArray();
        for(auto name:kb::script::ScriptSceneComponentApi::ComponentNames()) {auto c=J::MakeObject();c.Set("name",J::MakeString(std::string{name}));auto properties=J::MakeArray();for(auto& p:kb::script::ScriptSceneComponentApi::ComponentProperties(name)) {auto v=J::MakeObject();v.Set("name",J::MakeString(std::string{p.name}));v.Set("type",J::MakeString(kb::script::ToString(p.type)));v.Set("writable",J::MakeBool(p.writable));auto names=J::MakeArray();for(auto n:p.enumNames) names.Append(J::MakeString(std::string{n}));v.Set("enumNames",std::move(names));properties.Append(std::move(v));}c.Set("properties",std::move(properties));components.Append(std::move(c));}out.Set("components",std::move(components));return success();
    }
    if(op=="find_entity"||op=="create_entity") {
        if(!Fields(j,{"op","name","preset"})||(op=="find_entity"&&j.Find("preset"))) return fail("invalid entity request fields");std::string name;if(!Text(j,"name",name)||name.empty()||name.size()>256) return fail("entity name must contain 1..256 UTF-8 bytes");Entity found;for(auto e:All(scene)) if(scene.Entities().Name(e)==name) {if(found.IsValid()) return fail("entity name is ambiguous");found=e;}
        if(op=="find_entity") {if(!found.IsValid()) return fail("entity was not found");out.Set("entity",J::MakeString(std::to_string(found.Id())));return success();}
        if(found.IsValid()) return fail("entity name already exists");std::string preset="Empty";if(j.Find("preset")&&!Text(j,"preset",preset)) return fail("preset must be a string");if(!kb::scene::SceneCreateMenu::Find(preset)) return fail("unknown create preset");
        if(!context_.ExecuteSceneCommand("Create "+name,[&](){auto result=kb::scene::SceneCreateMenu::Create(scene,preset,name,context_.Project());found=result.entity;error=result.error;return found.IsValid();})) return fail(error.empty()?"could not create entity":error);
        out.Set("entity",J::MakeString(std::to_string(found.Id())));return success();
    }
    if(op=="undo"||op=="redo"||op=="save_scene") {
        if(!Fields(j,{"op"})) return fail("history/save request accepts only op");const bool ok=op=="undo"?context_.UndoSceneCommand():op=="redo"?context_.RedoSceneCommand():context_.SaveCurrentScene();if(!ok) return fail(op+" failed");out.Set("succeeded",J::MakeBool(true));return success();
    }
    if(op=="log") {std::string message;if(!Fields(j,{"op","message"})||!Text(j,"message",message)||message.size()>4096) return fail("log requires a message up to 4096 bytes");context_.Console().Info("Python",message);return success();}
    std::string entityText;std::uint64_t id;if(!Text(j,"entity",entityText)||!Id(entityText,id)) return fail("entity must be an opaque decimal string handle");const Entity entity{id};if(!scene.Entities().IsAlive(entity)) return fail("entity handle is stale");
    if(op=="delete_entity") {if(!Fields(j,{"op","entity"})) return fail("invalid delete fields");if(!context_.ExecuteSceneCommand("Delete entity",[&](){scene.Entities().Destroy(entity);return true;})) return fail("delete failed");return success();}
    std::string component;if(!Text(j,"component",component)) return fail("component must be a string");
    if(op=="add_component") {
        if(!Fields(j,{"op","entity","component"})) return fail("invalid add component fields");if(!kb::scene::SceneComponentAuthoring::Find(component)) return fail("unknown authoring component");
        if(!kb::scene::SceneComponentAuthoring::Has(scene,entity,component)&&!context_.AddComponentToEntity(entity,component)) return fail("component could not be added; check its required project plugin");return success();
    }
    if(op!="get_property"&&op!="set_property") return fail("unknown authoring op: "+op);
    if(!Fields(j,{"op","entity","component","property","value"})||(op=="get_property"&&j.Find("value"))) return fail("invalid property request fields");std::string property;if(!Text(j,"property",property)) return fail("property must be a string");
    if(op=="get_property") {auto result=kb::script::ScriptSceneComponentApi::GetProperty(scene,entity,component,property);if(!result.succeeded) return fail(result.error);out.Set("value",Output(result.value));return success();}
    const kb::script::ScriptSceneComponentPropertyDesc* desc=nullptr;for(auto& p:kb::script::ScriptSceneComponentApi::ComponentProperties(component)) if(p.name==property) desc=&p;
    if(!desc||!desc->writable) return fail("unknown or read-only component property");auto* input=j.Find("value");V value;if(!input||!Input(*input,desc->type,value)) return fail("property value has an invalid type or range");
    if(!context_.ExecuteSceneCommand("Set "+component+"."+property,[&](){auto result=kb::script::ScriptSceneComponentApi::SetProperty(scene,entity,component,property,value);error=result.error;return result.succeeded;})) return fail(error.empty()?"property edit failed":error);return success();
}
}
