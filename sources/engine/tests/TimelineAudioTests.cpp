#include "TestSupport.hpp"
#include "scene/TimelineAudioPlayback.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "engine/assets/AssetImportService.hpp"
#include "engine/assets/bake/RuntimeAssetPack.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTimelines.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/TimelineAssetIO.hpp"
#include "engine/scene/TimelineAssetLoader.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>

namespace kb::tests {

void VerifyTimelineAudioProject(const char* filename) {
#if defined(KB_AUDIO_MINIAUDIO_PLUGIN_PATH)
    using Audio = kb::audio::AudioPlayback;
    kb::project::ProjectDescriptor descriptor;
    descriptor.disableEnginePluginsByDefault = true;
    descriptor.plugins.push_back({.name="Audio.Miniaudio",.binaryPath=KB_AUDIO_MINIAUDIO_PLUGIN_PATH});
    kb::scene::Scene scene{descriptor};
    Require(Audio::ConfigureOffline(scene),"Timeline package audio provider is unavailable");
    const std::filesystem::path input{filename};
    if (input.extension()==".kbpack") {
        auto pack=std::make_shared<kb::assets::bake::RuntimeAssetPack>();
        Require(pack->Mount(input,kb::assets::bake::WindowsX64BakeTargetProfile())==kb::assets::bake::RuntimeAssetPackStatus::Success &&
            scene.Assets().Manager().MountRuntimePack(pack),"Timeline package failed to mount");
    } else Require(scene.Assets().MountProject(input) && scene.Assets().Discover()>0,"Timeline source project failed to mount");
    const auto* metadata=scene.Assets().Manager().Registry().FindByPath("/Game/Sequence.kbtimeline");
    Require(metadata!=nullptr,"Timeline sequence missing from project");
    const auto owner=scene.Entities().CreateObject({.name="Target"});
    static_cast<void>(scene.Entities().CreateObject({.name="Animated",.parent=owner}));
    scene.Components().Lights().Set(owner.Entity(),{});
    scene.Runtime().SetPlaying(true);
    const auto instance=scene.Timelines().Create(metadata->id.value,owner.Entity());
    Require(instance && scene.Timelines().Seek(instance,0.5F) && scene.Timelines().Play(instance),"Packaged Timeline could not start");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.Runtime().DrainSceneSystemErrors().empty(),"Packaged Timeline update failed");
    const auto energy=[&](bool allowEnded=false) {
        // An integer number of fixture periods makes the comparison independent
        // of the file stream's first ready buffer and the memory decoder's phase.
        auto warmup=Audio::RenderOffline(scene,4800); auto pcm=Audio::RenderOffline(scene,4800);
        const auto diagnostic="Packaged Timeline PCM render failed: frames="+std::to_string(pcm.frames)+" error="+pcm.error;
        Require(warmup.succeeded && pcm.succeeded && (!pcm.samples.empty() || (allowEnded && pcm.frames==0)),diagnostic.c_str());
        if (pcm.samples.empty()) return 0.0;
        double sum=0; for (const float value:pcm.samples) sum+=std::abs(value); return sum/pcm.samples.size();
    };
    double audible=0;
    unsigned readyReads=0;
    for (; readyReads<6 && audible<=0.001; ++readyReads) {
        if (readyReads) std::this_thread::sleep_for(std::chrono::milliseconds{1});
        audible=energy();
    }
    std::cout << "TimelinePCMReadyReads " << readyReads << '\n';
    if (audible<=0.001) {
        const auto& record=kb::scene::SceneAccess::State(scene).timelines.at(instance);
        std::cerr << "Timeline audio cursor=" << record.timeSeconds << " playing=" << record.playing
            << " scenePlaying=" << scene.Runtime().IsPlaying() << " segments=" << record.segments.size() << '\n';
        for (const auto& segment : record.segments) for (const auto& voice : segment.audioVoices)
            std::cerr << "voice=" << voice.id << " paused=" << voice.paused << " active=" << Audio::IsVoicePlaying(scene,voice.id)
                << " position=" << Audio::VoicePlaybackSeconds(scene,voice.id) << '\n';
    }
    Require(audible>0.001,"Packaged Timeline audio is silent");
    Require(scene.Timelines().Pause(instance),"Packaged Timeline pause failed");
    const auto paused=energy(); Require(paused<0.000001,"Packaged Timeline pause continued rendering audio");
    Require(scene.Timelines().SetPlayback(instance,-1,false) && scene.Timelines().Play(instance),"Packaged reverse setup failed");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.Runtime().DrainSceneSystemErrors().empty(),"Packaged reverse update failed");
    const auto reverse=energy(true); Require(reverse<0.000001,"Packaged reverse leaked forward audio");
    Require(scene.Timelines().Release(instance),"Packaged Timeline release failed");
    std::cout << "TimelinePCM " << audible << ' ' << paused << ' ' << reverse << '\n';
#else
    static_cast<void>(filename);
    Require(false,"Timeline package verification requires the audio provider");
#endif
}

void RunTimelineAudioTests(const std::filesystem::path& root) {
#if defined(KB_AUDIO_MINIAUDIO_PLUGIN_PATH)
    using Audio = kb::audio::AudioPlayback;
    using Timeline = kb::scene::TimelineAudioPlayback;
    Require(!std::filesystem::path{KB_AUDIO_MINIAUDIO_PLUGIN_PATH}.empty(),
        "Timeline audio test requires the real audio provider");
    const auto project = root / "TimelineAudio";
    std::filesystem::create_directories(project);
    const auto wave = project / "Tone.wav";
    {
        std::ofstream output{wave, std::ios::binary | std::ios::trunc};
        const auto number = [&](std::uint32_t value, unsigned bytes) {
            for (unsigned i = 0; i < bytes; ++i) output.put(static_cast<char>((value >> (i * 8U)) & 255U));
        };
        output.write("RIFF", 4); number(192036, 4); output.write("WAVEfmt ", 8);
        number(16, 4); number(1, 2); number(1, 2); number(48000, 4); number(96000, 4);
        number(2, 2); number(16, 2); output.write("data", 4); number(192000, 4);
        for (unsigned i = 0; i < 96000; ++i)
            number(static_cast<std::uint16_t>(static_cast<std::int16_t>(
                3276.0 * std::sin(6.283185307179586 * 440.0 * i / 48000.0))), 2);
        Require(output.good(), "Timeline audio PCM fixture did not write");
    }
    kb::project::ProjectDescriptor descriptor;
    descriptor.disableEnginePluginsByDefault = true;
    descriptor.plugins.push_back({.name = "Audio.Miniaudio", .binaryPath = KB_AUDIO_MINIAUDIO_PLUGIN_PATH});
    kb::scene::Scene scene{descriptor};
    Require(scene.IsModuleActive("Audio.Miniaudio") && Audio::ConfigureOffline(scene),
        "Timeline audio provider did not enter offline rendering");
    Require(scene.Assets().MountProject(project), "Timeline audio project could not mount");
    const auto imported = kb::assets::AssetImportService::ImportFiles(
        scene.Assets().Manager(), std::array{wave}, "/Game/Audio");
    Require(imported.Succeeded() && imported.items.size() == 1, "Timeline audio clip could not import");
    const auto owner = scene.Entities().CreateObject({.name = "AudioTarget"});
    kb::scene::TimelineAudioTrack track{.binding = "Target", .clipReference = "/Game/Audio/Tone.wav",
        .startSeconds = 0.25F, .endSeconds = 1.25F, .clipOffsetSeconds = 0.125F};
    kb::scene::TimelineAudioVoice voice;
    std::string error;
    Require(kb::scene::ValidateTimelineAudioTrack(track, 2.0F, error), "Valid timeline audio track was rejected");
    const auto sync = [&](float time, float rate, bool playing, bool jump) {
        return Timeline::Synchronize(scene, track, imported.items.front().id.value, owner.Entity(),
            time, rate, playing, jump, voice, error);
    };
    unsigned renderIndex = 0;
    const auto energy = [&](bool allowEnded = false) {
        auto pcm = Audio::RenderOffline(scene, 2048);
        const auto message = "Timeline audio PCM read " + std::to_string(++renderIndex) +
            " succeeded=" + std::to_string(pcm.succeeded) + " frames=" + std::to_string(pcm.frames) +
            " channels=" + std::to_string(pcm.channels) + ": " + pcm.error;
        Require(pcm.succeeded && (!pcm.samples.empty() || (allowEnded && pcm.frames == 0)), message.c_str());
        if (pcm.samples.empty()) return 0.0;
        double sum = 0;
        for (float sample : pcm.samples) sum += std::abs(sample);
        return sum / pcm.samples.size();
    };
    Require(sync(0.0F, 1.0F, true, false) && voice.id == 0, "Audio began before its interval");
    Require(sync(0.5F, 1.0F, true, true) && voice.id != 0, "Timeline clip failed to start");
    Require(Audio::RenderOffline(scene, 64).succeeded, "Timeline seek did not reach the mixer");
    Require(Audio::VoicePlaybackSeconds(scene, voice.id) >= 0.375F &&
        Audio::VoicePlaybackSeconds(scene, voice.id) < 0.39F,
        "Timeline clip offset did not include its elapsed interval");
    static_cast<void>(energy());
    Require(energy() > 0.001, "Timeline audio produced no audible PCM");
    const auto original = voice.id;
    Require(sync(0.5F, 1.0F, false, false) && voice.paused, "Timeline pause did not pause its voice");
    static_cast<void>(energy());
    Require(energy() < 0.000001, "Paused timeline audio continued producing PCM");
    Require(sync(0.75F, 1.0F, true, true) && voice.id == original && !voice.paused,
        "Timeline resume did not retain its paused voice");
    Require(Audio::RenderOffline(scene, 64).succeeded, "Timeline resume seek did not reach the mixer");
    Require(Audio::VoicePlaybackSeconds(scene, voice.id) >= 0.625F &&
        Audio::VoicePlaybackSeconds(scene, voice.id) < 0.64F,
        "Timeline resume did not synchronize the new cursor");
    Require(sync(0.5F, -1.0F, true, true) && voice.id == 0 && !Audio::IsVoicePlaying(scene, original),
        "Reverse timeline playback leaked a forward audio voice");
    Require(sync(0.5F, 1.0F, false, true) && voice.id == 0, "Silent scrub started an audio voice");
    Require(sync(0.5F, 1.0F, true, true) && voice.id != 0, "Forward playback did not restart after reverse");
    const auto restarted = voice.id;
    Require(sync(1.25F, 1.0F, true, false) && voice.id == 0 && !Audio::IsVoicePlaying(scene, restarted),
        "Audio voice survived the end of its timeline interval");

    const auto* clip = scene.Assets().Manager().Registry().Find(imported.items.front().id);
    Require(clip != nullptr, "Imported audio metadata is missing");
    track.clipReference = clip->virtualPath.generic_string();
    kb::scene::TimelineAsset asset;
    asset.durationSeconds = 2.0F;
    asset.bindings.push_back({"Target", "."});
    asset.audioTracks.push_back(track);
    const auto timelinePath = project / "Assets" / "AudioSequence.kbtimeline";
    std::filesystem::create_directories(timelinePath.parent_path());
    Require(kb::scene::TimelineAssetIO::Save(timelinePath, asset), "Audio timeline did not save");
    static_cast<void>(scene.Assets().Discover());
    const auto* metadata = scene.Assets().Manager().Registry().FindByPath("/Game/AudioSequence.kbtimeline");
    Require(metadata != nullptr, "Audio timeline was not discovered");
    kb::scene::TimelineAssetLoader loader;
    const auto dependencies = loader.DiscoverDependencies(*metadata, scene.Assets().Manager().Registry());
    const auto dependencyError = loader.ValidateDependencies(*metadata, scene.Assets().Manager().Registry());
    std::string dependencyDiagnostic = "Audio timeline dependencies: count=" + std::to_string(dependencies.size()) +
        " error=" + dependencyError.value_or("none") + " source=" + metadata->physicalPath.generic_string();
    for (const auto& registered : scene.Assets().Manager().Registry().All())
        dependencyDiagnostic += " [" + registered.virtualPath.generic_string() + ":" + registered.type + "]";
    Require(dependencies.size() == 1 && dependencies.front() == imported.items.front().id &&
        !dependencyError, dependencyDiagnostic.c_str());
    const auto loaded = kb::scene::TimelineAssetIO::Load(timelinePath);
    Require(loaded && loaded->audioTracks.size() == 1 &&
        loaded->audioTracks.front().clipReference == track.clipReference &&
        loaded->audioTracks.front().clipOffsetSeconds == track.clipOffsetSeconds,
        "Audio timeline roundtrip lost clip settings");
    const auto instance = scene.Timelines().Create(metadata->id.value, owner.Entity());
    Require(instance != 0 && scene.Timelines().Seek(instance, 0.5F) && scene.Timelines().Play(instance),
        "Audio timeline instance could not start");
    static_cast<void>(scene.Runtime().Update(0.0F));
    static_cast<void>(energy());
    Require(energy() > 0.001, "Scene timeline did not play its audio track");
    Require(scene.Timelines().Pause(instance), "Audio timeline could not pause");
    static_cast<void>(energy());
    Require(energy() < 0.000001, "Scene timeline pause left audio playing");
    Require(scene.Timelines().Seek(instance, 0.75F) && scene.Timelines().Play(instance),
        "Audio timeline could not seek and resume");
    static_cast<void>(scene.Runtime().Update(0.0F));
    static_cast<void>(energy());
    Require(energy() > 0.001, "Scene timeline did not resume audio after seeking");
    Require(scene.Timelines().Release(instance), "Audio timeline could not release");
    static_cast<void>(energy(true));
    Require(energy(true) < 0.000001, "Released timeline leaked an audio voice");
    kb::scene::TimelineAsset parent;
    parent.durationSeconds = 2.0F;
    parent.bindings.push_back({"Receiver", "."});
    parent.sequenceTracks.push_back({"/Game/AudioSequence.kbtimeline", 0.25F, 1.25F, 0.25F, 1.0F,
        {{"Target", "Receiver"}}});
    Require(kb::scene::TimelineAssetIO::Save(project / "Assets" / "NestedAudio.kbtimeline", parent),
        "Nested audio sequence did not save");
    static_cast<void>(scene.Assets().Discover());
    const auto* nestedMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/NestedAudio.kbtimeline");
    Require(nestedMetadata != nullptr, "Nested audio sequence was not discovered");
    const auto nested = scene.Timelines().Create(nestedMetadata->id.value, owner.Entity());
    Require(nested && scene.Timelines().Seek(nested, 0.5F) && scene.Timelines().Play(nested),
        "Nested audio sequence could not start");
    static_cast<void>(scene.Runtime().Update(0.0F));
    static_cast<void>(energy());
    Require(energy() > 0.001, "Nested audio sequence produced no PCM");
    Require(scene.Timelines().Pause(nested), "Nested audio sequence could not pause");
    static_cast<void>(energy());
    Require(energy() < 0.000001, "Nested audio pause left its child voice playing");
    Require(scene.Timelines().Play(nested), "Nested audio sequence could not resume");
    static_cast<void>(scene.Runtime().Update(0.0F));
    static_cast<void>(energy());
    Require(energy() > 0.001, "Nested audio sequence failed to resume");
    Require(scene.Timelines().SetPlayback(nested, -1.0F, false), "Nested audio reverse setup failed");
    static_cast<void>(energy(true));
    Require(energy(true) < 0.000001, "Nested reverse playback retained a forward audio voice");
    Require(scene.Timelines().Release(nested), "Nested audio sequence could not release");
#else
    static_cast<void>(root);
    std::cout << "Timeline audio provider checks skipped: provider target is disabled\n";
#endif
}

} // namespace kb::tests
