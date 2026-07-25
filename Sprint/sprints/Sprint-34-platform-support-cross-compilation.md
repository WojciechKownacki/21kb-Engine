# Sprint 34 · Platform Support & Cross-Compilation

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver explicit runtime, toolchain, rendering, input, storage, packaging, certification, and automated-test support for each declared desktop, mobile, console, and web target.

## Platform Runtime Services

- [ ] Add a per-platform application entry point and lifecycle
- [ ] Add suspend, resume, focus-loss, and low-memory handling
- [ ] Add per-platform user-data, config, cache, and temp paths
- [ ] Add per-platform save-data storage and quotas
- [ ] Add per-platform system dialogs and message boxes
- [ ] Add per-platform clipboard and system integration
- [ ] Add per-platform locale and region detection
- [ ] Add per-platform power, thermal, and battery state
- [ ] Add per-platform display, refresh-rate, and HDR capability query
- [ ] Add per-platform network reachability
- [ ] Add per-platform notifications and system events
- [ ] Add a capability manifest per platform

## Windows Platform

- [ ] Add a Direct3D and Vulkan backend selection on Windows
- [ ] Add window creation, DPI awareness, and multi-monitor
- [ ] Add keyboard, mouse, and raw-input handling
- [ ] Add controller support (XInput and general gamepad)
- [ ] Add HDR output on capable displays
- [ ] Add borderless, fullscreen-exclusive, and windowed modes
- [ ] Add an installer package and silent-install support
- [ ] Add executable and package code signing
- [ ] Add crash minidump capture on Windows
- [ ] Add save and config storage in the user profile
- [ ] Add store-distribution packaging
- [ ] Add Windows platform tests

## Linux Platform

- [ ] Add a Vulkan backend on Linux
- [ ] Add X11 and Wayland windowing backends
- [ ] Add controller support via evdev
- [ ] Add file paths following the desktop base-directory spec
- [ ] Add packaging as a portable archive
- [ ] Add packaging as a self-contained app image
- [ ] Add packaging for common distribution package managers
- [ ] Add distribution and driver compatibility handling
- [ ] Add crash reporting on Linux
- [ ] Add fractional-scaling and multi-monitor support
- [ ] Add Linux platform tests

## macOS Platform

- [ ] Add a Metal backend on macOS
- [ ] Add Cocoa windowing and event handling
- [ ] Add universal binaries for Apple Silicon and Intel
- [ ] Add controller support via the platform game-controller framework
- [ ] Add code signing and notarization
- [ ] Add app sandbox and entitlements
- [ ] Add packaging as a disk image and store build
- [ ] Add Retina and multi-display handling
- [ ] Add platform achievements and leaderboards integration
- [ ] Add crash reporting on macOS
- [ ] Add macOS platform tests

## Android Platform

- [ ] Add a Vulkan and OpenGL-ES backend on Android
- [ ] Add app-bundle and split-ABI packaging
- [ ] Add the native activity and lifecycle bridge
- [ ] Add a native-to-Java interop layer
- [ ] Add touch, gesture, and gamepad input
- [ ] Add runtime permissions handling
- [ ] Add scoped and external storage handling
- [ ] Add the back-button and navigation handling
- [ ] Add platform game-services integration
- [ ] Add in-app-purchase billing integration
- [ ] Add push notifications
- [ ] Add device and GPU compatibility tiers
- [ ] Add thermal throttling and sustained-performance handling
- [ ] Add store-listing and asset-delivery packaging
- [ ] Add Android platform tests

## iOS Platform

- [ ] Add a Metal backend on iOS
- [ ] Add app packaging, provisioning, and signing
- [ ] Add store and test-distribution builds
- [ ] Add in-app-purchase integration
- [ ] Add platform game-services integration
- [ ] Add privacy and tracking-consent handling
- [ ] Add touch, gesture, and controller input
- [ ] Add the application lifecycle and background handling
- [ ] Add device tiers and thermal management
- [ ] Add safe-area and notch handling
- [ ] Add entitlements and capability configuration
- [ ] Add iOS platform tests

## Console Platforms

- [ ] Add a per-console graphics-backend integration
- [ ] Add console platform-SDK integration behind the abstraction
- [ ] Add controller and peripheral input per console
- [ ] Add console save-data and storage APIs
- [ ] Add user, account, and sign-in handling
- [ ] Add trophies, achievements, and presence per console
- [ ] Add suspend, resume, and quick-resume handling
- [ ] Add memory and performance constraints per console
- [ ] Add dev-kit deployment and debugging
- [ ] Add certification-requirement checklists per console
- [ ] Add store and package submission per console
- [ ] Add console platform tests

## Web & WebAssembly Platform

- [ ] Add a WebGPU rendering backend
- [ ] Add a WebGL fallback backend
- [ ] Add a WebAssembly build of the engine
- [ ] Add multi-threading via shared memory where available
- [ ] Add SIMD acceleration in the web build
- [ ] Add memory-limit handling and growth
- [ ] Add async fetch-based asset loading
- [ ] Add browser storage for saves and cache
- [ ] Add gamepad, keyboard, mouse, and touch input on the web
- [ ] Add fullscreen and pointer-lock handling
- [ ] Add load-time reduction and progressive streaming
- [ ] Add browser and device compatibility handling
- [ ] Add web platform tests

## Cross-Compilation & Toolchains

- [ ] Add a build configuration per target platform
- [ ] Add toolchain management for each platform
- [ ] Add platform SDK discovery and versioning
- [ ] Add conditional compilation per platform and feature
- [ ] Add a platform abstraction that isolates platform code
- [ ] Add cross-compilation from a single host where supported
- [ ] Add per-platform dependency and third-party management
- [ ] Add remote and cloud build execution
- [ ] Add reproducible cross-platform builds
- [ ] Add build caching across platforms
- [ ] Add per-platform shader and asset cooking
- [ ] Add automated multi-platform build verification

## Platform Certification & Compliance

- [ ] Add per-platform technical-requirement checklists
- [ ] Add automated checks for common certification failures
- [ ] Add age-rating and content-descriptor metadata
- [ ] Add accessibility-compliance checks per platform
- [ ] Add privacy and data-handling compliance
- [ ] Add store-metadata and asset requirements
- [ ] Add loading-time and performance requirement checks
- [ ] Add controller and input requirement checks
- [ ] Add a certification-readiness report
