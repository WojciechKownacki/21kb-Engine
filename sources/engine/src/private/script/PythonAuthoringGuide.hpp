#pragma once
#include <string_view>
namespace kb::script {
inline constexpr std::string_view kPythonAuthoringGuide=R"guide(# Embedded Python editor authoring

Windows x64 editor builds bundle pinned CPython 3.14.10. `kb21` is a built-in
in-process editor module: it calls the C++ editor authoring host directly, then
uses existing SceneCreateMenu, ScriptSceneComponentApi, editor history and scene
save backends. It never launches kb_cli or a Python subprocess.

Run trusted author scripts with `kb_editor --project <file.21kbproject>
--python-script <Author.py>`. This opens the document model, runs Python and exits
without a window. Save explicitly; a successful script does not implicitly save.
A headless editor scenario can execute `{ "op":"python_script", "path":"..." }`
against its current document. Errors return a failing exit/report with the Python
exception type/message. The interpreter uses the bundled standard library and
ignores host PYTHONPATH/site-packages. Each execution has fresh script globals;
standard imported modules persist for that interpreter's lifetime.

```python
import kb21
cube = kb21.create_entity("Block", "Cube")
kb21.set_property(cube, "Transform", "localPosition.x", 2.0)
assert kb21.get_property(cube, "Transform", "localPosition.x") == 2.0
kb21.add_component(cube, "Tags")
kb21.undo()
kb21.redo()
kb21.save_scene()
kb21.log("Block saved")
```

`schema()` returns the existing component/property catalogue (including writable
flags and integer enum names). Values are scalar bool/int/float/string according
to that catalogue. Entity/component/hash/Int64 values use decimal strings to keep
all 64 bits. Entity handles are opaque strings; `find_entity(name)` requires a
unique name, rejects missing/ambiguous names and can reacquire handles after undo,
redo or a reload. `delete_entity(handle)` removes an object through history.
Each successful mutation is undoable; an invalid property/value preserves the
scene. Script exceptions stop execution; earlier successful operations remain
in the document and can be undone. There is no script-wide implicit transaction.

The public C++ surface is `kb/editor/authoring/EditorAuthoring.hpp`:
`IEditorAuthoringHost::Request(json,response,error)` is the common host contract;
`EditorPythonRuntime::Execute(host,code,filename,error)` and `ExecuteFile` embed
Python against it. Request op names match the Python functions; fields are
`name,preset`, `entity,component,property,value` or `message`. `schema` returns
`components`; create/find return `entity`; get returns `value`. Calls and runtime
construction/destruction belong to the editor owner thread. Worker-thread author
calls, stale handles, unknown/read-only properties and authoring during Play fail
explicitly. No interpreter pointer or Python object crosses the public header.

This is a trusted authoring API, not a sandbox or gameplay language. It does not
provide pip packages, Python panels, a REPL, arbitrary project asset writers or
runtime Python in exported games. Existing C++/Lua gameplay consumes the saved
scene normally. Other hosts explicitly report embedded Python unavailable until
a corresponding interpreter distribution is integrated and qualified.
)guide";
}
