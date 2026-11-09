#pragma once
#include "engine/scene/SceneAccessibility.hpp"
#include "engine/scene/SceneAccessibilityWindow.hpp"
#include "engine/scene/SceneVideoPlayers.hpp"
#include "engine/scene/SceneUIComponents.hpp"
#include "engine/ui/DynamicImageAsset.hpp"
#include <future>
#include <memory>
namespace kb::audio {struct AudioClipAsset;}
namespace kb::scene {
class VideoDecoder;
class UIAccessibilityWindowBridge;
struct UIVideoRecord {
    SceneEntity owner{};
    std::uint64_t clipId=0,imageId=0;
    UIRawImage previousImage{};
    UIVideoStatus status;
    bool loop=false;
    std::shared_ptr<VideoDecoder> decoder;
};
struct SceneUIMediaState {
    // Initialized by Scene's host constructor. SDK copies route long-lived work
    // here so native module reload cannot leave decoder/job deleters in an old DLL.
    decltype(&SceneAccessibilityWindow::Attach) attachWindow=nullptr;
    std::shared_ptr<UIAccessibilityWindowBridge> windowBridge;
    decltype(&SceneVideoPlayers::Play) play=nullptr;
    decltype(&SceneVideoPlayers::Seek) seek=nullptr;
    decltype(&SceneAccessibility::SpeakFocused) speak=nullptr;
    decltype(&SceneAccessibility::SetScreenReaderEnabled) enableReader=nullptr;
    decltype(&SceneAccessibility::WaitForSpeech) waitSpeech=nullptr;
    void (*update)(Scene&)=nullptr;
    void (*updateReader)(Scene&)=nullptr;
    bool readerEnabled=false,discardSpeech=false;
    SceneEntity announced{};
    std::uint64_t nextSpeech=1,speechVoice=0;
    std::future<UIAccessibilitySpeech> speechJob;
    UIAccessibilitySpeech speech;
    std::vector<UIVideoRecord> videos;
};
SceneUIMediaState& EnsureUIMedia(Scene&);
void InitializeUIMedia(Scene&);
void ShutdownUIMedia(Scene&);
void UpdateUIMedia(Scene&);
void UpdateUIReader(Scene&);
void PublishUIAccessibilityWindow(Scene&);
}
