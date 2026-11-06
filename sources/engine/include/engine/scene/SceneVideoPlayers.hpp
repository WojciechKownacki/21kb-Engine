#pragma once
#include "engine/scene/SceneEntity.hpp"
#include <cstdint>
#include <string>
#include <string_view>
namespace kb::scene {
class Scene;
struct UIVideoStatus {
    bool available=false,playing=false,paused=false,ended=false;
    std::uint64_t voiceId=0,frameRevision=0;
    double audioSeconds=0,videoSeconds=0,durationSeconds=0;
    std::uint32_t width=0,height=0;
    std::string error;
};
class SceneVideoPlayers final {
public:
    [[nodiscard]] static bool Play(Scene&,SceneEntity rawImageOwner,std::string_view videoAsset,bool loop,std::string& error);
    [[nodiscard]] static UIVideoStatus Read(const Scene&,SceneEntity);
    [[nodiscard]] static bool Pause(Scene&,SceneEntity);
    [[nodiscard]] static bool Resume(Scene&,SceneEntity);
    [[nodiscard]] static bool Seek(Scene&,SceneEntity,double seconds,std::string& error);
    static void Stop(Scene&,SceneEntity);
};
}
