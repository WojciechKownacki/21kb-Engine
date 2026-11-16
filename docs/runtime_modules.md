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
library lease is unsupported. The legacy module ABI is 27 for the extended
context, loader and component interface; rebuild C++ providers. The public C SDK stays at 1.

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

## Asset types and components from a module

A C++ module can add its own asset types and its own component types. Both are registered when the module attaches
to a scene and removed when it detaches, and neither needs a second registry: loaders go to the scene's
`AssetManager`, component types to the scene's ECS world.

### An asset type

Register a loader whose `Extensions()` names the file extension. Discovery gives every file with that extension the
loader's type, and the manager loads it like a built-in type, synchronously or asynchronously. The loader should read
its input with `AssetLoadRequest::ReadSourceBytes`, which serves both a loose file and a cooked package. A package
stores the source bytes unchanged, so the bytes a game loads are the bytes that were cooked. Override
`DiscoverDependencies` to name the assets a file depends on; the dependency list travels in the package manifest.

### A component type

A component is a trivially copyable struct. Describe its fields once, in the order and with the labels the Inspector
should show:

```cpp
struct Health { float current; std::int32_t maximum; bool regenerates; };

constexpr kb::scene::SceneModuleComponentField kFields[] = {
    KB_SCENE_MODULE_FIELD(Health, current, kb::ecs::ComponentFieldType::Float32, "Current", "Hit points left."),
    KB_SCENE_MODULE_FIELD(Health, maximum, kb::ecs::ComponentFieldType::Int32, "Maximum", "Hit points when full."),
    KB_SCENE_MODULE_FIELD(Health, regenerates, kb::ecs::ComponentFieldType::Bool, "Regenerates", "Heal over time."),
};

class HealthModule final : public kb::modules::IEngineModule {
public:
    void OnLoad(kb::modules::EngineModuleContext& context) override { context_ = context; }
    void OnSceneAttach(kb::scene::Scene& scene) override {
        if (!context_->RegisterSceneComponent<Health>(scene,
                { .name = "Health", .label = "Health", .category = "Gameplay", .fields = kFields })) {
            throw std::runtime_error("Health registration failed");
        }
    }
    void OnSceneDetach(kb::scene::Scene& scene) override {
        static_cast<void>(context_->UnregisterSceneComponent(scene, "Health"));
    }
    // Metadata() omitted
private:
    std::optional<kb::modules::EngineModuleContext> context_;
};
```

Once registered, the component is an ordinary ECS component: `world.Set<Health>(entity, value)` adds it and queries
such as `world.CreateQuery<Health>()` find it. In addition:

- Scenes and prefabs save it. A scene file stores each module component by name with its values encoded by field name
  and type (scene file version 68; older files load with none). A prefab asset stores the same bytes as hexadecimal
  text, only on nodes that have module components.
- Scripts read and write it by name through the existing component property calls, for example
  `self:GetProperty("Health", "current")`. A vector field gives one property per lane, such as `offset.x`.
- Tools describe it from the registration: `EngineLibraryComponentInspectorRegistry::Find("Health")` returns the label,
  category, and a labelled, tooltipped row for every field, and `ScriptSceneComponentApi::ComponentProperties("Health")`
  returns the script-visible fields. The descriptions stay valid until the process ends.
- A name used by a built-in component is refused.

### Data while no module provides the type

Saved values are never dropped. A scene that holds a component whose module is not loaded loads normally; the
values stay beside their object as opaque bytes and are written back unchanged when the scene is saved. The same
happens when a module unloads or is replaced during development: `UnregisterSceneComponent` moves the live values of every object to
opaque bytes and removes the component from the objects, and registering the type again (after the reload) turns the
bytes back into live components by field name. So a hot reload keeps the data of module components.

A value is applied only when it carries every registered field with the same type. A value that does not fit the
reloaded type, for example after a field was added, stays opaque and unchanged, and
`SceneModuleComponents::Diagnostics()` reports it. Changing the binary layout of a component that live data still
uses is not migrated beyond this; restore the type to read the data again.

### Defaults and prefabs

`RegisterSceneComponent<T>(scene, desc, defaults)` takes the value a new component gets; it is `T{}` when left out, so
a constructor or default member initializers of the type give it. Add Component uses it, and the Inspector marks the
fields that differ from it and resets a field to it.

A field of a module component edited in a prefab instance is recorded as an override of that field alone, so a prefab
update keeps it and changes the other fields. Overrides are a list of property overrides rather than flag bits:
`moduleComponents.<name>/<field>` for a field, and `moduleComponents.<name>` for a component the instance added or
removed. Revert puts the prefab's component back and Apply copies the instance's component into the prefab. An
override of a field the prefab no longer has changes nothing. The override list adds no scene file field; files that
predate module components load with none.

### Limits

A component name cannot contain a slash. A module's Inspector section is drawn from its registered fields, with the
label and tooltip of each; there is no limit on how many module components a project registers. Revert and Apply work
on a whole component, as they do for built-in ones.
