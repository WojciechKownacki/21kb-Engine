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
run. The test sends five trusted browser clicks: play, pause, resume, stop and
play again. It never calls `AudioContext.resume()` or changes autoplay policy.

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
qualify mobile browsers, AudioWorklet, background interruption or route changes.

The current native `Audio.DeviceStatus` can report playback available while
the browser context is suspended awaiting its first gesture. The report records
both native status and actual context state; activation-aware native diagnostics
remain to be implemented and verified.
