# Browser save storage

The Web player mounts Emscripten IDBFS and loads existing data before creating
the scene or running scripts. Its writable root is scoped to the page's origin
and URL directory. Deployments in different directories have separate slots;
query strings do not change their identity. Keep that directory stable across
updates. Moving an export does not migrate its saves.

`Save.Write(slot)` and `Settings.Write(slot)` serialize the normal authenticated
save format into separate files in the host's sandboxed `Saves` directory.
Their `written` result confirms the working filesystem write. On Web it does
not confirm an IndexedDB commit. Use the same APIs from Lua, native scripts or
visual graphs; the script function catalog exposes their signatures.
Lua uses `CallFunction("Save.Write", {slot="slot_1"})` for these catalog
operations. Single-output operations return that value directly;
`CallFunction("Save.StorageStatus")` returns the status/diagnostic table.

After writing, call `Save.Flush()` or `Settings.Flush()`. The returned `started`
boolean confirms that synchronization was accepted, including an operation
already in progress. Both domains share the storage transaction. Poll
`Save.StorageStatus()` or `Settings.StorageStatus()` on later frames:

| Status | Meaning |
| --- | --- |
| `Clean` | The browser transaction completed for all tracked writes. |
| `Dirty` | Working files have changes that need another flush. |
| `Pending` | An asynchronous transaction is in progress. |
| `Failed` | Synchronization failed; `diagnostic` describes the error. |
| `Unavailable` | The host has not configured persistent storage. |

A write made during a flush cannot be acknowledged by that earlier operation.
If the result becomes `Dirty`, request another flush. Quota failures and aborted
transactions remain `Failed` until a new write or explicit retry; they never
become `Clean` merely because a frame passed. Report completion to the player
only after `Clean`. A failed working write still returns `written=false`,
independently of the synchronization status.

Each flush persists only that page's tracked file mutations and directory
entries in one IndexedDB transaction. Independent pages can save different
slots without deleting each other's files, including when both pages started
before either slot existed. A failed request aborts the whole captured batch.
Writes to the same named slot use the last committed transaction. Pages keep
their own working view; reload to read another page's latest committed files.
The existing IDBFS database format remains compatible.

A timed out operation requires reloading before retrying, since its browser
transaction could still be running. Late callbacks cannot change that failure
to success. Await a native `WriteAsync` future before flushing its results.

Flush while the page is running. Closing a page does not wait for asynchronous
work, and this player does not promise an unload flush. Browser data clearing,
private profile lifetime and browser storage eviction remain browser policies.
Initialization denial or a timed out initial read stops startup with
`data-kb-error=storage-init`; the player does not silently use volatile saves.

On native hosts, writes already reach their normal filesystem and `Flush`
completes immediately with `Clean`. This API does not add an operating-system
`fsync` or claim protection against physical power loss. The browser bridge
runs on the Web player's main thread; threaded Web storage is not verified.

The browser behavior follows the
[Emscripten filesystem contract](https://emscripten.org/docs/api_reference/Filesystem-API.html#idbfs).
The engine links the SDK's licensed IDBFS library without copying its source.

`sources/game/tests/web/run_game_storage_tests.py` authors a fresh project with
the CLI, cooks it, clicks a native UI button and observes the normal Lua save
API. It checks actual hardware rendering, closes and reopens the owned browser
with the same profile, verifies save/settings values, imposes a real quota on
its local origin, aborts a real IndexedDB transaction, and checks the preceding
committed data after restart. A separate profile verifies storage denial.
After a quota failure it removes the local quota limit and retries through the
native button. The tracked revision must stay unchanged and the committed
values must survive another browser restart.

For the timeout case, the test keeps an actual IndexedDB transaction active,
which queues the player's synchronization behind it. The production deadline
remains 15 seconds. The timeout must report failure and refuse an overlapping
retry. Releasing the held transaction allows the actual queued commit to finish;
its late callback must leave the reported timeout unchanged. Reopening the
browser then reads the resulting valid data. No save result or completion
callback is substituted.
The expected value comes from the native document captured at the flush
request, so the test distinguishes that batch from a later unconfirmed edit.

`sources/game/tests/web/run_game_storage_pages_tests.py` opens two players in
one owned profile and origin before either writes. Trusted text and Enter input
select independent native save slots, and both flushes must complete. A third
player must read both values. GPU captures verify the native fields and every
player must retain a live hardware context with compiled shaders. This checks
independent named files; it does not merge concurrent edits within one slot.

Different URL directories must remain isolated. Missing hardware, unexpected
fallback, failed checks and timeouts fail the test. Fault injection is limited
to that test's own local page, origin and profiles.

`kb_engine_tests script-save` runs the focused native script save/settings
contract, including flush/status discovery and unconfigured storage. It also
remains part of the full script suite.
