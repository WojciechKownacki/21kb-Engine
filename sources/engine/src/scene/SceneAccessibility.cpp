#include "engine/scene/SceneAccessibility.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneUI.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneUIComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/input/InputText.hpp"
#include <algorithm>
#include <charconv>
#include <unordered_map>

namespace kb::scene {
namespace {
std::string Number(float value){char bytes[64];const auto encoded=std::to_chars(bytes,bytes+sizeof(bytes),value,std::chars_format::general,6);return encoded.ec==std::errc{}?std::string{bytes,encoded.ptr}:std::string{};}
bool Hidden(const Scene& scene,SceneEntity entity){for(std::size_t depth=0;entity.IsValid()&&depth<128;++depth){const auto* a=scene.Components().UI().TryGet<UIAccessibility>(entity);if(a&&a->hidden)return true;entity=scene.Hierarchy().Parent(entity);}return false;}
bool Password(const Scene& scene,SceneEntity entity){const auto* input=scene.Components().UI().TryGet<UIInputField>(entity);return input&&(input->contentType==UIInputContentType::Password||input->contentType==UIInputContentType::Pin);}
std::string Caption(const Scene& scene,const SceneUIFrameElement& element){
    if(const auto* a=scene.Components().UI().TryGet<UIAccessibility>(element.entity);a&&!UIAccessibilityLabel(*a).empty())return std::string{UIAccessibilityLabel(*a)};
    if(const auto* field=scene.Components().UI().TryGet<UIInputField>(element.entity))return std::string{UIInputPlaceholder(*field)};
    if(!element.text)return {};
    const auto text=UITextContent(*element.text);
    // Rich markup uses an explicit semantic label rather than a second markup parser.
    if(element.text->richText&&text.find('<')!=std::string_view::npos)return {};
    return std::string{text};
}
UIAccessibilityRole Infer(const Scene& scene,const SceneUIFrameElement& element){
    const auto ui=scene.Components().UI();const auto entity=element.entity;
    if(ui.Has<UIInputField>(entity))return UIAccessibilityRole::TextField;
    if(ui.Has<UIToggle>(entity))return UIAccessibilityRole::CheckBox;
    if(ui.Has<UISlider>(entity)||ui.Has<UIScrollbar>(entity))return UIAccessibilityRole::Slider;
    if(ui.Has<UIButton>(entity)||ui.Has<UIDropdown>(entity))return UIAccessibilityRole::Button;
    if(ui.Has<UIProgressBar>(entity))return UIAccessibilityRole::ProgressBar;
    if(element.text)return UIAccessibilityRole::Text;
    if(element.image||element.rawImage||element.sprite)return UIAccessibilityRole::Image;
    return UIAccessibilityRole::Group;
}
}
UIAccessibilityTree SceneAccessibility::Read(const Scene& scene){
    UIAccessibilityTree tree;const auto& frame=scene.UI().Frame();tree.focused=scene.UI().Focused();
    if(frame.refusal.HasValue()){tree.error=frame.refusal.reason;return tree;}
    if(frame.elements.size()>2048){tree.error="accessibility tree exceeds 2048 layout elements";return tree;}
    std::unordered_map<std::uint64_t,std::size_t> included;
    for(const auto& element:frame.elements){
        if(element.effectiveOpacity<=0||Hidden(scene,element.entity)||(frame.modalActive&&element.canvasSortingOrder<frame.modalSortingOrder))continue;
        const auto ui=scene.Components().UI();const auto* annotation=ui.TryGet<UIAccessibility>(element.entity);
        UIAccessibilityNode node;node.entity=element.entity;node.role=annotation&&annotation->role!=UIAccessibilityRole::Automatic?annotation->role:Infer(scene,element);
        node.name=Caption(scene,element);node.hint=annotation?std::string{UIAccessibilityHint(*annotation)}:std::string{};
        float left=element.corners[0].x,right=left,top=element.corners[0].y,bottom=top;
        for(const auto p:element.corners){left=std::min(left,p.x);right=std::max(right,p.x);top=std::min(top,p.y);bottom=std::max(bottom,p.y);}
        left=std::max(left,element.clipRect.x);right=std::min(right,element.clipRect.x+element.clipRect.width);top=std::max(top,element.clipRect.y);bottom=std::min(bottom,element.clipRect.y+element.clipRect.height);
        node.bounds={left,top,std::max(0.0F,right-left),std::max(0.0F,bottom-top)};node.focused=node.entity==tree.focused;node.focusable=element.interactionEnabled;node.enabled=!element.selectable||element.interactionEnabled;node.password=Password(scene,node.entity);
        if(node.name.empty()&&(node.role==UIAccessibilityRole::Button||node.role==UIAccessibilityRole::CheckBox||node.role==UIAccessibilityRole::Slider)) {
            for(const auto& child:frame.elements){auto parent=scene.Hierarchy().Parent(child.entity);for(std::size_t depth=0;parent.IsValid()&&depth<128;++depth){if(parent==node.entity){const auto label=Caption(scene,child);if(!label.empty()){if(!node.name.empty())node.name+=' ';node.name+=label;}break;}parent=scene.Hierarchy().Parent(parent);}}
        }
        if(node.name.empty())node.name=scene.Entities().Name(node.entity);
        if(const auto* toggle=ui.TryGet<UIToggle>(node.entity)){node.checked=toggle->toggled;node.value=toggle->toggled?"checked":"unchecked";}
        if(const auto* slider=ui.TryGet<UISlider>(node.entity)){node.hasRange=true;node.minimum=slider->minimum;node.maximum=slider->maximum;node.numericValue=slider->value;node.value=Number(slider->value);}
        if(const auto* progress=ui.TryGet<UIProgressBar>(node.entity)){node.hasRange=true;node.minimum=progress->minimum;node.maximum=progress->maximum;node.numericValue=progress->value;node.value=Number(progress->value);}
        if(const auto* scroll=ui.TryGet<UIScrollbar>(node.entity)){node.hasRange=true;node.minimum=0;node.maximum=1;node.numericValue=scroll->value;node.value=Number(scroll->value);}
        if(ui.Has<UIInputField>(node.entity)&&!node.password)if(const auto* text=ui.TryGet<UIText>(node.entity))node.value=std::string{UITextContent(*text)};
        included.emplace(node.entity.Id(),tree.nodes.size());tree.nodes.push_back(std::move(node));
    }
    for(auto& node:tree.nodes){auto parent=scene.Hierarchy().Parent(node.entity);for(std::size_t depth=0;parent.IsValid()&&depth<128;++depth){if(included.contains(parent.Id())){node.parent=parent;break;}parent=scene.Hierarchy().Parent(parent);}}
    return tree;
}
std::string SceneAccessibility::Describe(const UIAccessibilityNode& node){
    static constexpr const char* roles[]{"automatic","group","text","button","checkbox","slider","text field","image","video","progress","list"};
    const auto role=static_cast<std::size_t>(node.role);std::string result=node.name;if(role<std::size(roles)){if(!result.empty())result+=". ";result+=roles[role];}
    if(!node.password&&!node.value.empty())result+=". "+node.value;if(node.password)result+=". password";if(!node.enabled)result+=". disabled";if(!node.hint.empty())result+=". "+node.hint;return result;
}
bool SceneAccessibility::Focus(Scene& scene,SceneEntity entity){const auto tree=Read(scene);const auto node=std::find_if(tree.nodes.begin(),tree.nodes.end(),[&](const auto& n){return n.entity==entity;});return node!=tree.nodes.end()&&node->enabled&&node->focusable&&scene.UI().SetFocus(entity);}
bool SceneAccessibility::Invoke(Scene& scene,SceneEntity entity){const auto tree=Read(scene);const auto node=std::find_if(tree.nodes.begin(),tree.nodes.end(),[&](const auto& n){return n.entity==entity;});return node!=tree.nodes.end()&&node->enabled&&(node->role==UIAccessibilityRole::Button||node->role==UIAccessibilityRole::CheckBox)&&scene.UI().Activate(entity);}
bool SceneAccessibility::SetValue(Scene& scene,SceneEntity entity,std::string_view text,std::string& error){
    error.clear();const auto tree=Read(scene);const auto node=std::find_if(tree.nodes.begin(),tree.nodes.end(),[&](const auto& n){return n.entity==entity;});
    if(node==tree.nodes.end()||!node->enabled){error="accessible element is unavailable or disabled";return false;}
    if(node->role==UIAccessibilityRole::TextField){
        if(const auto* field=scene.Components().UI().TryGet<UIInputField>(entity);field&&field->readOnly){error="text field is read-only";return false;}
        if(text.size()>=UIText::MaxUtf8Bytes){error="accessible text value exceeds the field byte budget";return false;}
        std::array<char32_t,UIText::MaxUtf8Bytes> decoded{};const auto decodedText=kb::input::DecodeUtf8(text,decoded);
        if(!decodedText.wellFormed||decodedText.truncated){error="accessible text value is not valid UTF-8";return false;}
        if(!Focus(scene,entity)){error="text field could not receive focus";return false;}
        SceneUITextInputState input{entity,{decoded.data(),decodedText.codePointCount},decodedText.codePointCount,decodedText.codePointCount};
        if(!scene.UI().QueueTextInputState(std::move(input))){error="text field rejected the value";return false;}return true;
    }
    error="value editing is supported for text fields; other controls use their typed UI API";return false;
}
}
