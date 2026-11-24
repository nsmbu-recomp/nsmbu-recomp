<!-- GitHub issue #64: Title: Game logic running below 60 Hz and inconsistent frame pacing — v0.8.4 (CLOSED) https://github.com/nsmbu-recomp/nsmbu-recomp/issues/64 -->

### What happens

Issue:

The game logic is running slightly slower than the expected 60 (59.6) steps per second, even though the displayed FPS remains locked to the selected target.

Frame pacing is also inconsistent. The FPS counter reports a stable frame rate, but the actual motion is not completely smooth.

Comparison:

Frame pacing has improved significantly compared to v0.8.0, but both issues are still noticeable in v0.8.4.

Expected behavior:

Game logic should consistently run at 60 steps per second.

Smooth, evenly paced frames at the selected refresh rate.

No slowdown in game speed.

### Steps to reproduce

All the time. 

### Where you were

World 1-1

### Version

v0.8.4

### Game version

NSMBU + NSLU (USA)

### Platform

Windows

### Hardware

I913500 rtx 4090

### Logs

No crash. Game works fine. Just a small framepace issue

### From the app

_No response_
