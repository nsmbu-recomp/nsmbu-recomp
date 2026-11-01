# New Super Mario Bros. U on Android

New Super Mario Bros. U runs natively on Android devices.
It is not an emulator.
The game's own code is recompiled for ARM processors and draws with Vulkan.
You need your own copy of the Wii U game; this project never provides game files, game code or keys.

## What you need

- A 64-bit Android device with Android 13 or newer and Vulkan 1.1 or newer.
- A custom driver such as Mesa Turnip if your Adreno driver is too old (see [Android GPU support](android-gpu.md)).
- A few GB of free storage while setup runs.
- Your own New Super Mario Bros. U for Wii U, as an extracted game folder, a Cemu `.wua` archive, or a `.wud`/`.wux` disc image with its keys.

## Set up the game on the device

Every release has the setup app: download `NSMBU-<version>-android-arm64.apk` from the [Releases](https://github.com/nsmbu-recomp/nsmbu-recomp/releases) page.
The `android-x86_64` APK is for emulators and Chromebooks.
The APK carries no game code: it builds the game on your device from your own copy.
Later versions install over it and keep your saves and settings.
When an update needs it, the device prepares the game again.

1. Install the APK.
   If you already have a version that you built on your PC, export your save first (long-press the app icon), then uninstall that version: the two are signed differently.
2. Open the app, tap **Choose your game** and pick what you have:
   - **Extracted game folder**: put the game in a folder of its own (for example `Download/NSMBU`) and choose that folder, the one that contains `code`, `content` and `meta`.
     Android does not allow choosing Download itself, the top of the internal storage or `Android/data`.
     If you choose the folder just above the game, the app finds the game inside it.
     If you choose a wrong folder, the app tells you which folder to choose before it copies anything.
   - **Cemu archive (.wua)**: a single file, which you can choose from any folder.
   - **Disc image (.wud / .wux) with keys**: a single file and its disc key and Wii U common key.
     A Cemu `keys.txt` with labelled `common =` and `disc =` lines covers both keys.
     **Add the 1.3.0 update folder (optional)** merges an extracted update into the game.
3. Tap **Start**.
   The device extracts, translates and compiles the game once, and the screen shows the progress.
   You can leave the app while setup runs.
   **Pause** stops setup safely, and setup also pauses by itself when the device is too warm or the battery is low.
4. When setup is complete, tap **Play**.
   **Rebuild the game** runs setup again for this version of the app.
   Pieces that still match this version are kept.

The app icon skips this installer once a game is ready.
The installer opens again only when you open a new version of the app.
That screen stays up until you leave it.
The next open of that version starts the game.
Long-press the app icon and choose **Game setup** to open the installer yourself.
**Settings > Android** describes this.
**Quit to the installer** on that tab leaves the game and opens the installer.
The installer stays open when the game closes.

When you choose a different game, the app deletes its copy of the earlier choice to free space.
The game that you can already play stays until the new one is ready.

### Or: build it on your PC

1. Install the Android SDK and NDK, JDK 17 or 21, CMake, Ninja and Python 3.
2. Extract your game and generate its code, as in the *Building* steps of the [README](../README.md).
3. Run `nsmbu apk`, or `android/build_native.sh` then `./gradlew assembleRelease` in `android/`.
4. On the phone, open the app and tap **Choose your game**, then pick the folder that contains `code`, `content` and `meta` (for example a copy under Download).
   The app copies it privately and **Play** uses that copy.
   You can still use `nsmbu apk` over USB instead.
5. If you built manually and skipped both, install the APK and copy the game into `Android/data/org.nsmbrecomp.nsmbu/files/game/` with `adb push`.

An APK that you build on your PC contains your recompiled game.
Keep it for your own device and never share it.

## Playing

- **On-screen controls**: a stick, the D-pad, A B X Y, L R ZL ZR and + −.
  The drawn A B X Y follow your face-button layout in **Settings > Controls**.
  The button under the view button (top left) shows or hides them.
- **Controllers**: a Bluetooth or USB controller works too.
  The touch buttons hide while a controller is connected; a touch in the top left corner brings them back for a few seconds.
  **Settings > Controls > Touch buttons** can also show them always or hide them.
- **Touch layout**: hold the show/hide button to edit the layout.
  Drag a control to move it, select one and use **Size −** or **Size +**, then tap **Done**.
  **Reset** restores the original layout.
  The layout is saved on this device.
- **Settings**: the Back button or gesture opens and closes Settings.
  Press + and − together on the on-screen controls or a controller for the same menu.

  While Settings is open, touch the menu or use a controller: D-pad or left stick to move, A to activate, B to go back, LB/RB for tabs, Start+Select to close.
  The Home button of a controller opens them too.
- **Phone vibration**: **Settings > Controls > Phone vibration** is **Touch controls** by default, so game rumble vibrates the phone while you play with the on-screen controls.
  **No controller motor** also vibrates the phone while the connected controller has no motor.
  **Off** turns phone vibration off.
  The **Rumble** switch silences the phone and the controllers.
- **Full screen**: the game fills the screen, also beside a camera cutout; swipe from an edge to show the system bars for a moment.

## Higher frame rates and heat

The game runs at 60 frames a second.
Higher rates in **Settings > Graphics > Frame rate** add in-between frames, so the device works harder and gets warmer.
When the device says that it is close to getting too hot, the in-between frames pause and the game keeps 60 fps.
When the device has cooled down, the higher rate comes back by itself.

Handhelds with a cooling fan (AYANEO, KONKR, AYN, Retroid) wait longer than phones before they pause.
**Settings > Graphics > Heat guard** changes this:

- **Normal** (default): the limit for your kind of device.
- **Relaxed**: waits longer before it pauses, on any device.
- **Off**: never pauses for heat.

Relaxed and Off keep the higher rate longer, but a hot device slows itself down, which can make the game stutter.
If that happens, go back to Normal.

## Files and logs

- The system Files app shows the app's folders (saves, settings, logs).
  The game and the shader cache are read-only there, and key files are never shown.
- **Share log** in the setup app shares the game's log from the last run and the run before it.
  It also shares the current setup's status and the latest extract, translation, compile, and link logs.
  A failed setup can be sent before the game has run.
- After a crash, the next start offers the crash report to share.
  The report also covers exits that Android records itself, such as low-memory kills and driver aborts.
  Normal quits and stops by the save tools do not make a report.
  Sharing is optional.

## Fair play

- Use your own copy of the game, dumped from your own disc or console.
- If someone offers a ready-made APK with the game inside, it is not from this project.

## Questions

**Do I need a controller?**
No.
The on-screen controls cover the whole GamePad.

**Is it normal that my device gets hot?**
The game is demanding at higher frame rates.
See [Higher frame rates and heat](#higher-frame-rates-and-heat) for the Heat guard setting.

**Something does not work.**
Open an [issue](https://github.com/nsmbu-recomp/nsmbu-recomp/issues) with your device model, Android version and what happened, and attach the shared log.

---

A fan project, not affiliated with or endorsed by Nintendo.
New Super Mario Bros. U is a trademark of Nintendo.
