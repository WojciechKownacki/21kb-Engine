#include "engine/scene/SceneAccessibilityWindow.hpp"
#include "engine/scene/SceneAccessibility.hpp"
#include "engine/scene/SceneUI.hpp"
#include "engine/scene/Scene.hpp"
#include "scene/SceneUIMedia.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <mutex>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <UIAutomation.h>
#include <new>
namespace kb::scene {
namespace {
struct Snapshot {
    UIAccessibilityTree tree;
    UiaRect window{};
    std::string windowName;
    bool visible=false,keyboardFocus=false;
};
enum class ActionKind {Focus,Invoke};
struct Action {ActionKind kind{};SceneEntity entity{};};
class Provider;
struct WindowData {
    Provider* root=nullptr;
    std::mutex mutex;
    HWND window=nullptr;
    UINT actionMessage=0;
    bool closed=false;
    std::uint64_t sceneId=0;
    std::shared_ptr<const Snapshot> snapshot;
    std::vector<Action> actions;
};
HRESULT Text(std::string_view utf8,BSTR* result) {
    if(!result)return E_POINTER;*result=nullptr;
    if(utf8.empty()){*result=SysAllocStringLen(nullptr,0);return *result?S_OK:E_OUTOFMEMORY;}
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),static_cast<int>(utf8.size()),nullptr,0);
    if(count<=0)return E_INVALIDARG;*result=SysAllocStringLen(nullptr,static_cast<UINT>(count));if(!*result)return E_OUTOFMEMORY;
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.data(),static_cast<int>(utf8.size()),*result,count);return S_OK;
}
int Control(UIAccessibilityRole role) {
    switch(role){case UIAccessibilityRole::Button:return UIA_ButtonControlTypeId;case UIAccessibilityRole::CheckBox:return UIA_CheckBoxControlTypeId;case UIAccessibilityRole::Slider:return UIA_SliderControlTypeId;case UIAccessibilityRole::TextField:return UIA_EditControlTypeId;case UIAccessibilityRole::Text:return UIA_TextControlTypeId;case UIAccessibilityRole::Image:case UIAccessibilityRole::Video:return UIA_ImageControlTypeId;case UIAccessibilityRole::ProgressBar:return UIA_ProgressBarControlTypeId;case UIAccessibilityRole::List:return UIA_ListControlTypeId;default:return UIA_GroupControlTypeId;}
}
class Provider final : public IRawElementProviderSimple,public IRawElementProviderFragment,public IRawElementProviderFragmentRoot,public IInvokeProvider,public IValueProvider,public IToggleProvider {
    std::atomic<ULONG> references{1};
    std::shared_ptr<WindowData> data;
    SceneEntity entity{};
    std::shared_ptr<const Snapshot> Read() const {
        std::lock_guard lock(data->mutex);return data->closed?nullptr:data->snapshot;
    }
    const UIAccessibilityNode* Node(const Snapshot& s) const {
        const auto it=std::find_if(s.tree.nodes.begin(),s.tree.nodes.end(),[&](const auto& n){return n.entity==entity;});return it==s.tree.nodes.end()?nullptr:&*it;
    }
    HRESULT Queue(Action action) {
        try {
            std::lock_guard lock(data->mutex);if(data->closed||!data->snapshot)return UIA_E_ELEMENTNOTAVAILABLE;
            if(entity.IsValid()){const auto* n=Node(*data->snapshot);if(!n)return UIA_E_ELEMENTNOTAVAILABLE;if(!n->enabled)return UIA_E_ELEMENTNOTENABLED;}
            if(data->actions.size()>=64)return HRESULT_FROM_WIN32(ERROR_BUSY);
            data->actions.push_back(std::move(action));if(!PostMessageW(data->window,data->actionMessage,0,0)){data->actions.pop_back();return HRESULT_FROM_WIN32(GetLastError());}return S_OK;
        }catch(...){return E_OUTOFMEMORY;}
    }
    HRESULT Fragment(SceneEntity id,IRawElementProviderFragment** out) {
        if(!id.IsValid()){std::lock_guard lock(data->mutex);if(data->closed||!data->root)return UIA_E_ELEMENTNOTAVAILABLE;data->root->AddRef();*out=static_cast<IRawElementProviderFragment*>(data->root);return S_OK;}
        auto* p=new(std::nothrow)Provider(data,id);if(!p)return E_OUTOFMEMORY;*out=static_cast<IRawElementProviderFragment*>(p);return S_OK;
    }
public:
    Provider(std::shared_ptr<WindowData> d,SceneEntity e):data(std::move(d)),entity(e){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(iid==IID_IUnknown||iid==__uuidof(IRawElementProviderSimple))*out=static_cast<IRawElementProviderSimple*>(this);
        else if(iid==__uuidof(IRawElementProviderFragment))*out=static_cast<IRawElementProviderFragment*>(this);
        else if(iid==__uuidof(IRawElementProviderFragmentRoot)&&!entity.IsValid())*out=static_cast<IRawElementProviderFragmentRoot*>(this);
        else if(iid==__uuidof(IInvokeProvider)||iid==__uuidof(IValueProvider)||iid==__uuidof(IToggleProvider)) {
            const auto s=Read();const auto* n=s?Node(*s):nullptr;if(!n)return E_NOINTERFACE;
            if(iid==__uuidof(IInvokeProvider)&&n->role==UIAccessibilityRole::Button&&n->focusable)*out=static_cast<IInvokeProvider*>(this);
            else if(iid==__uuidof(IToggleProvider)&&n->role==UIAccessibilityRole::CheckBox&&n->focusable)*out=static_cast<IToggleProvider*>(this);
            else if(iid==__uuidof(IValueProvider)&&n->role==UIAccessibilityRole::TextField&&!n->password)*out=static_cast<IValueProvider*>(this);
            else return E_NOINTERFACE;
        }
        else return E_NOINTERFACE;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++references;}
    ULONG STDMETHODCALLTYPE Release() override{const auto n=--references;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out) override{if(!out)return E_POINTER;*out=ProviderOptions_ServerSideProvider;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern,IUnknown** out) override{
        if(!out)return E_POINTER;*out=nullptr;const auto s=Read();if(!s)return UIA_E_ELEMENTNOTAVAILABLE;const auto* n=Node(*s);if(!n)return entity.IsValid()?UIA_E_ELEMENTNOTAVAILABLE:S_OK;
        if(pattern==UIA_InvokePatternId&&n->role==UIAccessibilityRole::Button&&n->focusable)*out=static_cast<IInvokeProvider*>(this);
        if(pattern==UIA_TogglePatternId&&n->role==UIAccessibilityRole::CheckBox&&n->focusable)*out=static_cast<IToggleProvider*>(this);
        if(pattern==UIA_ValuePatternId&&n->role==UIAccessibilityRole::TextField&&!n->password)*out=static_cast<IValueProvider*>(this);
        if(*out)AddRef();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property,VARIANT* out) override {
        if(!out)return E_POINTER;VariantInit(out);const auto s=Read();if(!s)return UIA_E_ELEMENTNOTAVAILABLE;const auto* n=Node(*s);if(entity.IsValid()&&!n)return UIA_E_ELEMENTNOTAVAILABLE;
        auto boolean=[&](bool value){out->vt=VT_BOOL;out->boolVal=value?VARIANT_TRUE:VARIANT_FALSE;return S_OK;};
        auto text=[&](std::string_view value){out->vt=VT_BSTR;return Text(value,&out->bstrVal);};
        switch(property){
        case UIA_ControlTypePropertyId:out->vt=VT_I4;out->lVal=n?Control(n->role):UIA_PaneControlTypeId;return S_OK;
        case UIA_NamePropertyId:return text(n?n->name:s->windowName);
        case UIA_HelpTextPropertyId:return text(n?n->hint:"");
        case UIA_FrameworkIdPropertyId:return text("21kb");
        case UIA_AutomationIdPropertyId:{if(!n)return text("RuntimeUI");char bytes[32];const auto encoded=std::to_chars(bytes,bytes+sizeof(bytes),n->entity.Id());return text({bytes,static_cast<std::size_t>(encoded.ptr-bytes)});}
        case UIA_IsKeyboardFocusablePropertyId:return boolean(!n||n->focusable);
        case UIA_HasKeyboardFocusPropertyId:return boolean(n&&n->focused&&s->keyboardFocus);
        case UIA_IsEnabledPropertyId:return boolean(!n||n->enabled);
        case UIA_IsPasswordPropertyId:return boolean(n&&n->password);
        case UIA_IsControlElementPropertyId:case UIA_IsContentElementPropertyId:return boolean(true);
        case UIA_IsOffscreenPropertyId:return boolean(!s->visible||(n&&(n->bounds.width<=0||n->bounds.height<=0)));
        case UIA_ValueIsReadOnlyPropertyId:return boolean(true);
        case UIA_ValueValuePropertyId:if(n&&!n->password)return text(n->value);return S_OK;
        case UIA_ToggleToggleStatePropertyId:if(n&&n->role==UIAccessibilityRole::CheckBox){out->vt=VT_I4;out->lVal=n->checked?ToggleState_On:ToggleState_Off;}return S_OK;
        default:return S_OK;}
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override{if(!out)return E_POINTER;*out=nullptr;if(!Read())return UIA_E_ELEMENTNOTAVAILABLE;return entity.IsValid()?S_OK:UiaHostProviderFromHwnd(data->window,out);}
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,IRawElementProviderFragment** out) override {
        if(!out)return E_POINTER;*out=nullptr;const auto s=Read();if(!s)return UIA_E_ELEMENTNOTAVAILABLE;const auto* n=Node(*s);if(entity.IsValid()&&!n)return UIA_E_ELEMENTNOTAVAILABLE;
        if(direction==NavigateDirection_Parent)return entity.IsValid()?Fragment(n->parent,out):S_OK;
        if(direction==NavigateDirection_FirstChild||direction==NavigateDirection_LastChild){
            if(direction==NavigateDirection_FirstChild){for(const auto& child:s->tree.nodes)if(child.parent==entity)return Fragment(child.entity,out);}
            else for(auto it=s->tree.nodes.rbegin();it!=s->tree.nodes.rend();++it)if(it->parent==entity)return Fragment(it->entity,out);return S_OK;
        }
        if(!n)return S_OK;
        const UIAccessibilityNode* previous=nullptr;bool next=false;
        for(const auto& sibling:s->tree.nodes){if(sibling.parent!=n->parent)continue;if(next)return Fragment(sibling.entity,out);if(sibling.entity==entity){if(direction==NavigateDirection_PreviousSibling)return previous?Fragment(previous->entity,out):S_OK;if(direction==NavigateDirection_NextSibling)next=true;else return E_INVALIDARG;}previous=&sibling;}return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override {
        if(!out)return E_POINTER;*out=nullptr;if(!entity.IsValid())return S_OK;
        *out=SafeArrayCreateVector(VT_I4,0,5);if(!*out)return E_OUTOFMEMORY;
        const int values[]{UiaAppendRuntimeId,static_cast<int>(data->sceneId),static_cast<int>(data->sceneId>>32),static_cast<int>(entity.Id()),static_cast<int>(entity.Id()>>32)};
        for(LONG i=0;i<5;++i){auto value=values[i];const auto hr=SafeArrayPutElement(*out,&i,&value);if(FAILED(hr)){SafeArrayDestroy(*out);*out=nullptr;return hr;}}return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override {
        if(!out)return E_POINTER;*out={};const auto s=Read();if(!s)return UIA_E_ELEMENTNOTAVAILABLE;if(!entity.IsValid()){*out=s->window;return S_OK;}const auto* n=Node(*s);if(!n)return UIA_E_ELEMENTNOTAVAILABLE;*out={n->bounds.x,n->bounds.y,n->bounds.width,n->bounds.height};return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override{if(!out)return E_POINTER;*out=nullptr;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetFocus() override{return Queue({ActionKind::Focus,entity});}
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override{if(!out)return E_POINTER;*out=nullptr;std::lock_guard lock(data->mutex);if(data->closed||!data->root)return UIA_E_ELEMENTNOTAVAILABLE;data->root->AddRef();*out=static_cast<IRawElementProviderFragmentRoot*>(data->root);return S_OK;}
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x,double y,IRawElementProviderFragment** out) override {
        if(!out)return E_POINTER;*out=nullptr;const auto s=Read();if(!s)return UIA_E_ELEMENTNOTAVAILABLE;
        for(auto it=s->tree.nodes.rbegin();it!=s->tree.nodes.rend();++it){const auto& b=it->bounds;if(x>=b.x&&x<b.x+b.width&&y>=b.y&&y<b.y+b.height)return Fragment(it->entity,out);}const auto& b=s->window;return x>=b.left&&x<b.left+b.width&&y>=b.top&&y<b.top+b.height?Fragment({},out):S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** out) override{if(!out)return E_POINTER;*out=nullptr;const auto s=Read();if(!s)return UIA_E_ELEMENTNOTAVAILABLE;return s->keyboardFocus&&s->tree.focused.IsValid()?Fragment(s->tree.focused,out):S_OK;}
    HRESULT STDMETHODCALLTYPE Invoke() override{return Queue({ActionKind::Invoke,entity});}
    HRESULT STDMETHODCALLTYPE Toggle() override{return Invoke();}
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* out) override{if(!out)return E_POINTER;const auto s=Read();const auto* n=s?Node(*s):nullptr;if(!n)return UIA_E_ELEMENTNOTAVAILABLE;*out=n->checked?ToggleState_On:ToggleState_Off;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR) override {return UIA_E_NOTSUPPORTED;}
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* out) override{const auto s=Read();const auto* n=s?Node(*s):nullptr;if(!n)return UIA_E_ELEMENTNOTAVAILABLE;return n->password?UIA_E_NOTSUPPORTED:Text(n->value,out);}
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* out) override{if(!out)return E_POINTER;const auto s=Read();const auto* n=s?Node(*s):nullptr;if(!n)return UIA_E_ELEMENTNOTAVAILABLE;*out=TRUE;return S_OK;}
};
constexpr wchar_t kWindowProperty[]=L"21kb.RuntimeUI.Accessibility";
}
class UIAccessibilityWindowBridge {
public:
    Scene* scene=nullptr;
    std::shared_ptr<WindowData> data;
    Provider* root=nullptr;
    bool comOwned=false;
    static LRESULT CALLBACK Message(HWND window,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR id,DWORD_PTR reference) {
        auto* bridge=reinterpret_cast<UIAccessibilityWindowBridge*>(reference);
        if(message==WM_GETOBJECT&&static_cast<LONG>(lparam)==UiaRootObjectId)return UiaReturnRawElementProvider(window,wparam,lparam,bridge->root);
        if(message==bridge->data->actionMessage){
            std::vector<Action> actions;{std::lock_guard lock(bridge->data->mutex);actions.swap(bridge->data->actions);}
            for(const auto& action:actions){switch(action.kind){case ActionKind::Focus: ::SetFocus(window);if(action.entity.IsValid())static_cast<void>(SceneAccessibility::Focus(*bridge->scene,action.entity));else ::SetFocus(window);break;case ActionKind::Invoke:static_cast<void>(SceneAccessibility::Invoke(*bridge->scene,action.entity));break;}}
            PublishUIAccessibilityWindow(*bridge->scene);return 0;
        }
        if(message==WM_NCDESTROY){std::lock_guard lock(bridge->data->mutex);bridge->data->closed=true;RemovePropW(window,kWindowProperty);RemoveWindowSubclass(window,&Message,id);}
        return DefSubclassProc(window,message,wparam,lparam);
    }
    ~UIAccessibilityWindowBridge(){
        if(data){std::lock_guard lock(data->mutex);data->closed=true;data->root=nullptr;data->actions.clear();if(IsWindow(data->window)){if(GetPropW(data->window,kWindowProperty)==this)RemovePropW(data->window,kWindowProperty);RemoveWindowSubclass(data->window,&Message,reinterpret_cast<UINT_PTR>(this));}}
        if(root){UiaDisconnectProvider(root);root->Release();}if(comOwned)CoUninitialize();
    }
};
bool SceneAccessibilityWindow::Attach(Scene& scene,void* handle,std::string& error){
    auto& state=EnsureUIMedia(scene);if(state.attachWindow!=&SceneAccessibilityWindow::Attach)return state.attachWindow(scene,handle,error);error.clear();const auto window=static_cast<HWND>(handle);
    if(!IsWindow(window)||GetWindowThreadProcessId(window,nullptr)!=GetCurrentThreadId()){error="accessibility requires a native window on the scene owner thread";return false;}
    if(state.windowBridge&&state.windowBridge->data->closed)state.windowBridge.reset();
    if(state.windowBridge&&state.windowBridge->data->window==window)return true;
    if(GetPropW(window,kWindowProperty)){error="the window already exposes another runtime UI tree";return false;}
    const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(initialized)&&initialized!=RPC_E_CHANGED_MODE){error="UI Automation COM initialization failed";return false;}
    auto bridge=std::make_shared<UIAccessibilityWindowBridge>();bridge->comOwned=SUCCEEDED(initialized);bridge->scene=&scene;bridge->data=std::make_shared<WindowData>();bridge->data->window=window;bridge->data->sceneId=scene.Id();bridge->data->actionMessage=RegisterWindowMessageW(L"21kb.RuntimeUI.Accessibility.Actions");bridge->root=new(std::nothrow)Provider(bridge->data,{});bridge->data->root=bridge->root;
    if(!bridge->root||!bridge->data->actionMessage||!SetPropW(window,kWindowProperty,bridge.get())||!SetWindowSubclass(window,&UIAccessibilityWindowBridge::Message,reinterpret_cast<UINT_PTR>(bridge.get()),reinterpret_cast<DWORD_PTR>(bridge.get()))){error="UI Automation window hook could not be installed";return false;}
    state.windowBridge=std::move(bridge);PublishUIAccessibilityWindow(scene);return true;
}
void SceneAccessibilityWindow::Detach(Scene& scene){auto& state=SceneAccess::State(scene).uiMedia;if(state)state->windowBridge.reset();}
void PublishUIAccessibilityWindow(Scene& scene){
    const auto state=SceneAccess::State(scene).uiMedia;if(!state||!state->windowBridge)return;const auto bridge=state->windowBridge;const auto window=bridge->data->window;if(!IsWindow(window))return;
    auto snapshot=std::make_shared<Snapshot>();snapshot->tree=SceneAccessibility::Read(scene);if(!snapshot->tree.error.empty())snapshot->tree.nodes.clear();
    wchar_t title[256]{};const int titleLength=GetWindowTextW(window,title,256);const int titleBytes=WideCharToMultiByte(CP_UTF8,0,title,titleLength,nullptr,0,nullptr,nullptr);snapshot->windowName.resize(static_cast<std::size_t>(std::max(0,titleBytes)));if(titleBytes>0)WideCharToMultiByte(CP_UTF8,0,title,titleLength,snapshot->windowName.data(),titleBytes,nullptr,nullptr);if(snapshot->windowName.empty())snapshot->windowName="User interface";
    RECT client{};GetClientRect(window,&client);POINT origin{};ClientToScreen(window,&origin);snapshot->window={static_cast<double>(origin.x),static_cast<double>(origin.y),static_cast<double>(client.right),static_cast<double>(client.bottom)};snapshot->visible=IsWindowVisible(window)!=0;snapshot->keyboardFocus=::GetFocus()==window;
    const auto size=scene.UI().Frame().viewportSize;const float scaleX=size.x>0?static_cast<float>(client.right)/size.x:1,scaleY=size.y>0?static_cast<float>(client.bottom)/size.y:1;
    for(auto& n:snapshot->tree.nodes){n.bounds.x=n.bounds.x*scaleX+static_cast<float>(origin.x);n.bounds.y=n.bounds.y*scaleY+static_cast<float>(origin.y);n.bounds.width*=scaleX;n.bounds.height*=scaleY;}
    SceneEntity previous{};std::size_t count=0;{std::lock_guard lock(bridge->data->mutex);if(bridge->data->closed)return;if(bridge->data->snapshot){previous=bridge->data->snapshot->keyboardFocus?bridge->data->snapshot->tree.focused:SceneEntity{};count=bridge->data->snapshot->tree.nodes.size();}bridge->data->snapshot=snapshot;}
    if(count!=snapshot->tree.nodes.size())UiaRaiseStructureChangedEvent(bridge->root,StructureChangeType_ChildrenInvalidated,nullptr,0);
    if(snapshot->keyboardFocus&&previous!=snapshot->tree.focused&&snapshot->tree.focused.IsValid()){auto* focused=new(std::nothrow)Provider(bridge->data,snapshot->tree.focused);if(focused){UiaRaiseAutomationEvent(focused,UIA_AutomationFocusChangedEventId);focused->Release();}}
}
}
#else
namespace kb::scene {
bool SceneAccessibilityWindow::Attach(Scene&,void*,std::string& error){error="native UI Automation requires Windows";return false;}
void SceneAccessibilityWindow::Detach(Scene&){}
void PublishUIAccessibilityWindow(Scene&){}
}
#endif
