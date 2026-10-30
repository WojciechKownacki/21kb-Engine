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

`PucLuaScriptRuntime` implements the same optional provider interface. At a completed
execution boundary it copies every live Lua allocation and pins its address until
the image is released. Restore copies those bytes back, including suspended thread
stacks, instruction state, closures, tables, registry, garbage collector and standard
library RNG/iterator userdata. It also restores the script/module code catalog,
coroutine references, exposed values and debugger state. Retained API closures use
the current execution context when a coroutine resumes in another frame.

Images belong to one live VM in the same process. They cannot serialize external
native resources referenced by light userdata or extensions. Capture and restore
reject execution in progress and host-managed event subscriptions. This VM snapshot
does not capture script-host lifecycle, shared state or event queues. A weak VM
lifetime rejects use after close; the image owns its allocator storage so detached
image destruction remains safe.

`PayloadBytes()` reports copied Lua allocation bytes. `LuaMemoryOwnedBytes()` reports
allocator capacities, block headers, retained dead blocks and image storage; the
configured Lua memory budget includes that history. It excludes C++ runtime catalogs,
system allocator overhead and process RSS, so it is not total world history memory.
Releasing images frees their copies and any dead blocks no remaining image needs.

Engine module ABI 19 requires rebuilding C++ modules after the runtime allocator
change; an older module is rejected before its factory runs.
