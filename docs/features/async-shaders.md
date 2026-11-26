# Async shader compile (experimental)

This option is under **Settings → Graphics → Effects** on the Vulkan renderer.
Metal always compiles on the render thread.

## What it does

| Setting | Behaviour |
| --- | --- |
| Off (default) | A new shader compiles on the render thread and the picture can freeze until it is ready |
| On | The shader compiles beside the frame.
  The draw that needs it is skipped until the shader is ready, so the picture keeps moving |

An object can be missing for a moment while its shader compiles.

## Overrides

`NSMBU_VK_ASYNC_SHADERS=1` or `=0` forces the choice for one start and greys out the checkbox.

Async shader compile applies only when the Vulkan renderer is active.
The macOS app can use Metal or Vulkan.