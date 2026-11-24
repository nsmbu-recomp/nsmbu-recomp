<!-- GitHub issue #38: The HUD displays incorrectly in 21:9 format; it appears centered. [Intermittent drops in frame rate performance] (CLOSED) https://github.com/nsmbu-recomp/nsmbu-recomp/issues/38 -->

### What happens

When starting the game in a 21:9 aspect ratio, it’s noticeable that the HUD is fixed in the center, which gives it an odd appearance on an ultrawide display—this applies to the menus, the elements at the edges of the screen during level loading, and even the HUD for time, coins, and points within each level.

<img width="3414" height="1439" alt="Image" src="https://github.com/user-attachments/assets/04de18cf-525f-4dfe-8354-0670ba7de5bc" />

<img width="3414" height="1439" alt="Image" src="https://github.com/user-attachments/assets/3f7b8e9e-9146-4653-93dd-692619829cd9" />

<img width="3413" height="1439" alt="Image" src="https://github.com/user-attachments/assets/dd603c37-321f-4b10-8acf-a8ec46f86fb8" />

<img width="3415" height="1439" alt="Image" src="https://github.com/user-attachments/assets/5f07d22c-1e26-41e7-a094-302ad4727f1f" />

<img width="3415" height="1437" alt="Image" src="https://github.com/user-attachments/assets/2c57dfef-af82-4da6-bcf6-ee9556d68d7f" />



### Steps to reproduce

1. I start the game.
2. On the main menu, when the characters run, the plants in the background disappear at the edges of the 16:9 area, even though the game is set to 21:9.
3. When selecting worlds, purple borders appear, cropping the aspect ratio.
4. When starting a level, the transition effect appears cropped to 16:9.
5. During gameplay, the HUD (showing stats like time, points, coins, lives, etc.) is centered as if it were in a 16:9 format; it does not extend to the sides as it did in previous versions.
6. Within the levels, the edges of structures appear empty where walls end.

<img width="3429" height="1426" alt="Image" src="https://github.com/user-attachments/assets/5c80bd23-09c9-41c5-8447-aca45445153f" />

### Where you were

Basically from the start of the game—in the menus, and throughout almost the entire HUD layout.

### Version

NSMBU-v0.7.2-windows-x86_64

### Game version

Europe 1.3.0

### Platform

Windows

### Hardware

7 5800X3D, 32Gb

### Logs

There are no crashes, but there is a drop in performance; at times during gameplay within levels, and while traversing the world map, the image stutters and the frame rate dips.

### From the app

_No response_
