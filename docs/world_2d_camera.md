# World 2D play camera

The "Map Camera" that `Create > 2D Object > Map Top Down` and `Map Platformer` make (and `kb_cli map2d new`
and `map2d view` keep) frames the map in Play. By default it is still, in the middle of the map, and shows the
map's client box (`view`, 15 x 11 tiles) from the bottom to the top of the viewport. The settings below are
stored in the map file (`.kbmap2d`, one `camera` line, absent while everything is at its default) and apply to
both presets, flat and wall.

| Setting | Meaning |
|---|---|
| `pixelsPerUnit` | Above zero: pixel perfect. One world unit is this many texels of the sprites. |
| `followTarget` | The name of the entity the camera follows. Empty: the camera stays where it is. |
| `smoothingSeconds` | Time in which the camera covers about two thirds of its distance to the target. 0: at once. |
| `deadZoneWidth`, `deadZoneHeight` | Drag margins in tiles: the target may roam this far around the camera's centre before it is dragged along. |
| `clampToMap` | Keeps the view inside the map. A map smaller than the view stays centred. |

Set them from the command line:

```
kb_cli map2d set --project <dir> --map /Game/Maps/World.kbmap2d --camera-pixels-per-unit 16 \
  --camera-follow Hero --camera-smoothing 0.25 --camera-dead-zone 1.5,0.5 --camera-clamp yes
kb_cli map2d info --project <dir> --map /Game/Maps/World.kbmap2d
```

`--camera-follow none` clears the target. There is no editor panel for these settings yet.

## Pixel perfect

With `pixelsPerUnit` set the camera, once per frame:

- takes the viewport height in pixels that the game host gives the scene UI (`Scene.UI().SetViewport`, which the
  Windows player does every frame) and picks the largest whole number of screen pixels per texel, the zoom,
  that still shows the client box height: `zoom = max(1, floor(viewportHeight / (viewHeight * tileSize * pixelsPerUnit)))`;
- sets the orthographic height to `viewportHeight / (pixelsPerUnit * zoom)`, so that one texel is exactly `zoom`
  screen pixels, whatever the window size or aspect ratio;
- rests the camera on whole screen pixels (`1 / (pixelsPerUnit * zoom)` world units), so that fractional movement of
  the target moves the picture a pixel at a time and the sprites do not shimmer.

A viewport shorter than the client box shows one pixel per texel and therefore less than the client box. Until the
host has published a viewport, the camera keeps the client box height.

Texture filtering is not part of this setting: it does not change how a sprite sheet is sampled, and which filter the
sprite materials use was not verified here (so a sheet may still be smoothed at zoom levels above one pixel per texel).
What the preset does give the picture is a pixel profile (`/Game/Maps/PP_Map2D.kbppfx`) without anti-aliasing and tone
mapping, and atlas cells that sample half a texel inside their cell, so that neighbouring sprites do not bleed.

## Follow

The camera is moved in the plane of the map (across and up the screen) relative to the map entity, which is assumed not
to be rotated. Each frame it keeps the drag margins, eases towards the target with smoothing that does not depend on the
frame rate, and stays inside the map when asked. A target that jumps further than the client box in one frame (a teleport)
is followed at once; so is the first frame. The follow target is found by name; the first entity of that name is used.
