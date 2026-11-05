#include "GameWindow.hpp"
#include "engine/platform/win32/Win32InputCollector.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneUIComponentSet.hpp"
#include "engine/scene/SceneUI.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/ui/UIComponentCatalog.hpp"
#include "engine/scene/SceneAccessibilityWindow.hpp"
#include <objbase.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <string>
#include <chrono>
#include <thread>

#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

namespace {
int Client(HWND window) {
    using Microsoft::WRL::ComPtr;
    Require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"UIA client COM initialization failed");
    {
        ComPtr<IUIAutomation> automation;Require(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation))),"UIA client could not start");
        ComPtr<IUIAutomationElement> root;Require(SUCCEEDED(automation->ElementFromHandle(window,&root))&&root,"UIA client could not find the host window");
        BSTR framework=nullptr;Require(SUCCEEDED(root->get_CurrentFrameworkId(&framework)),"UIA framework query failed");const bool own=framework&&std::wstring_view{framework}==L"21kb";SysFreeString(framework);Require(own,"UIA fell back to the default window provider");
        auto find=[&](const wchar_t* name){VARIANT value{};value.vt=VT_BSTR;value.bstrVal=SysAllocString(name);ComPtr<IUIAutomationCondition> condition;const auto hr=automation->CreatePropertyCondition(UIA_NamePropertyId,value,&condition);VariantClear(&value);Require(SUCCEEDED(hr),"UIA name condition failed");ComPtr<IUIAutomationElement> element;Require(SUCCEEDED(root->FindFirst(TreeScope_Descendants,condition.Get(),&element))&&element,"UIA semantic child is missing");return element;};
        const auto button=find(L"Start sample");CONTROLTYPEID role=0;Require(SUCCEEDED(button->get_CurrentControlType(&role))&&role==UIA_ButtonControlTypeId,"UIA button role is wrong");
        RECT bounds{};Require(SUCCEEDED(button->get_CurrentBoundingRectangle(&bounds))&&bounds.right>bounds.left&&bounds.bottom>bounds.top,"UIA screen bounds are empty");
        ComPtr<IUIAutomationTreeWalker> walker;ComPtr<IUIAutomationElement> parent;Require(SUCCEEDED(automation->get_ControlViewWalker(&walker))&&SUCCEEDED(walker->GetParentElement(button.Get(),&parent))&&parent,"UIA fragment navigation failed");
        const auto entry=find(L"Editable sample");ComPtr<IUIAutomationValuePattern> value;Require(SUCCEEDED(entry->GetCurrentPatternAs(UIA_ValuePatternId,IID_PPV_ARGS(&value)))&&value,"UIA value pattern unavailable");BSTR text=nullptr;Require(SUCCEEDED(value->get_CurrentValue(&text)),"UIA text value failed");const bool content=text&&std::wstring_view{text}==L"Initial value";SysFreeString(text);Require(content,"UIA did not expose the live text value");BOOL readonly=FALSE;Require(SUCCEEDED(value->get_CurrentIsReadOnly(&readonly))&&readonly,"UIA value pattern must advertise its read-only editing boundary");BSTR unsupported=SysAllocString(L"Unsupported edit");const auto rejected=value->SetValue(unsupported);SysFreeString(unsupported);Require(FAILED(rejected),"UIA silently accepted an unsupported value edit");
        const auto password=find(L"Protected sample");BOOL protectedValue=FALSE;Require(SUCCEEDED(password->get_CurrentIsPassword(&protectedValue))&&protectedValue,"UIA password flag missing");VARIANT secret{};Require(SUCCEEDED(password->GetCurrentPropertyValue(UIA_ValueValuePropertyId,&secret)),"UIA password property failed");const bool safe=secret.vt!=VT_BSTR||!secret.bstrVal||SysStringLen(secret.bstrVal)==0;VariantClear(&secret);Require(safe,"UIA exposed password text");
        Require(SUCCEEDED(entry->SetFocus()),"UIA focus request failed");
        ComPtr<IUIAutomationInvokePattern> invoke;Require(SUCCEEDED(button->GetCurrentPatternAs(UIA_InvokePatternId,IID_PPV_ARGS(&invoke)))&&invoke&&SUCCEEDED(invoke->Invoke()),"UIA invoke failed");
    }
    CoUninitialize();return 0;
}
void Probe(kb::game::GameWindow& window,kb::scene::Scene& scene,kb::scene::SceneEntity button) {
    using namespace kb::scene;
    UIAccessibility annotation;Require(SetUIAccessibilityLabel(annotation,"Start sample"),"UIA button label invalid");scene.Components().UI().Set(button,annotation);
    const auto canvas=scene.Entities().Object(scene.Hierarchy().Parent(button));
    auto input=BuildUIComponentPreset(UIComponentPreset::InputField);input.rectTransform->offsetMin={10,10};input.rectTransform->offsetMax={200,45};Require(SetUITextContent(*input.text,"Initial value"),"UIA value fixture invalid");input.accessibility.emplace();Require(SetUIAccessibilityLabel(*input.accessibility,"Editable sample"),"UIA entry label invalid");
    const auto entry=scene.Entities().CreateEntity(SceneObjectDesc{.parent=canvas});ApplySceneUIComponents(scene.Components().UI(),entry,input);
    input.inputField->contentType=UIInputContentType::Password;Require(SetUITextContent(*input.text,"Hidden"),"UIA password fixture invalid");Require(SetUIAccessibilityLabel(*input.accessibility,"Protected sample"),"UIA password label invalid");const auto secret=scene.Entities().CreateEntity(SceneObjectDesc{.parent=canvas});ApplySceneUIComponents(scene.Components().UI(),secret,input);
    Require(scene.UI().Update(640,480,{},0),"UIA initial layout failed");static_cast<void>(scene.UI().DrainEvents());std::string error;Require(SceneAccessibilityWindow::Attach(scene,window.Handle(),error),error.c_str());
    const auto checkpoint=scene.Runtime().CaptureRuntimeState(error);Require(checkpoint!=nullptr,error.c_str());Require(scene.Runtime().RestoreRuntimeState(*checkpoint,error),error.c_str());Require(scene.UI().Update(640,480,{},0),"UIA layout after checkpoint restore failed");
    wchar_t path[32768]{};Require(GetModuleFileNameW(nullptr,path,32768)!=0,"Cannot find UIA client executable");std::wstring command=L"\""+std::wstring{path}+L"\" --uia-client="+std::to_wstring(reinterpret_cast<std::uintptr_t>(window.Handle()));STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
    Require(CreateProcessW(path,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=0,"Cannot launch the separate UIA client");CloseHandle(process.hThread);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);bool focused=false,invoked=false;
    while(WaitForSingleObject(process.hProcess,0)==WAIT_TIMEOUT&&std::chrono::steady_clock::now()<deadline){Require(window.PumpMessages(),"UIA host closed unexpectedly");Require(scene.UI().Update(640,480,{},0),"UIA host layout failed");for(const auto& event:scene.UI().DrainEvents()){focused=focused||(event.entity==entry&&event.type==SceneUIEventType::Focused);invoked=invoked||(event.entity==button&&event.type==SceneUIEventType::Clicked);}std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    DWORD exitCode=STILL_ACTIVE;GetExitCodeProcess(process.hProcess,&exitCode);if(exitCode==STILL_ACTIVE){TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,1000);}CloseHandle(process.hProcess);Require(exitCode==0,"Separate UIA client failed or timed out");
    // UIA actions acknowledge a queued owner-thread request. The client can exit
    // before the final posted message is pumped, so observe its completion too.
    const auto actionsDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while((!focused||!invoked||scene.UI().Focused()!=button)&&std::chrono::steady_clock::now()<actionsDeadline){Require(window.PumpMessages(),"UIA host closed before queued actions completed");Require(scene.UI().Update(640,480,{},0),"UIA action layout failed");for(const auto& event:scene.UI().DrainEvents()){focused=focused||(event.entity==entry&&event.type==SceneUIEventType::Focused);invoked=invoked||(event.entity==button&&event.type==SceneUIEventType::Clicked);}std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    Require(focused&&invoked&&scene.UI().Focused()==button,"UIA actions did not reach the scene owner thread");SceneAccessibilityWindow::Detach(scene);Require(SceneAccessibilityWindow::Attach(scene,window.Handle(),error),"UIA reattach after detach failed");SceneAccessibilityWindow::Detach(scene);
    std::cout<<"Native UI Automation client PASS tree role value password bounds focus invoke detach checkpoint\n";
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2&&std::string_view{argv[1]}.starts_with("--uia-client="))return Client(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(std::stoull(argv[1]+13))));
        Require(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != 0,
            "Cannot enable per-monitor DPI awareness");
        kb::input::Win32InputCollector input;
        kb::game::GameWindow window;
        Require(window.Open(L"Responsive UI test", 640U, 480U, false, input, false), "Cannot open hidden host");
        std::uint32_t width{}, height{};
        Require(window.ConsumeResize(width, height) && width == 640U && height == 480U,
            "Requested initial client dimensions changed");
        using namespace kb::scene;
        Scene scene{SceneMode::PrefabPrivate};
        const auto canvas = scene.Entities().CreateObject();
        ApplySceneUIComponents(scene.Components().UI(), canvas.Entity(), BuildUIComponentPreset(UIComponentPreset::Canvas));
        const auto button = scene.Entities().CreateObject(SceneObjectDesc{.parent = canvas});
        auto components = BuildUIComponentPreset(UIComponentPreset::Button);
        components.rectTransform->anchorMin = {.25F, .25F};
        components.rectTransform->anchorMax = {.75F, .75F};
        components.rectTransform->offsetMin = {0.0F, 0.0F};
        components.rectTransform->offsetMax = {0.0F, 0.0F};
        ApplySceneUIComponents(scene.Components().UI(), button.Entity(), components);

        struct Transition { unsigned int dpi; LONG width; LONG height; };
        constexpr std::array transitions{
            Transition{144U, 960, 720}, Transition{192U, 1280, 960},
            Transition{120U, 600, 800}, Transition{96U, 640, 480}};
        for (const auto& transition : transitions) {
            RECT suggested{40, 50, 40 + transition.width, 50 + transition.height};
            // Deliver the real host message in-process so its RECT has the same lifetime
            // and address space as an OS notification. No physical display is changed.
            SendMessageW(window.Handle(), WM_DPICHANGED, MAKELONG(transition.dpi, transition.dpi),
                reinterpret_cast<LPARAM>(&suggested));
            RECT actual{}, client{};
            Require(GetWindowRect(window.Handle(), &actual) != 0 && GetClientRect(window.Handle(), &client) != 0,
                "Cannot read resized window");
            Require(actual.left == suggested.left && actual.top == suggested.top &&
                actual.right == suggested.right && actual.bottom == suggested.bottom,
                "DPI change must apply the suggested window bounds");
            Require(window.ConsumeResize(width, height) && width == static_cast<unsigned int>(client.right) &&
                height == static_cast<unsigned int>(client.bottom), "DPI resize must reach the runtime surface");
            Require(!window.ConsumeResize(width, height), "DPI resize must be consumed exactly once");
            Require(IsWindowVisible(window.Handle()) == 0, "DPI change exposed the hidden host");
            Require(scene.UI().Update(static_cast<float>(width), static_cast<float>(height), {}, .016F),
                "UI failed to update after DPI change");
            Require(scene.UI().HitTest({static_cast<float>(width) * .5F, static_cast<float>(height) * .5F}) == button.Entity(),
                "Anchored control became unreachable after DPI change");
            Require(scene.UI().HitTest({static_cast<float>(width) * .9F, static_cast<float>(height) * .9F}) != button.Entity(),
                "Control hit bounds escaped its anchors after DPI change");
        }
        Probe(window,scene,button.Entity());
        std::cout << "Hidden native DPI transitions and UI hit testing passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
