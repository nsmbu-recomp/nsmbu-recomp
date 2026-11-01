# Linux

NSMBU on Linux uses the Vulkan renderer through SDL3.

## Release install

Download the Linux zip or AppImage from [GitHub Releases](https://github.com/nsmbu-recomp/nsmbu-recomp/releases).

x86-64 and arm64 builds need glibc 2.35 or newer and a Vulkan 1.1 driver.

Start `nsmbu-launcher` from the zip, or run the AppImage.

The first start opens the installer.
Each later start launches the game.

The installer uses Zenity or KDialog when one of those programs is installed.
If no file window opens, type the path.
You can also drag a file into the window and press Enter.

Player paths, logs, and setup files are in [logs.md](../logs.md).

## Build from source

Set up the game on this machine ([game/README.md](../../../game/README.md)), then use the `nsmbu` CLI ([cli.md](../../developer/cli.md)):

```bash
nsmbu game usa-1.3.0 --recomp
nsmbu build
nsmbu run
```

Agent and CI verification default to Linux with the USA 1.3.0 dump ([verification.md](../../developer/verification.md)).

Vulkan notes, validation, and renderer smoke tests are in [vulkan.md](../../developer/vulkan.md).

Profiling and benchmarks are in [performance.md](../../developer/performance.md).

## Related

- [../macos/README.md](../macos/README.md) — Apple Silicon desktop build
- [../android/README.md](../android/README.md) — phone and tablet APK
- [../ios/README.md](../ios/README.md) — iOS build (requires a Mac host)
