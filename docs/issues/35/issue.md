<!-- GitHub issue #35: Allow render resolution below native (lower-end / iGPU hardware) (CLOSED) https://github.com/nsmbu-recomp/nsmbu-recomp/issues/35 -->

### What would you like?

The in-game settings menu (F12 / `) lets me raise the render resolution, but I can't find a way to go
  **below** the native resolution. On low-power hardware the game is noticeably sluggish, and a lower
  internal resolution would probably make it playable.

  Could you add an option to render at a lower internal resolution, for example:

  - a render scale below 1.0x (0.5x, 0.75x…), or fixed presets such as 540p / 360p for the TV image
  - optionally a separate, lower resolution for the GamePad view (or the option to turn it off) when it
  isn't displayed
  - the image upscaled to the output resolution (bilinear or nearest neighbour)

  Some other recomp projects offer this kind of "performance" setting, and it helps a lot on mini PCs and
  handhelds.

  Tested on: v0.7.2, renderer: Vulkan

  Thanks for the project!



### Platform (if it matters)

  Linux – Intel N100 mini PC (Intel UHD iGPU), 1080p TV
