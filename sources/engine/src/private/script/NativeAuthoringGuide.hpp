#pragma once

#include <string_view>

namespace kb::script {

inline constexpr std::string_view kNativeAuthoringGuide = R"guide(# C++ and Lua authoring

Create a native class in your project, compile its module against the installed
engine SDK, and attach its descriptor to an entity:

```powershell
kb_cli script-new --project . --name MotionProbe --language cpp
kb_cli entity add --project . --name Probe
kb_cli script-build --project . --script /Game/Scripts/MotionProbe.native
kb_cli scene-attach --project . --scene /Game/Scenes/Main.21kbscene --node Probe --script /Game/Scripts/MotionProbe.native
kb_cli run --project . --frames 3
```

`Source/NativeScripts/MotionProbe` contains a standalone CMake project.
`script-build` incrementally builds that project, without rebuilding the engine.
The generated registration checks the SDK ABI; keep that check. The class defines
static functions named like the Lua hooks (`Created`, `Activated`, `Ready`,
`FixedTick`, `Tick`, `LateTick`, `Deactivated`, `Destroyed`, each taking a
`ScriptExecutionContext*`), and `kb::script::RegisterNativeScript<Class>` registers
the ones it finds. In `Ready`, use public C++ API to change the scene:

```cpp
#include "engine/scene/SceneTransforms.hpp"

// Inside MotionProbe::Ready(kb::script::ScriptExecutionContext* context):
auto transforms = context->GetScene().Transforms();
auto transform = transforms.Get(context->Self());
transform.localPosition.x = 3.0F;
transforms.Set(context->Self(), transform);
```

A field shown in the Inspector is one line in the `.native` file, `expose = speed Float =
2.5` (types as in Lua `@expose`: Bool, Int, Float, String...). Read it in a callback
with `kb::script::NativeField(*context, "speed")` from
`engine/script/NativeScriptFields.hpp`: the value set on that object, else the default.
The values are saved with the scene, so a rebuilt module sees them unchanged.

Other shared gameplay operations are available through `context->CallFunction`
and the function-specific API topics. Script errors and build failures return
diagnostics and a nonzero CLI exit code; a compiler error is listed with its file
and line (`Probe.cpp(12): error ...`), also in the editor Console.

For Lua, create `LuaProbe` with `--language lua`, edit
`Assets/Scripts/LuaProbe.lua`, validate it with `kb_cli validate --project .
Assets/Scripts/LuaProbe.lua`, and attach `/Game/Scripts/LuaProbe.lua` in the same
way. The matching scene change is:

```lua
function Ready(self)
    Transform.SetPosition(self.entity, 3, 0, 0)
end
```

In Editor Settings > General > Code editor, choose Automatic, VS Code or Visual
Studio. The project stores the choice in `Config/EditorSettings.ini`, section
`[SourceCode]`, key `Editor` (`Auto`, `VSCode`, `VisualStudio`). Automatic prefers
VS Code, then Visual Studio; a missing explicitly selected IDE reports an error.
Double-click a `.native` or `.lua` asset: the existing asset activation route opens
its source in the selected IDE. Native sources open the nearest project CMake
folder; Lua opens the game project folder. Visual Studio discovery uses
`VSINSTALLDIR` or the installed `vswhere`; VS Code uses its install locations or PATH.

After saving C++, run `script-build` again and restart Play to load the rebuilt
module. After saving Lua, restart Play; an open Lua document also observes external
saves. Export cooks the descriptors and stages native modules with the same SDK
and runtime backend. Native code requires a compatible C++ toolchain on the authoring
machine; the exported Windows player does not compile project sources at startup.
)guide";

} // namespace kb::script
