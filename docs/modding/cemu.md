# Cemu graphics packs (`kind: cemu`)

Status: **live**.
Cemu packages are graphics/shader packs the player places in the mods folder.
They do not replace arbitrary game content archives; use [content.md](content.md) for that.

See [mod-manager.md](mod-manager.md) for install and shared manifest fields.

## Player workflow

Put one Cemu graphics pack under the mods folder as its own subdirectory with `manifest.json` (`kind: cemu`, `cemu_dir`) and its `rules.txt` / shaders.
Use **Mods → Open mods folder** to find that directory.
The adapter reads `rules.txt` versions 4/5, checks the USA NSMBU title ID, and exposes each preset category as a dropdown.
Enable it and restart.
Changing a preset or disabling also requires a restart.
The active snapshot stays fixed until exit; disable and restart before replacing or removing the folder.

## Manifest fields

| Field | Meaning |
| --- | --- |
| `kind` | `cemu` |
| `cemu_dir` | Relative folder holding `rules.txt` and shader files (empty for the package root) |

## Supported rules

Supported graphics rules are width/height/depth, formats/tileModes filters, and `overwriteWidth`/`overwriteHeight`, applied to physical render-target dimensions.
Guest memory, resource formats, and the original game files remain unchanged.
Matching rules replace the native resolution scale rather than multiplying it.

Pure dimension packs work in Metal and Vulkan.
Packs containing GLSL require a build configured with `-DNSMBU_RENDERER=BOTH` or `VULKAN`, and Vulkan selected at runtime: choose it in Graphics, restart, then enable the pack.
Metal shader translation is not implemented.
If Vulkan falls back to Metal, the pack's shader and dimension changes are suppressed and the UI reports the backend requirement.

## Shaders

Custom `16hex_16hex_vs.txt` and `_ps.txt` shaders use Cemu-compatible base and auxiliary hashes (`vendor/cemu/graphic_pack_hash.h`).
Preset variables expand before GLSL→SPIR-V compilation.
The adapter restores the legacy pixel support block only for matching shaders that expect `uf_fragCoordScale`, then validates descriptors, buffer member offsets/strides/array lengths, and location types against the original shader.
Failed compilation or layout mismatch retains the original shader, with a log diagnostic and applied/rejected counts in Mods.
Matching a filename is not a guarantee of compatibility with this backend.

## Aspect and patches

The official NSMBU Resolution pack's exact EUR/JAP/USA aspect constant patch table is recognized and mapped to the recomp's native projection adapter.
No arbitrary instruction patch is applied.
Other `patches.txt` contents, geometry shaders, unsupported rules/conditions, format replacement, DLC/code/meta payloads, and mixed content-plus-shader packs are rejected.
Select a single pack rather than an entire Cemu pack collection.
Models/UI in `content` use the separate content replacement adapter in [content.md](content.md).
Drop a Definition-only Cemu level pack into the mods folder as described in [cemu-content.md](cemu-content.md) (detect `rules.txt`, ignore `meta/`, no player `manifest.json`).

## Presets

Preset expressions support finite arithmetic, parentheses, variables, min/max/floor/ceil/round; missing variables and cycles are rejected.
Dimension limits are 1–16384 and aspect ratios 1–4.
Overlapping rules from different enabled packs and duplicate shader variants are rejected.
Both imported presets and changes to an enabled pack are checked before saving the profile.

## References

Primary format reference: [Cemu graphics pack documentation](https://github.com/cemu-project/cemu_graphic_packs/wiki/How-to-create-Graphic-Packs).
Compatibility was checked against the public [NSMBU Resolution pack](https://github.com/cemu-project/cemu_graphic_packs/tree/master/Resolutions/NSMBU_Resolution) and [NSMBU Contrasty pack](https://github.com/cemu-project/cemu_graphic_packs/tree/master/Enhancements/NSMBU_Contrasty).
Their sources are not part of the repository, and the host tests use synthetic fixtures only.
This covers the tested adapter paths, not universal Cemu graphics-pack compatibility or the visual accuracy of every preset.

## Tests

`mod_cemu`, `mod_cemu_startup`, and `mod_cemu_backend` cover parsing and startup against synthetic fixtures.
See [mod-manager.md](mod-manager.md#validation).
