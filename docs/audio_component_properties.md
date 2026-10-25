# Audio component authoring

Audio Source and Audio Listener expose their authored properties through the
shared scene property API, Lua and Visual Graph property operations, and CLI
component commands. Both canonical names (`AudioSource`, `AudioListener`) and
display names (`Audio Source`, `Audio Listener`) are accepted.

For an existing project and WAV file:

```sh
kb_cli import --project Sample --destination /Game/Audio Tone.wav
kb_cli entity add --project Sample --create "Audio Source" --name Sound
kb_cli component set --project Sample --entity Sound --type "Audio Source" \
  --set clipAssetId=/Game/Audio/Tone.21kb --set spatial=false \
  --set loop=true --set volume=0.25
kb_cli entity add --project Sample --create "Audio Listener" --name Listener
kb_cli component set --project Sample --entity Listener --type AudioListener \
  --set priority=7 --set localUser=0
```

Clip assignment accepts an existing audio asset by project path or asset id;
zero clears it. Missing and non-audio assets are rejected. The same asset-kind
policy is used by the editor's clip assignment. Playback requests remain
separate operations such as `Audio.PlaySource`, `Audio.PauseSource`,
`Audio.ResumeSource` and `Audio.StopSource`.

| Source properties | Valid values |
| --- | --- |
| `clipAssetId` | Audio asset reference or zero |
| `volume`, `rolloff`, `dopplerFactor` | Finite values greater than or equal to zero |
| `pitch` | Finite value at least 0.01 |
| `pan` | Finite value from -1 to 1 |
| `spatialBlend` | Finite value from 0 to 1 |
| `minDistance`, `maxDistance` | Finite minimum at least 0.01; maximum at least the minimum |
| `attenuationModel` | `None`, `Inverse`, `Linear`, `Exponential` |
| `loop`, `spatial`, `autoplay`, `enabled`, `mute` | Boolean values |
| `outputBus` | Empty for the implicit master, or a valid mixer name of at most 63 bytes |

| Listener properties | Valid values |
| --- | --- |
| `priority` | Signed 32-bit integer |
| `localUser` | Unsigned 32-bit local user id |
| `primary`, `enabled` | Boolean values |

Writes validate a candidate before changing the live source, using the same
persistable-value rules as editor authoring. Invalid values preserve the
previous property. A failed CLI component batch does not save earlier settings
from that batch.

`kb_engine_tests script-api` checks the catalog and component property bindings,
including audio ranges, asset refusal, routing tokens, integer widths and all
786 registered property entries. The broader `script` suite retains those
checks as well. `run_game_audio_property_tests.py` authors its own WAV, source
and listener through the CLI, cooks the package and reads authored and mutated
properties in the actual browser runtime. It also requires a hardware-rendered
native UI capture. This property scenario does not qualify audio output,
browser activation, interruptions or physical device routing.
