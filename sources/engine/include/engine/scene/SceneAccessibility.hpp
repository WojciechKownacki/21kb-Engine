#pragma once
#include "engine/scene/SceneEntity.hpp"
#include "engine/ui/interaction/UIAccessibility.hpp"
#include "engine/math/EngineMath.hpp"
#include <memory>
#include <string>
#include <vector>
namespace kb::scene {
class Scene;
struct UIAccessibilityNode {
    SceneEntity entity{},parent{};
    UIAccessibilityRole role=UIAccessibilityRole::Group;
    std::string name,value,hint;
    kb::math::Rect bounds{};
    bool enabled=true,focusable=false,focused=false,password=false,checked=false;
    bool hasRange=false;
    double minimum=0,maximum=1,numericValue=0;
};
struct UIAccessibilityTree {
    std::vector<UIAccessibilityNode> nodes;
    SceneEntity focused{};
    std::string error;
};
struct UIAccessibilitySpeech {
    std::uint64_t id=0,voiceId=0;
    bool ready=false,succeeded=false;
    std::string text,error;
    std::uint32_t sampleRate=0,channels=0;
    std::shared_ptr<const std::vector<float>> samples;
};
class SceneAccessibility final {
public:
    [[nodiscard]] static UIAccessibilityTree Read(const Scene&);
    [[nodiscard]] static std::string Describe(const UIAccessibilityNode&);
    [[nodiscard]] static bool Focus(Scene&,SceneEntity);
    [[nodiscard]] static bool Invoke(Scene&,SceneEntity);
    [[nodiscard]] static bool SetValue(Scene&,SceneEntity,std::string_view,std::string& error);
    [[nodiscard]] static std::uint64_t SpeakFocused(Scene&,std::string& error);
    static void SetScreenReaderEnabled(Scene&,bool);
    [[nodiscard]] static UIAccessibilitySpeech ReadSpeech(const Scene&,std::uint64_t);
    [[nodiscard]] static bool WaitForSpeech(Scene&,std::uint64_t,std::uint32_t timeoutMilliseconds);
};
}
