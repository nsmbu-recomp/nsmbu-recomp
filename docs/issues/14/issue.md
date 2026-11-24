<!-- GitHub issue #14: Story Mode freezes on Acorn Plains world map with music still playing (v0.6.0 and v0.6.1) (CLOSED) https://github.com/nsmbu-recomp/nsmbu-recomp/issues/14 -->

### What happens

New Super Mario Bros. U freezes when entering Story Mode.

On v0.6.1, the game usually freezes after selecting "Begin Story Mode," preventing me from reaching the world map. The log reports:

`[gx2] render thread exception: mismatched Vulkan attachments (frame work aborted)`

On v0.6.0, I was able to get past the opening sequence and reach the Acorn Plains world map. However, the game became stuck on the screen showing Peach's Castle and Bowser's airships.

Background music continues playing, but gameplay no longer progresses or responds to controls.

On v0.6.0, I can still open F12 and navigate the settings overlay normally while the world map remains frozen. The log continues reporting approximately 60 FPS without the Vulkan exception.

Expected behavior: Story Mode should load normally, allowing me to navigate the world map and enter levels.

I have no mods installed. The problem occurs on both versions.

<img width="1375" height="824" alt="Image" src="https://github.com/user-attachments/assets/6c53414e-28d4-4afb-9368-9e63fe627ec1" />

### Steps to reproduce

1. Launch NSMBU v0.6.0 or v0.6.1 on Windows 11.
2. Select "Begin Story Mode" from the main menu.
3. Wait for the opening sequence and world map to load.
4. On v0.6.1, the game typically freezes during the transition into Story Mode.
5. On v0.6.0, the Acorn Plains world map appears, but gameplay becomes stuck.
6. Background music continues playing.
7. On v0.6.0, press F12. The settings overlay still opens and remains responsive, even though gameplay is frozen.

The issue has occurred repeatedly across multiple launches.

### Where you were

Acorn Plains (World 1), during the opening Story Mode sequence and initial world map, before entering World 1-1.

On v0.6.0, the game becomes stuck while displaying Peach's Castle and Bowser's airships.

On v0.6.1, the freeze usually occurs earlier, after selecting "Begin Story Mode."

I have not been able to consistently enter and play World 1-1.

### Version

v0.6.0 (c2540f4) v0.6.1 (a3a84a0)

### Platform

Windows 11 25H2 (build 26200.9711)

### Hardware

CPU: AMD Ryzen 7 7800X3D (8-Core) GPU: NVIDIA GeForce RTX 5070 RAM: 32 GB OS: Windows 11 25H2 (Build 26200.9711)

### Logs

***v0.6.0 — World map freeze**

[[nsmbu-v0.6.0.log](https://github.com/user-attachments/files/33913050/nsmbu.log)](https://github.com/user-attachments/files/33913050/nsmbu.log)

**v0.6.1 — Vulkan rendering error**

[[nsmbu-v0.6.1.log](https://github.com/user-attachments/files/33913055/nsmbu.log)](https://github.com/user-attachments/files/33913055/nsmbu.log)

Attached logs from v0.6.0 and v0.6.1 showing different behavior during the same Story Mode freezing issue.

Troubleshooting performed:
- Tested 60 Hz and 165 Hz display refresh rates.
- Tested 60 FPS and different frame-rate settings.
- Tested 1× internal resolution.
- Tested NVIDIA RTX 5070 and integrated AMD graphics.
- Tested Wii U GamePad and Pro Controller modes.
- Tested a separate installation of v0.6.0.
- Confirmed no mods are installed.

The freeze persists across both versions.

### From the app

_No response_
