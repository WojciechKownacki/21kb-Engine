# Sprite 2D and Animated Sprite

A **Sprite 2D** draws a picture, or one cell of a sprite sheet, on its own in a scene, without a tile map. An **Animated Sprite**
is the same component with a frame animation: `frameCount` above 1 plays that many cells, starting at `cell`.

The component belongs to the World 2D plugin (`World.Map2D`, on by default). The plugin draws every sprite through a hidden child
object with a generated quad, built with the same mesh builder, materials and packaging as a tile map. The child is never saved
and does not show in the Hierarchy; picking it selects the sprite's object.

## Fields

| Field | Meaning |
|---|---|
| `imageAssetId` | the picture (a project image); `component set ... --set image=/Game/Sprites/Hero.png` chooses it by path and records its size |
| `materialAssetId` | optional sprite material (`--set material=/Game/Materials/Sprite.kbmat`); empty uses the built-in unlit cutout material |
| `cellWidth`, `cellHeight` | size of one cell in pixels; 0 uses the whole image (height 0 with a width: square cells) |
| `margin`, `spacing` | pixels around and between cells |
| `imageWidth`, `imageHeight` | size of the picture; filled in when the picture is chosen, read from the image when 0 |
| `cell` | the cell shown, row by row from the top left; the first frame of an animation |
| `frameCount`, `framesPerSecond`, `loop`, `playing`, `time` | the animation: cells `cell` to `cell + frameCount - 1`, speed, wrap or stop on the last, pause, clock in seconds |
| `pixelsPerUnit` | picture pixels per scene unit (a 32 pixel cell at 32 is one unit) |
| `pivotX`, `pivotY` | pivot as a fraction of the cell from its top left (0.5, 0.5 is the centre, 0.5, 1 the bottom centre) |
| `flipX`, `flipY` | mirror about the pivot |
| `tintR`, `tintG`, `tintB`, `tintA` | colour multiplier and opacity, 0 to 1 |
| `sortingOrder` | higher draws in front, -1000 to 1000 |
| `wall` | upright plane facing -Z for a side view (default), or a flat plane for a top-down map |
| `visible` | draw the sprite |

While editing, an animated sprite shows the frame at `time`; the animation only runs while playing. Scripts read and write every
field through the component API (`Sprite2D`).

## From the command line

    kb_cli entity add --project P --create "Animated Sprite" --name Hero
    kb_cli component set --project P --entity Hero --type Sprite2D --set image=/Game/Sprites/Hero.png --set cellWidth=32 --set pixelsPerUnit=32

A picture that is not an image of the project is refused with the reason.

## Colours

Sprites use an unlit material, so a scene without image settings is brightened by automatic exposure. The editor's 2D map view
sets up image settings without tonemapping or exposure (`/Game/Maps/PP_Map2D.kbppfx`); for a scene of sprites only, create such
a profile (`kb_cli post-process-new`, then `post-process-set` with `tonemapEnabled=0` and `outputTransform.autoExposure.enabled=0`)
and assign it with `kb_cli scene-post-process`. Automation scenarios use the `exact_colors` step.

## Limits

One animation per sprite (a run of consecutive cells); several named clips need several sprites or a script that changes `cell`
and `frameCount`. A sprite is one quad: no mesh deformation or skeletal 2D animation.

## Tilemap animation and bounds

Map dimensions are 1–4096 tiles on each axis. Coordinates start at zero and must be less than the corresponding dimension; floors range from -50 through 50. A map supports 256 sheets and 32 combined item/border sprites per cell. Invalid dimensions and out-of-range imported cells are rejected; an intentional valid shrink removes cells outside the new area and can be undone in the editor.

Maps store sparse cells, grouped at runtime into occupied 16×16 chunks per floor. An animation frame change rebuilds only chunks containing changed frames. Unchanged frames and paused animation do not rebuild geometry.

In the map animation editor, the speed form also sets a phase in seconds for selected instances of that graphic. The optional sixth sprite-transform value stores this phase: `sheet:cell@rotation/flip/tint/offsetX/offsetY/phaseSeconds`. Existing five-value sprites retain phase zero. Clips loop through their frame list at their authored FPS. CLI painting accepts `--phase <seconds>` with `--sprite` and `--cell`.

Scripts can call `CallFunction("Map2D.SetAnimationPaused", {entity=self.entity, paused=true})` and pass `false` to resume from the same animation clock. This controls one map without pausing simulation. Reloading the source resets its clock while retaining its pause state; starting a new play session starts unpaused. Authored instance phases survive save/reload.

## Tilemap floors and drawing order

Sprite2D and TilemapRenderer use the same drawing-order unit: each `sortingOrder` step moves the image 0.5 local units toward the viewer (negative Z for wall maps, positive Y for flat maps). On the same plane and floor, a higher order draws in front. This is a depth offset, not a screen overlay: entity transforms and map floors still affect depth. Use matching transforms and floors for layers that should be ordered only by this setting.

A map floor contributes `floor * floorHeight` to depth. Within a cell the ground is below borders, followed by items in their stored order. Optional Y sorting adds less than 0.4 local units by map row, so lower rows cover higher rows within the same floor and drawing order. It sorts tiles within that map; independent Sprite2D objects use their own transform and drawing order. Parallax moves a layer with the camera and does not change its drawing order.

Tile atlas textures referenced by maps cook as exact-size RGBA8 with one mip level. This supports padding, gaps and partial image rows without resizing UV coordinates or averaging neighboring cells into lower mips. It uses four bytes per source texel; other textures retain their target compression policy. Graphic polygon/slope outlines follow sprite rotation, flip, aspect and offset. Explicit per-cell collision overrides and one-way platforms remain grid-based.
