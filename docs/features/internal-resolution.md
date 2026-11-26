# Internal resolution and image filters (experimental)

These options are under **Settings → Graphics**.

## Internal resolution

The Wii U TV target is 720p.
Pick a scale under **Internal resolution**:

| Preset | Scale |
| --- | --- |
| 360p | 0.5× — fewer pixels, upscaled to the window |
| 540p | 0.75× |
| 720p | 1× — original |
| 1080p | 1.5× |
| 1440p | 2× |
| 2160p/4K | 3× |

Higher scales sharpen distant detail and cost more GPU time.

`NSMBU_RES_SCALE` sets the scale for one start.

## Image filters

Under **Effects**:

- **16x anisotropic filtering** — sharper textures at shallow angles when the GPU supports it
- **Edge smoothing (FXAA)** — softens jagged edges when the GPU supports it

Both apply at once and are saved with your settings.