#pragma once

#include <string_view>

namespace kb::script {

inline constexpr std::string_view kPbrMaterialsGuide = R"guide(# Builtin PBR advanced materials

Author a `builtin.pbr` `.kbmat` asset with `sheenColor` (linear RGB) and
`sheenRoughnessFactor`, `transmissionFactor`, or `anisotropyStrength` and
`anisotropyRotation` (radians). Zero weights retain ordinary PBR. Transmission
also reads `thicknessFactor`, `attenuationColor` and `attenuationDistance`
(zero distance means no absorption). The same fields exist in public C++:

```cpp
kb::render::RenderMaterialDesc material;
material.sheenColor[0] = 0.8F;
material.sheenRoughnessFactor = 0.6F;
material.transmissionFactor = 0.5F;
material.anisotropyStrength = 0.8F;
material.anisotropyRotation = 0.9F;
// Register with RenderResourceRegistry::RegisterMaterial(material).
```

An authored asset starts with `version 1`, `materialType builtin.pbr`, and
`materialTypeVersion 1`, followed by one parameter/value per line. For example:

```text
baseColor 0.8 0.8 0.8 1
roughnessFactor 0.4
sheenColor 0.8 0.04 0.02
sheenRoughnessFactor 0.6
```

Assign the saved asset through the shared gameplay operation. From C++, call
`context->CallFunction("MeshRenderer.SetMaterial", arguments)` with named
`material` (virtual path) and optional `entity` arguments. In Lua:

```lua
assert(MeshRenderer.SetMaterial("/Game/Materials/Fabric.kbmat", self.entity))
```

The scalar and color fields survive asset load, save and cook. Runtime scalar/bool
material-instance overrides remain a graph-material interface; they reject builtin
parameters rather than reporting an ignored successful write.

Advanced builtin surfaces use the shared forward composition pass in forward and
deferred scenes, keeping their opaque/masked depth and blend state. They are omitted
from the earlier depth/GBuffer pass, so the opaque snapshot contains the scene behind
transmitting surfaces. Sheen uses a Charlie lobe; anisotropy shapes GGX around the
mesh tangent. Transmission reads the opaque scene snapshot with a view-normal
refraction offset and Beer absorption; without a snapshot it samples the refracted
environment. It cannot show offscreen objects or other forward-composited surfaces.
Advanced surfaces follow the existing forward-composition motion-vector limitations.
The sheen/transmission/anisotropy texture slots remain explicitly unsupported;
the numeric fields described here are evaluated by the shader.

`kb_editor_advanced_pbr_package` checks C++/Lua assignment, zero/nonzero and secondary
parameter GPU changes, exact return to zero, editor scene reload and a source-free
cooked Windows frame. Other runtime backends require their own qualification.
)guide";

} // namespace kb::script
