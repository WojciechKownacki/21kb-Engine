# Sprint 41 · Scripting, Audio & Input — Detailed Engineering Tasks

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Close concrete implementation gaps across scripting, audio, and input using production integrations, explicit runtime contracts, focused automated verification, and measurable real-time behavior.

## Scripting

- [ ] Implement an engine-integrated coroutine scheduler exposed to all script backends as wait-seconds, wait-frames, and wait-until-condition primitives that suspend a behaviour function mid-body (via yield on Lua and a resumable state machine on native and visual-graph) and resume it on the correct tick group, so sequential logic needs no manual state machines.
- [ ] Implement a script-facing Task.Start that accepts a script-authored coroutine body and drives it through the scene task system, completing the deferred half of the Task API so scripts, not only native code, can spawn, await, and cancel long-running asynchronous work.
- [ ] Add a per-behaviour instruction budget by installing a Lua count hook that aborts a script with a diagnostic once it exceeds a configurable opcode count per lifecycle invocation, so a runaway loop in one behaviour cannot hang the frame.
- [ ] Add a per-state or per-behaviour memory ceiling via a custom Lua allocator that fails allocation past a configurable byte cap and surfaces it as a script diagnostic, so a script cannot exhaust process memory.
- [ ] Implement hot-reload state preservation for Lua behaviours that serializes the previous environment's live exposed and script-declared persistent variables before the environment swap and re-injects matching values afterward, so editing a script mid-play does not reset gameplay state.
- [ ] Implement per-instance exposed-variable override application for the visual-graph backend so editor-authored overrides seed graph default-value pins before the created lifecycle, achieving parity with the Lua backend instead of the current no-op.
- [ ] Implement per-instance exposed-variable storage and override application for the native backend so compiled behaviours declare exposed fields addressable per entity, removing the native-backend no-op.
- [ ] Build a debug-adapter-protocol server that bridges the existing Lua debug hook's pause, step, breakpoint, and call-stack machinery to a socket endpoint so an external editor can attach, set breakpoints, step, and inspect frames over a standard protocol.
- [ ] Extend breakpoints with a condition expression and a hit-count field, evaluating the condition in the paused frame's environment inside the hook so a breakpoint only stops when its predicate is true or its Nth hit is reached.
- [ ] Add a watch and evaluate facility that compiles and runs an arbitrary expression string against a paused frame's locals and upvalues and returns a typed variable snapshot, so a debugger can evaluate expressions and inspect nested tables on demand.
- [ ] Add a set-variable capability that writes a new value into a named local or upvalue of a paused frame so a debugger can mutate state at a breakpoint.
- [ ] Introduce a managed C#/.NET script backend implementing the backend interface that loads a compiled assembly, maps lifecycle and event callbacks and the existing function-registry surface into managed method calls, and marshals script values across the boundary, adding a fourth first-class scripting language.
- [ ] Extend the script value type with array and string-keyed-map variants plus marshalling on every backend boundary, so structured data such as lists and records can cross the script boundary without being flattened into separate scalar pins.
- [ ] Add first-class vector, quaternion, and color value types to the script value system and function-registry pins so transform, physics, and renderer APIs pass composite math types as single arguments instead of three float pins.
- [ ] Add a per-behaviour and per-registered-function CPU-time profiler that samples execution duration during lifecycle, event, and call dispatch and exposes an aggregated per-frame report, so designers can find which scripts and API calls dominate the budget.
- [ ] Add a change-notification observer mechanism to the shared script state so a behaviour can subscribe to a key and be invoked when its value changes, replacing per-frame polling for cross-behaviour shared data.
- [ ] Implement native-plugin hot-reload state preservation that snapshots a native behaviour's exposed and registered instance data before unloading the shared library and restores it after the rebuilt plugin reloads.
- [ ] Add a sandbox capability policy per behaviour asset that gates which registered API namespaces a given script may call and rejects disallowed calls with a diagnostic, so content-authored scripts run with least privilege.
- [ ] Add structured error objects carrying source line, chunk, and call stack to script diagnostics emitted from failed safe calls, so runtime script errors surface a navigable location in tooling.
- [ ] Implement a deterministic script fixed-tick group that runs flagged behaviours on the physics fixed timestep with an accumulator, so gameplay logic requiring stable step size runs independently of render frame rate.

## Audio

- [ ] Build a per-bus DSP insert chain using a node graph so each authored mixer bus can host an ordered list of effect nodes routed between the bus source and its parent, turning buses into real processing chains rather than volume and mute only.
- [ ] Implement a low-pass filter effect node selectable per bus and per source with authorable cutoff, so environmental muffling and dialogue clarity can be shaped without pre-baked assets.
- [ ] Implement a reverb effect node with authorable room parameters as a shared aux bus plus per-source send levels, so multiple sources feed one reverb tail through a send-and-return topology.
- [ ] Implement a parametric equalizer effect node with multiple bands usable on any bus so mixes can be tone-shaped at authoring time.
- [ ] Implement a dynamics compressor and limiter effect node on the master and arbitrary buses with threshold, ratio, attack, and release parameters, so the mix is protected from clipping and can be glued.
- [ ] Add sidechain ducking that keys one bus's gain reduction off another bus's signal level so, for example, music is ducked by a dialogue bus each audio tick.
- [ ] Extend mixer snapshots beyond bus volumes to also carry per-bus mute, pitch, and effect-parameter overrides, and interpolate all of them during snapshot transitions, so a snapshot captures a full mix state.
- [ ] Add a per-clip streaming policy that selects stream-versus-decode-to-memory and honor it instead of unconditionally streaming, so short one-shot effects play from a decoded in-memory buffer without per-play disk streaming.
- [ ] Implement an in-memory decoded-PCM cache keyed by clip asset id so repeated one-shots of the same short clip share one decoded buffer instead of re-decoding per voice.
- [ ] Implement asynchronous clip loading and decoding on a worker so one-shot playback and source creation never block the game thread on first-touch decode, resolving the sound once decoding completes.
- [ ] Implement sample-accurate scheduled playback that starts a voice at a specified future audio-clock frame, exposed through the play descriptor, so cues fire exactly on the audio clock rather than at frame boundaries.
- [ ] Add beat and tempo-quantized scheduling built on the sample-accurate start-time mechanism so a cue can be scheduled to the next bar or beat of an authored tempo, enabling rhythmic music layering.
- [ ] Replace binary occlusion with a filtered occlusion model that additionally drives a per-voice low-pass cutoff proportional to ray-blocked coverage, so occluded sources are muffled and attenuated rather than only attenuated.
- [ ] Distinguish obstruction from occlusion by sampling the direct path and the reverb-send path separately, applying obstruction to the dry signal while leaving the reverb send audible, so a source behind a thin wall sounds correctly indirect.
- [ ] Add multi-listener support that drives multiple listeners bound to per-local-user cameras and routes each spatial source to the nearest listener, so split-screen local multiplayer gets correct per-viewport 3D audio.
- [ ] Add directional source cones with inner and outer angle and outer gain so sources such as speakers and character mouths radiate directionally instead of omnidirectionally.
- [ ] Implement reverb-zone volumes as scene components that select an environmental reverb preset by listener position and crossfade the master reverb parameters on zone transitions.
- [ ] Add per-bus and master metering that reports peak and RMS levels each audio tick through a lock-free channel to the game thread so editor meters and audio debugging can display live signal levels.
- [ ] Add an optional binaural HRTF spatialization path selectable per source so headphone users get true binaural positioning instead of only vector-based panning.
- [ ] Implement looping-voice virtualization that records stolen looping one-shots and automatically restarts them at their computed current position when pool capacity frees, so an important looping sound resumes instead of being permanently lost to voice stealing.
- [ ] Add a music crossfade helper that ties two voices to a shared normalized fade parameter and ramps their send volumes inversely over an authorable duration, so track transitions need no manual per-frame volume scripting.
- [ ] Add distance-based air-absorption filtering that scales a per-voice low-pass cutoff with listener distance so far-away sources lose high frequencies realistically, layered on top of distance attenuation.

## Input

- [ ] Implement a device-to-local-user binding layer that assigns a specific gamepad index and optionally the shared keyboard and mouse to each local user and routes only that device's state into that user's evaluation, so local multiplayer players control separate characters from separate controllers.
- [ ] Implement a press-any-button-to-join flow that watches all unassigned connected devices for a first actuation and returns the actuating device so it can be bound to a newly created local user, so drop-in local co-op works without pre-assignment.
- [ ] Emit device hotplug events into the script event bus by diffing gamepad connectivity across frames so gameplay can react to controllers being plugged in or lost instead of polling.
- [ ] Implement an interactive rebind capture flow that listens for the next actuated key across all devices with cancel and timeout, returns it as a candidate, and feeds it into the rebind path, so a settings screen can offer press-a-key-to-bind.
- [ ] Extend rebinding to address an individual composite slot via a binding-id and slot-index pair, closing the gap where a composite's slots cannot be independently rebound.
- [ ] Implement last-used-device tracking that records whether the most recent actuation came from keyboard and mouse or a specific gamepad and exposes it per local user so UI can switch button-prompt glyphs to match the active device.
- [ ] Implement the reserved field-of-view-scaling modifier by giving the input subsystem access to the active camera's field of view and scaling the value accordingly, converting the reserved enum slot into a working sensitivity-versus-zoom modifier.
- [ ] Add an input-buffering trigger option that remembers an actuation for a configurable window so a slightly-early press still satisfies the action when its gate opens later that window, enabling forgiving action-game timing.
- [ ] Implement a directional-sequence combo trigger that fires when a configured ordered sequence of actions or directions completes within a time budget, extending the trigger set beyond the single-action gate.
- [ ] Add an action-level toggle modifier that converts a momentary press into a latched on/off state resolved in the mapping evaluator, so accessibility and preference toggles need no per-behaviour scripting.
- [ ] Introduce named haptic effect assets describing a magnitude-over-time curve and dual-motor mix and a player that drives the vibration backend each frame from the curve, so designers author rumble patterns instead of only setting constant motor magnitudes.
- [ ] Implement per-local-user haptics routing that maps a local user to its bound device index so setting vibration addresses a specific player's controller rather than a raw device index.
- [ ] Add virtual on-screen touch control regions that map touch contacts to actions so mobile builds get button and stick input through the same action system, going beyond a single binary touch key.
- [ ] Add a mouse-delta relative-motion evaluation path distinct from absolute pointer position so first-person look uses frame-to-frame delta unaffected by cursor clamping or absolute resets.
- [ ] Implement per-device sensitivity and dead-zone calibration profiles that scale and curve a device's analog inputs before modifier evaluation and persist alongside the rebind profile so players can tune stick response per controller.
- [ ] Add a rebind-UI query API that enumerates every rebindable binding in the active contexts with its current key, display name, and conflict status so a settings screen can render the full remap table without walking asset internals.
- [ ] Implement an input-consumption report that records which higher-priority mapping context consumed a key this frame and exposes it so gameplay can detect when UI or console captured input rather than silently receiving nothing.
- [ ] Add a snapshot-diff comparison utility over input recordings that reports the first frame two recordings diverge in resolved action state, turning deterministic replay into a regression-testable golden-file mechanism for input logic.
