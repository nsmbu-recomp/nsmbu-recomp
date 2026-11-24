# Frame rate

Guest logic stays at 60 steps a second.
Pictures above that rate are extra draws between those steps.
The present cap is 30, 60, 120, 144, 165, or 240, or a custom cap from 60 to 1000.
The `NSMBU_*` variables are in [env.md](env.md) under Frame interpolation.
The sead frame order is in `vendor/decomp/docs/frame-loop.md`.

## In-between pictures

An in-between picture does not run another logic step.
Course models blend their bones between the last two logic poses.
The port also rebuilds each model's view-projection from the camera halfway between those steps and copies that matrix into the model's uniform block.
`RenderObjRenderMgr::calcView` (`0x024FBF6C`) is where that copy happens.
The renderer view box is then put back to the logic step before the rest of the draw.
Characters and other draws that use those uniforms stay on the in-between camera.
Draws that read the live view box stay on the 60 Hz step.

The blend weight is 1 on the logic picture and the open fraction of the 60 Hz step on a hold.
Both the bone blend and the model camera use that same weight.

## Course tiles

Course tiles do not use the model uniform block.
The course-layer draws read the live ortho at `RenderInfo+0x18`.
Top, bottom, left, and right are the floats at `0x9C`, `0xA0`, `0xA4`, and `0xA8`.
The ground and brick passes are in that layer, not only `0x0269325C`.
`sead::OrthoProjection::setTBLR` (`0x02A06818`) stores those floats.
`Renderer::calcForAreaTask` (`0x024FA76C`) fills that ortho from the renderer view box at `Renderer+0x10` (`min x`, `min y`, `max x`, `max y`).
The call is `setTBLR(-min y, -max y, min x, max x)`.
Wide aspect is applied in that `setTBLR` hook, so the tile edges have to go through the same hook as the model projection.

While the view box is restored to the logic step before this read, the ground steps at 60 Hz and the characters stay smooth.

## Do not leave the in-between box up for the whole draw

Putting the interpolated view box back only after `procDraw_` returns does move the tiles with the in-between camera.
It also leaves that box in place for `calcView` and for every other draw.
Those draws use positions that were computed on the logic step.
The picture then jitters, because the camera sample and the positions are no longer the same step.
`calcForAreaTask` stores the projection on the 3D layer at `+0x48`.
Calling it when that layer pointer is null writes guest address `0x48` and the process crashes.
That is what happened when the box was applied outside the game's own camera update.

## What the port does

The logic box is restored as soon as the model view-projection has been copied.
Course tiles do not read that box when the picture is shown.
`LayerMgr::calcDL` records their projection into the display list with `GX2SetVertexUniformReg`.
A later present repeats that list.
The port keeps the guest address of each recorded course-camera matrix.
That matrix matches the course camera except the vertical scale, which the tile draw negates.
The port writes the in-between camera into those words and keeps the negated vertical scale.
The model pose is baked while that in-between camera is still installed.
The logic box is restored before `procDraw_` runs.
The port waits for the render thread to copy that picture before it writes those uniforms again.
Pipes and enemies already follow that matrix because their uniform block stays in memory.
The tile matrix does not.

## Grass

The foreground grass is a `TexQuadDeco` quad.
`0x0268B67C` calls `updateGrassAndFlowers` (`0x02686428`) on the 60 Hz step.
That call writes each bush position at `+0x44` and a width pair at `+0x54`.
Kind 3 also uses an offset vector at `+0x4C`.
The draw at `0x02501D08` builds quads from those arrays in world space.
It uses the wave heap for edge width and scale (`f29` and the float at `+4` in each eight-byte pair), vec3 positions at `+0x44`, and per-quad sway at `TexQuadDeco+0x570`.
The `Bg+0x38D4` / `+0x38D8` / `+0x38DC` tables only supply instance pointers and tags; do not blend them.
`0x0268B70C` runs while the logic step records the picture.
A hold step does not call it.
That step's `procDraw_` repeats the recorded vertices.
The port adds the gap between the record camera and the in-between camera to every baked vertex X and Y in the recorded half of the vertex buffer.
Each vertex is 0x88 bytes, and the position is the first three floats.
The half is selected by the word at `+0x394` on the draw object (`*(parent+0x24)` of `*0x101D2DB0`).
It puts the original bits back after the render thread copies the picture.
The side list at `+0x30` is left alone.
When `+0xD28` is zero that list is one vec3, and writing the quad count past it clears the next object's header.
`NSMBU_INTERP_GRASS_TRACE` counts `[interp] grass px` lines after the course camera moves 4 units.
`scr` is that quad in TV pixels under the view box at record time.
`slip` is how many pixels the quad moved relative to that camera since the previous line.
`hold=0` on every line means the samples are logic pictures only.
