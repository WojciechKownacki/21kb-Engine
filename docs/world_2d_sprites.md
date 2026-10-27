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
