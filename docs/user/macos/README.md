# macOS

NSMBU on macOS ships as `NSMBU.app` on Apple Silicon (macOS 14 or newer).

The default build includes Metal and Vulkan (MoltenVK).

Pick the renderer in Settings → Graphics, or set `NSMBU_RENDERER`.

## Release install

Download the macOS zip from [GitHub Releases](https://github.com/nsmbu-recomp/nsmbu-recomp/releases).

Unzip into a writable folder and open `NSMBU.app`.

The first start opens the installer.
Each later start launches the game.

The installer uses the system file chooser.
You can also drag a file into the window and press Enter.

If macOS blocks the app, see [If the system blocks the app](../blocked-app.md).

Player paths, logs, and setup files are in [logs.md](../logs.md).

## Build from source

Set up the game on this Mac ([game/README.md](../../../game/README.md)), then:

```bash
nsmbu game usa-1.3.0 --recomp
nsmbu build
nsmbu run
```

Architecture and recompilation overview: [how-it-works.md](../../developer/how-it-works.md).

MoltenVK behaviour, menus, and macOS-specific Vulkan defaults: [vulkan.md](../../developer/vulkan.md).

## iOS from this Mac

The iPhone and iPad app is built on the same machine after `build/gen` exists.

See [../ios/README.md](../ios/README.md).

## Related

- [../linux/README.md](../linux/README.md) — Vulkan/SDL desktop
- [../android/README.md](../android/README.md) — Android APK
