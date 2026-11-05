#include "engine/script/NativeScriptPlugin.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"
#include <array>
#include <stdexcept>
extern "C" void ecs_os_set_api_defaults(void);
static void Ready(kb::script::ScriptExecutionContext* c){
 auto& scene=c->GetScene();kb::scene::SceneEntity cube;
 for(auto e:scene.Hierarchy().RootEntities())if(scene.Entities().Name(e)=="GeneratedCube")cube=e;
 if(!cube.IsValid())throw std::runtime_error("Python-authored cube is missing");
 auto value=kb::script::ScriptSceneComponentApi::GetProperty(scene,cube,"Transform","localPosition.x");
 if(!value.succeeded||value.value.AsFloat()!=1.25F)throw std::runtime_error("Python-authored property did not reach C++");
 if(!c->CallFunction("Log",std::array{kb::script::ScriptFunctionArgument{"message",kb::script::ScriptValue{std::string{"native-python-content PASS"}}}}).Succeeded())throw std::runtime_error("probe log failed");
}
KB_NATIVE_SCRIPT_PLUGIN_EXPORT bool kb_register_native_scripts(kb::script::NativeScriptPluginApi* api){ecs_os_set_api_defaults();return api&&api->version==kb::script::kNativeScriptPluginApiVersion&&api->registerLifecycle(api->user,"project.NativePythonProbe",kb::script::ScriptLifecycleEvent::Ready,&Ready);}
