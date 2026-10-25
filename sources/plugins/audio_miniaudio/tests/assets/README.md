`tone.ogg` is an engine test fixture generated from an analytic one-second
mono 440 Hz sine at 48 kHz, with peak amplitude 3276/32768. It was encoded as
Ogg Vorbis using libsndfile 1.2.2 through soundfile 0.14.0. The generated
waveform and fixture use the repository's MIT license. No recording was used.

The fixture has 48000 decoded frames and 5761 encoded bytes. Native tests use
it for direct, imported-memory and packaged-memory playback, plus local
malformed-container cases. Browser tests measure its real mixer output through
normal import, cook and runtime loading.

`seek.ogg` uses the same generated waveform amplitude, sample rate, duration,
encoder and MIT license, with 440 Hz in its first half and 880 Hz in its second.
The distinct halves let transport tests verify actual PCM after forward and
backward seeking, followed by looping across the end of the clip. It contains
48000 decoded frames and 5737 encoded bytes.
