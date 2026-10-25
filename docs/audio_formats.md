# Audio clip formats

The shared import and runtime catalog accepts WAV, FLAC, MP3 and Ogg Vorbis
(`.ogg`), with case-insensitive extensions. Ogg support uses the already bundled
stb_vorbis decoder inside the miniaudio implementation. Decoder selection is
explicit, so renaming another codec to `.ogg` does not make it supported.

Direct file playback streams through the resource manager. Imported and cooked
clips retain their encoded payload and initialize the decoder from memory.
Ogg readiness checks validate the first Vorbis page and a complete final page
with end-of-stream and matching stream serial, including page CRCs. Inspection
uses bounded page buffers and a 64 KiB tail window, followed by the existing
256-frame native decoder probe. This does not scan or fully decode a whole
clip at load time. The supported Ogg profile is a single logical Vorbis stream;
chained streams, Ogg Opus and other Ogg codecs are outside this profile.

`kb_audio_runtime_tests formats`, `import-memory` and `package-memory` cover
actual decoding and playback. Malformed fixtures include invalid signatures,
truncation, damaged first/final page checksums and trailing data; refusal must
preserve active backend resources. `kb_engine_tests assets` checks the shared
catalog and loader. `kb_renderer_tests runtime-pack-validation` checks direct
and imported audio payloads inside real packs.

`sources/game/tests/web/run_game_audio_ogg_tests.py` uses the full browser fixture
arguments. It imports the generated Ogg sine, cooks a normal package and
requires actual native PCM after trusted activation. It checks gain, mute,
440/880 Hz pitch, disable/re-enable and native UI hardware rendering. See
[browser audio](browser_audio.md) for output bounds and platform limitations.

The bundled decoder's original source and license remain intact. Its selected
MIT license and Sean Barrett attribution are included in the distribution
manifest and generated game notices.
