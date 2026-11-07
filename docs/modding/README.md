# Modding

This folder is the modding documentation for NSMBU recomp.

## Live

| Doc | What it covers |
| --- | --- |
| [mod-manager.md](mod-manager.md) | In-game Mods tab, install, storage, shared manifest fields |
| [cemu.md](cemu.md) | `kind: cemu` graphics and shader packs |
| `vendor/decomp/include/game/CommonWindowMgr.h`, `layout/CommonWindow.h`, `layout_sys/AnimLayout.h`, `layout/CourseSelectMenu.h` | Pause, `Multi2D`, and the world-map start menu |

The Mods tab installs Cemu graphics and shader packs only.
Any other package kind is rejected.

## Investigation

| Doc | What it covers |
| --- | --- |
| [intro-skip.md](intro-skip.md) | Native boot path; open work is which confirm button the popup uses |
| [content.md](content.md) | File-replacement packs are not installed |
| [cemu-content.md](cemu-content.md) | File-only Cemu level packs are not installed |
