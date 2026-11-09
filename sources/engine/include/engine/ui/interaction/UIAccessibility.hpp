#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
namespace kb::scene {
enum class UIAccessibilityRole : std::uint8_t {
    Automatic, Group, Text, Button, CheckBox, Slider, TextField, Image, Video, ProgressBar, List
};
struct UIAccessibility {
    static constexpr std::string_view StableId="kb21.ui.accessibility";
    static constexpr std::uint32_t SchemaVersion=1;
    UIAccessibilityRole role=UIAccessibilityRole::Automatic;
    std::array<char,256> label{},hint{};
    bool hidden=false;
};
inline std::string_view UIAccessibilityLabel(const UIAccessibility& a) noexcept {return {a.label.data(),static_cast<std::size_t>(std::find(a.label.begin(),a.label.end(),'\0')-a.label.begin())};}
inline std::string_view UIAccessibilityHint(const UIAccessibility& a) noexcept {return {a.hint.data(),static_cast<std::size_t>(std::find(a.hint.begin(),a.hint.end(),'\0')-a.hint.begin())};}
inline bool SetUIAccessibilityLabel(UIAccessibility& a,std::string_view value) noexcept {if(value.size()>=a.label.size()||value.find('\0')!=std::string_view::npos)return false;a.label.fill('\0');std::copy(value.begin(),value.end(),a.label.begin());return true;}
inline bool SetUIAccessibilityHint(UIAccessibility& a,std::string_view value) noexcept {if(value.size()>=a.hint.size()||value.find('\0')!=std::string_view::npos)return false;a.hint.fill('\0');std::copy(value.begin(),value.end(),a.hint.begin());return true;}
}
