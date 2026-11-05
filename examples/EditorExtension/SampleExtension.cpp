#include "kb/editor/extensions/EditorExtension.h"
#include <string>
#include <cstring>
namespace {
struct State { const KbEditorExtensionHost* host=nullptr;unsigned clicks=0; };
int Create(void* user,const KbEditorExtensionHost* host,const char** error) {
    auto& state=*static_cast<State*>(user);const char* response=nullptr;
    if(!host->request(host->user,"{\"op\":\"find_entity\",\"name\":\"ExtensionCube\"}",&response,error)) {
        if(!host->request(host->user,"{\"op\":\"create_entity\",\"name\":\"ExtensionCube\",\"preset\":\"Cube\"}",&response,error)) return 0;
    }
    // Entity handles are decimal strings; this tiny sample reads that one field.
    const char* key=response?std::strstr(response,"\"entity\":\""):nullptr;if(!key) {if(error) *error="authoring response has no entity";return 0;}key+=10;const char* end=std::strchr(key,'"');if(!end) return 0;
    const std::string id{key,end};const auto request=std::string{"{\"op\":\"set_property\",\"entity\":\""}+id+"\",\"component\":\"Transform\",\"property\":\"localPosition.x\",\"value\":1.5}";
    if(!host->request(host->user,request.c_str(),&response,error)) return 0;
    ++state.clicks;const auto text="ExtensionCube x=1.5; actions="+std::to_string(state.clicks);
    if(!host->setPanelText(host->user,"sample.blockout.panel",text.c_str(),error)) return 0;
    host->log(host->user,"sample-extension action PASS");return 1;
}
void Shutdown(void* user) {auto* state=static_cast<State*>(user);const auto* host=state->host;delete state;host->log(host->user,"sample-extension resources released");}
}
extern "C" KB_EDITOR_EXTENSION_EXPORT int kb_editor_extension_entry(const KbEditorExtensionHost* host,KbEditorExtensionDesc* out) {
    if(!host||!out||host->version!=KB_EDITOR_EXTENSION_API_VERSION||host->size<sizeof(KbEditorExtensionHost)) return 0;
    auto* state=new State{host};*out={KB_EDITOR_EXTENSION_API_VERSION,sizeof(KbEditorExtensionDesc),"sample.blockout",state,&Shutdown};const char* error=nullptr;
    const KbEditorActionDesc action{"sample.blockout.create","Create blockout cube",state,&Create};
    if(!host->registerAction(host->user,&action,&error)) return 0;
    const KbEditorPanelDesc panel{"sample.blockout.panel","Sample Tools","Create a cube in the current editor document.","sample.blockout.create","Create or update cube"};
    return host->registerPanel(host->user,&panel,&error);
}
