# Third-Party Licenses

This directory contains external dependencies used by 21kb-Engine.

`third_party_manifest.json` is the complete, machine-readable inventory of every third-party
component compiled into, linked into or shipped with a 21kb product, with its license, copyright
and license text. Packaging builds each player's `THIRD_PARTY_NOTICES.txt`, `Licenses/` directory
and CycloneDX SBOM (`sbom.cdx.json`) from it; `scripts/third_party_notices.py --product engine`
builds the same for the editor and tools. Add a dependency there before it is built into a product.

## bgfx

- Repository: https://github.com/bkaradzic/bgfx
- Local path: `third_party/bgfx`
- Revision: `0d4141d`
- License: BSD 2-Clause
- License file: `third_party/bgfx/LICENSE`

bgfx copyright and license notices must be retained in source distributions.
For binary distributions, the bgfx copyright notice, license conditions and
disclaimer must be reproduced in the documentation and/or other materials.

## bgfx.cmake vendor bundle

- Repository: https://github.com/bkaradzic/bgfx.cmake
- Local path: `third_party/bgfx.cmake`
- Includes: `bgfx`, `bx`, and `bimg`
- License: CC0 1.0 for the CMake wrapper, BSD 2-Clause for `bgfx`, `bx`, and `bimg`
- License files: `third_party/bgfx.cmake/LICENSE`, `third_party/bgfx.cmake/bgfx/LICENSE`,
  `third_party/bgfx.cmake/bx/LICENSE`, `third_party/bgfx.cmake/bimg/LICENSE`

The bundled bgfx family copyright and license notices must be retained in
source distributions. For binary distributions, the BSD copyright notices,
license conditions, and disclaimers must be reproduced in the documentation
and/or other materials.

## Flecs

- Repository: https://github.com/SanderMertens/flecs
- Local path: `third_party/flecs`
- Revision: `e3f4d11`
- License: MIT
- License file: `third_party/flecs/LICENSE`

Flecs copyright and permission notices must be included in all copies or
substantial portions of the software.

## miniaudio

- Repository: https://github.com/mackron/miniaudio
- Local path: `third_party/miniaudio`
- Revision: `9634bed`
- License: Unlicense or MIT No Attribution, at your option
- License file: `third_party/miniaudio/LICENSE`

miniaudio can be used under either license option documented in its license
file. If the MIT No Attribution option is chosen, no attribution is required by
the license text.

## ufbx

- Repository: https://github.com/ufbx/ufbx
- Local path: `third_party/ufbx`
- Revision: `fcc5d6ba444cfd3eb80677dba5e37e493941abe5`
- License selected: MIT
- License file: `third_party/ufbx/LICENSE`

ufbx is supplied as its upstream single-file loader (`ufbx.c` and `ufbx.h`).
The MIT copyright and permission notice must be included in all copies or
substantial portions of the software.

## Jolt Physics

- Repository: https://github.com/jrouwe/JoltPhysics
- Local path: `third_party/jolt`
- Revision: `c581b5f`
- License: MIT
- License file: `third_party/jolt/LICENSE`

Jolt Physics copyright and permission notices must be included in all copies or
substantial portions of the software.

## Monocypher

- Website: https://monocypher.org
- Release: `4.0.2`, fetched by CMake from
  `https://monocypher.org/download/monocypher-4.0.2.tar.gz` at a pinned SHA-256
- Files used: `src/monocypher.c`, `src/optional/monocypher-ed25519.c`
- License selected: BSD 2-Clause (dual licensed with CC0 1.0)
- License file: `third_party/licenses/monocypher-4.0.2.txt`

Monocypher provides the engine's signatures, hashes, message authentication and
authenticated encryption. For binary distributions the BSD copyright notice,
license conditions and disclaimer are reproduced with the shipped licenses.

## Zstandard

- Repository: https://github.com/facebook/zstd
- Release: `1.5.7`, fetched by CMake from
  `https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz` at a pinned SHA-256
- Files used: `lib/common`, `lib/compress` and `lib/decompress` (no legacy formats, no
  dictionary builder, no assembly decoder)
- License selected: BSD 3-Clause (dual licensed with GPLv2)
- License file: `third_party/licenses/zstd-1.5.7.txt`

Zstandard compresses the blocks of asset packs. For binary distributions the BSD
copyright notice, license conditions and disclaimer are reproduced with the shipped
licenses.

## etcpak

- Repository: https://github.com/wolfpld/etcpak
- Local path: `third_party/bgfx.cmake/bimg/3rdparty/etc2`
- License: BSD 3-Clause
- License file: `third_party/bgfx.cmake/bimg/3rdparty/etc2/LICENSE.txt`

The ETC2 codec is the copy embedded by bimg. Its copyright notice, license
conditions, and disclaimer must accompany source and binary distributions.

## Box2D

- Repository: https://github.com/erincatto/box2d
- Local path: `third_party/box2d`
- Revision: `f2086ed`
- License: MIT
- License file: `third_party/box2d/LICENSE`

Box2D copyright and permission notices must be included in all copies or
substantial portions of the software.

## Heroicons

- Repository: https://github.com/tailwindlabs/heroicons
- Local path: `third_party/heroicons`
- Revision: `616b7a4dbbf3d011760af8066262cd5c6b3868f3`
- License: MIT
- License file: `third_party/heroicons/LICENSE`

Heroicons copyright and permission notices must be included in all copies or
substantial portions of the software.

## Lucide

- Repository: https://github.com/lucide-icons/lucide
- Local path: `third_party/lucide`
- License: ISC
- License file: `third_party/lucide/LICENSE`

Lucide copyright and permission notices must be included in all copies.

## DejaVu Fonts

- Repository: https://github.com/dejavu-fonts/dejavu-fonts
- Local path: `sources/editor/content/EditorShell/Fonts`
- Files: `DejaVuSans.ttf`, `DejaVu-LICENSE.txt`
- License: DejaVu Fonts License / Bitstream Vera derived license
- License file: `sources/editor/content/EditorShell/Fonts/DejaVu-LICENSE.txt`

DejaVu font copyright and license notices must be retained with the font files.

## Components bundled with bgfx, bx and bimg

Compiled into every player:

- TinySTL (BSD 2-Clause), inside bx
- Arm ASTC Encoder (Apache 2.0)
- TinyEXR (with OpenEXR code) (BSD 3-Clause; text in `licenses/tinyexr.txt`)
- miniz (MIT)
- LodePNG (zlib)
- stb (stb_image, stb_truetype) (MIT or Unlicense)
- meshoptimizer (MIT), also compiled into the renderer's mesh baker
- cgltf (with jsmn) (MIT), the glTF reader

21kb patches cgltf, bimg and stb_image in place to harden them against malformed files; each change is
marked with a `21kb:` comment and described under `modified` in `third_party_manifest.json`.
- DirectX-Headers (MIT), Windows players
- Khronos API headers (OpenGL, OpenGL ES, EGL, Vulkan) (MIT and Apache 2.0; text in `licenses/khronos.txt`)
- RenderDoc in-application API header (MIT; text in `licenses/renderdoc.txt`)
- Dawn and Tint (WebGPU) (BSD 3-Clause), WebGPU players
- AndroidX AppCompat, Games Activity and their AndroidX dependencies (Apache 2.0), Android players

Compiled only into the editor, the cooker and the shader compiler:

- edtaa3 (MIT)
- Android ETC1 encoder (Apache 2.0)
- Image Quality Assessment (IQA) (BSD 2-Clause)
- libsquish (MIT)
- NVIDIA Texture Tools (BC6H/BC7 codecs) (MIT and Apache 2.0)
- PVRTC codec (BSD 2-Clause)
- fcpp (BSD 2-Clause)
- GLSL Optimizer (MIT)
- glslang (BSD 3-Clause, BSD 2-Clause, MIT and Apache 2.0)
- SPIRV-Cross (Apache 2.0)
- SPIRV-Tools (Apache 2.0)
- Tint (WGSL shader compiler) (BSD 3-Clause)

## Lua

- Website: https://www.lua.org
- Version: 5.4.8, fetched at configure time with a pinned SHA-256 (root `CMakeLists.txt`)
- License: MIT
- License file: `third_party/licenses/lua-5.4.8.txt`

## Editor icons

- Fluent UI System Icons (MIT), `third_party/fluentui-system-icons`
- Fluent UI toolbar icons (MIT), `third_party/luizengine-fluentui-toolbar`
- Devicon (MIT), `third_party/devicon`

Box2D is vendored but not built into any product.

## Notes

The root `LICENSE` file applies to 21kb-Engine itself. Third-party dependencies
remain under their own licenses.
