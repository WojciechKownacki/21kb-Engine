#pragma once
#include <string_view>
namespace kb::script {
inline constexpr std::string_view kRuntimeUIGuide=R"guide(# Runtime UI: MSDF, accessibility and video

C++ is the primary authoring interface. Lua calls the same scene backend.

## Scalable text
Set `UIText::fontRenderMode = UITextFontRenderMode::MultiChannelSignedDistanceField`
and select an imported outline font. HarfBuzz outlines feed msdfgen edge coloring
and RGB signed distances; the shared UI atlas/composite renders the RGB median.
Legacy Bitmap and SignedDistanceField retain their modes. MSDF requires the field
shader and effects within the eight-pixel atlas padding at its 64-pixel design size;
unsupported requests report a text refusal, without silently switching formats.
The existing component CLI property `fontRenderMode=2` persists this mode.

## Semantic reading and focus
Add `UIAccessibility` with `SetUIAccessibilityLabel` / `SetUIAccessibilityHint`,
an optional explicit role and hidden flag. Labels persist through scene save/cook.
`SceneAccessibility::Read(scene)` returns the current laid-out semantic tree,
parent links, screen bounds, focus, enabled state and typed values. Hidden ancestors,
modal occlusion and password values are respected. Rich markup needs an explicit
semantic label. `Focus`, `Invoke` and `SetValue` use existing UI interaction queues.
SetValue supports text fields; other values use their typed component API.

```cpp
#include "engine/scene/SceneAccessibility.hpp"
std::string error;
if (kb::scene::SceneAccessibility::Focus(scene, button)) {
    auto request = kb::scene::SceneAccessibility::SpeakFocused(scene, error);
    // Poll ReadSpeech after UI updates. WaitForSpeech is for bounded tools/tests.
}
kb::scene::SceneAccessibility::SetScreenReaderEnabled(scene, true);
```
Windows SAPI synthesizes plain text asynchronously; the resulting PCM plays through
existing AudioPlayback. One synthesis job and one latest result are retained. Labels
are limited to 2048 speech bytes and PCM to 8 MiB. This built-in reader is opt-in;
Windows players attach a UI Automation fragment provider to the real HWND. Hosts can
use `SceneAccessibilityWindow::Attach(scene, nativeWindow, error)` for one full-client
UI tree, and `Detach` before releasing the window. Immutable snapshots serve COM
queries; focus/invoke/toggle actions return to the scene owner thread. The provider
exposes roles, names, parents, screen bounds, live non-password values and focus.
UIA ValuePattern is explicitly read-only; keyboard input and typed C++/Lua APIs edit
fields. Password values are never exposed. Built-in speech is distinct from UIA:
real HWND/UIA client qualification does not claim Narrator listening behavior.
TalkBack and VoiceOver bridges are not provided or host-qualified.
Lua: `Accessibility.Node(index)` returns count plus a node (zero based),
`Focus(entity)`, `Invoke(entity)`, `SetValue(entity,value)`, `Speak()`,
`Speech(id)`, `ScreenReader(enabled)`. Inspect multiple outputs with CallFunction.

## Video in an existing raw image
Import a video with the normal `import` command. Bind its `/Game/...21kb` asset
from a persisted C++/Lua script and declare its normal `@asset` dependency.

```cpp
#include "engine/scene/SceneVideoPlayers.hpp"
if (!kb::scene::SceneVideoPlayers::Play(scene, imageEntity,
        "/Game/Video/Clip.21kb", true, error)) { /* report error */ }
auto state = kb::scene::SceneVideoPlayers::Read(scene, imageEntity);
```
The owner requires UIRawImage. Windows Media Foundation (Windows) or the NDK media
codecs (Android) decode the imported payload, including from a mounted package.
PCM uses the normal mixer/voice; its cursor is the video clock. Decoded RGB32 frames update one GPU texture per active image. Pause,
resume, seek and loop use the same voice, and Stop restores the previous raw image.
Seek operates on a live voice; call Play again after natural end. The rendered-frame
audio clock classifies natural EOF even when a UI frame misses the final voice cursor.
Lua: `UIVideo.Play(entity,asset,loop)`, `Info(entity)`, `Pause`, `Resume`,
`Seek(entity,seconds)`, `Stop`. Info exposes audio/video timestamps and frame revision.

Limits: Windows and Android host codecs, unrotated SDR, audio track required, 60 seconds,
64 MiB source, 1920x1080, four simultaneous players, 64 decode frames per update.
Unsupported hosts/codecs, missing Media Feature Pack and changing formats report errors.
Author source assets and binding scripts; transient decoder/speech state is not a
runtime checkpoint format. Windows playback does not qualify the Android decoder.
)guide";
}
