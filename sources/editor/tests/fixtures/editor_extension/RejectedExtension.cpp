#include "kb/editor/extensions/EditorExtension.h"
struct State { const KbEditorExtensionHost* host; };
static void Shutdown(void* user) {auto* state=static_cast<State*>(user);const auto* host=state->host;delete state;host->log(host->user,"rejected-extension resources released");}
static int Action(void*,const KbEditorExtensionHost*,const char**) {return 1;}
extern "C" KB_EDITOR_EXTENSION_EXPORT int kb_editor_extension_entry(const KbEditorExtensionHost* host,KbEditorExtensionDesc* out) {
    if(!host||!out||host->version!=KB_EDITOR_EXTENSION_API_VERSION||host->size<sizeof(KbEditorExtensionHost)) return 0;
#if KB_REJECT_MODE == 1
    *out={KB_EDITOR_EXTENSION_API_VERSION+1,sizeof(KbEditorExtensionDesc),"sample.bad",nullptr,nullptr};return 1;
#else
    const char* id=KB_REJECT_MODE==2?"sample.failure":"sample.duplicate";
    auto* state=new State{host};*out={KB_EDITOR_EXTENSION_API_VERSION,sizeof(KbEditorExtensionDesc),id,state,&Shutdown};const char* error=nullptr;
    const KbEditorActionDesc action{"sample.rejected.action","Rejected action",state,&Action};
    if(!host->registerAction(host->user,&action,&error)) return 0;
#if KB_REJECT_MODE == 3
    return host->registerAction(host->user,&action,&error);
#else
    const KbEditorPanelDesc panel{"sample.rejected.panel","Rejected panel","This installation must roll back.","sample.rejected.action","Rejected button"};
    if(!host->registerPanel(host->user,&panel,&error)) return 0;
    return 0;
#endif
#endif
}
