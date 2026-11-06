# Terrain level of detail

A terrain (`.kbterrain`) is drawn as a grid of chunks (`chunkQuads` quads across, 32 by default). Every chunk exists at
`lodCount` levels of detail; level `n` steps over `2^n` quads, so each level has a quarter of the triangles of the one
before it. The scene pipeline picks the level of every chunk for every view and pass, not one level for the whole terrain.

## How a level is chosen

The builder measures, for every chunk and level, how far the surface of that level strays from the full-resolution
surface (the level error, in mesh units, never decreasing with the level). A chunk shows the coarsest level whose error,
projected into the pixels of the view that draws it, stays under the terrain's `lodPixelError` (2 pixels by default,
0.25 to 16). The size of a pixel comes from the camera and the height of its target; an orthographic view uses one size for every
chunk. A shadow map is drawn at the levels the view it shades picks, so the surface that casts a shadow is the surface
that view draws. Flat ground
therefore drops to the coarsest level at once while rough ground keeps its detail near the camera. A mesh instance's
`lodBias` and `lodEnabled` apply on top.

`lodPixelError` is a setting of the terrain: `kb_cli terrain settings --terrain <file> --lod-pixel-error 2`, or the
Advanced drawer of the Terrain bar.

## Seams

Neighbouring chunks share their border vertices, so two chunks at the same level meet exactly. Where one chunk is drawn
at another level than a neighbour, its **skirt** is drawn too: a wall hung below the border of the chunk, as deep as the
errors of the two borders can part them (`skirtDepth`, computed with the level errors and kept up to date when a brush
changes the heights, and never under half a cell). The skirt vertices follow the grid in the vertex buffer, one run per
chunk. A chunk draws its skirt only beside a neighbour at another level.

## Draw calls and layers

A chunk is one draw per material layer at its level, plus its skirt where it needs one. A material layer is drawn in a
chunk only where its weight reaches 3% of the surface somewhere in the chunk; the faint tail of a soft brush costs no pass.

## Tests

`kb_terrain_lod_tests` checks the chunk grid the builder writes, that for every pair of levels of two neighbouring chunks
the skirt of the higher border reaches the lower one along the whole shared edge, that a chunk beside another level draws
its skirt, and that the triangles of a terrain in view fall as the camera moves away. `kb_terrain_lod_tests bench`
prints the triangles, draws and pipeline time of 1025x1025 and 2049x2049 terrains from the ground and from above.
`kb_cli_terrain_lod_images` (GPU) renders a hilly terrain at three level errors and requires the pictures to agree.
