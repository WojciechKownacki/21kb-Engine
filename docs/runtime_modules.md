# Runtime modules

Packaged project descriptors require one nonempty name per module reference,
including disabled references. Duplicate names are rejected before cooking and
when reading the mounted runtime manifest. This prevents contradictory activation
flags from selecting different providers during packaging and startup.

Android and browser hosts construct their linked providers from the descriptor.
The shared provider table defines supported names; an enabled unsupported provider
is an error. Desktop DLL paths in these descriptors do not require manual changes
for a static host. Windows packages resolve custom DLLs beneath their sealed
runtime root and reject missing files.
Explicit `Input` and `Script` references select host modules and require no DLL;
their activation flags still apply.

## Development reload

The module host loads replacement binaries into separate shadow copies before
detaching any scene. Missing binaries, export errors and ABI rejection leave the
current modules attached and their live resources intact. Diagnostics identify
the rejected replacement. Restoring a valid file allows another reload attempt.

After binary validation succeeds, the host detaches scenes, releases unused query
plans, disables and unloads the old modules, then initializes the replacements and
reattaches the scenes. Provider runtime state is recreated by those lifecycle
callbacks. Initialization failure after this point uses load rollback; it does
not restore the previous instance.

Before unloading, the host releases unused query plans from every attached
scene's world. A plan still owned by a live query is not discarded by this cache
cleanup; modules must release those queries during detach.

Component, tag and relation registries own their type-name keys. They do not keep
`std::type_index` objects pointing into a library after unload. The component
address cache is invalidated when a library closes; its fallback lookup uses the
owned name. Reloading the same type preserves its identifier and existing data.
ECS components remain restricted to trivially copyable, trivially destructible
values; this does not make plugin function pointers stored inside a value safe
after unload, or support changing a live component's binary layout.

The host takes ownership of a factory result immediately through the plugin's
exported destroy function. A metadata error or inconsistent module name destroys
the object before closing the library. Plugin-defined exceptions are also
destroyed while the library is mapped. Validated metadata is copied into the
host wrapper and is not requested repeatedly from the plugin.

The physics provider removes its scene system and backend on detach. Repeated
attachment is idempotent; reload constructs one replacement system rather than
leaving earlier simulations attached.

Modules must remove their systems, callbacks and registrations before unloading.
An external resource whose destructor or callback belongs to a plugin must not
outlive that plugin's library. Binary validation does not provide automatic
migration or lifetime tracking for arbitrary external registrations.

## Loader and payload ownership

Legacy C++ modules register dynamic asset loaders through
`EngineModuleContext::RegisterAssetLoader`. Copy the context received in `OnLoad`
when registration happens later during scene attachment. The context dispatches
registration to host code, which wraps the loader and its decoded or published
payloads with a shared library lifetime. Calling the plain `AssetManager` overload
directly from a dynamic plugin does not establish that ownership contract.

Remove the loader with `AssetManager::UnregisterLoader` during detach. Removal
cancels queued work, waits for active decoding, discards pending results and clears
that type's cached payloads. Replacement similarly drains the previous loader.
External handles preserve their original immutable payload; new loads use the
replacement loader. A preserved payload's original destructor runs while its DLL
is still mapped. The native library closes after its final dependent loader or
payload is released. Weak asset handles retain only host-owned control blocks and
can safely expire after the DLL closes.

Loaders and payload deleters must own their required state independently of the
module instance. Keeping code mapped does not preserve the module object after
`OnUnload`. Modules must also stop their own workers and remove other callbacks
before returning from detach. This is lifetime retention, not state migration.

Stable host integrations can use `EngineModuleLibrary::Retain` and the explicit
library-lifetime overload of `AssetManager::RegisterLoader`. Keep all ownership
wrappers in the host; transferring a raw plugin-owned control block without its
library lease is unsupported. The legacy module ABI is 14 for the extended
context and loader interface; rebuild C++ providers. The public C SDK stays at 1.

## Public C module SDK

External modules can include `engine/modules/EngineModuleSdk.h` without linking
engine libraries. Export `kb_get_engine_module_api` using `KB_MODULE_SDK_EXPORT`
and `KB_MODULE_CALL`. Version 1 requires exact structure sizes, the same pointer
width and the platform's default C structure alignment. Reject unsupported
versions or output sizes before writing the output. The host validates the
returned version, size, identity, dependencies and mandatory functions before
calling `create`. This entry takes precedence over legacy C++ module exports.

Only fixed-width values, opaque handles, borrowed byte views and C function
pointers cross this interface. The module allocates and destroys its own instance;
even a failed `create` transfers a non-null output to `destroy`. Fallible callbacks
return a status and may write the caller's fixed error buffer. Cleanup callbacks
must succeed; no callback may throw across the boundary. Legacy C++ module exports
still require a matching engine build and are not this SDK's binary contract.

The host table provides scoped entity creation, position access and scene update
registration. All operations run on the owning thread. Entity handles belong to
one module attachment and cannot be used with another scene. Update registration
is allowed during `attach`; userdata ownership transfers only on success. The
supplied release function runs exactly once on unregister, detach, failed attach
or unload, before the library closes. A callback cannot unregister itself while
executing; that operation returns `KB_MODULE_BUSY`.

The engine removes registrations before calling `detach`, then destroys remaining
SDK-created entities. Successful reload recreates these objects through `attach`;
their state is not migrated. Engine code may retain `EngineModuleCallback` handles.
After cleanup they return `KB_MODULE_STALE_HANDLE`, without retaining a function
pointer or plugin allocation. Scene objects must outlive their module host or be
explicitly detached before scene destruction. Destroying a host during callback
dispatch is unsupported.

`EngineModuleSdkPlugin.c` demonstrates a separate C plugin using only the public
header. Its test build uses a separate static C runtime on Windows and verifies
allocation cleanup, live scene updates, repeated reload, incompatible replacement,
partial factory/attachment failure and retained callback invalidation. This SDK
does not yet expose asset-loader or custom component-type registration.
