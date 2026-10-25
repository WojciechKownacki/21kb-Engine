# Audio mixer effects

Audio mixer buses retain ordered
low-pass, high-pass, peaking equalizer, delay, reverb, compressor and limiter
stages, with stable effect IDs and independent parameters. The miniaudio provider builds actual native graph nodes with the
vendored miniaudio API; the dependency's existing license and notices remain
in its source tree. Repeated stages keep separate parameters and state.
Bypassed stages retain validated settings and create no processing node.

The primitive accepts 1–8 channels, sample rates from 8 to 192 kHz, at most
16 stages, low/high-pass orders 1–8, and finite cutoff/center frequencies from 1 Hz
through 49% of the sample rate. Peaking EQ is second order, with gain from -24 to
+24 dB and Q from 0.1 through 16. Gain and Q apply only to EQ; other kinds refuse
nondefault values for these fields. Each stage also checks the poles of its actual
quantized `float32` coefficients. Parameter combinations that become unstable
after conversion are refused, including an eighth-order 1 Hz filter at 192 kHz.
EQ boost requires input headroom; floating-point output can exceed 1.0.

Delay uses a separate dry path and a delayed feedback path with independent
state per channel. Its parameters are finite time greater than zero through
2000 ms, feedback from 0 through 0.95, and wet/dry gains from 0 through 1.
Time rounds to the nearest sample, with a minimum of one sample. Defaults are
250 ms, zero feedback, wet 0.5 and dry 1. The recurrence is
`echo[n] = input[n-D] + feedback * echo[n-D]`, with zero initial state, and
`output[n] = dry * input[n] + wet * echo[n]`. Feedback never moves the first tap
to time zero. Delay tails continue after the input reaches its end.
Unused parameters must retain their defaults for each effect kind.

Active delay lines and reverb state share a 32 MiB budget per chain, checked before construction.
Delay's charge is `roundedFrames * channels * sizeof(float)`; reverb charges
its complete fixed state. Bypassed stages allocate neither. Constructing a replacement can temporarily retain both
old and candidate state, so their combined bound is 64 MiB plus node/container
overhead. Delay is not a limiter: its worst-case gain for a bounded input is
`dry + wet / (1 - feedback)`, multiplied across repeated stages. Callers must
reserve input headroom, including any EQ boost.

Reverb uses the existing Verblib implementation in miniaudio's extra reverb
node. Its MIT No Attribution/public-domain licensing and authorship notices
remain in the dependency. Room size, damping, stereo width and wet/dry gains
are finite values from 0 through 1; defaults are 0.5, 0.25, 1, 0.5 and 1.
The wrapper explicitly applies every parameter after vendor node initialization
and normalizes the vendor's wet/dry scaling so dry 1 means direct gain 1.
The wet path uses the vendor's default mono-summed input and stereo diffusion.
Room size changes decay; it does not specify a physical room dimension.
Freeze mode is not exposed. Reverb requires mono/stereo and 22.05–192 kHz;
an unsupported active configuration is refused before allocating or changing
the old graph. A bypassed reverb retains its settings on any supported chain
format. The configured vendor maximum is five times 44.1 kHz, with one shared
state layout for its implementation and callers; the engine still caps at
192 kHz. The large fixed state is heap-owned only by active reverb stages.

Compressor and limiter are engine-owned nodes in the same graph. Their peak
detector uses the largest absolute sample across all channels in each frame
and applies one gain to every channel, preserving their relative levels.
The compressor uses a hard knee: above the threshold, each input dB above it
becomes `1 / ratio` output dB above it. Threshold is -60 through 0 dBFS, ratio
1 through 20, attack 0 through 200 ms, release 1 through 5000 ms and makeup
0 through 24 dB. Defaults are -12 dBFS, 4:1, 10 ms, 100 ms and 0 dB.
Gain approaches its target exponentially in the linear amplitude domain;
the selected time is one time constant. Zero attack applies reduction
immediately. Makeup applies after this gain. Attack transients and makeup
can exceed 1.0, so callers reserve headroom or place a limiter afterward.

Limiter ceiling is -24 through 0 dBFS, default -1 dBFS. It applies immediate
gain reduction on every frame and exponential recovery with the same release
range, default 100 ms. The ceiling bounds discrete PCM sample peaks within
float rounding; inter-sample peaks and lookahead are outside its contract.
Both nodes accept the full chain channel/sample-rate range and allocate no
memory during processing. A non-finite input sample silences its entire linked
frame and leaves the state usable for subsequent finite input. Compressor
arithmetic uses double precision and saturates at the finite float range on
overflow; this does not cap normal audio at 1.0.

Invalid parameters and failed construction preserve the
previous graph and signal. Partial node initialization also releases filter
storage when the graph-node allocation fails. Allocator callbacks are copied;
their user data must remain alive until the nodes are reset.

The graph and attached source/target outlive the chain. Configuration and graph
attachment run on the owner thread outside concurrent PCM reads. Successful
construction builds unattached nodes; callers attach the new chain before
reading it. Reset releases owned filter nodes. An empty or fully bypassed chain
attaches the source directly to the target.

`kb_audio_filter_tests` reads real generated sine waves through the node graph.
Sixty-seven response cases check empty/bypassed, repeated and mixed stages, both
orders of a mixed pair, mono/stereo/eight-channel output, sample-rate boundaries
and sixteen active stages. Four 4800-frame blocks per case must match an
independent Butterworth or peaking transfer-function response within 1%. EQ cases
cover boost/cut, Q boundaries, repeated boosts, inverse boost/cut pairs and mixed
filter/EQ chains. Input amplitude reserves headroom for each expected gain.
Additional tests reject
invalid settings and attachments, exercise orders 1–8, and verify that rejected
edits preserve the playing signal. Twenty-four boundary combinations must stay
bounded or refuse unstable coefficients while keeping the previous graph. The
known 1 Hz instability also compares 32 PCM blocks against an independently
running unchanged graph and requires zero leaked candidate allocations.
Every observed allocation in a three-stage
replacement is separately refused with both a low-pass and EQ chain already
playing; the old chain must keep its exact live
allocation count and requested bytes, then release them all on reset.

`kb_audio_delay_tests` reads actual graph PCM and compares every sample to an
independent double-precision time-offset recurrence within an absolute 2e-6.
Twenty-five impulse cases cover tails after a one-frame source ends, separate
channels, wet-only and silent output, bypass, repeated delays, sixteen active
stages, feedback boundaries, 8–192 kHz and one through eight channels. Five read
block sizes exercise internal buffering. Six first-tap cases check rounding.
Eight continuous step/tone cases also check mixed low/high-pass, EQ and delay
chains in both orders against independently calculated filter recurrences.
Twenty-four invalid configurations and an over-budget chain must fail before
allocating or changing the old signal. Every one of ten observed vendor
allocation positions in a mixed replacement is refused with either a filter
or a delay already playing; its PCM, live allocations and requested bytes must
remain unchanged and reset must release all owned allocations.

`kb_audio_reverb_tests` reads thirteen real graph impulse responses, including
the tail after a one-frame source ends, dry-gain normalization, width zero,
mono/stereo, sample-rate boundaries, repeated differing effects, sixteen active
stages, bypass and mixed delay/reverb in both orders. PCM must match direct
Verblib processing within an absolute 2e-6 across different read blocks. This
control uses the same vendor kernel; separate properties require nonzero late
tail energy, stereo diffusion, identical centered wet channels at width zero,
a longer tail with larger room size and exact silence from silent input.
Twenty-four invalid parameter sets, unsupported active formats and combined
over-budget delay/reverb state must fail before allocation and preserve the
old PCM. All eight observed allocations in a replacement are separately
refused while reverb plays; reset must leave zero owned allocations.

`kb_audio_dynamics_tests` checks 136 static response cases against a dB-domain
reference, including sample-rate/channel boundaries, parameter extremes,
sixteen stages, bypass and repeated differing compressors. Twenty-six envelope
cases compare actual PCM to closed-form exponential attack/release within an
absolute 2e-6, across four read block sizes and 8–192 kHz. Switching compressor
and limiter order must produce the separately predicted nonlinear results.
Abrupt level changes, maximum finite float input, non-finite input and EOF
check the limiter ceiling, finite saturation, recovery and silence. Thirty-two
invalid configurations for each playing kind must fail before allocation;
all six vendor allocation positions in a mixed replacement are separately
refused for each kind, preserving exact old PCM and storage until reset.

The tests pass in x64 MSVC Release and Debug ASan, and in a 32-bit WebAssembly
ASan/UBSan build of the same sources. The WebAssembly check uses the real
graph/filter/delay/reverb/dynamics/waveform implementation with device I/O, decoders and threading
omitted. One stereo second-order low/high-pass stage requested 7760 bytes from the vendor
allocator on x64 and 7744 bytes in WebAssembly. A stereo peaking EQ stage requested
7696 bytes on both tested architectures. A stereo 5 ms delay at 48 kHz requested
28800 bytes on both tested architectures, including its split/merge
graph nodes and 1920-byte delay line. One reverb stage requests 517800 bytes on
x64 (510120 fixed-state bytes) or 517568 bytes in WebAssembly (509888 state
bytes), independent of its active channel count and sample rate. A compressor
or limiter requests 7680 vendor graph bytes on both architectures; its small
scalar state lives in its C++ node. Native ASan
instruments engine-owned wrappers and tests; the WebAssembly build also
instruments the vendor implementations. These figures exclude C++ node
objects, container storage, source/output buffers and sanitizer overhead.

Mixer assets with effects use `kbmixer 2`; routing-only saves retain version 1.
The loader accepts legacy routing assets and versions 1 and 2, rejects future
versions and refuses effect records in version 1. Each effect follows its bus
declaration and retains every applicable parameter, bypass and position.
Saving validates the complete candidate before atomically replacing the file.
Duplicate effect IDs, more than sixteen effects per bus, unknown kinds,
inapplicable fields, non-finite values and invalid ranges are errors.
The module ABI is 13; rebuild providers against the current headers.

CLI authoring uses the shared `AudioMixerEffectEditing` service:

```text
kb_cli audio-mixer new --project Sample --name Main --bus Effects
kb_cli audio-mixer set --project Sample --mixer /Game/Audio/Main.kbmixer --effect-bus Effects --add-effect Tone=equalizer --set-effect Tone.cutoffHz=440 --set-effect Tone.gainDb=6
kb_cli audio-mixer set --project Sample --mixer /Game/Audio/Main.kbmixer --effect-bus Effects --move-effect Tone=0
kb_cli audio-mixer show --project Sample --mixer /Game/Audio/Main.kbmixer --json
kb_cli audio-mixer assign --project Sample --mixer /Game/Audio/Main.kbmixer
kb_cli run --project Sample --frames 4 --report audio --json --pcm-frames 4096
```

`--remove-effect Id` removes a stage. Repeating `--add-effect` creates separate
stages, including repeated kinds. `--set-effect Id.bypass=true` retains its
parameters. Move indexes start at zero. New/set accept `--dry-run` without
creating directories or replacing files; new/set/show emit the versioned
`21kb.audio-mixer/v1` JSON schema with all applicable parameter names.

Runtime DSP-only changes preserve playing voices and bus identities. Candidate
construction happens outside PCM read exclusion; a short mutex protects the
actual attachment swap. Unreachable old nodes are then destroyed. This is a
synchronized control update, with no hard real-time latency guarantee. A failed
edit retains the prior graph, marks its settings stale and exposes an error;
retries are limited to once per second. An initially unsupported chain rejects
the named route instead of silently playing dry. Bus gain, snapshots and mute
apply after all its effects, including delay and reverb tails. A successful
replacement resets that bus's effect history; it does not crossfade old tails.

`AudioPlayback::ConfigureOffline` explicitly replaces device playback with a
48 kHz stereo engine. `RenderOffline` returns actual finite PCM, its format and
frame count, and refuses reads outside 1–384000 frames. `DspReport` only observes
the installed settings. Normal device reinitialization leaves offline mode.
The Lua equivalents are `Audio.ConfigureOffline()`, `Audio.RenderOffline(frames)`
and `Audio.DspState(bus)`. Configure before starting one-shot voices; changing
the device mode stops existing one-shots and rebuilds component-owned sources.
CLI audio reports require a provider and actual complete PCM, reject stale
settings and report peak, RMS, frames, applied effects and mixing wall time.
They cap probes at 600 simulation ticks and 1920000 total PCM frames.

`kb_audio_runtime_tests mixer-dsp` verifies all seven effect kinds through actual
imported WAV playback, saved/reloaded mixers and public PCM readback. It checks
an independent filter/gain response, nonlinear limiter order, separate repeated
stages, bypass, failed-edit signal preservation and post-effect tail mute.
`mixer-dsp-concurrent` checks 64 saved edits while PCM is read on another thread,
and another 64 through the production device callback using the native null
device backend. These tests pass in native Release and ASan. A null device is
a callback concurrency test, with no physical playback qualification.

`kb_game_audio_dsp_package` runs CLI effect edits and real PCM assertions, cooks
the project, then checks effects and mute/unmute inside a hidden native player
using packaged assets and the actual provider. A separate incorrect script
assertion must fail the process. Windows native package execution is checked;
audible device comparison, other packaged platforms, memory/latency workload
qualification and hosted CI remain pending. The thin
WebAssembly kernel checks above do not establish browser mixer playback.

The mixer Inspector exposes all seven kinds on each bus, with separate controls
for every applicable parameter, bypass, removal and movement through the chain.
Repeated kinds get distinct stable IDs. Inspector edits call the same typed
editing service as CLI commands, then use the asset command history for atomic
save, cache invalidation and Undo/Redo. Neither editing nor undoing a mixer
marks the scene dirty. Pending text edits retain the asset, bus, effect and
field identity; a reorder or external edit cannot redirect an old buffer to a
different effect. External effect changes also prevent conflicting Undo.

`kb_editor_tests audio-mixer-inspector` checks real row geometry, hit testing and
GDI painting without opening windows. It exercises 27 parameter edits, fifty
saved operations with exact Undo/Redo, distinct repeated effects, invalid input,
stale edits, cache reload and all 32 translated catalogs. The normal editor
event dispatch and scene command objects also compile against these controls.
This does not establish a linked GUI application or audible preview result.

`kb_editor_audio_dsp_package` extends the packaged test with
`--editor-tests <kb_editor_tests>`: the actual saved Inspector asset becomes the
project mixer, CLI diagnostics must report its installed parameters, and the
cooked player checks the fourth-order 200 Hz low-pass response to a 440 Hz tone
against an independent bilinear Butterworth calculation within 2% after warmup.
The remaining six authored stages retain their bypassed parameters. Mute,
unmute and an intentionally incorrect assertion are checked in the package.
This workflow runs in native Windows Release; full editor ASan execution,
meters, dedicated mixer panels and runtime preview qualification remain pending.

`kb_audio_runtime_tests mixer-dsp-workload` (CTest `kb_audio_mixer_workload`)
provides a short repeatable performance workload. It reads 393216 actual stereo
frames per case at 48 kHz: one bus with eight voices, then eight buses with
64 voices and all seven active effect kinds on each bus. Eight different clip
IDs respect the per-clip voice limit; every requested voice and effect must
remain active, PCM must be finite and non-silent, and native routing/decoder
resources must reach zero on shutdown.

The report separates mixing wall time from owner-thread and process CPU time
measured with native Windows counters. CPU time includes the same PCM
verification work in both cases; CPU accounting can be coarser than the wall
timer. Twelve batches each contain 32 reads of 1024
frames; their median and maximum mixing times are batch totals. Process RSS and
private committed bytes are sampled before construction, while active and
after scene destruction. These are sampled process values, not allocator
requests or a measured memory peak. Unavailable counters say `unavailable`.
This offline workload is a baseline for changes; it does not establish device
callback deadlines, audible latency, real-time scheduling or a performance
budget on other machines. Sanitized runs check correctness, not release speed.

The private `MiniaudioMeterNode` building block observes a native graph through
a passthrough node. `kb_audio_meter_tests` compares actual graph PCM bit for bit
with a graph without a tap, including a single-frame source at EOF, mono,
stereo and eight channels. Capture is explicit and disabled capture does not
accumulate history. Peak and RMS describe the last processed block per channel;
peak hold, clipping sample counts (`abs(sample) > 1.0`) and non-finite sample
counts accumulate until reset. Non-finite samples contribute zero energy to
RMS, whose denominator remains the complete block frame count. Observation
never repairs or changes the audio. Counters saturate rather than wrap.

One PCM writer publishes coherent snapshots using lock-free atomics. A reader
makes at most eight attempts and reports busy without replacing its previous
snapshot. Configuration, reset and node destruction require exclusion from
the PCM writer; ordinary snapshots do not. Tests cover allocation failure,
shutdown, concurrent native reads and Wasm ASan/UBSan graph execution.

`AudioMetering::Configure(scene, true)` enables actual production meters;
`Reset` clears history, and `Configure(scene, false)` disables and clears it.
These operations exclude PCM reads through the existing graph lock and preserve
voices and DSP history. Capture is off by default and resets on provider
shutdown or reinitialization. `Report` only observes installed state. Named bus
taps run after that bus's effects and fader, before its parent's processing;
the master observes the complete output returned by `ReadPcm`, including the
native device callback. Bus processing quanta and presented master blocks can
differ because of graph buffering. Each reading declares its own block and
cumulative frame counts. Last-block peak/RMS must not be mistaken for a whole
caller-requested PCM buffer's measurement. Meter state is transient and is not
serialized into mixer assets.

The optional `IAudioMeteringBackend` capability leaves the existing playback
interface and scene layout unchanged. Missing capabilities are unsupported;
an uninitialized provider or a busy snapshot does not fabricate readings.
Native Windows tests load an actual current provider DLL through the public
facade and also verify that a previously built ABI13 provider still renders DSP
PCM while refusing an explicit meter probe.

`run --project Sample --report audio-meters --json --frames 4 --pcm-frames 4096`
requires actual offline PCM and capture, and returns `21kb.audio-meters/v1`.
It retains DSP diagnostics and adds `meters.master` and named `meters.buses`,
with per-channel measurements. Cumulative 64-bit counters use decimal strings.
The existing `audio` report continues to work without the optional capability.
The packaged DSP test verifies the CLI's loaded DLL meter output independently
against PCM. Native runtime tests additionally cover clipping in a child while
its parent attenuates, parent mute, reset preserving transport, failed DSP edits
preserving the old signal, and concurrent reset/read during 64 DSP changes on
an offline reader and an actual null-device callback.

Three Release repetitions alternate capture order on the same workload. Median
mixing wall totals per 393216 frames were 17.37 ms disabled / 19.04 ms enabled
for one bus and eight voices, and 316.35 / 326.99 ms for eight buses, 64 voices
and 56 DSP stages. Peak/RMS remained identical to the pre-meter baseline.
The latter case's median process CPU accounting was 328.13 ms in both modes;
the simple case's coarse counter varied between 15.63 and 31.25 ms. Active RSS
was about 11.1 MiB and 17.6 MiB respectively; capture-mode differences were
within 72 KiB and are not a memory-saving claim. These are local sampled values,
not allocation sizes, measured peaks or device deadline guarantees. Full live
editor meters/preview controls, script meter access, packaged meter assertions,
physical-device timing and other platform integration remain pending.
