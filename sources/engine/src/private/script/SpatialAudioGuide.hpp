#pragma once
#include <string_view>
namespace kb::script {
inline constexpr std::string_view kSpatialAudioGuide=R"guide(# HRTF and first-order AmbiX

The miniaudio provider can render mono sources through a real HRTF convolution,
or decode first-order Ambisonics to binaural stereo using Steam Audio 4.8.1.
Both use the existing voice pool, mixer buses, volume/mute/pan/pitch/loop,
stop/pause/resume/seek and offline PCM APIs. Existing panning remains mode zero.

```cpp
#include "engine/audio/SpatialAudioPlayback.hpp"
kb::audio::AudioPlayDesc desc;
desc.clipAssetId = monoClipId;
desc.position = {-1,0,0};
desc.dopplerFactor = 0; // not supported by the new spatial DSP modes
// Ordinary miniaudio attenuation models are retained.
auto voice = kb::audio::SpatialAudioPlayback::Play(scene, desc,
    kb::audio::SpatialAudioMode::Binaural);
if (!voice.Succeeded()) throw std::runtime_error(voice.error);
// A world-fixed first-order soundfield is ACN/SN3D, four channels W,Y,Z,X:
desc.clipAssetId = ambixClipId;
desc.position = {};
desc.attenuationModel = kb::audio::AudioAttenuationModel::None;
auto field = kb::audio::SpatialAudioPlayback::Play(scene, desc,
    kb::audio::SpatialAudioMode::AmbisonicsFirstOrder);
```

```lua
-- @asset /Game/Audio/Point.wav
-- @asset /Game/Audio/Field.wav
local point = assert(Audio.Play("/Game/Audio/Point.wav", {
  entity=World.FindByName("Emitter"), spatialization=1, dopplerFactor=0}))
-- The field's caller/entity is at the world origin, with no attached owner:
local field = assert(Audio.Play("/Game/Audio/Field.wav", {
  entity=self.entity, spatialization=2, dopplerFactor=0, attenuationModel=0}))
```

Use existing audio assets/codecs. HRTF requires exactly one decoded input channel;
AmbiX requires four decoded channels in ACN order with SN3D normalization. The
caller explicitly selects the format; a generic quad-speaker WAV is not AmbiX.
SN3D converts to the SDK's N3D basis before decoding. The world-fixed field rotates
with the primary listener. Its authored position/velocity must be zero, with no
owner, no distance attenuation and spatialBlend=1. HRTF follows point-source
position and listener orientation; bilinear directional interpolation and the
existing spatialBlend value control the filter. Invalid channel counts, modes
and unsupported combinations fail before a candidate steals another voice.

Persist the clips and the scene's binding script, declaring dynamic asset
references in Lua or native descriptors. C++ uses the same backend capability
through SpatialAudioPlayback, preserving the AudioPlayDesc layout. A provider
without that capability rejects the request. There is no second playback engine.

Processing uses fixed 512-frame DSP blocks buffered for arbitrary miniaudio reads,
stereo float output, and the engine sample rate. Seek resets convolution state;
looping preserves filter continuity; stop releases the decoder and effect.
Distance gain uses the bundled miniaudio attenuation functions. Doppler is
explicitly unsupported in the new modes and must be zero. One primary engine
listener is supported. This does not implement acoustic simulation or reflections.

Windows x64 is the initial SDK/PCM qualification. Other builds retain panning and
explicitly reject these DSP modes until their SDK integration is provided. The
Windows exporter stages phonon.dll and its complete license notices/SBOM with
the existing provider. CPU HRTF/AmbiX does not require the SDK's optional GPU
libraries. The SDK archive is pinned by SHA256 in EnableSteamAudio.cmake.

SDK documentation: https://valvesoftware.github.io/steam-audio/doc/capi/guide.html
Licenses and SDK third-party notices: third_party/licenses/steamaudio.
)guide";
}
