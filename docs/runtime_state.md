# Runtime provider state

`kb::core::IRuntimeStateProvider` is an optional, separate interface for native
provider checkpoints. `PhysicsBackend::RuntimeStateProvider(scene)` returns the
installed physics implementation's interface, or null when it is unavailable.
This leaves the existing physics backend virtual interface unchanged.

Call these operations on the scene's owner thread between updates. Capture may
finish and publish a pending pipelined physics step. Capture provider state before
the corresponding ECS state so both describe that completed simulation boundary.
Keep the snapshot as an owning `std::unique_ptr<RuntimeStateSnapshot>`.
`PayloadBytes()` reports serialized native payload bytes, excluding container
capacity, engine bookkeeping and externally retained resources.

The Jolt provider records the native solver's global, body, contact and constraint
state, each virtual character's native state, engine-side body and character
bookkeeping, root-motion and pending command queues, and pending collision events.
Snapshots retain immutable shapes and reconstruction settings, and keep the SDK
allocator/type registry alive, without retaining live characters or constraints.
Restore rebuilds borrowed ECS write-back rows and publishes restored dynamic body
and character poses.

`ValidateRuntimeState` checks the provider lifetime, configuration and native
definitions without applying the snapshot. Restore the corresponding ECS entities
and components first. Jolt reconstructs removed or replaced native bodies with
their recorded identifiers, recreates constraints in native solver index order,
and restores virtual characters and layer responses. Extra authored body definitions
or mismatching components are rejected with a diagnostic. Adding and removing
unrelated ECS entities is allowed. A snapshot from another scene or a restarted
provider is rejected.

The caller must coordinate the corresponding entity/component state and other
stateful providers. This interface alone does not restore scene clocks, scripts,
animation, audio or arbitrary ECS state. It is not an on-disk format or a promise
of bitwise simulation equivalence across platforms. Allocation failures or errors
during a native restore are reported; no general rollback transaction is claimed.

`kb_physics_state_tests` loads the actual provider module and compares 60 frames
after restoring body contacts, a distance constraint and a virtual character. It
exercises synchronous and pipelined stepping, unrelated ECS structural changes,
native body/character/constraint reconstruction, detached snapshot destruction,
and rejection of incompatible provider lifetimes and authored definitions.

## Lua VM state

`PucLuaScriptRuntime` implements the same optional provider interface. It captures
reachable script/module tables, cycles and shared aliases, function upvalues,
the standard math library RNG state, and exposed instance values. Retained Lua
references preserve object identities; a weak VM lifetime makes detached snapshot
destruction safe after the VM closes. Capture and restore use protected calls,
pause automatic garbage collection and bound graph depth, object count and payload.

Capture rejects coroutine instruction state, opaque userdata without a codec and
host-managed event subscriptions. Restore also rejects changed script/module code
catalogs or another VM lifetime. These refusals must be handled by the caller;
this VM snapshot does not capture script-host lifecycle, shared state or event
queues. Lua payload accounting measures captured values, excluding retained VM
object storage and container capacity. It is not a total history memory measure.

The added C++ runtime interface and lifetime member change the public Lua runtime
layout. Engine module ABI 18 requires rebuilding C++ modules; an older module is
rejected before its factory runs.
