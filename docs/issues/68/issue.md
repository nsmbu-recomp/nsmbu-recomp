<!-- GitHub issue #68: Dual Nacon Controllers not working in multiplayer (OPEN) https://github.com/nsmbu-recomp/nsmbu-recomp/issues/68 -->

### What happens

When connecting two Nacon controllers, the second controller does not work or is not recognized for Player 2, even when changing configuration settings.
Controller model: Nacom GC-200WL, Nacom GC100

### Steps to reproduce

**Steps to reproduce:**
1. Connect two Nacon controllers to the PC via USB/Bluetooth.
2. Launch the NSMBU Recompiled executable.
3. Open the Input settings menu (F1).
4. Configure Player 1 as a "Wii U Pro Controller" and map it to the first Nacon controller.
5. Configure Player 2 as a "Wii U Pro Controller" and map it to the second Nacon controller.
6. Save the settings and start a multiplayer game.
7. Try to press buttons on the Player 2 controller to join or move.

**Actual result:**
The second Nacon controller mistakenly controls Player 1 instead of Player 2.

**Expected result:**
Player 2 should be able to control the second character independently using the second Nacon controller.


### Where you were

We are attempting to configure two Nacon controllers for local multiplayer. Even after ensuring that Player 1 and Player 2 are both set as "Wii U Pro Controller" (avoiding the Wii U GamePad duplicate conflict) and ensuring background software like Steam is closed, the game fails to properly separate the two Nacon inputs.


### Version

v0.9.1

### Game version

Europe base disc

### Platform

Windows

### Hardware

None

### Logs

None

### From the app

_No response_
