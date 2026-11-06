#pragma once
#include <string_view>
namespace kb::script {
inline constexpr std::string_view kEditorExtensionGuide=R"guide(# External C++ editor extensions

`kb/editor/extensions/EditorExtension.h` is the versioned C ABI for external
editor DLLs. No engine library, private header or engine rebuild is needed by a
module. The complete small C++ example and standalone CMake project are in
`examples/EditorExtension`. Configure it with `-DKB_EDITOR_SDK_ROOT=<engine SDK>
-DKB_EXTENSION_OUTPUT=<project>/Binaries/EditorExtensions`, then build the
SampleExtension target with the project's compiler. It exports
`kb_editor_extension_entry(host,outDescriptor)`.

Copy Sample.kbeditorext to the project's `Editor/Extensions` folder. Schema 1 has
exactly `schema`, `id` and `module`; module is a project-relative DLL path such as
`Binaries/EditorExtensions/SampleExtension.dll`. The Windows editor loads these
manifests on startup. Installation errors appear in its Console and leave the
base editor available. Editor DLLs/manifests are author tools outside Assets;
exported games do not load them.

The entry checks host version and size, creates module state, fills a matching
version/size/id/user/shutdown descriptor, then registers actions and panels.
A panel is an ordinary dock tab with a copied UTF-8 paragraph and one action
button; its action must belong to that module. The sample button creates or
updates ExtensionCube through the common editor authoring request backend,
then changes its panel text. Actions may call `host->request` with the JSON
contract documented in PythonAuthoring. This invokes C++ directly; Python and
kb_cli are not involved in extension action execution.

Registration is allowed only during entry. Authoring requests become available
after successful installation; failed installation therefore cannot leave scene
edits. All registered actions/panels roll back on a rejected entry, incompatible
SDK version or descriptor. Each accepted module owns its callbacks and copied
panel state. Unload first removes callbacks and dock cards, calls shutdown while
the DLL is still loaded, then releases its module handle. The module must join
workers and free its own allocations in shutdown. Reusing host pointers after
unload is invalid. Callback execution and all host APIs belong to the owner
thread. DLLs are trusted native code, not sandboxed plugins.

The public headless editor automation controls are `extension_load_project`,
`extension_install` with manifest `path`, `extension_unload` with module `id`,
`extension_invoke` with action `id`, and `extension_click` with panel `id`.
`capture` accepts `panel:"extension:<panel-id>"` and paints the production panel.
This provides explicit install/unload/action control without a separate plugin
management window. Existing editor history/save/reload works for resulting scene
content; unloading a tool preserves authored content.

Limits: Windows DLL host qualified, 8 modules, 128 total actions / 32 total panels,
32 actions / 8 panels per module. IDs are bounded ASCII identifiers; labels/titles
are at most 128 UTF-8 bytes, panel body 8192 bytes. Minimal panels have no arbitrary
widget tree, custom graphics callback or floating window. Their temporary IDs
are excluded from saved workspaces; workspace restore/reset keeps installed tools
registered. This SDK has a separate version from gameplay native-script/module
ABIs. Other operating-system loaders and a graphical install manager are outside
this initial extension point.
)guide";
}
