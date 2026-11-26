# Higher frame rates (experimental)

Frame rate is under **Settings → Graphics → Frame rate (experimental)**.

## Presets

| Choice | Behaviour |
| --- | --- |
| 30 fps | Two logic steps per picture so the game keeps its speed |
| 60 fps | The Wii U rate — one logic step per picture |
| 120, 144, 165, 240 fps | Extra pictures between fixed 60 Hz logic steps |
| Custom | Any rate from 60 to 1000 |
| Unlocked | As many pictures as the device can draw while logic stays at 60 steps per second |

Rates above 60 draw in-between pictures from the last two game poses.
That is smoother motion, not faster gameplay.

The fps number is how fast pictures are drawn.
Your display may not show every frame when its refresh rate is lower.

`NSMBU_INTERP=0` keeps pictures at 60 fps even when a higher cap is selected.

## Android heat guard

Higher rates work the device harder.
When Android reports that the device is close to overheating, in-between frames pause and the game stays at 60 fps until the device cools.

**Settings → Graphics → Heat guard** on Android:

| Setting | What it does |
| --- | --- |
| Normal (default) | Pauses in-between frames at the limit for your device kind |
| Relaxed | Waits longer before pausing |
| Off | Never pauses in-between frames for heat |

Relaxed and Off keep the higher rate longer, but a hot device may still slow down and stutter.
See [Android](../user/android.md).

`NSMBU_HEAT_GUARD` sets Normal, Relaxed, or Off for one start.