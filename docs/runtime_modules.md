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

The physics provider removes its scene system and backend on detach. Repeated
attachment is idempotent; reload constructs one replacement system rather than
leaving earlier simulations attached.

Modules must remove their systems, callbacks and registrations before unloading.
An external resource whose destructor or callback belongs to a plugin must not
outlive that plugin's library. Binary validation does not provide automatic
migration or lifetime tracking for arbitrary external registrations.
