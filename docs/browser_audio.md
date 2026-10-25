# Browser audio verification

The audio CLI scenario uses a fresh browser profile, the exported player and
a normal cooked project. It generates a one-second, looping mono 440 Hz WAV at
48 kHz with PCM16 peak 3276, imports it, assigns an Audio Source at volume 0.25
and adds a primary listener. Native UI clicks call the public `Audio.PlaySource`,
`PauseSource`, `ResumeSource` and `StopSource` operations through Lua.

Run `sources/game/tests/web/run_game_audio_tests.py` with the full fixture
arguments documented in the [browser test README](../sources/game/tests/web/README.md).
The player must render native UI on an identifiable hardware GPU. Before any
gesture, the WebAudio context must be suspended and no output callback may have
run. The first five trusted browser clicks request play, pause, resume, stop and
play again. Two more request explicit backend restart after controlled
interruptions. The test never calls `AudioContext.resume()` or changes autoplay
policy.

The observer delegates `createScriptProcessor` unchanged and attaches a
read-only `audioprocess` listener after the backend installs its synchronous
native callback. It measures the output buffer after that callback fills it;
it does not synthesize samples or invoke the mixer. Each phase requires four
consecutive output blocks. Playing blocks must match the authored sine RMS
within 2 percent and 440 Hz within 30 Hz. The frequency tolerance covers the
zero-crossing estimator's 2048-frame window at 48 kHz (23.4375 Hz resolution).
Resume and replay must also remain within 10 percent of the first playback RMS.
Paused and stopped output peaks must be below 0.00001. Transport must retain
one context. The observer keeps at most 128 block summaries and no PCM arrays.

This scenario passed on desktop Chromium with the current ScriptProcessor
backend: actual PCM RMS approximately 0.01767, peak 0.02499, and silent
pause/stop blocks. GPU rendering uses bgfx. This verifies delivery to WebAudio,
not physical speaker output, acoustic latency or device routing. It does not
qualify mobile browsers, AudioWorklet, OS interruption or route changes.

`Audio.DeviceStatus` returns `PlaybackSuspended` (4), with `available=false`,
when a started browser device has not produced output callbacks yet, or its
callbacks have stopped. Existing status values 0 through 3 retain their values.
The backend observes miniaudio's public `onProcess` callback; it neither reads
private JavaScript device state nor takes over browser activation. Recent output
counts as available for at least 250 ms, or four callback blocks if longer, to
allow callback jitter. This is output-activity detection with that bounded
delay, not an immediate query of the browser context state.

The source transport remains valid while output is suspended, preserving
queued or autoplay intent. The scenario requires the native suspended status
before the first gesture, then actual PCM and available status after activation.
After replay, it explicitly calls the standard context `suspend()` API as an
interruption injection and requires suspended native status, no further output
callbacks and retained playing intent. Two cycles then recover through trusted
native UI clicks calling `Audio.Reinitialize`. Each must restore the same PCM
amplitude and close all previous contexts, retaining one running context.

Between those cycles, an owned second tab takes focus and hides the player.
The test requires actual PCM while hidden and after focus returns, with stable
amplitude, followed by a second native UI GPU capture. Desktop tab visibility
and explicit native backend recovery passed. OS interruption, automatic
interruption recovery, device switching and physical mobile routes remain
unverified. Restart can briefly stop output; this scenario does not establish
gapless recovery or an acoustic latency bound.
