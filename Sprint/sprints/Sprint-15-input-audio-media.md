# Sprint 15 · Input / Audio / Media

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver production input, audio, video, voice, haptics, and media services with cross-platform device handling, authoring workflows, accessibility, streaming, synchronization, diagnostics, and bounded real-time performance.

## Input Core & Devices

- [ ] Add a device abstraction over all input hardware
- [ ] Add keyboard device support
- [ ] Add mouse device support with buttons, movement, and wheel
- [ ] Add gamepad device support with buttons, sticks, and triggers
- [ ] Add touchscreen device support
- [ ] Add pen and stylus support with pressure and tilt
- [ ] Add motion-sensor support (accelerometer, gyroscope)
- [ ] Add XR controller device support
- [ ] Add device hotplug detection and reconnection
- [ ] Add per-device state snapshots each frame
- [ ] Add both event-driven and polled access
- [ ] Add raw and processed value access
- [ ] Add device capability queries
- [ ] Add device naming, ids, and product metadata
- [ ] Add unified button, axis, and vector value types
- [ ] Add timestamps on input events
- [ ] Add per-platform device backends behind the abstraction
- [ ] Add device debug inspection

## Action Mapping & Bindings

- [ ] Add named input actions with typed values
- [ ] Add action maps grouping related actions
- [ ] Add mapping contexts with priority stacking
- [ ] Add binding an action to multiple devices and keys
- [ ] Add composite bindings (2D vector from keys, 1D axis)
- [ ] Add control schemes per device class
- [ ] Add automatic scheme switching on device use
- [ ] Add context push, pop, and enable/disable
- [ ] Add per-context consumption so higher contexts block lower
- [ ] Add action phases (started, ongoing, performed, canceled)
- [ ] Add action value queries and event callbacks
- [ ] Add chorded and modifier-key bindings
- [ ] Add binding groups for platform-specific sets
- [ ] Add data-driven action and binding assets
- [ ] Add runtime creation and modification of bindings
- [ ] Add validation of action and binding configuration
- [ ] Add versioning and migration of input assets
- [ ] Add a scripting API for actions and values

## Input Processing

- [ ] Add deadzone processing for sticks and triggers
- [ ] Add sensitivity and scaling processors
- [ ] Add invert and clamp processors
- [ ] Add normalization and vector-magnitude processors
- [ ] Add smoothing and acceleration processors
- [ ] Add a press interaction
- [ ] Add a hold interaction with duration
- [ ] Add a tap and multi-tap interaction
- [ ] Add a slow-tap and long-press interaction
- [ ] Add a chord interaction
- [ ] Add input buffering with a configurable window
- [ ] Add repeat and echo handling
- [ ] Add custom processor and interaction plugins
- [ ] Add ordering of processors in a chain
- [ ] Add per-binding processor overrides
- [ ] Add processing debug visualization

## Rebinding & Accessibility

- [ ] Add runtime interactive rebinding
- [ ] Add listen-for-next-input capture
- [ ] Add conflict detection and resolution
- [ ] Add cancel and reset-to-default rebinding
- [ ] Add save and load of custom bindings
- [ ] Add binding presets and profiles
- [ ] Add exclusion of reserved keys from rebinding
- [ ] Add hold-versus-toggle accessibility options
- [ ] Add remapping for one-handed and alternative layouts
- [ ] Add input-assist options (auto-run, sticky modifiers)
- [ ] Add per-action sensitivity and deadzone in settings
- [ ] Add a rebinding UI with live capture
- [ ] Add validation and warnings for unbound actions
- [ ] Add accessibility presets

## Touch & Gestures

- [ ] Add multi-touch point tracking
- [ ] Add touch begin, move, stationary, and end phases
- [ ] Add tap and double-tap gestures
- [ ] Add long-press gestures
- [ ] Add swipe and fling gestures with direction
- [ ] Add pinch and zoom gestures
- [ ] Add rotate gestures
- [ ] Add pan and drag gestures
- [ ] Add gesture recognizers with priorities
- [ ] Add on-screen virtual joysticks
- [ ] Add on-screen virtual buttons and d-pads
- [ ] Add customizable on-screen control layouts
- [ ] Add touch-to-action binding
- [ ] Add touch debug visualization

## XR & Motion Controllers

- [ ] Add XR controller pose tracking
- [ ] Add XR controller buttons, triggers, and thumbsticks
- [ ] Add hand-tracking joint poses
- [ ] Add gesture recognition for hands
- [ ] Add gaze and head-pose input
- [ ] Add grip and aim pose distinction
- [ ] Add XR haptic output
- [ ] Add binding of XR input to actions
- [ ] Add controller model and pointer visualization
- [ ] Add interaction rays and selection
- [ ] Add XR input capability negotiation
- [ ] Add XR input debug visualization

## Haptics & Force Feedback

- [ ] Add gamepad rumble with low and high motors
- [ ] Add trigger haptics where supported
- [ ] Add haptic patterns and envelopes
- [ ] Add per-device haptic capability queries
- [ ] Add playback, layering, and stop of haptics
- [ ] Add intensity scaling and user settings
- [ ] Add XR controller haptics
- [ ] Add spatialized and directional haptics
- [ ] Add haptic asset authoring
- [ ] Add haptics debug and test tools

## Local Multiplayer & Device Assignment

- [ ] Add multiple local users
- [ ] Add device pairing and assignment per user
- [ ] Add join and leave flows for local players
- [ ] Add per-user action state and contexts
- [ ] Add device reassignment and reconnection handling
- [ ] Add split-screen input routing
- [ ] Add a lobby-style press-to-join flow
- [ ] Add per-user binding profiles
- [ ] Add unpaired-device handling
- [ ] Add local-user debug inspection

## Input Runtime & Integration

- [ ] Add input polling and dispatch within the frame loop
- [ ] Add window and application focus handling
- [ ] Add input consumption and priority between UI and gameplay
- [ ] Add routing of input to the UI system first
- [ ] Add input components in the ECS for gameplay
- [ ] Add per-frame action snapshots for systems
- [ ] Add fixed-step input sampling for deterministic gameplay
- [ ] Add input recording and playback
- [ ] Add input injection for automated testing
- [ ] Add a scripting API for input queries and events
- [ ] Add pause-and-resume of input capture
- [ ] Add input flushing on context changes
- [ ] Add latency measurement of input to action
- [ ] Add input event logging and diagnostics

## Input User-Friendly & Editor

- [ ] Add an action and binding editor
- [ ] Add a visual device-and-binding map
- [ ] Add live input preview and testing
- [ ] Add ready-made control-scheme presets
- [ ] Add automatic detection of the active device for prompts
- [ ] Add device-appropriate button glyphs and prompts
- [ ] Add plain-language binding labels
- [ ] Add a beginner-friendly default setup that just works
- [ ] Add warnings for missing or conflicting bindings
- [ ] Add a gallery of example input setups

## Audio Engine Core

- [ ] Add an audio engine on the miniaudio backend
- [ ] Add a dedicated audio mixing thread
- [ ] Add configurable sample rate and buffer size
- [ ] Add output-device selection and enumeration
- [ ] Add device hotplug and default-device following
- [ ] Add a master output with volume and mute
- [ ] Add a voice pool with a configurable limit
- [ ] Add voice priority and stealing
- [ ] Add virtualization of inaudible voices
- [ ] Add revival of virtual voices when audible again
- [ ] Add per-voice state (playing, paused, stopped, virtual)
- [ ] Add sample-accurate scheduling
- [ ] Add resampling for mismatched sample rates
- [ ] Add channel-count handling (mono, stereo, surround)
- [ ] Add clock and timeline for synchronized playback
- [ ] Add glitch and underrun detection
- [ ] Add engine start, stop, and reset
- [ ] Add thread-safe command submission to the audio thread
- [ ] Add memory accounting for audio
- [ ] Add engine statistics (active voices, CPU, memory)

## Sound Sources & Playback

- [ ] Add one-shot sound playback
- [ ] Add looping sound playback
- [ ] Add 2D non-spatialized sources
- [ ] Add 3D spatialized sources
- [ ] Add play, stop, pause, and resume
- [ ] Add per-source volume and pitch
- [ ] Add fade-in and fade-out
- [ ] Add start offset and seeking
- [ ] Add per-source priority
- [ ] Add playback rate and time-stretch
- [ ] Add randomized pitch and volume variation
- [ ] Add sound variation sets with weighting
- [ ] Add follow-entity sources that track a transform
- [ ] Add one-shot fire-and-forget helpers
- [ ] Add loop points and sustain regions
- [ ] Add playback completion callbacks and events
- [ ] Add source pooling and reuse
- [ ] Add source debug inspection

## Mixing & Buses

- [ ] Add a hierarchy of mixer buses and groups
- [ ] Add submixes feeding parent buses
- [ ] Add per-bus volume, pitch, and mute
- [ ] Add solo and bypass per bus
- [ ] Add send and return routing
- [ ] Add side-chain ducking (music under dialogue)
- [ ] Add mixer snapshots and transitions
- [ ] Add category buses (music, sfx, dialogue, ambience, ui)
- [ ] Add user volume settings per category
- [ ] Add automatic ducking rules
- [ ] Add metering per bus
- [ ] Add real-time parameters driving mix values
- [ ] Add a data-driven mixer asset
- [ ] Add mixer validation
- [ ] Add mixer authoring and preview
- [ ] Add mixer state save and restore

## 3D / Spatial Audio

- [ ] Add distance attenuation with configurable curves
- [ ] Add min and max distance and rolloff models
- [ ] Add stereo and surround panning by direction
- [ ] Add doppler-effect pitch shifting
- [ ] Add source spread and size
- [ ] Add a listener driven by the active camera
- [ ] Add multiple listeners for split-screen
- [ ] Add listener weighting for nearest-listener audio
- [ ] Add HRTF binaural spatialization
- [ ] Add ambisonic encoding and decoding
- [ ] Add ambient and directional bed handling
- [ ] Add air-absorption filtering over distance
- [ ] Add early reflections approximation
- [ ] Add velocity tracking for doppler
- [ ] Add per-source spatialization toggles
- [ ] Add attenuation-shape authoring (sphere, cone, box)
- [ ] Add spatialization debug visualization
- [ ] Add near-field and focus handling

## DSP & Effects

- [ ] Add a reverb effect
- [ ] Add a parametric equalizer
- [ ] Add a compressor and limiter
- [ ] Add a delay and echo effect
- [ ] Add low-pass, high-pass, and band-pass filters
- [ ] Add distortion and saturation
- [ ] Add chorus and flanger
- [ ] Add pitch shifting and formant control
- [ ] Add effect chains per source and per bus
- [ ] Add effect bypass and wet/dry mix
- [ ] Add real-time parameter control of effects
- [ ] Add a custom-DSP plugin interface
- [ ] Add effect ordering and routing
- [ ] Add effect presets
- [ ] Add convolution reverb from impulse responses
- [ ] Add a spectrum analyzer and metering
- [ ] Add effect cost budgets
- [ ] Add effect authoring and preview

## Occlusion & Propagation

- [ ] Add occlusion tests between source and listener
- [ ] Add obstruction handling for partial blocking
- [ ] Add low-pass filtering driven by occlusion
- [ ] Add reverb zones with blending
- [ ] Add portal-based sound propagation between rooms
- [ ] Add geometry-based acoustic approximation
- [ ] Add dynamic reverb from the surrounding space
- [ ] Add material-based absorption and transmission
- [ ] Add diffraction approximation around edges
- [ ] Add propagation cost budgets and throttling
- [ ] Add async occlusion queries
- [ ] Add reverb-zone authoring
- [ ] Add propagation debug visualization
- [ ] Add integration with the physics query system

## Audio Data & Banks

- [ ] Add an audio-clip asset with format metadata
- [ ] Add compressed and uncompressed clip support
- [ ] Add streaming of large clips from disk
- [ ] Add async decode off the audio thread
- [ ] Add sound banks grouping related clips
- [ ] Add bank load and unload on demand
- [ ] Add reference counting of loaded audio
- [ ] Add preloading and warmup of critical sounds
- [ ] Add per-platform compression and quality settings
- [ ] Add loudness normalization metadata
- [ ] Add memory budgets and residency for audio
- [ ] Add clip import and conversion
- [ ] Add clip validation
- [ ] Add streaming and bank diagnostics

## Interactive Music & Ambience

- [ ] Add layered music with independently controlled stems
- [ ] Add music transitions synced to bars and beats
- [ ] Add stingers and one-shot musical accents
- [ ] Add tempo, beat, and bar tracking
- [ ] Add horizontal re-sequencing of music segments
- [ ] Add vertical layering driven by gameplay intensity
- [ ] Add crossfades and quantized switches
- [ ] Add ambient beds with looping textures
- [ ] Add randomized ambient one-shots with timing rules
- [ ] Add ambience zones with blending
- [ ] Add real-time parameters driving music and ambience
- [ ] Add a music-state and transition graph
- [ ] Add music authoring and preview
- [ ] Add music and ambience validation

## Audio Middleware Integration

- [ ] Add an abstraction for external audio middleware
- [ ] Add loading of middleware banks
- [ ] Add posting of middleware events
- [ ] Add real-time parameters passed to middleware
- [ ] Add switches and states for middleware
- [ ] Add listener and source registration with middleware
- [ ] Add a fallback to the built-in engine when middleware is absent
- [ ] Add middleware profiling and diagnostics
- [ ] Add capability negotiation with middleware
- [ ] Add memory and voice accounting through middleware
- [ ] Add a consistent gameplay API across built-in and middleware
- [ ] Add validation of middleware integration

## Audio Runtime & Integration

- [ ] Add audio update within the frame loop
- [ ] Add a listener bound to the active camera
- [ ] Add audio-source components in the ECS
- [ ] Add transform-driven 3D source positions
- [ ] Add pause of gameplay audio on focus loss
- [ ] Add a scripting API for playback and parameters
- [ ] Add animation-event-driven sound triggers
- [ ] Add physics-impact-driven sound triggers
- [ ] Add occlusion updates throttled per frame
- [ ] Add voice and CPU budgets enforced at runtime
- [ ] Add time-scale and pause interaction with audio
- [ ] Add save and restore of audio settings
- [ ] Add runtime device-change handling
- [ ] Add audio runtime diagnostics

## Audio Authoring & User-Friendly

- [ ] Add a mixer editor with buses and sends
- [ ] Add an attenuation-curve editor
- [ ] Add one-click 3D sound setup with good defaults
- [ ] Add drag-and-drop sound assignment to objects
- [ ] Add plain-language controls (loudness, distance, echo)
- [ ] Add live preview and audition of sounds
- [ ] Add ready-made sound and mix presets
- [ ] Add a beginner mode hiding advanced routing
- [ ] Add warnings for clipping and missing sounds
- [ ] Add a sound-effect library browser
- [ ] Add sensible defaults so audio works immediately
- [ ] Add a gallery of example audio setups

## Video Decoding & Formats

- [ ] Add a video decoder abstraction
- [ ] Add support for common container formats
- [ ] Add support for common video codecs
- [ ] Add hardware-accelerated decoding where available
- [ ] Add a software-decode fallback
- [ ] Add decoding on a background thread
- [ ] Add frame queueing and pacing
- [ ] Add color-space conversion to renderable formats
- [ ] Add decode error handling and recovery
- [ ] Add decode performance diagnostics
- [ ] Add per-platform decoder backends
- [ ] Add decoder capability queries

## Media Playback & Control

- [ ] Add play, pause, stop, and resume
- [ ] Add seeking to a timestamp
- [ ] Add looping and playback-rate control
- [ ] Add rendering video to a texture
- [ ] Add rendering video into UI widgets
- [ ] Add rendering video onto materials and surfaces
- [ ] Add synchronized audio-track playback
- [ ] Add audio/video sync correction
- [ ] Add multiple simultaneous video players
- [ ] Add buffering and preload control
- [ ] Add playback state events and callbacks
- [ ] Add end-of-media and error events
- [ ] Add frame-accurate stepping
- [ ] Add a scripting API for media control

## Streaming Video & Subtitles

- [ ] Add streaming playback from local files
- [ ] Add streaming playback from network sources
- [ ] Add adaptive-bitrate handling for network streams
- [ ] Add buffering and rebuffering handling
- [ ] Add subtitle and caption rendering
- [ ] Add multiple subtitle tracks and selection
- [ ] Add multiple audio tracks and selection
- [ ] Add localization of subtitles
- [ ] Add subtitle timing and styling
- [ ] Add closed-caption accessibility support
- [ ] Add stream error recovery
- [ ] Add streaming diagnostics

## Cutscene & UI Video Integration

- [ ] Add a fullscreen movie player
- [ ] Add skippable video playback
- [ ] Add pre-rendered cutscene playback
- [ ] Add background and looping UI video
- [ ] Add video as an animated UI element
- [ ] Add transitions into and out of video
- [ ] Add input handling during video (skip, pause)
- [ ] Add handoff between video and gameplay
- [ ] Add video-in-world screens and displays
- [ ] Add cutscene-video validation

## Voice Capture & Encoding

- [ ] Add microphone capture
- [ ] Add capture-device selection and enumeration
- [ ] Add configurable sample rate and frame size
- [ ] Add Opus encoding of captured audio
- [ ] Add voice-activity detection
- [ ] Add push-to-talk mode
- [ ] Add open-mic mode with a threshold
- [ ] Add noise suppression
- [ ] Add echo cancellation
- [ ] Add automatic gain control
- [ ] Add input-level metering and monitoring
- [ ] Add a local test-your-mic loopback
- [ ] Add capture start, stop, and mute
- [ ] Add capture device hotplug handling
- [ ] Add capture diagnostics
- [ ] Add per-user capture settings

## Voice Transmission & Channels

- [ ] Add network transmission of encoded voice
- [ ] Add voice channels and rooms
- [ ] Add team and proximity channels
- [ ] Add positional (spatial) voice by speaker location
- [ ] Add proximity attenuation for spatial voice
- [ ] Add per-channel join and leave
- [ ] Add speaker priority and ducking of others
- [ ] Add mute and block per speaker
- [ ] Add bandwidth-aware bitrate adaptation
- [ ] Add packet-loss concealment
- [ ] Add server relay and peer-to-peer options
- [ ] Add integration with the networking layer
- [ ] Add transmission diagnostics
- [ ] Add per-channel policy configuration

## Voice Playback & Mixing

- [ ] Add per-speaker voice playback
- [ ] Add a jitter buffer for smooth playback
- [ ] Add spatialization of positional voice
- [ ] Add mixing of voice with game audio
- [ ] Add ducking of game audio under voice
- [ ] Add per-speaker volume and mute
- [ ] Add a speaking indicator and levels
- [ ] Add voice routing to the mixer buses
- [ ] Add late-packet and dropout handling
- [ ] Add playback diagnostics
- [ ] Add accessibility options for voice
- [ ] Add per-user playback settings

## Voice Moderation & Safety

- [ ] Add local mute and block lists
- [ ] Add server-side mute and ban
- [ ] Add reporting of abusive voice
- [ ] Add transcription hooks for moderation
- [ ] Add profanity and safety filtering hooks
- [ ] Add parental controls and age-gating
- [ ] Add default-safe settings for minors
- [ ] Add consent and privacy handling for capture
- [ ] Add audit logging for moderation actions
- [ ] Add moderation diagnostics

## Performance & Budgets

- [ ] Add a voice budget for concurrent audio
- [ ] Add DSP-cost budgets and throttling
- [ ] Add streaming and decode budgets for audio and video
- [ ] Add input-to-action latency measurement
- [ ] Add audio-thread load monitoring
- [ ] Add video-decode cost monitoring
- [ ] Add voice-chat bandwidth and CPU monitoring
- [ ] Add per-system profiling and attribution
- [ ] Add a headless media benchmark harness
- [ ] Add machine-readable media metrics for CI
- [ ] Add memory budgets across input, audio, and media
- [ ] Add over-budget diagnostics

## Testing & Validation

- [ ] Add action-mapping and binding tests
- [ ] Add rebinding and conflict-resolution tests
- [ ] Add interaction and processor tests
- [ ] Add touch-gesture recognition tests
- [ ] Add local-multiplayer device-assignment tests
- [ ] Add deterministic input recording and playback tests
- [ ] Add audio mixing and routing tests
- [ ] Add 3D spatialization and attenuation tests
- [ ] Add DSP-effect correctness tests
- [ ] Add occlusion and propagation tests
- [ ] Add audio streaming and bank tests
- [ ] Add interactive-music transition tests
- [ ] Add video-decode and playback tests
- [ ] Add subtitle and track-selection tests
- [ ] Add voice capture, encode, and transmit tests
- [ ] Add voice spatialization and mixing tests
- [ ] Add latency and budget stress tests
- [ ] Add cross-platform device and format tests

## Procedural Audio & Node-Based Sound Graphs

- [ ] Add a node-based procedural audio graph
- [ ] Add a runtime graph execution engine for audio
- [ ] Add a playable procedural-audio source asset
- [ ] Add reusable sub-patches and presets
- [ ] Add runtime and scripted graph construction
- [ ] Add oscillator nodes (sine, saw, square, additive, supersaw)
- [ ] Add noise nodes (white, Perlin, low-frequency)
- [ ] Add filter nodes (low-pass, high-pass, band-pass, band-split, dynamic)
- [ ] Add envelope nodes (attack-decay, ADSR, follower)
- [ ] Add delay nodes (mono, stereo, grain, tap)
- [ ] Add reverb and diffuser nodes
- [ ] Add distortion, bitcrusher, and waveshaper nodes
- [ ] Add modulation nodes (ring mod, flanger, chorus, phaser)
- [ ] Add pitch-shift and Doppler nodes
- [ ] Add panner, mid/side, and crossfade nodes
- [ ] Add sample-and-hold and mixer nodes
- [ ] Add trigger and control nodes (sequence, repeat, counter, gate, compare, select)
- [ ] Add music-theory nodes (note-to-frequency, note quantizer, scale-to-array, tempo-to-seconds)
- [ ] Add graph input and output parameters exposed to gameplay
- [ ] Add a wave-writer node for capturing output
- [ ] Add a node library with search and metadata
- [ ] Add graph authoring, preview, and debugging

## Sound Cue Graphs, Classes & Submixes

- [ ] Add a sound-cue graph assembling sounds from nodes
- [ ] Add random, weighted, and shuffle selection nodes
- [ ] Add concatenate and sequence nodes
- [ ] Add mixer and layering nodes
- [ ] Add modulator nodes (pitch, volume, continuous)
- [ ] Add attenuation and distance-crossfade nodes
- [ ] Add branch, switch, and quality-level nodes
- [ ] Add delay and looping nodes
- [ ] Add a sound-class hierarchy for grouped control
- [ ] Add runtime sound mixes that adjust classes
- [ ] Add a submix graph with sends and returns
- [ ] Add per-submix effect chains
- [ ] Add cue templates and presets
- [ ] Add sound-cue authoring and preview
- [ ] Add sound-cue validation

## Audio Modulation & Control Buses

- [ ] Add audio control buses
- [ ] Add modulation patches driving parameters
- [ ] Add bus mixes that combine modulation sources
- [ ] Add modulation destinations (volume, pitch, effect parameters)
- [ ] Add curve-shaped modulation response
- [ ] Add real-time parameter routing from gameplay
- [ ] Add layered and priority modulation
- [ ] Add modulation presets
- [ ] Add modulation authoring and preview
- [ ] Add modulation debug inspection

## Real-Time Audio Analysis

- [ ] Add real-time loudness and broadcast-loudness metering
- [ ] Add a real-time spectrum analyzer
- [ ] Add constant-Q log-frequency analysis
- [ ] Add onset and beat detection
- [ ] Add peak and RMS metering
- [ ] Add both real-time and offline analysis paths
- [ ] Add analysis output routed to gameplay and visuals
- [ ] Add music-reactive parameter feeds
- [ ] Add per-source and per-submix analysis
- [ ] Add analysis widgets (meter, oscilloscope, spectrum)
- [ ] Add analysis cost budgets
- [ ] Add analysis debug visualization

## Sample-Accurate Music Clock & Quantization

- [ ] Add a sample-accurate musical clock
- [ ] Add a configurable metronome with tempo and meter
- [ ] Add quantized event scheduling to beats and bars
- [ ] Add beat-locked playback of sounds and music
- [ ] Add a quantized command queue on the audio thread
- [ ] Add game-thread notifications on beats and bars
- [ ] Add multiple synchronized clocks
- [ ] Add tempo changes and ramps
- [ ] Add quantization boundaries (beat, bar, phrase)
- [ ] Add clock debug readouts

## Procedural Synthesis & Motor Synth

- [ ] Add a subtractive virtual-analog synth component
- [ ] Add a granular-synthesis component
- [ ] Add a wavetable-synth component
- [ ] Add a tone and test-signal generator
- [ ] Add polyphony and voice management for synths
- [ ] Add MIDI and parameter control of synths
- [ ] Add a procedural engine/motor synthesizer driven by RPM
- [ ] Add grain-table authoring for motor synthesis
- [ ] Add load, throttle, and gear response for motor synth
- [ ] Add synth presets
- [ ] Add synth authoring and preview
- [ ] Add synth cost budgets

## Procedural Ambience & Soundscape

- [ ] Add a data-driven procedural ambience system
- [ ] Add placement points and palettes for ambient sounds
- [ ] Add rules for density, spacing, and timing of one-shots
- [ ] Add layered ambient beds with blending
- [ ] Add zone-driven ambience selection
- [ ] Add time-of-day and weather-driven ambience
- [ ] Add randomized non-repeating playback
- [ ] Add soundscape authoring and preview
- [ ] Add soundscape budgets and diagnostics
- [ ] Add soundscape debug visualization

## Spatialization Plugins, Soundfield & External Routing

- [ ] Add a spatialization plugin interface
- [ ] Add binaural HRTF spatialization backends
- [ ] Add a soundfield (ambisonic) format
- [ ] Add ambisonic encoding and decoding
- [ ] Add soundfield endpoints and rendering
- [ ] Add an occlusion and reverb plugin interface
- [ ] Add routing of audio into external engines at the mixer boundary
- [ ] Add offline and faster-than-real-time audio rendering
- [ ] Add platform spatial-audio endpoint support
- [ ] Add plugin capability negotiation and fallback
- [ ] Add spatialization debug visualization

## Interactive Music & MIDI

- [ ] Add MIDI file import and parsing
- [ ] Add a music map with tempo and timeline
- [ ] Add a MIDI clock synchronized to the music clock
- [ ] Add a MIDI-driven sampler with stems
- [ ] Add interactive stem mixing by intensity
- [ ] Add quantized transitions between music sections
- [ ] Add stingers and fills triggered on beats
- [ ] Add a music sequencer for interactive scores
- [ ] Add real-time parameters driving the score
- [ ] Add MIDI-controller input to music
- [ ] Add interactive-music authoring and preview
- [ ] Add interactive-music validation

## Audio Gameplay Volumes & Reverb Zones

- [ ] Add audio gameplay volumes that override audio in a region
- [ ] Add per-volume reverb settings
- [ ] Add per-volume occlusion and attenuation overrides
- [ ] Add per-volume submix routing
- [ ] Add blending between overlapping volumes
- [ ] Add priority and layering of volumes
- [ ] Add interior and exterior transitions
- [ ] Add modular audio gameplay components
- [ ] Add volume authoring tools
- [ ] Add volume debug visualization

## Media Framework, Sources & Playlists

- [ ] Add a media-player facade over multiple backends
- [ ] Add media sources for files, streams, and platform inputs
- [ ] Add time-synchronizable media sources
- [ ] Add a media texture for rendering video
- [ ] Add a media sound component for video audio
- [ ] Add media playlists with ordering and looping
- [ ] Add a media clock and track model
- [ ] Add multiple simultaneous media players
- [ ] Add pluggable platform and codec backends
- [ ] Add hardware-accelerated video decoders
- [ ] Add image-sequence and high-dynamic-range frame playback
- [ ] Add GPU tiled and mipped frame playback for large plates
- [ ] Add media events and state callbacks
- [ ] Add a media preview and inspection tool
- [ ] Add media-framework diagnostics

## Professional Video I/O & Timecode

- [ ] Add a capture-card abstraction for professional video
- [ ] Add serial-digital video capture and output
- [ ] Add network video input and output
- [ ] Add genlock synchronization
- [ ] Add timecode sources and synchronization
- [ ] Add multi-source timecode alignment
- [ ] Add frame-accurate capture and output
- [ ] Add color-space and format conversion for I/O
- [ ] Add audio embedding and de-embedding with video
- [ ] Add hardware capability detection
- [ ] Add a professional-I/O configuration UI
- [ ] Add professional-I/O diagnostics

## Game Capture, Encoding & Frame Streaming

- [ ] Add a hardware video and audio encoder abstraction
- [ ] Add capture of the game framebuffer to encoded video
- [ ] Add capture of game audio synchronized to video
- [ ] Add replay and highlight export to video files
- [ ] Add configurable resolution, bitrate, and codec
- [ ] Add a rendered-frame streaming pipeline over the network
- [ ] Add remote input injection from streamed clients
- [ ] Add cloud and browser play of streamed frames
- [ ] Add low-latency streaming transport
- [ ] Add adaptive quality for streamed frames
- [ ] Add multiple concurrent streaming sessions
- [ ] Add a frame-capture pipeline feeding encoders and streaming
- [ ] Add screenshot and clip-capture helpers
- [ ] Add capture and streaming budgets
- [ ] Add capture and streaming diagnostics

## Advanced Input: Device Properties, UI Navigation & External Devices

- [ ] Add a device-property system for output effects
- [ ] Add per-device LED color control
- [ ] Add adaptive-trigger resistance and effects
- [ ] Add spatialized and parameterized force feedback
- [ ] Add curve- and buffer-driven haptic effects
- [ ] Add player-mappable input configurations
- [ ] Add input injection for automation and replay
- [ ] Add a query for which contexts and keys map to an action
- [ ] Add a UI input-routing and focus-navigation layer
- [ ] Add gamepad, mouse, and touch input-method switching
- [ ] Add an action bar and activatable widget input stack
- [ ] Add directional UI navigation
- [ ] Add an external input-device plugin interface
- [ ] Add MIDI-controller input
- [ ] Add remote-control protocol bindings
- [ ] Add advanced-input debug tooling
